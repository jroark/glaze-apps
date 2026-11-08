/* Native tests of packages/sky1942/game.c (host cc, ASan/UBSan).
 * Run: make -C tests sky1942-test */
#include "../../packages/sky1942/game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Game g;

/* Run n fixed steps with the stick parked at rest. */
static void steps(Game *g, int n)
{
    Stick s = {0, 0};
    for (int i = 0; i < n; i++)
        game_tick(g, s, STEP_MS);
}

static int count_shots(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_SHOTS; i++)
        if (g->shots[i].alive) {
            n++;
            assert(g->shots[i].vy == -8);
        }
    return n;
}

/* Park one fighter of hp 1 at (x, y), motionless. */
static void park_enemy(Game *g, int i, int x, int y)
{
    Enemy *e = &g->enemies[i];
    e->alive = 1;
    e->type = E_FIGHTER;
    e->hp = 1;
    e->x = (int16_t)x;
    e->y = (int16_t)y;
    e->vx = 0;
    e->vy = 0;
}

static void park_bomb(Game *g, int i, int x, int y)
{
    Bomb *b = &g->bombs[i];
    b->alive = 1;
    b->x = (int16_t)x;
    b->y = (int16_t)y;
    b->vx = 0;
    b->vy = 3;
}

int main(void)
{
    /* New game: plane low and centred, 3 lives, wave 1 starts with an intro. */
    game_new(&g, 1, 0);
    assert(g.px == FIELD_W / 2 && g.py == FIELD_H - 24);
    assert(g.lives == START_LIVES && g.wave == 1 && !g.over && !g.score);
    assert(g.state == W_INTRO && g.state_timer == 100);
    assert(g.best == 0 && g.rng != 0);
    game_new(&g, 0, 500);           /* seed 0 must still be a usable rng */
    assert(g.rng != 0 && g.best == 500);

    /* Determinism: same seed, same stick, same world after 200 steps. */
    Game a;
    Stick s = {1, -1};
    game_new(&a, 9, 0);
    game_new(&g, 9, 0);
    for (int i = 0; i < 200; i++) {
        game_tick(&a, s, STEP_MS);
        game_tick(&g, s, STEP_MS);
    }
    assert(memcmp(&a, &g, sizeof(g)) == 0);

    /* Stick movement: 3 px per step, clamped inside the field. */
    game_new(&g, 1, 0);
    Stick right = {1, 0};
    game_tick(&g, right, STEP_MS);
    assert(g.px == FIELD_W / 2 + 3);
    for (int i = 0; i < 100 && g.px < FIELD_W - 10; i++)
        game_tick(&g, right, STEP_MS);
    assert(g.px == FIELD_W - 10);  /* PX_MAX */
    Stick up = {0, -1};
    for (int i = 0; i < 100 && g.py > 110; i++)
        game_tick(&g, up, STEP_MS);
    assert(g.py == 110);           /* PY_MIN */

    /* Touch control puts the plane under the finger, clamped. */
    game_new(&g, 1, 0);
    game_drag(&g, 30, 150);
    assert(g.px == 30 && g.py == 150);
    game_drag(&g, -50, 1000);
    assert(g.px == 10 && g.py == FIELD_H - 12);

    /* Auto-fire: a volley at once, none for a while, then another volley. */
    game_new(&g, 1, 0);
    steps(&g, 1);
    assert(count_shots(&g) == 2);
    steps(&g, 3);                  /* step 4: still cooling down */
    assert(count_shots(&g) == 2);
    steps(&g, 10);                 /* step 14: volley two */
    assert(count_shots(&g) == 4);

    /* Shooting down a fighter: +100, explosion, enemy gone. */
    game_new(&g, 1, 0);
    g.state = W_FIGHT;             /* no spawning */
    g.spawn_left = 0;
    g.spawn_timer = 0;
    park_enemy(&g, 0, g.px, 100);
    for (int i = 0; i < 40 && g.score == 0; i++)
        steps(&g, 1);
    assert(g.score == 100 && g.best == 100);
    assert(!g.enemies[0].alive);
    int fx_live = 0;
    for (int i = 0; i < MAX_FX; i++)
        if (g.fx[i].alive)
            fx_live++;
    assert(fx_live == 1);

    /* Ramming kills the enemy (no points) and costs a life. */
    game_new(&g, 1, 0);
    g.state = W_FIGHT;
    g.spawn_left = 0;
    g.spawn_timer = 0;
    g.fire_cool = 12;              /* no shot in this step: it is a ram, not a gun kill */
    park_enemy(&g, 0, g.px, g.py);
    steps(&g, 1);
    assert(g.lives == START_LIVES - 1 && !g.enemies[0].alive);
    assert(g.inv == INV_STEPS);
    assert(g.score == 0);

    /* A bomb hits: -1 life, all bombs cleared, invincible afterwards. */
    game_new(&g, 1, 0);
    g.state = W_FIGHT;
    g.spawn_left = 0;
    g.fire_cool = 12;
    park_bomb(&g, 0, g.px, g.py + 2);
    park_bomb(&g, 1, g.px, 5);     /* far away, but the blast clears it too */
    steps(&g, 1);
    assert(g.lives == START_LIVES - 1);
    assert(!g.bombs[0].alive && !g.bombs[1].alive);
    /* Invincible: another bomb at the same spot does nothing. */
    park_bomb(&g, 0, g.px, g.py);
    steps(&g, 10);
    assert(g.lives == START_LIVES - 1 && g.inv > 0);
    /* Once inv fades the same bomb kills the last life: game over. */
    g.lives = 1;
    g.inv = 1;
    park_bomb(&g, 0, g.px, g.py + 2);
    steps(&g, 2);
    assert(g.over && g.lives == 0);
    /* Nothing moves after game over. */
    uint16_t scroll = g.scroll;
    steps(&g, 30);
    assert(g.scroll == scroll);

    /* Wave clear: bonus, a breather, then wave 2 with 8 queued planes. */
    game_new(&g, 1, 0);
    g.state = W_FIGHT;
    g.spawn_left = 0;
    g.spawn_timer = 0;
    steps(&g, 1);
    assert(g.score == 50u * g.wave && g.state == W_CLEAR && g.state_timer == 60);
    steps(&g, 59);
    assert(g.state == W_CLEAR && g.state_timer == 1);
    steps(&g, 1);
    assert(g.state == W_INTRO && g.wave == 2 && g.state_timer == 100);
    steps(&g, 100);
    assert(g.state == W_FIGHT && g.spawn_left == 8 && g.spawn_timer == 5);

    /* The intro really queues planes and the fight spawns them: wave 1 queues
     * 6, the first one appears at step 106. */
    game_new(&g, 5, 0);
    steps(&g, 105);
    assert(g.state == W_FIGHT && g.spawn_left == 6);
    steps(&g, 1);
    assert(g.spawn_left == 5);
    int any = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g.enemies[i].alive)
            any = 1;
    assert(any);

    /* A long stall cannot teleport: 60 s advances at most 5 steps. */
    game_new(&g, 1, 0);
    game_tick(&g, (Stick){0, 0}, 60000);
    assert(g.scroll == 5);
    assert(g.acc < STEP_MS);

    /* Save round trip; damaged records are refused. */
    uint8_t buf[SAVE_BYTES];
    game_new(&g, 3, 400);
    game_drag(&g, 80, 140);
    game_tick(&g, (Stick){0, 0}, 35 * STEP_MS);
    game_encode(&g, buf);
    Game back;
    assert(game_decode(&back, buf, SAVE_BYTES) && memcmp(&back, &g, sizeof(g)) == 0);
    assert(!game_decode(&back, buf, SAVE_BYTES - 1));
    uint8_t bad[SAVE_BYTES];
    memcpy(bad, buf, SAVE_BYTES);
    bad[12] ^= 1;                  /* corrupt the struct: checksum */
    assert(!game_decode(&back, bad, SAVE_BYTES));
    memcpy(bad, buf, SAVE_BYTES);
    bad[0] ^= 1;                   /* corrupt the magic */
    assert(!game_decode(&back, bad, SAVE_BYTES));
    /* Impossible states still checksum correctly, so they must be rejected
     * by the field checks. */
    Game t;
#define REJECT(mod) do { t = g; mod; game_encode(&t, bad); \
        assert(!game_decode(&back, bad, SAVE_BYTES)); } while (0)
    REJECT(t.lives = 9);
    REJECT(t.lives = 0);           /* lives 0 but not over */
    REJECT(t.over = 2);
    REJECT(t.wave = 0);
    REJECT(t.wave = 61);
    REJECT(t.state = 3);
    REJECT(t.rng = 0);
    REJECT(t.score = 101);         /* not a multiple of 50 */
    REJECT(t.best = t.score - 50);
    REJECT(t.acc = STEP_MS);
    REJECT(t.px = 0);
    REJECT(t.py = FIELD_H);
    REJECT(t.inv = INV_STEPS + 1);
    REJECT(t.enemies[0].alive = 2);
    REJECT(park_enemy(&t, 0, 5, 100); t.enemies[0].x = 5000);
    REJECT(park_enemy(&t, 0, 5, 100); t.enemies[0].vy = 99);
    REJECT(park_bomb(&t, 0, 5, 100); t.bombs[0].y = -999);
    REJECT(t.shots[0].alive = 1; t.shots[0].vy = 4);
    REJECT(t.fx[0].alive = 1; t.fx[0].age = 99);
#undef REJECT
    /* A hand-built live state still round-trips. */
    park_enemy(&g, 2, 100, 55);
    park_bomb(&g, 3, 100, 180);
    g.state = W_FIGHT;
    game_encode(&g, buf);
    assert(game_decode(&back, buf, SAVE_BYTES) && memcmp(&back, &g, sizeof(g)) == 0);

    puts("sky1942-test: ok");
    return 0;
}
