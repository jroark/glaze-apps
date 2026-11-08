/* Host tests for packages/pacman/game.c. Run with: make -C tests pacman-test */
#include "../../packages/pacman/game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define T(x, y) ((y) * MAZE_W + (x))

/* Geometry restated for the tests (do not reuse game.c helpers). */
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};

static void run(Game *g, int steps)
{
    for (int i = 0; i < steps; i++)
        game_tick(g, 16);
}

/* Move Pac by holding a direction for n steps. */
static void pac_go(Game *g, int dir, int steps)
{
    game_set_dir(g, dir);
    run(g, steps);
}

static void test_new(void)
{
    Game g;
    game_new(&g, 42, 500);
    assert(g.score == 0 && g.best == 500);
    assert(g.lives == 3 && g.level == 1);
    assert(g.state == ST_PLAY);
    assert(g.total_dots == game_total_dots() && g.total_dots > 100);
    assert(g.dots_eaten == 0);
    assert(g.pac_tile == T(7, 10) && g.pac_sub == 112 && g.pac_dir == DIR_LEFT);
    assert(!g.g[G_RED].in_house && g.g[G_RED].tile == T(7, 6));
    assert(g.g[G_PINK].in_house && g.g[G_PINK].tile == T(7, 8));
    assert(g.g[G_CYAN].in_house && g.g[G_CYAN].tile == T(6, 8));
    assert(g.g[G_ORANGE].in_house && g.g[G_ORANGE].tile == T(8, 8));
    assert(g.rng != 0);
    /* the four power pellets exist */
    int powers = 0;
    for (int t = 0; t < MAZE_W * MAZE_H; t++)
        if (game_power_at(&g, t))
            powers++;
    assert(powers == 4);
    /* start tile is empty floor, so Pac standing there eats nothing */
    assert(!game_dot_at(&g, g.pac_tile));
}

static void test_determinism(void)
{
    Game a, b;
    game_new(&a, 7, 0);
    game_new(&b, 7, 0);
    for (int i = 0; i < 200; i++) {
        game_tick(&a, 16);
        game_tick(&b, 16);
    }
    assert(a.pac_tile == b.pac_tile && a.pac_sub == b.pac_sub);
    for (int i = 0; i < GHOST_COUNT; i++) {
        assert(a.g[i].tile == b.g[i].tile);
        assert(a.g[i].sub == b.g[i].sub && a.g[i].dir == b.g[i].dir);
    }
    assert(a.score == b.score && a.rng == b.rng);
}

static void test_walk_table(void)
{
    /* walls, house and door */
    assert(!game_walk(T(0, 0), true));
    assert(game_walk(T(1, 1), false));
    assert(!game_walk(T(7, 7), false)); /* door: ghosts only */
    assert(game_walk(T(7, 7), true));
    assert(!game_walk(T(7, 8), false)); /* house: ghosts only */
    assert(game_walk(T(7, 8), true));
    /* tunnel mouths are open floor */
    assert(game_walk(T(0, 8), false) && game_walk(T(14, 8), false));

    /* every dot reachable by Pac from the start, by BFS */
    Game g;
    game_new(&g, 1, 0);
    int seen[MAZE_W * MAZE_H] = {0}, q[MAZE_W * MAZE_H], head = 0, tail = 0;
    seen[g.pac_tile] = 1;
    q[tail++] = g.pac_tile;
    while (head < tail) {
        int t = q[head++];
        for (int d = 0; d < 4; d++) {
            int x = t % MAZE_W + DX[d], y = t / MAZE_W + DY[d];
            if (x < 0)
                x += MAZE_W;
            if (x >= MAZE_W)
                x -= MAZE_W;
            if (y < 0 || y >= MAZE_H)
                continue;
            int n = y * MAZE_W + x;
            if (!seen[n] && game_walk(n, false)) {
                seen[n] = 1;
                q[tail++] = n;
            }
        }
    }
    for (int t = 0; t < MAZE_W * MAZE_H; t++)
        if (game_dot_at(&g, t))
            assert(seen[t]);
    assert(tail > 100);
}

static void test_pac_moves_and_eats(void)
{
    Game g;
    game_new(&g, 5, 0);
    int start_dots = game_total_dots();
    /* walking left from (7,10): (6,10) is floor, eat one pellet per tile */
    pac_go(&g, DIR_LEFT, 24); /* 24 steps * 8 = 192 units < 1 tile... */
    /* pac_speed 8/step, 224 units/tile: 28 steps per tile */
    assert(g.pac_dir == DIR_LEFT);
    pac_go(&g, DIR_LEFT, 60); /* ~2 more tiles */
    int px = g.pac_tile % MAZE_W;
    assert(g.pac_tile / MAZE_W == 10 && px < 7);
    assert(g.dots_eaten >= 1 && g.score == g.dots_eaten * 10u);
    assert(g.score > 0);

    /* walls stop Pac: face him at the top wall and keep pressing up */
    game_new(&g, 5, 0);
    g.pac_tile = (uint8_t)T(7, 1);
    g.pac_sub = 112;
    g.pac_dir = DIR_UP;
    pac_go(&g, DIR_UP, 200);
    assert(g.pac_tile == T(7, 1) && g.pac_sub == 112); /* r0 is wall: he sits */
    assert(start_dots == game_total_dots());
}

static void test_tunnel_wrap(void)
{
    Game g;
    game_new(&g, 3, 0);
    g.pac_tile = (uint8_t)T(0, 8);
    g.pac_sub = 0;
    g.pac_dir = DIR_LEFT;
    g.desired = DIR_NONE;
    run(&g, 30); /* 240 units: past the mouth into the right end */
    assert(g.pac_tile / MAZE_W == 8);
    assert(g.pac_tile % MAZE_W >= 12); /* wrapped to the right side */
    /* ghosts use the tunnel too: chase with the red ghost long after release */
}

static void test_power_fright_ghost_chain(void)
{
    Game g;
    game_new(&g, 9, 0);
    /* Pac at the centre of (2,1) walks left: it eats that dot (10), then
     * the power pellet at (1,1) (50). One tile is 28 steps at speed 8. */
    g.pac_tile = (uint8_t)T(2, 1);
    g.pac_sub = 112;
    g.pac_dir = DIR_LEFT;
    run(&g, 30);
    assert(g.dots_eaten == 2);
    assert(g.score == 60);
    assert(g.frightened && g.fright_timer > 300);
    /* eating frightened ghosts: chain values 200, 400, 800, 1600 */
    uint32_t base = g.score;
    for (int i = 0; i < GHOST_COUNT; i++) {
        /* park the red ghost right on Pac and trigger a collision */
        Game *p = &g;
        p->g[G_RED].tile = p->pac_tile;
        p->g[G_RED].sub = p->pac_sub;
        p->g[G_RED].dir = p->pac_dir;
        p->g[G_RED].in_house = 0;
        run(p, 1);
        uint32_t want = 200u << (unsigned)i;
        if (want > 1600)
            want = 1600;
        assert(g.score == base + want);
        base += want;
        assert(g.g[G_RED].in_house); /* sent home */
        assert(g.frightened);           /* still in power mode */
    }
    /* power mode expires */
    run(&g, 400);
    assert(!g.frightened && g.fright_timer == 0);
}

static void test_death_and_lives(void)
{
    Game g;
    game_new(&g, 11, 0);
    /* a normal ghost touching Pac kills: drop the red ghost on him */
    g.g[G_RED].tile = g.pac_tile;
    g.g[G_RED].sub = g.pac_sub;
    g.g[G_RED].dir = g.pac_dir;
    g.g[G_RED].in_house = 0;
    run(&g, 1);
    assert(g.state == ST_DYING && g.lives == 3);
    run(&g, 44); /* after the countdown a life is lost and places reset */
    assert(g.lives == 2);
    assert(g.state == ST_PLAY);
    assert(g.pac_tile == T(7, 10));
    /* lose the rest */
    for (int i = 0; i < 10 && g.state != ST_OVER; i++) {
        g.g[G_RED].tile = g.pac_tile;
        g.g[G_RED].sub = g.pac_sub;
        g.g[G_RED].dir = g.pac_dir;
        g.g[G_RED].in_house = 0;
        run(&g, 1);
        run(&g, 44);
    }
    assert(g.state == ST_OVER && g.lives == 0);
    /* the dead board is frozen */
    uint8_t pt = g.pac_tile;
    run(&g, 100);
    assert(g.pac_tile == pt && g.state == ST_OVER);
}

static void test_win_and_level(void)
{
    Game g;
    game_new(&g, 13, 0);
    /* park every ghost in the house so Pac is safe for the final pellet */
    for (int i = 0; i < GHOST_COUNT; i++) {
        g.g[i].in_house = 1;
        g.g[i].tile = (uint8_t)T(7, 8);
        g.g[i].sub = 112;
        g.g[i].dir = DIR_UP;
        g.g[i].release = 500;
    }
    g.pac_tile = (uint8_t)T(2, 1);
    g.pac_sub = 112;
    g.pac_dir = DIR_LEFT;
    uint32_t score_before = g.score;
    /* eat everything but the power pellet at (1,1), then walk over it */
    for (int t = 0; t < MAZE_W * MAZE_H; t++)
        if (game_dot_at(&g, t) && t != T(1, 1))
            g.eaten[t / 8] |= (uint8_t)(1u << (t % 8));
    g.dots_eaten = (uint8_t)(g.total_dots - 1);
    run(&g, 30);
    assert(g.state == ST_WIN);
    assert(g.score > score_before);
    uint32_t sc = g.score, best = g.best;
    uint8_t lv = g.level;
    game_next_level(&g);
    assert(g.level == lv + 1);
    assert(g.score == sc && g.best == best);
    assert(g.dots_eaten == 0 && g.state == ST_PLAY);
    assert(g.lives == 3);
}

static void test_tofu(void)
{
    Game g;
    game_new(&g, 17, 0);
    /* stage 19 eaten pellets without moving, then eat the 20th: that spawns
     * a fruit on TOFU_TILE. (1,1) and (2,1) are left for Pac's walk. */
    g.pac_tile = (uint8_t)T(2, 1);
    g.pac_sub = 112;
    g.pac_dir = DIR_LEFT;
    int staged = 0;
    for (int t = 0; t < MAZE_W * MAZE_H; t++) {
        if (game_dot_at(&g, t) && t != T(1, 1) && t != T(2, 1)) {
            g.eaten[t / 8] |= (uint8_t)(1u << (t % 8));
            g.dots_eaten++;
            if (++staged == 19)
                break;
        }
    }
    assert(g.dots_eaten == 19);
    run(&g, 14); /* Pac crosses (2,1) at once: its dot is the 20th */
    assert(g.dots_eaten == 20 && g.tofu == 1);
    assert(g.tofu_t > 0);
    uint32_t sc = g.score;
    /* park Pac on the fruit tile: standing at its centre eats it */
    g.pac_tile = (uint8_t)TOFU_TILE;
    g.pac_sub = 112;
    run(&g, 1);
    assert(!g.tofu && g.score == sc + 100);
}

static void test_ghost_ai_reaches(void)
{
    /* Run 400 steps with Pac wiggling: every ghost must leave the house
     * at some point, ghosts never walk free inside the house, and every
     * ghost tile stays walkable. Pac may die; that is fine. */
    Game g;
    game_new(&g, 21, 0);
    /* shorten house releases so all four are out well inside 400 steps,
     * even if a death resets them */
    g.g[G_PINK].release = 20;
    g.g[G_CYAN].release = 40;
    g.g[G_ORANGE].release = 60;
    int seen_free[GHOST_COUNT] = {0};
    for (int i = 0; i < 400; i++) {
        game_set_dir(&g, i % 100 < 50 ? DIR_LEFT : DIR_RIGHT);
        game_tick(&g, 16);
        for (int j = 0; j < GHOST_COUNT; j++) {
            assert(game_walk(g.g[j].tile, true));
            if (!g.g[j].in_house)
                seen_free[j] = 1;
            /* a free ghost must never stand inside the house */
            if (g.state == ST_PLAY && !g.g[j].in_house) {
                int y = g.g[j].tile / MAZE_W, x = g.g[j].tile % MAZE_W;
                assert(!(y == 8 && x >= 6 && x <= 8));
            }
        }
    }
    for (int j = 0; j < GHOST_COUNT; j++)
        assert(seen_free[j]);
}

static void test_house_bounce_bounded(void)
{
    /* house ghosts bouncing must keep sub in [0,223] (and dir vertical),
     * or a mid-bounce save would hold an impossible value */
    Game g, h;
    game_new(&g, 43, 0);
    for (int i = 0; i < 120; i++) { /* before the pink ghost's 125-step release */
        game_tick(&g, 16);
        for (int j = G_PINK; j < GHOST_COUNT; j++) {
            assert(g.g[j].sub <= 223);
            if (g.g[j].in_house && g.g[j].release)
                assert(g.g[j].dir == DIR_UP || g.g[j].dir == DIR_DOWN);
        }
    }
    uint8_t buf[SAVE_BYTES];
    game_encode(&g, buf);
    assert(game_decode(&h, buf, sizeof(buf)));
}

static void test_save_roundtrip(void)
{
    Game g, h;
    game_new(&g, 31, 100);
    run(&g, 300);
    uint8_t buf[SAVE_BYTES];
    game_encode(&g, buf);
    assert(game_decode(&h, buf, sizeof(buf)));
    assert(memcmp(&g, &h, sizeof g) == 0);

    /* bad size */
    assert(!game_decode(&h, buf, sizeof(buf) - 1));
    /* flipped body byte: checksum refuses */
    uint8_t bad[SAVE_BYTES];
    memcpy(bad, buf, sizeof(bad));
    bad[20] ^= 1;
    assert(!game_decode(&h, bad, sizeof(bad)));
    /* impossible values that still pass a re-checksum: build them directly */
    Game v;
    game_new(&v, 1, 0);
    v.pac_tile = (uint8_t)T(0, 0); /* inside a wall */
    game_encode(&v, buf);
    assert(!game_decode(&h, buf, sizeof(buf)));

    game_new(&v, 1, 100);
    v.score = 5000;
    v.best = 100; /* score above best */
    game_encode(&v, buf);
    assert(!game_decode(&h, buf, sizeof(buf)));

    game_new(&v, 1, 0);
    v.g[G_PINK].in_house = 0;
    v.g[G_PINK].tile = (uint8_t)T(7, 8); /* free ghost parked in the house */
    game_encode(&v, buf);
    assert(!game_decode(&h, buf, sizeof(buf)));

    game_new(&v, 1, 0);
    v.g[G_PINK].in_house = 0;
    v.g[G_PINK].tile = (uint8_t)T(7, 7); /* just left: on the door, valid */
    game_encode(&v, buf);
    assert(game_decode(&h, buf, sizeof(buf)));

    game_new(&v, 1, 0);
    v.level = 0;
    game_encode(&v, buf);
    assert(!game_decode(&h, buf, sizeof(buf)));

    game_new(&v, 0, 0);
    v.rng = 0;
    game_encode(&v, buf);
    assert(!game_decode(&h, buf, sizeof(buf)));

    /* an in-flight frightened state saves and loads */
    game_new(&v, 41, 0);
    v.pac_tile = (uint8_t)T(2, 1);
    v.pac_sub = 112;
    v.pac_dir = DIR_LEFT;
    run(&v, 30);
    assert(v.frightened);
    game_encode(&v, buf);
    assert(game_decode(&h, buf, sizeof(buf)));
    assert(h.frightened && h.fright_timer == v.fright_timer);
}

int main(void)
{
    test_new();
    test_determinism();
    test_walk_table();
    test_pac_moves_and_eats();
    test_tunnel_wrap();
    test_power_fright_ghost_chain();
    test_death_and_lives();
    test_win_and_level();
    test_tofu();
    test_ghost_ai_reaches();
    test_house_bounce_bounded();
    test_save_roundtrip();
    puts("pacman-test: ok");
    return 0;
}
