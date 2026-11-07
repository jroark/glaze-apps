/* Native tests of packages/__ID__/game.c (host cc, ASan/UBSan).
 * Run: make -C tests __ID__-test */
#include "../../packages/__ID__/game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Game g;

int main(void)
{
    /* New game: cursor centred, star elsewhere, full clock. */
    game_new(&g, 1, 0);
    assert(g.x == GRID_W / 2 && g.y == GRID_H / 2);
    assert(!(g.star_x == g.x && g.star_y == g.y));
    assert(g.ms_left == ROUND_MS && !g.over && g.score == 0);

    /* Same seed, same star. */
    Game a;
    game_new(&a, 7, 0);
    game_new(&g, 7, 0);
    assert(a.star_x == g.star_x && a.star_y == g.star_y);

    /* Movement stops at the edges. */
    game_new(&g, 1, 0);
    g.x = 0;
    assert(!game_move(&g, DIR_LEFT) && g.x == 0);
    assert(game_move(&g, DIR_RIGHT) && g.x == 1);
    assert(!game_move(&g, 4));

    /* Collecting scores 10, updates best, moves the star. */
    game_new(&g, 1, 0);
    assert(!game_collect(&g));
    g.x = g.star_x;
    g.y = g.star_y;
    assert(game_collect(&g) && g.score == 10 && g.best == 10);
    assert(!(g.star_x == g.x && g.star_y == g.y));

    /* The clock ends the round; nothing moves afterwards. */
    game_new(&g, 1, 0);
    game_tick(&g, ROUND_MS - 1);
    assert(!g.over && g.ms_left == 1);
    game_tick(&g, 5);
    assert(g.over && g.ms_left == 0 && !game_move(&g, DIR_UP));

    /* Save round trip; damaged records are refused. */
    uint8_t buf[SAVE_BYTES];
    game_new(&g, 3, 40);
    game_move(&g, DIR_UP);
    game_encode(&g, buf);
    Game back;
    assert(game_decode(&back, buf, SAVE_BYTES) && memcmp(&back, &g, sizeof(g)) == 0);
    assert(!game_decode(&back, buf, SAVE_BYTES - 1));
    buf[12] ^= 1;
    assert(!game_decode(&back, buf, SAVE_BYTES));

    puts("__ID__-test: ok");
    return 0;
}
