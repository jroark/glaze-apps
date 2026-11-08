/* Native tests for packages/breakout/game.c rules. */
#include "../../packages/breakout/game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Restate the geometry here so the test checks game.c, not its helpers. */
static int brick_x(int c) { return 10 + c * 21; }
static int brick_y(int r) { return 66 + r * 16; }

static int count_bricks(const Game *g, int *steel)
{
    int alive = 0;
    if (steel)
        *steel = 0;
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++) {
            if (g->bricks[r][c] == STEEL) {
                if (steel)
                    (*steel)++;
            } else if (g->bricks[r][c]) {
                alive++;
            }
        }
    return alive;
}

static void test_new(void)
{
    Game g;
    game_new(&g, 12345, 999);
    assert(g.lives == MAX_LIVES);
    assert(g.level == 1);
    assert(g.score == 0);
    assert(g.best == 999);
    assert(g.broken == 0);
    assert(!g.over);
    assert(!g.launch);
    assert(g.vx == 0 && g.vy == 0);
    assert(g.rng != 0);
    assert(g.paddle_x == ((FIELD_L + FIELD_R) / 2) * SUB);
    /* the stuck ball sits on the paddle */
    assert(g.ball_x == g.paddle_x);
    assert(g.ball_y == (PADDLE_Y - 2 - BALL_HALF) * SUB);

    /* level 1: three full rows of bricks, the lower three rows empty */
    int alive = count_bricks(&g, NULL);
    assert(alive == 33);
    assert(g.remaining_val == 33 * 7);
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            assert(g.bricks[r][c] == (r < 3 ? 1 : 0));

    /* determinism: same seed, same state; layout is seed-independent */
    Game a, b;
    game_new(&a, 7, 0);
    game_new(&b, 7, 0);
    assert(memcmp(&a, &b, sizeof(a)) == 0);
    game_new(&a, 7, 0);
    game_new(&b, 8, 0);
    assert(a.rng != b.rng);
    assert(memcmp(a.bricks, b.bricks, sizeof(a.bricks)) == 0);

    /* game_brick_rect matches the geometry the test assumes */
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++) {
            int x, y;
            game_brick_rect(r, c, &x, &y);
            assert(x == brick_x(c) && y == brick_y(r));
        }
}

static void test_paddle(void)
{
    Game g;
    game_new(&g, 1, 0);

    game_paddle_input(&g, -1);
    game_step(&g);
    assert(g.paddle_x == (((FIELD_L + FIELD_R) / 2) - PADDLE_STEP) * SUB);
    game_paddle_input(&g, 0);
    int32_t at = g.paddle_x;
    game_step(&g);
    assert(g.paddle_x == at);
    game_paddle_input(&g, 1);
    game_step(&g);
    assert(g.paddle_x == ((FIELD_L + FIELD_R) / 2) * SUB); /* back to the centre */

    game_paddle_input(&g, 5); /* illegal values -> 0 */
    game_step(&g);
    assert(g.paddle_input == 0);
    assert(g.paddle_x == ((FIELD_L + FIELD_R) / 2) * SUB);

    /* held input clamps at the walls */
    for (int i = 0; i < 20; i++) {
        game_paddle_input(&g, -1);
        game_step(&g);
    }
    assert(g.paddle_x == (FIELD_L + PADDLE_W / 2) * SUB);
    for (int i = 0; i < 40; i++) {
        game_paddle_input(&g, 1);
        game_step(&g);
    }
    assert(g.paddle_x == (FIELD_R - PADDLE_W / 2) * SUB);

    /* touch placement */
    game_set_paddle(&g, 100);
    assert(g.paddle_x == 100 * SUB);
    game_set_paddle(&g, -50);
    assert(g.paddle_x == (FIELD_L + PADDLE_W / 2) * SUB);
    game_set_paddle(&g, 1000);
    assert(g.paddle_x == (FIELD_R - PADDLE_W / 2) * SUB);

    /* a stuck ball follows the paddle */
    game_set_paddle(&g, 120);
    game_paddle_input(&g, 1);
    game_step(&g);
    assert(g.ball_x == g.paddle_x);
    assert(!g.launch);
    assert(g.vy == 0);
}

static void test_launch_and_walls(void)
{
    Game g;
    game_new(&g, 42, 0);

    game_launch(&g);
    assert(g.launch);
    assert(g.vy < 0); /* always served upward */
    assert(g.vx != 0 || g.vy != 0);
    int32_t vx = g.vx, vy = g.vy;
    game_launch(&g); /* no-op once flying */
    assert(g.vx == vx && g.vy == vy);
    int32_t x = g.ball_x;
    game_step(&g);
    assert(g.ball_x == x + vx);

    /* left wall */
    game_new(&g, 1, 0);
    g.launch = 1;
    g.vx = -2;
    g.vy = -2;
    g.ball_x = (FIELD_L + BALL_HALF) * SUB + 1; /* one step off the wall */
    g.ball_y = 200 * SUB;
    game_step(&g);
    assert(g.vx == 2);
    assert(g.ball_x >= (FIELD_L + BALL_HALF) * SUB);

    /* right wall */
    g.ball_x = (FIELD_R - BALL_HALF) * SUB - 1;
    g.vx = 2;
    game_step(&g);
    assert(g.vx == -2);
    assert(g.ball_x <= (FIELD_R - BALL_HALF) * SUB);

    /* top wall */
    g.ball_x = 120 * SUB;
    g.ball_y = (FIELD_T + BALL_HALF) * SUB + 1;
    g.vx = 2;
    g.vy = -2;
    game_step(&g);
    assert(g.vy == 2);
    assert(g.ball_y >= (FIELD_T + BALL_HALF) * SUB);
    assert(g.score == 0); /* walls are not bricks */
}

static void test_bricks(void)
{
    Game g;
    game_new(&g, 1, 0);
    g.launch = 1;
    g.ball_x = (brick_x(3) + BRICK_W / 2) * SUB;
    g.ball_y = (brick_y(2) + BRICK_H + BALL_HALF + 1) * SUB; /* below the lowest row */
    g.vx = 0;
    g.vy = -16;

    int ev = 0;
    for (int i = 0; i < 20 && !(ev & STEP_BRICK); i++)
        ev = game_step(&g);
    assert(ev & STEP_BRICK);
    assert(g.vy > 0); /* sent back down */
    assert(g.vx == 0);
    assert(g.bricks[2][3] == 0);
    assert(g.score == 7u); /* level 1 pays 7 * 1 */
    assert(g.best == 7u);
    assert(g.broken == 1);
    assert(g.remaining_val == 32 * 7);
    assert(g.lives == MAX_LIVES);

    /* steel bounces but is never scored or removed (one normal brick is left
     * on the field so the level does not clear) */
    memset(g.bricks, 0, sizeof(g.bricks));
    g.bricks[0][0] = STEEL;
    g.bricks[5][5] = 1;
    g.remaining_val = 7;
    g.score = 0;
    g.broken = 0;
    g.ball_x = (brick_x(0) + BRICK_W / 2) * SUB;
    g.ball_y = (brick_y(0) - BALL_HALF - 1) * SUB;
    g.vx = 0;
    g.vy = 16;
    ev = 0;
    for (int i = 0; i < 20 && !(ev & STEP_BRICK); i++)
        ev = game_step(&g);
    assert(ev & STEP_BRICK);
    assert(g.vy == -16); /* steel bounces without rescaling the ball */
    assert(g.bricks[0][0] == STEEL);
    assert(g.score == 0);
    assert(g.broken == 0);
    assert(g.remaining_val == 7);

    /* a side hit flips vx and scores */
    int32_t len2;
    memset(g.bricks, 0, sizeof(g.bricks));
    g.bricks[2][4] = 1;
    g.bricks[5][5] = 1;
    g.remaining_val = 14;
    g.score = 0;
    g.broken = 0;
    g.ball_x = (brick_x(4) - BALL_HALF - 1) * SUB; /* just left of the brick */
    g.ball_y = (brick_y(2) + BRICK_H / 2) * SUB;   /* mid-height of the row */
    g.vx = 16;
    g.vy = -16;
    ev = 0;
    for (int i = 0; i < 20 && !(ev & STEP_BRICK); i++)
        ev = game_step(&g);
    assert(ev & STEP_BRICK);
    assert(g.vx < 0); /* back the way it came */
    len2 = g.vx * g.vx + g.vy * g.vy;
    assert(len2 >= 26 * 26 && len2 <= 34 * 34); /* rescaled to the level speed */
    assert(g.bricks[2][4] == 0);
    assert(g.score == 7u);
    assert(g.remaining_val == 7);

    /* clearing the last brick starts the next level */
    assert(g.level == 1);
    memset(g.bricks, 0, sizeof(g.bricks));
    g.bricks[0][10] = 1;
    g.remaining_val = 7;
    g.launch = 1;
    g.ball_x = (brick_x(10) + BRICK_W / 2) * SUB;
    g.ball_y = (brick_y(0) + BRICK_H + BALL_HALF + 1) * SUB;
    g.vx = 0;
    g.vy = -2;
    ev = 0;
    for (int i = 0; i < 60 && !(ev & STEP_LEVEL); i++)
        ev = game_step(&g);
    assert(ev & STEP_LEVEL);
    assert(g.level == 2);
    assert(!g.launch); /* a new ball waits on the paddle */
    assert(g.broken == 0);
    int steel = 0;
    assert(count_bricks(&g, &steel) == 38 && steel == 0); /* level 2 pattern */
    assert(g.remaining_val == 38 * 7);
}

static void test_level_cap(void)
{
    Game g;
    game_new(&g, 1, 0);
    g.level = MAX_LEVEL;
    memset(g.bricks, 0, sizeof(g.bricks));
    g.bricks[0][0] = 1;
    g.remaining_val = 7;
    g.launch = 1;
    g.ball_x = (brick_x(0) + BRICK_W / 2) * SUB;
    g.ball_y = (brick_y(0) + BRICK_H + BALL_HALF + 1) * SUB;
    g.vx = 0;
    g.vy = -2;
    int ev = 0;
    for (int i = 0; i < 10 && !(ev & STEP_LEVEL); i++)
        ev = game_step(&g);
    assert(ev & STEP_LEVEL);
    assert(g.level == MAX_LEVEL); /* stays at the last level, replayed */
    int steel = 0;
    assert(count_bricks(&g, &steel) > 0);
    assert(g.score == 7u * MAX_LEVEL);
}

/* A tough brick takes two hits and scores only when it finally breaks. */
static void test_tough_brick(void)
{
    Game g;
    game_new(&g, 1, 0);
    memset(g.bricks, 0, sizeof(g.bricks));
    g.bricks[2][3] = 2;
    g.bricks[5][5] = 1; /* one brick left so the level does not clear */
    g.remaining_val = 14;
    g.launch = 1;
    g.ball_x = (brick_x(3) + BRICK_W / 2) * SUB;
    g.ball_y = (brick_y(2) + BRICK_H + BALL_HALF + 1) * SUB;
    g.vx = 0;
    g.vy = -16;

    int ev = 0;
    for (int i = 0; i < 20 && !(ev & STEP_BRICK); i++)
        ev = game_step(&g);
    assert(ev & STEP_BRICK);
    assert(g.bricks[2][3] == 1); /* cracked but still standing */
    assert(g.score == 0);
    assert(g.broken == 0);
    assert(g.remaining_val == 14);
    assert(g.vy > 0);

    /* a second hit from above finishes it */
    g.ball_y = (brick_y(2) - BALL_HALF - 1) * SUB;
    g.vy = 16;
    ev = 0;
    for (int i = 0; i < 20 && !(ev & STEP_BRICK); i++)
        ev = game_step(&g);
    assert(ev & STEP_BRICK);
    assert(g.bricks[2][3] == 0);
    assert(g.score == 7u);
    assert(g.broken == 1);
    assert(g.remaining_val == 7);
    assert(g.vy < 0);
}

static void test_paddle_bounce(void)
{
    Game g;
    game_new(&g, 1, 0);
    g.launch = 1;
    g.ball_x = g.paddle_x; /* dead centre */
    g.ball_y = (PADDLE_Y - BALL_HALF) * SUB - 1; /* one step from the paddle */
    g.vx = 2;
    g.vy = 2;
    int ev = game_step(&g);
    assert(ev & STEP_PADDLE);
    assert(g.vy < 0);
    assert(g.vx * g.vx * 4 <= g.vy * g.vy); /* a centre hit goes almost straight up */
    assert(g.ball_y <= (PADDLE_Y - BALL_HALF) * SUB);

    /* an edge hit goes steeply sideways, still upward */
    game_new(&g, 1, 0);
    g.launch = 1;
    g.ball_x = g.paddle_x + 20 * SUB; /* near the right edge */
    g.ball_y = (PADDLE_Y - BALL_HALF) * SUB - 1; /* one step from the paddle */
    g.vx = 2;
    g.vy = 2;
    ev = game_step(&g);
    assert(ev & STEP_PADDLE);
    assert(g.vy < 0);
    assert(g.vx > 0); /* deflected toward the right wall */
    assert(g.vx * g.vx + g.vy * g.vy >= 4);

    /* a miss falls through and costs a life */
    game_new(&g, 1, 0);
    g.launch = 1;
    game_set_paddle(&g, 10); /* parked at the left wall */
    g.ball_x = 220 * SUB;
    g.ball_y = (FIELD_BOTTOM - 2) * SUB; /* one step from the drop line */
    g.vx = 0;
    g.vy = 4;
    ev = 0;
    for (int i = 0; i < 20 && !(ev & STEP_LIFE); i++)
        ev = game_step(&g);
    assert(ev & STEP_LIFE);
    assert(!(ev & STEP_PADDLE));
    assert(g.lives == MAX_LIVES - 1);
    assert(!g.launch); /* a new ball waits on the paddle */
    assert(g.ball_x == g.paddle_x);
    assert(g.vx == 0 && g.vy == 0);
}

static void test_game_over(void)
{
    Game g;
    game_new(&g, 1, 0);
    for (int life = 0; life < MAX_LIVES; life++) {
        g.launch = 1;
        game_set_paddle(&g, 10);
        g.ball_x = 220 * SUB;
        g.ball_y = (FIELD_BOTTOM - 2) * SUB; /* one step from the drop line */
        g.vx = 0;
        g.vy = 4;
        int ev = 0;
        for (int i = 0; i < 20 && !(ev & STEP_LIFE); i++)
            ev = game_step(&g);
        assert(ev & STEP_LIFE);
        assert(g.lives == MAX_LIVES - life - 1);
    }
    assert(g.over);
    assert(g.lives == 0);
    assert(!g.launch);
    assert(g.vx == 0 && g.vy == 0);
    assert(game_step(&g) == 0);
    game_launch(&g);
    assert(!g.launch); /* refused when over */
}

static void test_speed_ramp(void)
{
    Game g;
    game_new(&g, 1, 0);
    memset(g.bricks, 0, sizeof(g.bricks));
    for (int r = 0; r < BRICK_ROWS; r++) {
        g.bricks[r][0] = 1;
        g.bricks[r][1] = 1; /* spare column: the level never clears here */
    }
    g.remaining_val = 2 * BRICK_ROWS * 7;
    g.launch = 1;
    g.vx = 0;
    g.vy = -2;
    int32_t x = (brick_x(0) + BRICK_W / 2) * SUB;
    for (int b = 0; b < 6; b++) {
        /* re-serve under the next intact brick of the column */
        g.ball_x = x;
        g.ball_y = (brick_y(5 - b) + BRICK_H + BALL_HALF + 1) * SUB;
        g.vy = -2;
        int ev = 0;
        for (int i = 0; i < 20 && !(ev & STEP_BRICK); i++)
            ev = game_step(&g);
        assert(ev & STEP_BRICK);
    }
    assert(g.score == 6u * 7u);
    assert(g.broken == 6);
    assert(g.level == 1);
    int32_t len2 = g.vx * g.vx + g.vy * g.vy;
    assert(len2 == 32 * 32); /* still the base speed of 2 px per step */

    /* two more bricks cross the 8-broken threshold: speed 3 */
    memset(g.bricks, 0, sizeof(g.bricks));
    for (int r = 1; r <= 3; r++)
        g.bricks[r][1] = 1;
    g.remaining_val = 3 * 7;
    for (int b = 0; b < 2; b++) {
        g.ball_x = (brick_x(1) + BRICK_W / 2) * SUB;
        g.ball_y = (brick_y(3 - b) + BRICK_H + BALL_HALF + 1) * SUB;
        g.vy = -2;
        g.vx = 0;
        int ev = 0;
        for (int i = 0; i < 20 && !(ev & STEP_BRICK); i++)
            ev = game_step(&g);
        assert(ev & STEP_BRICK);
    }
    assert(g.broken == 8);
    assert(g.remaining_val == 7); /* one brick left, so still level 1 */
    len2 = g.vx * g.vx + g.vy * g.vy;
    assert(len2 >= 39 * 39 && len2 <= 41 * 41); /* sped up to 40 subpx per step */
}

/* A steel ceiling that the ball can only bounce off: the watchdog must re-aim
 * it, because nothing else can ever score. */
static void test_stall_watchdog(void)
{
    Game g;
    game_new(&g, 1, 0);
    memset(g.bricks, 0, sizeof(g.bricks));
    for (int c = 0; c < BRICK_COLS; c++)
        g.bricks[5][c] = STEEL;
    g.bricks[0][0] = 1; /* unreachable from below, so the level never clears */
    g.remaining_val = 7;
    g.launch = 1;
    g.ball_x = 120 * SUB;
    g.ball_y = 200 * SUB;
    g.vx = 0;
    g.vy = -16;

    int ev = 0;
    for (int i = 0; i < 700 && !(ev & STEP_STALL); i++) {
        game_set_paddle(&g, SUB_PX(g.ball_x)); /* keep the ball in play */
        ev = game_step(&g);
    }
    assert(ev & STEP_STALL);
    assert(g.stall == 0);
    assert(g.vy < 0); /* re-aimed upward */
    assert(g.score == 0);
    assert(g.level == 1);
    assert(g.lives == MAX_LIVES);
}

/* A ball that squeezes into the corridor above the brick field is re-aimed
 * back down, where the paddle can reach it again. */
static void test_reaim_above_bricks(void)
{
    Game g;
    game_new(&g, 1, 0);
    memset(g.bricks, 0, sizeof(g.bricks));
    for (int c = 0; c < BRICK_COLS; c++)
        g.bricks[0][c] = STEEL; /* a ceiling just under the top wall */
    g.bricks[3][5] = 1;         /* keeps the level alive */
    g.remaining_val = 7;
    g.launch = 1;
    g.ball_x = 100 * SUB;
    g.ball_y = (FIELD_T + BALL_HALF + 1) * SUB;
    g.vx = 0;
    g.vy = -8;

    int ev = 0;
    for (int i = 0; i < 700 && !(ev & STEP_STALL); i++)
        ev = game_step(&g);
    assert(ev & STEP_STALL);
    assert(g.vy > 0); /* aimed back down at the paddle */
    assert(g.score == 0);
}

static void test_save(void)
{
    Game g;
    uint8_t buf[SAVE_BYTES];
    game_new(&g, 555, 100);
    for (int i = 0; i < 40; i++) {
        game_paddle_input(&g, (i / 10) % 2 ? 1 : -1);
        game_launch(&g);
        game_step(&g);
    }
    game_encode(&g, buf);
    Game r;
    assert(game_decode(&r, buf, SAVE_BYTES));
    assert(memcmp(&r, &g, sizeof(g)) == 0);

    /* short or corrupted files are refused */
    assert(!game_decode(&r, buf, SAVE_BYTES - 1));
    game_encode(&g, buf);
    buf[12] ^= 1;
    assert(!game_decode(&r, buf, SAVE_BYTES));
    game_encode(&g, buf);
    buf[0] ^= 0xFF;
    assert(!game_decode(&r, buf, SAVE_BYTES));

    /* impossible states, with a valid header/checksum */
    for (int i = 0; i < 9; i++) {
        Game bad;
        game_new(&bad, 3, 0);
        switch (i) {
        case 0: bad.bricks[0][0] = 5; break;           /* unknown hp value */
        case 1: bad.lives = MAX_LIVES + 1; break;      /* too many lives */
        case 2: bad.paddle_x = 0; break;               /* paddle outside the walls */
        case 3: bad.launch = 1; bad.vx = 3; break;     /* flying with vy == 0 */
        case 4: bad.remaining_val = 5000; break;       /* impossible value */
        case 5: bad.score = 3000000; break;            /* above the cap */
        case 6: bad.broken = 200; break;               /* more broken than bricks */
        case 7: bad.vx = 200; break;                /* impossible speed */
        case 8: bad.stall = 9999; break;            /* stall counter out of range */
        }
        game_encode(&bad, buf);
        assert(!game_decode(&r, buf, SAVE_BYTES));
    }
}

int main(void)
{
    test_new();
    test_paddle();
    test_launch_and_walls();
    test_bricks();
    test_tough_brick();
    test_level_cap();
    test_paddle_bounce();
    test_game_over();
    test_speed_ramp();
    test_stall_watchdog();
    test_reaim_above_bricks();
    test_save();
    puts("breakout-test: ok");
    return 0;
}
