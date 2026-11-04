#ifndef FLAPPY_GAME_H
#define FLAPPY_GAME_H
#include <stdint.h>
#include <stdbool.h>
/* Flappy rules in integers, no floating point and no platform dependencies.
 * The field is FL_W x FL_H px. y is 1/1000 px, x and pipe tops 1/10 px,
 * speeds px/s. The bird stays at x=FL_BIRD_X; pipes scroll left, drawn as
 * shafts plus 8 px rims outside the collision box. game_step advances 10 ms:
 * gravity +6 px/s, flap -300 px/s, floor catch 600 px/s. Gap 72 px, pipes
 * every 120 px at 60 px/s; 60+4*min(score/10,15) px/s as points pile up.
 * Pairs live in a ring with the oldest, scoring pair at x=head. */
#define FL_W 240
#define FL_H 224
#define FL_BIRD_X 40
#define FL_BIRD_R 7             /* collision box half-size; rims add a lip */
#define FL_PIPE_W 24
#define FL_GAP 72
#define FL_SPACING 120          /* px between pair left edges */
#define FL_TOP_MIN 16           /* gap clearance from the ceiling, px */
#define FL_BOT_MIN 16           /* ... from the floor, px */
#define FL_PIPES 3              /* pair slots; head always the oldest */
#define FL_SPEED_BASE 60        /* scroll px/s at score 0 */
#define FL_SPEED_MAX 120        /* ... and at score 150+ */
#define FL_GRAVITY 6            /* px/s gained per 10 ms step */
#define FL_FLAP_SPEED (-300)    /* a flap sets fall speed to this */
#define FL_MAX_FALL 600         /* terminal fall speed, px/s */
typedef struct {
    int32_t y,speed;            /* 1/1000 px, px/s (down +) */
    uint32_t score,best;
    uint32_t rng,t,acc;         /* t counts 10 ms steps since spawned */
    int16_t x[FL_PIPES],top[FL_PIPES]; /* 1/10 px, pair order from head */
    uint8_t passed[FL_PIPES];
    uint8_t head,over,pad;
    uint16_t speed_px;          /* current scroll speed */
    uint16_t spare;
} Game;
void game_new(Game *g,uint32_t seed,uint32_t best);
void game_flap(Game *g);
void game_step(Game *g);
void game_tick(Game *g,uint32_t ms);
unsigned game_level(const Game *g);
#define SAVE_BYTES (sizeof(Game)+12)
void game_encode(const Game *g,uint8_t *out);
bool game_decode(Game *g,const uint8_t *in,unsigned size);
#endif
