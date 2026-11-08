#ifndef PACMAN_GAME_H
#define PACMAN_GAME_H
/* Muncher rules. Pure C: no glaze/ includes, so tests/native/pacman.c can
 * test it on the host. main.c draws it and feeds it input.
 *
 * The maze is MAZE_W x MAZE_H tiles. An entity's position is its tile plus
 * `sub`: how far it has travelled through that tile along `dir`, in 1/16 px
 * units (a full tile is 224, the centre is 112). Turns happen at tile
 * centres. The simulation runs in fixed SIM_MS steps, so it is deterministic
 * and host-testable. */
#include <stdbool.h>
#include <stdint.h>

#define MAZE_W 15
#define MAZE_H 17
#define TILE_PX 14 /* pixels per tile; the maze is 210 x 238 px */
#define SIM_MS 16  /* one simulation step */

/* Ghost ids. */
enum { G_RED, G_PINK, G_CYAN, G_ORANGE, GHOST_COUNT };
/* Directions (a turn is always allowed to DIR_NONE via game_set_dir's guard). */
enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT, DIR_NONE = 255 };
/* What main.c shows over the maze. */
enum { ST_PLAY, ST_DYING, ST_WIN, ST_OVER };

typedef struct {
    uint8_t tile;      /* cell index row*MAZE_W+col */
    uint8_t sub;       /* 0..223 travelled along dir; 112 = centre */
    uint8_t dir;       /* DIR_UP..DIR_LEFT */
    uint8_t in_house;  /* 1 = inside the ghost house, waiting or leaving */
    uint16_t release;  /* sim steps left until it may leave the house */
} Ghost;

typedef struct {
    /* Pac */
    uint8_t pac_tile, pac_sub, pac_dir, desired;
    /* Ghosts */
    Ghost g[GHOST_COUNT];
    /* Pellets: bit set = eaten. The base layout is compiled into game.c. */
    uint8_t eaten[32]; /* MAZE_W*MAZE_H = 255 tiles */
    uint8_t total_dots, dots_eaten;
    /* Mode */
    uint8_t state;      /* ST_* */
    uint8_t frightened; /* 1 while the power mode runs */
    uint16_t fright_timer;
    uint16_t chain;     /* value of the next eaten ghost: 200..1600 */
    uint16_t state_timer; /* ST_DYING countdown in sim steps */
    uint16_t phase_t;   /* steps since the life began (scatter vs chase) */
    uint16_t step_cnt;  /* total sim steps, for animations */
    uint8_t tofu;       /* 1 = a bonus fruit sits on TOFU_TILE */
    uint8_t tofu_n;     /* fruits spawned this level (max 2) */
    uint16_t tofu_t;    /* steps until the fruit vanishes */
    /* Score */
    uint32_t score, best;
    uint8_t lives, level;
    uint32_t rng; /* xorshift state, never 0 */
} Game;

#define TOFU_TILE (10 * MAZE_W + 7) /* empty floor below the ghost house */
#define DYING_STEPS 44u            /* sim steps of the death animation */

/* Speeds in sub units (1/16 px) per SIM_MS step. */
uint8_t pac_speed(const Game *g);
uint8_t ghost_speed(const Game *g, int gi);

/* Layout queries (compiled-in tables). ghost=true asks for a ghost. */
bool game_walk(int tile, bool ghost);
bool game_dot_at(const Game *g, int tile);   /* pellet present right now? */
bool game_power_at(const Game *g, int tile); /* power pellet present now? */
int game_total_dots(void); /* normal pellets + 4 power pellets */
/* Centre of an entity in 1/16 px maze coordinates. main.c divides by 16
 * and adds its maze offset to draw. */
void game_abs(int tile, int sub, int dir, int *x, int *y);

void game_new(Game *g, uint32_t seed, uint32_t best);
/* Buffer Pac's next turn (DIR_UP..DIR_LEFT); applied at a tile centre. */
void game_set_dir(Game *g, int dir);
/* Advance the simulation; ms is consumed in whole SIM_MS steps, max 6/call. */
void game_tick(Game *g, uint32_t ms);
/* Start the next maze after ST_WIN (keeps score, best and rng). */
void game_next_level(Game *g);

/* Save record: 12-byte header (magic, size, checksum) then the Game. */
#define SAVE_BYTES (12 + sizeof(Game))
void game_encode(const Game *g, uint8_t *out);
/* Rejects wrong size, magic, checksum or impossible state. */
bool game_decode(Game *g, const uint8_t *in, unsigned size);
#endif
