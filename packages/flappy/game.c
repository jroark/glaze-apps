/* See game.h for the rules. Fixed point only: y is 1/1000 px, x and
 * pipe tops 1/10 px, speeds px/s, one game_step per 10 ms. */
#include "game.h"
#include <string.h>

#define MAGIC 0x31504C46u /* "FLP1" */
#define FL_TOP_MAX (FL_H - FL_GAP - FL_BOT_MIN) /* 136 px */

static uint32_t random32(Game *g) {
    g->rng ^= g->rng << 13; g->rng ^= g->rng >> 17; g->rng ^= g->rng << 5;
    return g->rng;
}

/* A pair enters at x (1/10 px) with a gap placed at random; x=head+i*
 * FL_SPACING*10 is exactly what game_step's ring produces, and tops stay
 * inside the range game_decode admits. */
static void spawn(Game *g, int slot, int x) {
    g->x[slot] = (int16_t)x;
    g->top[slot] = (int16_t)((FL_TOP_MIN + random32(g) % (FL_TOP_MAX - FL_TOP_MIN + 1)) * 10);
    g->passed[slot] = 0;
}

void game_new(Game *g, uint32_t seed, uint32_t best) {
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1;
    g->best = best;
    g->y = FL_H * 1000 / 2;
    g->speed_px = FL_SPEED_BASE;
    /* Three pairs queue off the right edge; the first arrives after 4 s. */
    for (int i = 0; i < FL_PIPES; i++)
        spawn(g, i, FL_W * 10 + i * FL_SPACING * 10);
}

void game_flap(Game *g) {
    if (!g->over) g->speed = FL_FLAP_SPEED;
}

static int hit(const Game *g, int slot) {
    int x0 = g->x[slot], x1 = x0 + FL_PIPE_W * 10;
    int bx0 = (FL_BIRD_X - FL_BIRD_R) * 10, bx1 = (FL_BIRD_X + FL_BIRD_R) * 10;
    if (x1 <= bx0 || x0 >= bx1) return 0; /* rims are scenery, game.h */
    int32_t top = (int32_t)g->top[slot] * 100, bottom = top + FL_GAP * 1000;
    return g->y - FL_BIRD_R * 1000 < top || g->y + FL_BIRD_R * 1000 > bottom;
}

void game_step(Game *g) {
    if (g->over) return;
    g->t++;

    g->y += g->speed * 10; /* px/s for 10 ms is 1/1000 px */
    g->speed += FL_GRAVITY;
    if (g->speed > FL_MAX_FALL) g->speed = FL_MAX_FALL;

    if (g->y - FL_BIRD_R * 1000 <= 0) { /* ceiling ends the run */
        g->y = FL_BIRD_R * 1000;
        g->over = 1;
        if (g->score > g->best) g->best = g->score;
        return;
    }
    if (g->y + FL_BIRD_R * 1000 >= FL_H * 1000) { /* floor does too */
        g->y = FL_H * 1000 - FL_BIRD_R * 1000;
        g->over = 1;
        if (g->score > g->best) g->best = g->score;
        return;
    }

    /* Scroll: speed_px is a multiple of 10 so the division is exact,
     * and even capped at 120 px/s a pair advances 12 units per step,
     * under FL_PIPE_W*10 - 2*FL_BIRD_R*10: no tunnelling. */
    int units = g->speed_px / 10;
    for (int i = 0; i < FL_PIPES; i++)
        g->x[i] = (int16_t)(g->x[i] - units);

    /* Head pair exits left; re-enters past the tail, head advances.
     * At most one pop per step: pairs sit FL_SPACING*10 apart. */
    if (g->x[g->head] + FL_PIPE_W * 10 <= 0) {
        int tail = (g->head + FL_PIPES - 1) % FL_PIPES;
        spawn(g, g->head, g->x[tail] + FL_SPACING * 10);
        g->head = (g->head + 1) % FL_PIPES;
    }

    /* Score once a pair's right edge clears the bird's centre line. */
    for (int i = 0; i < FL_PIPES; i++) {
        if (!g->passed[i] && g->x[i] + FL_PIPE_W * 10 <= FL_BIRD_X * 10) {
            g->passed[i] = 1;
            g->score++;
            int bonus = g->score / 10;
            if (bonus > 15) bonus = 15;
            g->speed_px = (uint16_t)(FL_SPEED_BASE + 4 * bonus);
        }
    }

    for (int i = 0; i < FL_PIPES; i++)
        if (hit(g, i)) {
            g->over = 1;
            g->speed = 0;
            if (g->score > g->best) g->best = g->score;
            break;
        }
}

void game_tick(Game *g, uint32_t ms) {
    g->acc += ms;
    while (g->acc >= 10) { game_step(g); g->acc -= 10; }
}

unsigned game_level(const Game *g) {
    unsigned l = g->score / 10;
    return l > 15 ? 15 : l;
}

/* Save record: magic, sizeof(Game), the struct, FNV-1a of the struct;
 * same shape as snake.c and tetris.c so the code reads alike. */
static uint32_t checksum(const uint8_t *p, unsigned n) {
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

void game_encode(const Game *g, uint8_t *out) {
    uint32_t hdr[2] = { MAGIC, (uint32_t)sizeof(Game) };
    memcpy(out, hdr, sizeof hdr);
    memcpy(out + 8, g, sizeof *g);
    uint32_t c = checksum(out + 8, sizeof *g);
    memcpy(out + 8 + sizeof *g, &c, sizeof c);
}

bool game_decode(Game *g, const uint8_t *in, unsigned size) {
    if (size != SAVE_BYTES) return false;
    uint32_t hdr[2], c;
    memcpy(hdr, in, sizeof hdr);
    if (hdr[0] != MAGIC || hdr[1] != (uint32_t)sizeof(Game)) return false;
    memcpy(&c, in + 8 + sizeof *g, sizeof c);
    if (checksum(in + 8, sizeof *g) != c) return false;

    Game t;
    memcpy(&t, in + 8, sizeof t);
    /* A hand-edited card must not smuggle in a state the rules
     * could not produce: every field gets the running game's bounds. */
    if (t.speed_px < FL_SPEED_BASE || t.speed_px > FL_SPEED_MAX) return false;
    if (t.speed < FL_FLAP_SPEED || t.speed > FL_MAX_FALL) return false;
    if (t.y < 0 || t.y > (int32_t)FL_H * 1000) return false;
    if (t.head >= FL_PIPES || t.over > 1 || t.acc >= 10) return false;
    for (int i = 0; i < FL_PIPES; i++) {
        if (t.x[i] < -FL_PIPE_W * 10 ||
            t.x[i] > FL_W * 10 + 2 * FL_SPACING * 10) return false;
        if (t.top[i] < FL_TOP_MIN * 10 || t.top[i] > FL_TOP_MAX * 10)
            return false;
        if (t.passed[i] > 1) return false;
    }
    /* The ring keeps exact FL_SPACING order from head. */
    int h1 = (t.head + 1) % FL_PIPES, h2 = (t.head + 2) % FL_PIPES;
    if (t.x[h1] - t.x[t.head] != FL_SPACING * 10 ||
        t.x[h2] - t.x[h1] != FL_SPACING * 10) return false;
    *g = t;
    return true;
}
