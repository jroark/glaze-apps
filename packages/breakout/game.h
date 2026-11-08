#ifndef BREAKOUT_GAME_H
#define BREAKOUT_GAME_H
/* Breakout rules. Pure C: no glaze/ includes, so tests/native/breakout.c can
 * test it on the host. main.c draws it and feeds it input. */
#include <stdbool.h>
#include <stdint.h>

/* Integer-only geometry. Positions and speeds are in 1/16 px; the field
 * rectangle is shared with main.c so drawing matches the physics. */
#define SUB 16
#define SUB_PX(v) ((v) / SUB)

/* FIELD_T sits below the HUD row main.c draws (y 33..47), so the wall bars
 * never cross the score, level or ball dots. */
#define FIELD_L 6    /* ball centre bounces inside these walls */
#define FIELD_R 233
#define FIELD_T 58
#define FIELD_BOTTOM 296

#define BALL_HALF 3  /* drawn as a circle of radius 3 */
#define MAX_LIVES 3

#define PADDLE_W 44
#define PADDLE_H 8
#define PADDLE_Y 284 /* top edge of the paddle */
#define PADDLE_STEP 6 /* px per game_step while a direction is held */

#define BRICK_COLS 11
#define BRICK_ROWS 6
#define BRICK_X0 10
#define BRICK_Y0 66
#define BRICK_W 18
#define BRICK_H 13
#define BRICK_PITCH_X 21 /* BRICK_W + 3 */
#define BRICK_PITCH_Y 16 /* BRICK_H + 3 */

#define STEEL 99 /* hp that bounces the ball but never breaks */
#define TOUGH 2  /* hp of a brick that needs two hits */
#define LEVEL_COUNT 5
#define MAX_LEVEL 9

/* game_step return value: bitwise-OR of what happened this step. */
enum {
    STEP_PADDLE = 1,          /* ball bounced off the paddle */
    STEP_BRICK = 2,           /* a brick lost hp or broke */
    STEP_LIFE = 4,            /* a ball was lost */
    STEP_LEVEL = 8,           /* the level was cleared; the next one started */
    STEP_OVER = 16,           /* the last ball was lost; game over */
    STEP_STALL = 32           /* a stuck-around ball was re-aimed upward */
};

typedef struct {
    int32_t paddle_x;     /* paddle centre, subpx; clamped inside the walls */
    int8_t paddle_input;  /* -1 left, 0 none, +1 right (main sets from keys) */
    uint8_t launch;       /* 1 = ball flying, 0 = ball stuck to the paddle */
    uint8_t over, pad0;
    int32_t ball_x, ball_y;   /* centre, subpx */
    int32_t vx, vy;           /* subpx per step (32 = 2 px); 0 while stuck */
    uint8_t lives;
    uint8_t level;            /* 1..MAX_LEVEL; pattern = (level - 1) % LEVEL_COUNT */
    uint8_t broken;           /* bricks broken on this level (drives ball speed) */
    uint8_t pad1;
    uint16_t stall;           /* steps since the last brick broke (anti-stuck) */
    uint8_t bricks[BRICK_ROWS][BRICK_COLS]; /* 0 gone, 1..2 hp, STEEL unbreakable */
    uint32_t rng;             /* xorshift state, never 0 */
    uint32_t score, best;
    uint16_t remaining_val;   /* points still on the field, incl. steel = 0 */
} Game;

#define STEP_MS 16u

void game_new(Game *g, uint32_t seed, uint32_t best);
int game_step(Game *g); /* one STEP_MS step; returns STEP_* flags */
void game_launch(Game *g);          /* release a stuck ball; no-op when flying */
void game_paddle_input(Game *g, int dir); /* -1/0/+1; other values -> 0 */
void game_set_paddle(Game *g, int center_px); /* touch: centre on this px */

/* Brick row r, column c at (x, y) in px, BRICK_W x BRICK_H. */
void game_brick_rect(int r, int c, int *x, int *y);

/* Save record: 12-byte header (magic, size, checksum) then the Game. */
#define SAVE_BYTES (12 + sizeof(Game))
void game_encode(const Game *g, uint8_t *out);
/* Rejects wrong size, magic, checksum or impossible state. */
bool game_decode(Game *g, const uint8_t *in, unsigned size);
#endif
