#include "game.h"
#include <string.h>

/* Bump the last character when the Game layout changes, so old saves are refused. */
#define SAVE_MAGIC 0x32475a47u /* "GZG2" */

/* Per-type tables: collision radius, score, hull, base fall speed (px/step). */
static const int E_R[3] = {10, 9, 14};
static const uint32_t E_PTS[3] = {100, 150, 400};
static const uint8_t E_HP[3] = {1, 1, 4};
static const int E_VY[3] = {2, 4, 1};

#define PX_MIN 10
#define PX_MAX (FIELD_W - 10)
#define PY_MIN 110
#define PY_MAX (FIELD_H - 12)
#define PL_SPEED 3
#define FIRE_COOL 12      /* steps between volleys */
#define FX_MAX_AGE 12

static uint32_t random32(Game *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int wave_budget(int wave)
{
    return clampi(4 + 2 * wave, 0, 24);
}

static int wave_gap(int wave)
{
    return clampi(40 - 2 * wave, 16, 40);
}

void game_new(Game *g, uint32_t seed, uint32_t best)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->best = best;
    g->px = FIELD_W / 2;
    g->py = FIELD_H - 24;
    g->lives = START_LIVES;
    g->wave = 1;
    g->state = W_INTRO;
    g->state_timer = 100;
}

void game_drag(Game *g, int x, int y)
{
    if (g->over)
        return;
    g->px = (int16_t)clampi(x, PX_MIN, PX_MAX);
    g->py = (int16_t)clampi(y, PY_MIN, PY_MAX);
}

static void add_fx(Game *g, int x, int y, int big)
{
    for (int i = 0; i < MAX_FX; i++) {
        if (!g->fx[i].alive) {
            g->fx[i].alive = 1;
            g->fx[i].x = (int16_t)x;
            g->fx[i].y = (int16_t)y;
            g->fx[i].age = 0;
            g->fx[i].big = (uint8_t)big;
            return;
        }
    }
}

static void credit(Game *g, uint32_t pts)
{
    g->score += pts;
    if (g->score > g->best)
        g->best = g->score;
}

static void kill_enemy(Game *g, Enemy *e, int credit_kill)
{
    e->alive = 0;
    add_fx(g, e->x, e->y, e->type == E_BOMBER);
    if (credit_kill)
        credit(g, E_PTS[e->type]);
}

static void spawn_enemy(Game *g)
{
    Enemy *e = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (!g->enemies[i].alive) { e = &g->enemies[i]; break; }
    if (!e)
        return;                       /* field full: try again next step */
    g->spawn_timer = (uint8_t)wave_gap(g->wave);
    g->spawn_left--;

    int pick = (int)(random32(g) % 100);
    int bomber_pct = clampi(12 + 2 * (int)g->wave, 0, 30);
    int type = E_FIGHTER;
    if (g->wave >= 2 && pick < bomber_pct)
        type = E_BOMBER;
    else if (g->wave >= 2 && pick >= 62)
        type = E_DIVER;

    int bonus = clampi((int)g->wave / 3, 0, 3);
    e->alive = 1;
    e->type = (uint8_t)type;
    e->hp = E_HP[type];
    e->x = (int16_t)(16 + (int)(random32(g) % (FIELD_W - 32)));
    e->y = -14;
    e->vy = (int16_t)(E_VY[type] + (type == E_BOMBER ? 0 : bonus));
    e->vx = (type == E_FIGHTER || type == E_BOMBER)
                ? (int16_t)((random32(g) & 1) ? 1 : -1) : 0;
    e->cool = (uint8_t)(40 + (int)(random32(g) % 40));
}

static void hit_player(Game *g)
{
    g->lives--;
    g->inv = INV_STEPS;
    add_fx(g, g->px, g->py, 1);
    for (int i = 0; i < MAX_BOMBS; i++)      /* the blast clears incoming bombs */
        g->bombs[i].alive = 0;
    if (g->lives == 0)
        g->over = 1;
}

static void fire_volley(Game *g)
{
    g->fire_cool = FIRE_COOL;
    for (int k = 0; k < 2; k++) {
        int x = g->px + (k ? 5 : -5);
        Shot *free = NULL;
        for (int i = 0; i < MAX_SHOTS; i++)
            if (!g->shots[i].alive) { free = &g->shots[i]; break; }
        if (!free)
            return;
        free->alive = 1;
        free->x = (int16_t)x;
        free->y = (int16_t)(g->py - 12);
        free->vy = -8;
    }
}

static void step(Game *g, Stick in)
{
    g->scroll = (uint16_t)(g->scroll + 1);
    g->px = (int16_t)clampi(g->px + PL_SPEED * in.dx, PX_MIN, PX_MAX);
    g->py = (int16_t)clampi(g->py + PL_SPEED * in.dy, PY_MIN, PY_MAX);
    if (g->inv)
        g->inv--;
    if (g->fire_cool)
        g->fire_cool--;
    else
        fire_volley(g);

    /* Wave state machine. */
    switch (g->state) {
    case W_INTRO:
        if (!--g->state_timer) {
            g->state = W_FIGHT;
            g->spawn_left = (uint8_t)wave_budget(g->wave);
            g->spawn_timer = 5;
        }
        break;
    case W_FIGHT:
        if (g->spawn_timer)
            g->spawn_timer--;
        else if (g->spawn_left)
            spawn_enemy(g);
        else {
            int any = 0;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (g->enemies[i].alive) { any = 1; break; }
            if (!any) {
                credit(g, 50u * g->wave);
                g->state = W_CLEAR;
                g->state_timer = 60;
            }
        }
        break;
    case W_CLEAR:
        if (!--g->state_timer) {
            if (g->wave < 60)
                g->wave++;
            g->state = W_INTRO;
            g->state_timer = 100;
        }
        break;
    }

    /* Enemies move; bombers drift and drop bombs. */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive)
            continue;
        if (e->vx && (e->x <= 10 || e->x >= FIELD_W - 10))
            e->vx = (int16_t)-e->vx;
        if (e->type == E_FIGHTER && random32(g) % 96 == 0)
            e->vx = (int16_t)-e->vx;
        e->x = (int16_t)(e->x + e->vx);
        e->y = (int16_t)(e->y + e->vy);
        if (e->type == E_BOMBER && e->y > 0 && e->y < FIELD_H - 30) {
            if (e->cool) {
                e->cool--;
            } else {
                e->cool = (uint8_t)clampi(90 - 6 * (int)g->wave, 40, 90);
                for (int k = 0; k < MAX_BOMBS; k++) {
                    if (!g->bombs[k].alive) {
                        g->bombs[k].alive = 1;
                        g->bombs[k].x = e->x;
                        g->bombs[k].y = (int16_t)(e->y + 10);
                        g->bombs[k].vx = 0;
                        g->bombs[k].vy = 3;
                        break;
                    }
                }
            }
        }
        if (e->y > FIELD_H + 16)
            e->alive = 0;             /* flew past: no penalty */
    }

    /* Player shots fly up and hit enemies. */
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &g->shots[i];
        if (!s->alive)
            continue;
        s->y = (int16_t)(s->y + s->vy);
        if (s->y < -8) {
            s->alive = 0;
            continue;
        }
        for (int k = 0; k < MAX_ENEMIES; k++) {
            Enemy *e = &g->enemies[k];
            if (!e->alive)
                continue;
            int dx = s->x - e->x, dy = s->y - e->y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= E_R[e->type] - 1 && dy <= E_R[e->type] + 3) {
                s->alive = 0;
                if (--e->hp == 0)
                    kill_enemy(g, e, 1);
                break;
            }
        }
    }

    /* Enemy bombs fall and hit the player (unless invincible). */
    for (int i = 0; i < MAX_BOMBS; i++) {
        Bomb *b = &g->bombs[i];
        if (!b->alive)
            continue;
        b->x = (int16_t)(b->x + b->vx);
        b->y = (int16_t)(b->y + b->vy);
        if (b->y > FIELD_H + 8) {
            b->alive = 0;
            continue;
        }
        int dx = b->x - g->px, dy = b->y - g->py;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx <= 6 && dy <= 6 && g->inv == 0) {
            b->alive = 0;
            hit_player(g);
        }
    }

    /* Ramming: the enemy dies, the player pays. */
    if (g->inv == 0) {
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &g->enemies[i];
            if (!e->alive)
                continue;
            int dx = e->x - g->px, dy = e->y - g->py;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= E_R[e->type] - 3 && dy <= E_R[e->type] + 2) {
                e->alive = 0;
                add_fx(g, e->x, e->y, e->type == E_BOMBER);
                hit_player(g);
                break;
            }
        }
    }

    /* Explosion animations age out. */
    for (int i = 0; i < MAX_FX; i++) {
        if (!g->fx[i].alive)
            continue;
        if (++g->fx[i].age > FX_MAX_AGE)
            g->fx[i].alive = 0;
    }
}

void game_tick(Game *g, Stick in, uint32_t ms)
{
    if (g->over)
        return;
    if (ms > 100)
        ms = 100;                     /* a stall must not teleport anything */
    g->acc = (uint16_t)(g->acc + ms);
    while (g->acc >= STEP_MS && !g->over) {
        g->acc = (uint16_t)(g->acc - STEP_MS);
        step(g, in);
    }
}

/* FNV-1a over the record. */
static uint32_t checksum(const uint8_t *p, unsigned n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

void game_encode(const Game *g, uint8_t *out)
{
    uint32_t header[3] = {SAVE_MAGIC, sizeof(Game), checksum((const uint8_t *)g, sizeof(*g))};
    memcpy(out, header, 12);
    memcpy(out + 12, g, sizeof(*g));
}

static int between(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

bool game_decode(Game *g, const uint8_t *in, unsigned size)
{
    uint32_t h[3];
    Game t;
    if (size != SAVE_BYTES)
        return false;
    memcpy(h, in, 12);
    memcpy(&t, in + 12, sizeof(t));
    if (h[0] != SAVE_MAGIC || h[1] != sizeof(Game) || h[2] != checksum(in + 12, sizeof(t)))
        return false;
    /* Validate every field a corrupt file could break. */
    if (t.over > 1 || t.lives > START_LIVES || (!t.over && t.lives == 0))
        return false;
    if (t.inv > INV_STEPS || t.wave < 1 || t.wave > 60 || t.state >= 3)
        return false;
    if (t.state_timer > 100 || t.spawn_left > 24 || t.spawn_timer > 40 ||
        t.fire_cool > FIRE_COOL || t.acc >= STEP_MS || !t.rng)
        return false;
    if (t.score % 50 || t.score > 900000u || t.best < t.score || t.best > 900000u)
        return false;
    if (!between(t.px, PX_MIN, PX_MAX) || !between(t.py, PY_MIN, PY_MAX))
        return false;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &t.enemies[i];
        if (e->alive > 1 || e->type >= 3)
            return false;
        if (e->alive && (!between(e->hp, 1, 4) || !between(e->x, -20, FIELD_W + 20) ||
                         !between(e->y, -20, FIELD_H + 40) || (e->vx < -1 || e->vx > 1) ||
                         !between(e->vy, 0, 7)))
            return false;
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        const Shot *s = &t.shots[i];
        if (s->alive > 1)
            return false;
        if (s->alive && (!between(s->x, 0, FIELD_W) || !between(s->y, -8, FIELD_H) ||
                         s->vy >= 0 || s->vy < -8))
            return false;
    }
    for (int i = 0; i < MAX_BOMBS; i++) {
        const Bomb *b = &t.bombs[i];
        if (b->alive > 1)
            return false;
        if (b->alive && (!between(b->x, -8, FIELD_W + 8) || !between(b->y, -8, FIELD_H + 8) ||
                         !between(b->vy, 1, 6) || (b->vx < -1 || b->vx > 1)))
            return false;
    }
    for (int i = 0; i < MAX_FX; i++) {
        const Fx *x = &t.fx[i];
        if (x->alive > 1 || x->big > 1 || (x->alive && x->age > FX_MAX_AGE))
            return false;
    }
    *g = t;
    return true;
}
