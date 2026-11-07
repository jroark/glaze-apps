#ifndef __GUARD___GAME_H
#define __GUARD___GAME_H
/* __NAME__ rules. Pure C: no glaze/ includes, so tests/native/__ID__.c can
 * test it on the host. main.c draws it and feeds it input. */
#include <stdbool.h>
#include <stdint.h>

#define GRID_W 10
#define GRID_H 10

enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };

typedef struct {
    uint8_t x, y;           /* cursor cell */
    uint8_t star_x, star_y; /* cell to collect */
    uint8_t over, pad[3];
    uint32_t rng;           /* xorshift state, never 0 */
    uint32_t score, best;
    uint32_t ms_left;       /* time left in this round */
} Game;

#define ROUND_MS 30000u

void game_new(Game *g, uint32_t seed, uint32_t best);
/* Move the cursor one cell; false at the edge or when the round is over. */
bool game_move(Game *g, int dir);
/* Collect the star under the cursor; false if there is none. */
bool game_collect(Game *g);
/* Advance the round clock by ms; ends the round at zero. */
void game_tick(Game *g, uint32_t ms);

/* Save record: 12-byte header (magic, size, checksum) then the Game. */
#define SAVE_BYTES (12 + sizeof(Game))
void game_encode(const Game *g, uint8_t *out);
/* Rejects wrong size, magic, checksum or impossible state. */
bool game_decode(Game *g, const uint8_t *in, unsigned size);
#endif
