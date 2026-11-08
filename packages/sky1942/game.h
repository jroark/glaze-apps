#ifndef SKY1942_GAME_H
#define SKY1942_GAME_H
/* 1942 rules: a vertical scrolling shooter. Pure C: no glaze/ includes, so
 * tests/native/sky1942.c can test it on the host. main.c draws it and feeds
 * it input. The play field is FIELD_W x FIELD_H, origin top-left; main.c
 * offsets it on screen. Positions are in whole pixels. */
#include <stdbool.h>
#include <stdint.h>

#define FIELD_W 220
#define FIELD_H 230

/* Simulation runs in fixed STEP_MS slices so it is deterministic. */
#define STEP_MS 20u

#define START_LIVES 3
#define INV_STEPS 100          /* invincibility after a hit, in steps */
#define MAX_ENEMIES 10
#define MAX_SHOTS 14
#define MAX_BOMBS 20
#define MAX_FX 8

enum { W_INTRO, W_FIGHT, W_CLEAR };   /* wave state machine */
enum { E_FIGHTER, E_DIVER, E_BOMBER };

typedef struct {
    int16_t x, y, vx, vy;
    uint8_t type, hp, alive, cool;    /* cool: steps until next bomb (bombers) */
} Enemy;

typedef struct {
    int16_t x, y, vy;                 /* player shots always travel up */
    uint8_t alive, pad[3];
} Shot;

typedef struct {
    int16_t x, y, vx, vy;
    uint8_t alive, pad[3];
} Bomb;

typedef struct {
    int16_t x, y;
    uint8_t age, big, alive, pad;     /* age: steps since the explosion */
} Fx;

typedef struct {
    int8_t dx, dy;                    /* -1, 0 or +1 per axis; (0,0) = idle */
} Stick;

typedef struct {
    int16_t px, py;                   /* player plane centre */
    uint16_t scroll;                  /* background scroll, px */
    uint8_t lives, inv, wave, state;
    uint8_t spawn_left, spawn_timer, fire_cool, pad;
    uint16_t state_timer, acc;        /* acc: ms not yet stepped */
    uint32_t rng;                     /* xorshift state, never 0 */
    uint32_t score, best;
    uint8_t over, pad2[3];
    Enemy enemies[MAX_ENEMIES];
    Shot shots[MAX_SHOTS];
    Bomb bombs[MAX_BOMBS];
    Fx fx[MAX_FX];
} Game;

void game_new(Game *g, uint32_t seed, uint32_t best);
/* Advance the world in fixed steps. ms is clamped so a stall cannot teleport
 * anyone. Auto-fire is part of the world, so nothing else is needed. */
void game_tick(Game *g, Stick in, uint32_t ms);
/* Touch control: put the plane under the finger (clamped to the field). */
void game_drag(Game *g, int x, int y);

/* Save record: 12-byte header (magic, size, checksum) then the Game. */
#define SAVE_BYTES (12 + sizeof(Game))
void game_encode(const Game *g, uint8_t *out);
/* Rejects wrong size, magic, checksum or impossible state. */
bool game_decode(Game *g, const uint8_t *in, unsigned size);
#endif
