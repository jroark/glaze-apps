#include "game.h"
#include <string.h>

/* Bump the last character when the Game layout changes, so old saves are refused. */
#define SAVE_MAGIC 0x31475a47u /* "GZG1" */

/* Maze: '#' wall, '.' pellet, 'o' power pellet, 'D' ghost house door,
 * 'H' ghost house interior (only ghosts stand there), ' ' plain floor.
 * Every row is left-right symmetric; the maze mirrors top-to-bottom except
 * for the house (rows 7-9). The ends of row 8 are the tunnel: it wraps. */
static const char *MAZE[MAZE_H] = {
    "###############",
    "#o...........o#",
    "#.#.##.#.##.#.#",
    "#.####.#.####.#",
    "#.#.........#.#",
    "#.#.##.#.##.#.#",
    "#.#.........#.#",
    "#.#.###D###.#.#",
    "..####HHH####..",
    "#.#.#######.#.#",
    "#.#.... ....#.#",
    "#.#.##.#.##.#.#",
    "#.#.........#.#",
    "#.####.#.####.#",
    "#.#.##.#.##.#.#",
    "#o...........o#",
    "###############",
};

static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
/* Ghost tie-break order at equal distance: up, left, down, right. */
static const int PREF[4] = {DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT};
/* Scatter corners per ghost id. */
static const int SCAT_X[GHOST_COUNT] = {13, 1, 13, 1};
static const int SCAT_Y[GHOST_COUNT] = {0, 0, MAZE_H - 1, MAZE_H - 1};

#define HOUSE_Y 8
#define DOOR_TILE (7 * MAZE_W + 7)
#define HOUSE_MID (HOUSE_Y * MAZE_W + 7)
#define PAC_START TOFU_TILE
#define TOFU_T 500u    /* a fruit stays this many steps */
#define HOUSE_RE_RELEASE 90u /* a ghost eaten returns here for this long */
#define FRIGHT_MAX 375u      /* longest power mode (level 1) */
#define RELEASE_MAX 500u

static uint32_t random32(Game *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static char maze_char(int tile)
{
    return MAZE[tile / MAZE_W][tile % MAZE_W];
}

bool game_walk(int tile, bool ghost)
{
    if (tile < 0 || tile >= MAZE_W * MAZE_H)
        return false;
    char c = maze_char(tile);
    if (c == '#')
        return false;
    if (c == 'H' || c == 'D')
        return ghost;
    return true;
}

static int is_house_tile(int t)
{
    return t >= HOUSE_Y * MAZE_W + 6 && t <= HOUSE_Y * MAZE_W + 8;
}

static int neigh(int tile, int dir)
{
    int x = tile % MAZE_W + DX[dir], y = tile / MAZE_W + DY[dir];
    if (x < 0)
        x += MAZE_W; /* tunnel wrap; only open at row 8 */
    if (x >= MAZE_W)
        x -= MAZE_W;
    if (y < 0 || y >= MAZE_H)
        return -1;
    return y * MAZE_W + x;
}

static int dot_base(int tile)
{
    char c = maze_char(tile);
    return c == '.' || c == 'o';
}

int game_total_dots(void)
{
    int n = 0;
    for (int t = 0; t < MAZE_W * MAZE_H; t++)
        n += dot_base(t);
    return n;
}

static int eaten_get(const Game *g, int tile)
{
    return (g->eaten[tile / 8] >> (tile % 8)) & 1;
}

static void eaten_set(Game *g, int tile)
{
    g->eaten[tile / 8] = (uint8_t)(g->eaten[tile / 8] | (1u << (tile % 8)));
}

bool game_dot_at(const Game *g, int tile)
{
    return tile >= 0 && tile < MAZE_W * MAZE_H && dot_base(tile) && !eaten_get(g, tile);
}

bool game_power_at(const Game *g, int tile)
{
    return tile >= 0 && tile < MAZE_W * MAZE_H && maze_char(tile) == 'o' && !eaten_get(g, tile);
}

uint8_t pac_speed(const Game *g)
{
    (void)g; /* speed does not scale with level in this build */
    return 8; /* 0.5 px per step; 2.2 tiles/s */
}

uint8_t ghost_speed(const Game *g, int gi)
{
    if (g->g[gi].in_house)
        return 6;
    if (g->frightened)
        return 5;
    return gi == G_RED ? 8 : 7;
}

static uint16_t fright_steps(const Game *g)
{
    int v = (int)FRIGHT_MAX - 60 * (int)(g->level - 1);
    return (uint16_t)(v < 125 ? 125 : v);
}

static void place_entities(Game *g)
{
    g->pac_tile = PAC_START;
    g->pac_sub = 112;
    g->pac_dir = DIR_LEFT;
    g->desired = DIR_NONE;
    g->g[G_RED].tile = (uint8_t)(6 * MAZE_W + 7);
    g->g[G_RED].sub = 112;
    g->g[G_RED].dir = DIR_LEFT;
    g->g[G_RED].in_house = 0;
    g->g[G_RED].release = 0;
    g->g[G_PINK].tile = (uint8_t)HOUSE_MID;
    g->g[G_PINK].sub = 112;
    g->g[G_PINK].dir = DIR_UP;
    g->g[G_PINK].in_house = 1;
    g->g[G_PINK].release = 125;
    g->g[G_CYAN].tile = (uint8_t)(HOUSE_Y * MAZE_W + 6);
    g->g[G_CYAN].sub = 112;
    g->g[G_CYAN].dir = DIR_UP;
    g->g[G_CYAN].in_house = 1;
    g->g[G_CYAN].release = 312;
    g->g[G_ORANGE].tile = (uint8_t)(HOUSE_Y * MAZE_W + 8);
    g->g[G_ORANGE].sub = 112;
    g->g[G_ORANGE].dir = DIR_UP;
    g->g[G_ORANGE].in_house = 1;
    g->g[G_ORANGE].release = RELEASE_MAX;
    g->frightened = 0;
    g->fright_timer = 0;
    g->chain = 0;
    g->phase_t = 0;
    g->state = ST_PLAY;
    g->state_timer = 0;
    g->tofu = 0;
    g->tofu_n = 0;
    g->tofu_t = 0;
}

static void new_board(Game *g)
{
    memset(g->eaten, 0, sizeof(g->eaten));
    g->total_dots = (uint8_t)game_total_dots();
    g->dots_eaten = 0;
    place_entities(g);
}

void game_new(Game *g, uint32_t seed, uint32_t best)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->best = best;
    g->lives = 3;
    g->level = 1;
    new_board(g);
}

void game_set_dir(Game *g, int dir)
{
    if (dir >= 0 && dir < 4)
        g->desired = (uint8_t)dir;
}

/* Scatter/chase schedule in sim steps since the life began (fright pauses it). */
static int in_scatter(const Game *g)
{
    return g->phase_t < 440 || (g->phase_t >= 1690 && g->phase_t < 2130);
}

/* Target tile for ghost gi while chasing. Returns 0 = choose randomly. */
static int ghost_target(const Game *g, int gi, int *tx, int *ty)
{
    if (g->g[gi].in_house) {
        *tx = DOOR_TILE % MAZE_W;
        *ty = DOOR_TILE / MAZE_W;
        return 1;
    }
    if (g->frightened)
        return 0;
    if (in_scatter(g)) {
        *tx = SCAT_X[gi];
        *ty = SCAT_Y[gi];
        return 1;
    }
    int px = g->pac_tile % MAZE_W, py = g->pac_tile / MAZE_W;
    int d = g->pac_dir;
    switch (gi) {
    case G_RED:
        *tx = px;
        *ty = py;
        break;
    case G_PINK:
        *tx = px + 4 * DX[d];
        *ty = py + 4 * DY[d];
        break;
    case G_CYAN: {
        int bx = g->g[G_RED].tile % MAZE_W, by = g->g[G_RED].tile / MAZE_W;
        int ax = px + 2 * DX[d], ay = py + 2 * DY[d];
        *tx = 2 * ax - bx;
        *ty = 2 * ay - by;
        break;
    }
    default: { /* Orange: chases from far, goes home when close */
        int cx = g->g[G_ORANGE].tile % MAZE_W, cy = g->g[G_ORANGE].tile / MAZE_W;
        int dist2 = (cx - px) * (cx - px) + (cy - py) * (cy - py);
        if (dist2 > 64) {
            *tx = px;
            *ty = py;
        } else {
            *tx = SCAT_X[G_ORANGE];
            *ty = SCAT_Y[G_ORANGE];
        }
        break;
    }
    }
    return 1;
}

/* Direction for ghost gi at its tile centre. */
static int pick_dir(const Game *g, int gi, int tile)
{
    int cand[4], nc = 0;
    int reverse = (g->g[gi].dir + 2) % 4;
    for (int i = 0; i < 4; i++) {
        int d = PREF[i], t = neigh(tile, d);
        if (t < 0 || !game_walk(t, true) || d == reverse)
            continue;
        /* house ghosts stay inside; free ghosts never go back in */
        if (g->g[gi].in_house ? !game_walk(t, true) : (is_house_tile(t) || t == DOOR_TILE))
            continue;
        cand[nc++] = d;
    }
    if (nc == 0)
        return reverse; /* dead end: turn around */
    int tx, ty;
    if (!ghost_target(g, gi, &tx, &ty)) {
        int r = (int)(random32((Game *)g) % (uint32_t)nc);
        return cand[r];
    }
    int best = cand[0], bd = 0x7fffffff;
    for (int i = 0; i < nc; i++) {
        int t = neigh(tile, cand[i]);
        int dx = t % MAZE_W - tx, dy = t / MAZE_W - ty;
        int dist = dx * dx + dy * dy;
        if (dist < bd) { /* PREF order breaks ties */
            bd = dist;
            best = cand[i];
        }
    }
    return best;
}

/* Eat what Pac stands on at the centre of tile. */
static void collect(Game *g, int tile)
{
    if (game_power_at(g, tile)) {
        eaten_set(g, tile);
        g->dots_eaten++;
        g->score += 50;
        g->frightened = 1;
        g->fright_timer = fright_steps(g);
        g->chain = 200;
        for (int i = 0; i < GHOST_COUNT; i++) {
            if (g->g[i].in_house)
                continue;
            g->g[i].dir = (uint8_t)((g->g[i].dir + 2) % 4);
            if (g->g[i].sub)
                g->g[i].sub = (uint8_t)(224 - g->g[i].sub);
        }
    } else if (game_dot_at(g, tile)) {
        eaten_set(g, tile);
        g->dots_eaten++;
        g->score += 10;
        if ((g->dots_eaten == 20 || g->dots_eaten == 50) && g->tofu_n < 2) {
            g->tofu = 1;
            g->tofu_n++;
            g->tofu_t = TOFU_T;
        }
    }
    if (g->tofu && tile == TOFU_TILE) {
        g->tofu = 0;
        g->score += 100;
    }
    if (g->score > g->best)
        g->best = g->score;
}

/* Decision at Pac's tile centre; returns -1 to stop there. */
static int pac_decide(void *ctx, int tile)
{
    Game *g = (Game *)ctx;
    collect(g, tile);
    int want = g->desired;
    if (want < 4 && game_walk(neigh(tile, want), false)) {
        g->desired = (uint8_t)want;
        return want;
    }
    if (game_walk(neigh(tile, g->pac_dir), false))
        return g->pac_dir;
    return -1;
}

typedef struct { Game *g; int gi; } GRef;

static int ghost_decide(void *ctx, int tile)
{
    GRef *r = (GRef *)ctx;
    return pick_dir(r->g, r->gi, tile);
}

/* Move an entity `speed` units along corridors, calling decide() at every
 * tile centre; decide returns a new direction or -1 to stop at the centre. */
static void move_entity(uint8_t *tile, uint8_t *sub, uint8_t *dir, int speed,
                        bool ghost, int (*decide)(void *, int), void *ctx)
{
    int remaining = speed;
    while (remaining > 0) {
        if (*sub == 112) {
            int d = decide(ctx, *tile);
            if (d < 0)
                return;
            *dir = (uint8_t)d;
        }
        if (*sub < 112) {
            int step = 112 - *sub;
            if (step > remaining)
                step = remaining;
            *sub = (uint8_t)(*sub + step);
            remaining -= step;
        } else { /* heading for the far edge and the next tile */
            int step = 224 - *sub;
            if (step > remaining)
                step = remaining;
            *sub = (uint8_t)(*sub + step);
            remaining -= step;
            if (*sub == 224) {
                int t = neigh(*tile, *dir);
                if (!game_walk(t, ghost)) {
                    *sub = 223; /* blocked (defensive): sit at the edge */
                    return;
                }
                *tile = (uint8_t)t;
                *sub = 0;
            }
        }
    }
}

/* Absolute 1/16 px position of an entity's centre, for collision distance.
 * sub counts *along dir*, so it mirrors when dir points left or up. */
void game_abs(int tile, int sub, int dir, int *x, int *y)
{
    *x = (tile % MAZE_W) * 224 +
         (dir == DIR_RIGHT ? sub : dir == DIR_LEFT ? 224 - sub : 112);
    *y = (tile / MAZE_W) * 224 +
         (dir == DIR_DOWN ? sub : dir == DIR_UP ? 224 - sub : 112);
}

static void send_home(Game *g, int gi)
{
    g->g[gi].tile = (uint8_t)HOUSE_MID;
    g->g[gi].sub = 112;
    g->g[gi].dir = DIR_UP;
    g->g[gi].in_house = 1;
    g->g[gi].release = HOUSE_RE_RELEASE;
}

/* One full simulation step. */
static void step_once(Game *g)
{
    g->step_cnt++;

    if (g->state == ST_DYING) {
        if (g->state_timer)
            g->state_timer--;
        if (!g->state_timer) {
            if (g->lives > 1) {
                g->lives--;
                place_entities(g);
            } else {
                g->lives = 0;
                g->state = ST_OVER;
            }
        }
        return;
    }
    if (g->state != ST_PLAY)
        return; /* ST_WIN and ST_OVER freeze the board */

    if (!g->frightened && g->phase_t < 60000)
        g->phase_t++;
    if (g->fright_timer) {
        g->fright_timer--;
        if (!g->fright_timer) {
            g->frightened = 0;
            g->chain = 0;
        }
    }
    if (g->tofu) {
        g->tofu_t--;
        if (!g->tofu_t)
            g->tofu = 0;
    }

    move_entity(&g->pac_tile, &g->pac_sub, &g->pac_dir, pac_speed(g), false,
                pac_decide, g);

    for (int i = 0; i < GHOST_COUNT; i++) {
        Ghost *gh = &g->g[i];
        if (gh->in_house) {
            if (gh->release) {
                gh->release--;
                if (!gh->release) {
                    /* released: snap to the middle tile, facing the door */
                    gh->tile = (uint8_t)HOUSE_MID;
                    gh->sub = 112;
                    gh->dir = DIR_UP;
                } else {
                    /* bounce up and down inside the house: sub counts along
                     * dir, so flipping direction mirrors the offset. The
                     * flip happens instead of the step, which keeps sub
                     * inside [40,184] forever. */
                    if (gh->dir == DIR_UP) {
                        if (gh->sub > 40)
                            gh->sub = (uint8_t)(gh->sub - 2);
                        else {
                            gh->dir = DIR_DOWN;
                            gh->sub = (uint8_t)(224 - gh->sub);
                        }
                    } else {
                        if (gh->sub < 184)
                            gh->sub = (uint8_t)(gh->sub + 2);
                        else {
                            gh->dir = DIR_UP;
                            gh->sub = (uint8_t)(224 - gh->sub);
                        }
                    }
                }
                continue;
            }
            GRef ref = {g, i};
            move_entity(&gh->tile, &gh->sub, &gh->dir, ghost_speed(g, i), true,
                        ghost_decide, &ref);
            if (gh->tile == DOOR_TILE)
                gh->in_house = 0;
            continue;
        }
        GRef ref = {g, i};
        move_entity(&gh->tile, &gh->sub, &gh->dir, ghost_speed(g, i), true,
                    ghost_decide, &ref);

        /* Collision: near the same point (speeds are far below the threshold). */
        int px, py, gx, gy;
        game_abs(g->pac_tile, g->pac_sub, g->pac_dir, &px, &py);
        game_abs(gh->tile, gh->sub, gh->dir, &gx, &gy);
        int dx = px > gx ? px - gx : gx - px, dy = py > gy ? py - gy : gy - py;
        if (dx < 44 && dy < 44) {
            if (g->frightened) {
                g->score += g->chain ? g->chain : 200;
                if (g->score > g->best)
                    g->best = g->score;
                g->chain = (uint16_t)(g->chain ? g->chain * 2 : 400);
                if (g->chain > 1600)
                    g->chain = 1600;
                send_home(g, i);
            } else {
                g->state = ST_DYING;
                g->state_timer = DYING_STEPS;
                return;
            }
        }
    }

    if (g->dots_eaten >= g->total_dots)
        g->state = ST_WIN;
}

void game_tick(Game *g, uint32_t ms)
{
    if (ms > 100)
        ms = 100; /* a stall must not teleport anything */
    while (ms >= SIM_MS) {
        ms -= SIM_MS;
        step_once(g);
    }
}

void game_next_level(Game *g)
{
    if (g->level < 99)
        g->level++;
    new_board(g);
}

/* ---- save / load ---- */

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
    if (!t.rng || t.lives > 3 || t.lives == 0 || t.level < 1)
        return false;
    if (t.state > ST_OVER)
        return false;
    if (t.score > t.best || t.best > 9999990u)
        return false;
    if (t.total_dots != (uint8_t)game_total_dots() || t.dots_eaten > t.total_dots)
        return false;
    for (int tile = 0; tile < MAZE_W * MAZE_H; tile++)
        if (eaten_get(&t, tile) && !dot_base(tile))
            return false;
    if (!game_walk(t.pac_tile, false) || t.pac_sub > 223 || t.pac_dir > 3)
        return false;
    if (t.desired != 255 && t.desired > 3)
        return false;
    for (int i = 0; i < GHOST_COUNT; i++) {
        const Ghost *gh = &t.g[i];
        if (!game_walk(gh->tile, true) || gh->sub > 223 || gh->dir > 3)
            return false;
        if (gh->in_house > 1)
            return false;
        if (gh->in_house && !is_house_tile(gh->tile))
            return false;
        /* a free ghost may pass the door for one step while leaving,
         * but must never stand inside the house */
        if (!gh->in_house && is_house_tile(gh->tile))
            return false;
        if (gh->release > RELEASE_MAX)
            return false;
    }
    if (t.frightened > 1 || (t.frightened && !t.fright_timer) ||
        t.fright_timer > FRIGHT_MAX)
        return false;
    if (t.chain > 1600)
        return false;
    if (t.state == ST_DYING && (!t.state_timer || t.state_timer > DYING_STEPS))
        return false;
    if (t.state != ST_DYING && t.state_timer)
        return false;
    if (t.phase_t > 60000 || (t.step_cnt == 0 && t.score))
        return false;
    if (t.tofu > 1 || t.tofu_n > 2 || t.tofu_t > TOFU_T || (!t.tofu && t.tofu_t))
        return false;
    *g = t;
    return true;
}
