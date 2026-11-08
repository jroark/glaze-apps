#include "game.h"
#include <string.h>

/* Bump the last character when the Game layout changes, so old saves are refused.
 * (0x33.. = "GZG3": the play field moved down to clear the HUD row.) */
#define SAVE_MAGIC 0x33475a47u /* "GZG3" */

/* '.' empty, '1' one-hit brick, '2' tough brick (two hits), 'S' steel.
 * Each pattern is BRICK_ROWS rows of BRICK_COLS characters. Steel is left out
 * of the real levels on purpose: it can wall off a brick that then takes ages
 * to find. Each brick is worth 7 points times the level. */
static const char *PATTERNS[LEVEL_COUNT] = {
    /* level 1: three rows (33 bricks) */
    "11111111111"
    "11111111111"
    "11111111111"
    "..........."
    "..........."
    "...........",
    /* level 2: a well (38 bricks) */
    "11111111111"
    "11.......11"
    "11.......11"
    "11.......11"
    "11.......11"
    "11111111111",
    /* level 3: pyramid with a tough tip (42 bricks, 5 tough) */
    "....222...."
    "...21112..."
    "..1111111.."
    ".111111111."
    "11111111111"
    ".1.11111.1.",
    /* level 4: diamond with two tough cores (51 bricks, 2 tough) */
    "11111111.11"
    ".111121111."
    "..1112111.."
    "1.11111111."
    "111.111.111"
    ".1.11111.1.",
    /* level 5: comb, a tough crest (53 bricks, 6 tough) */
    "2.2.2.2.2.2"
    "11111111111"
    ".111111111."
    "11111111111"
    ".1.11111.1."
    "1111.1.1111",
};

/* Launch and paddle-bounce directions, indexed by hit position across the
 * paddle: index 0 is the left edge, 6 the centre (straight up). The vertical
 * component is constant, so the widest angle stays playable (about 55 degrees
 * off vertical) and the ball always makes progress toward the paddle. */
static const int8_t ANGLE_VX[13] = { -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6 };
static const int8_t ANGLE_VY[13] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 };

/* Launches use the middle entries only: no near-horizontal serves. */
#define LAUNCH_MIN 2
#define LAUNCH_MAX 10

/* Ball speed in subpx per step (32 = 2 px per step = about 125 px/s). */
#define SPEED_MAX 64
static const uint16_t SPEED_TABLE[LEVEL_COUNT] = { 32, 36, 40, 44, 48 };

/* Collisions are resolved in sub-steps of at most this many subpx (1 px), so a
 * fast ball cannot jump over a brick or the paddle. */
#define SUB_STEP 16

/* Re-aim a ball that has not broken anything for this many steps: a level
 * full of steel must still be finishable. */
#define STALL_LIMIT 600

static uint32_t random32(Game *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static int brick_value(uint8_t hp)
{
    return (hp && hp != STEEL) ? 7 : 0;
}

/* Integer sqrt rounded to the nearest value, so normalising the ball's speed
 * does not drift upward (math.h is not available). */
static int32_t isqrt32(uint32_t n)
{
    uint32_t lo = 0, hi = 65536;
    while (lo + 1 < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (mid * mid <= n)
            lo = mid;
        else
            hi = mid;
    }
    if (n - lo * lo > lo)
        lo++;
    return (int32_t)lo;
}

/* (a * mult) / div, rounded to nearest, computed in 32 bits. */
static int32_t muldiv_round(int32_t a, int mult, int32_t div)
{
    if (div <= 0)
        return a;
    int neg = a < 0;
    uint32_t m = (uint32_t)(neg ? -a : a);
    uint32_t q = (m * (uint32_t)mult + (uint32_t)div / 2) / (uint32_t)div;
    return (int32_t)(neg ? -(int32_t)q : (int32_t)q);
}

/* Scale a direction pair to |v| == speed subpx/step, keeping the sign of vy
 * and never a near-flat ball (it would crawl sideways forever). The length is
 * taken 16x scaled, so low speeds still normalise accurately. */
static void scale_speed(int32_t *vx, int32_t *vy, int speed)
{
    uint32_t m2 = (uint32_t)(*vx * *vx + *vy * *vy);
    if (!m2) { /* degenerate direction: send it up */
        *vx = 0;
        *vy = -speed;
        return;
    }
    int32_t len16 = isqrt32(m2 << 8);
    if (len16 < 16)
        len16 = 16;
    int neg = *vy < 0;
    *vx = muldiv_round(*vx, speed * 16, len16);
    *vy = muldiv_round(*vy, speed * 16, len16);
    if (*vy > -speed / 4 && *vy < speed / 4)
        *vy = neg ? -(speed / 4) : speed / 4;
}

/* Base speed: per level, +8 subpx per 8 bricks broken on this level. */
static int current_speed(const Game *g)
{
    int speed = (int)SPEED_TABLE[(g->level - 1) % LEVEL_COUNT] + (g->broken / 8) * 8;
    return speed > SPEED_MAX ? SPEED_MAX : speed;
}

static void set_stuck(Game *g)
{
    g->launch = 0;
    g->vx = g->vy = 0;
    g->ball_x = g->paddle_x;
    g->ball_y = (PADDLE_Y - 2 - BALL_HALF) * SUB;
}

/* Pick a fresh angle from the launch table: upward, unless the ball has got
 * above the bricks, where the paddle can never reach it. */
static void serve_angle(Game *g)
{
    int i = LAUNCH_MIN + (int)(random32(g) % (uint32_t)(LAUNCH_MAX - LAUNCH_MIN + 1));
    int above = g->ball_y < (BRICK_Y0 + BRICK_H) * SUB;
    g->vx = ANGLE_VX[i];
    g->vy = above ? ANGLE_VY[i] : -ANGLE_VY[i];
    scale_speed(&g->vx, &g->vy, current_speed(g));
    g->stall = 0;
}

static void random_launch(Game *g)
{
    serve_angle(g);
    g->launch = 1;
}

static void layout_level(Game *g)
{
    const char *p = PATTERNS[(g->level - 1) % LEVEL_COUNT];
    int total = 0;
    for (int i = 0; i < BRICK_ROWS * BRICK_COLS; i++) {
        uint8_t hp = p[i] == '2' ? TOUGH : (p[i] == 'S' ? STEEL : (p[i] == '1' ? 1 : 0));
        g->bricks[i / BRICK_COLS][i % BRICK_COLS] = hp;
        total += brick_value(hp);
    }
    g->remaining_val = (uint16_t)total;
    g->broken = 0;
    g->stall = 0;
}

void game_new(Game *g, uint32_t seed, uint32_t best)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->best = best;
    g->lives = MAX_LIVES;
    g->level = 1;
    g->paddle_x = ((FIELD_L + FIELD_R) / 2) * SUB;
    layout_level(g);
    set_stuck(g);
}

/* Bricks: the ball has just moved; push it out of any brick it now overlaps
 * on the axis it penetrated least, bouncing it and scoring if breakable. */
static void hit_bricks(Game *g, int *ev)
{
    for (int r = 0; r < BRICK_ROWS; r++) {
        int by = BRICK_Y0 + r * BRICK_PITCH_Y;
        int bx0 = SUB_PX(g->ball_x - BALL_HALF * SUB), bx1 = SUB_PX(g->ball_x + BALL_HALF * SUB);
        int by0 = SUB_PX(g->ball_y - BALL_HALF * SUB), by1 = SUB_PX(g->ball_y + BALL_HALF * SUB);
        if (by1 <= by || by0 >= by + BRICK_H)
            continue;
        for (int c = 0; c < BRICK_COLS; c++) {
            uint8_t hp = g->bricks[r][c];
            if (!hp)
                continue;
            int bx = BRICK_X0 + c * BRICK_PITCH_X;
            int ox1 = bx1 < bx + BRICK_W ? bx1 : bx + BRICK_W;
            int ox0 = bx0 > bx ? bx0 : bx;
            int oy1 = by1 < by + BRICK_H ? by1 : by + BRICK_H;
            int oy0 = by0 > by ? by0 : by;
            if (ox1 <= ox0 || oy1 <= oy0)
                continue; /* no overlap */
            *ev |= STEP_BRICK;
            if (ox1 - ox0 < oy1 - oy0) { /* a left or right face */
                if (g->ball_x < (bx + BRICK_W / 2) * SUB) {
                    g->ball_x = (bx - BALL_HALF) * SUB - 1;
                    if (g->vx > 0)
                        g->vx = -g->vx;
                } else {
                    g->ball_x = (bx + BRICK_W + BALL_HALF) * SUB + 1;
                    if (g->vx < 0)
                        g->vx = -g->vx;
                }
            } else if (g->ball_y < (by + BRICK_H / 2) * SUB) { /* the top face */
                g->ball_y = (by - BALL_HALF) * SUB - 1;
                if (g->vy > 0)
                    g->vy = -g->vy;
            } else { /* the bottom face */
                g->ball_y = (by + BRICK_H + BALL_HALF) * SUB + 1;
                if (g->vy < 0)
                    g->vy = -g->vy;
            }
            if (hp == STEEL) {
                /* indestructible: it only bounced the ball */
            } else if (hp > 1) {
                g->bricks[r][c] = (uint8_t)(hp - 1); /* tough brick: one hit left */
            } else {
                g->bricks[r][c] = 0;
                g->broken++;
                g->stall = 0;
                g->score += (uint32_t)brick_value(hp) * g->level;
                if (g->score > g->best)
                    g->best = g->score;
                g->remaining_val = (uint16_t)(g->remaining_val - brick_value(hp));
                scale_speed(&g->vx, &g->vy, current_speed(g));
            }
            /* re-read the box for the next brick */
            bx0 = SUB_PX(g->ball_x - BALL_HALF * SUB);
            bx1 = SUB_PX(g->ball_x + BALL_HALF * SUB);
            by0 = SUB_PX(g->ball_y - BALL_HALF * SUB);
            by1 = SUB_PX(g->ball_y + BALL_HALF * SUB);
        }
    }
}

static void hit_walls(Game *g)
{
    if (g->ball_x - BALL_HALF * SUB < FIELD_L * SUB) {
        g->ball_x = (FIELD_L + BALL_HALF) * SUB;
        if (g->vx < 0)
            g->vx = -g->vx;
    } else if (g->ball_x + BALL_HALF * SUB > FIELD_R * SUB) {
        g->ball_x = (FIELD_R - BALL_HALF) * SUB;
        if (g->vx > 0)
            g->vx = -g->vx;
    }
    if (g->ball_y - BALL_HALF * SUB < FIELD_T * SUB) {
        g->ball_y = (FIELD_T + BALL_HALF) * SUB;
        if (g->vy < 0)
            g->vy = -g->vy;
    }
}

/* The paddle turns the ball upward; where it landed sets the outgoing angle. */
static void hit_paddle(Game *g, int *ev)
{
    if (g->vy <= 0 ||
        g->ball_y + BALL_HALF * SUB < PADDLE_Y * SUB ||
        g->ball_y - BALL_HALF * SUB >= (PADDLE_Y + PADDLE_H) * SUB ||
        g->ball_x + BALL_HALF * SUB < g->paddle_x - PADDLE_W * SUB / 2 ||
        g->ball_x - BALL_HALF * SUB > g->paddle_x + PADDLE_W * SUB / 2)
        return;
    int hit = (int)(g->ball_x - g->paddle_x) / SUB; /* px from the centre */
    int idx = (hit * 12 + PADDLE_W * 13 / 2) / PADDLE_W;
    if (idx < 0)
        idx = 0;
    if (idx > 12)
        idx = 12;
    g->vx = ANGLE_VX[idx];
    g->vy = -ANGLE_VY[idx];
    if (g->vx == 0) /* a dead-centre hit: tilt it so the rally cannot repeat */
        g->vx = (random32(g) & 1) ? 2 : -2;
    scale_speed(&g->vx, &g->vy, current_speed(g));
    g->ball_y = (PADDLE_Y - BALL_HALF) * SUB - 1;
    *ev |= STEP_PADDLE;
}

/* Below the drop line the ball is gone. */
static void check_drop(Game *g, int *ev)
{
    if (g->ball_y <= FIELD_BOTTOM * SUB)
        return;
    *ev |= STEP_LIFE;
    g->lives--;
    g->stall = 0;
    if (!g->lives) {
        g->over = 1;
        *ev |= STEP_OVER;
    }
    set_stuck(g);
}

/* One frame of ball motion. Collisions are resolved in sub-steps of at most
 * 1 px, so even at the top speed nothing gets jumped over. */
static void advance_ball(Game *g, int *ev)
{
    int32_t mag = (g->vx < 0 ? -g->vx : g->vx) + (g->vy < 0 ? -g->vy : g->vy);
    int sub = (int)((mag + SUB_STEP - 1) / SUB_STEP);
    if (sub < 1)
        sub = 1;
    if (sub > 8)
        sub = 8;
    for (int i = 1; i <= sub && g->launch; i++) {
        g->ball_x += g->vx * i / sub - g->vx * (i - 1) / sub;
        g->ball_y += g->vy * i / sub - g->vy * (i - 1) / sub;
        hit_bricks(g, ev);
        hit_walls(g);
        hit_paddle(g, ev);
        check_drop(g, ev);
    }
}

int game_step(Game *g)
{
    int ev = 0;
    if (g->over)
        return 0;

    int dir = g->paddle_input;
    if (dir < -1 || dir > 1)
        dir = 0;
    g->paddle_x += dir * PADDLE_STEP * SUB;
    if (g->paddle_x < (FIELD_L + PADDLE_W / 2) * SUB)
        g->paddle_x = (FIELD_L + PADDLE_W / 2) * SUB;
    if (g->paddle_x > (FIELD_R - PADDLE_W / 2) * SUB)
        g->paddle_x = (FIELD_R - PADDLE_W / 2) * SUB;

    if (!g->launch) {
        g->ball_x = g->paddle_x;
        return 0;
    }

    advance_ball(g, &ev);

    /* A ball that bounces around without breaking anything (steel walls can
     * mirror it forever) gets a fresh upward angle. */
    if (g->launch && ++g->stall >= STALL_LIMIT) {
        serve_angle(g);
        ev |= STEP_STALL;
    }

    if (g->remaining_val == 0 && !g->over) {
        ev |= STEP_LEVEL;
        if (g->level < MAX_LEVEL)
            g->level++;
        layout_level(g);
        set_stuck(g);
    }
    return ev;
}

void game_launch(Game *g)
{
    if (g->over || g->launch)
        return;
    random_launch(g);
}

void game_paddle_input(Game *g, int dir)
{
    if (dir < -1 || dir > 1)
        dir = 0;
    g->paddle_input = (int8_t)dir;
}

void game_set_paddle(Game *g, int center_px)
{
    int32_t x = (int32_t)center_px * SUB;
    int32_t lo = (FIELD_L + PADDLE_W / 2) * SUB, hi = (FIELD_R - PADDLE_W / 2) * SUB;
    if (x < lo)
        x = lo;
    if (x > hi)
        x = hi;
    g->paddle_x = x;
    if (!g->launch)
        g->ball_x = x;
}

void game_brick_rect(int r, int c, int *x, int *y)
{
    *x = BRICK_X0 + c * BRICK_PITCH_X;
    *y = BRICK_Y0 + r * BRICK_PITCH_Y;
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
    if (t.paddle_x < (FIELD_L + PADDLE_W / 2) * SUB || t.paddle_x > (FIELD_R - PADDLE_W / 2) * SUB)
        return false;
    if (t.paddle_input < -1 || t.paddle_input > 1)
        return false;
    if (t.launch > 1 || (t.launch && (!t.vx || !t.vy)) || (!t.launch && (t.vx || t.vy)))
        return false;
    if (t.vx < -96 || t.vx > 96 || t.vy < -96 || t.vy > 96)
        return false;
    if (t.over > 1 || t.lives > MAX_LIVES || (t.lives == 0 && !t.over))
        return false;
    if (!t.rng || t.level < 1 || t.level > MAX_LEVEL)
        return false;
    if (t.best < t.score || t.score > 1000000u || t.broken > BRICK_ROWS * BRICK_COLS)
        return false;
    if (t.stall >= STALL_LIMIT)
        return false;
    if (t.ball_x < (FIELD_L + BALL_HALF) * SUB || t.ball_x > (FIELD_R - BALL_HALF) * SUB ||
        t.ball_y < (FIELD_T + BALL_HALF) * SUB || t.ball_y > FIELD_BOTTOM * SUB)
        return false;
    if (!t.launch && (t.ball_x != t.paddle_x ||
                      t.ball_y != (PADDLE_Y - 2 - BALL_HALF) * SUB))
        return false;
    if (t.remaining_val > 1000)
        return false;
    int rem = 0;
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++) {
            uint8_t hp = t.bricks[r][c];
            if (hp != 0 && hp != 1 && hp != 2 && hp != STEEL)
                return false;
            rem += brick_value(hp);
        }
    if ((uint16_t)rem != t.remaining_val)
        return false;
    *g = t;
    return true;
}
