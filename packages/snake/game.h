#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H
#include <stdint.h>
#include <stdbool.h>
/* 22x20 grid, cells indexed y*SW+x. Directions 0=up..3=left.
 * The body is a ring of cells with head at body[head]. No platform dependencies. */
#define SW 22
#define SH 20
#define SNAKE_MAX (SW*SH)
#define SNAKE_FOOD_NONE SNAKE_MAX
enum {SNAKE_UP,SNAKE_RIGHT,SNAKE_DOWN,SNAKE_LEFT};
typedef struct {
    uint16_t body[SNAKE_MAX];
    uint16_t food,pad;
    int32_t head,len;
    uint8_t dir,next_dir,over,won;
    uint32_t rng,score,best,eaten,tick;
} Game;
void game_new(Game *g,uint32_t seed,uint32_t best);
bool game_turn(Game *g,int dir);
bool game_step(Game *g);
void game_tick(Game *g,uint32_t ms);
unsigned game_level(const Game *g);
unsigned game_speed(const Game *g);
#define SAVE_BYTES (sizeof(Game)+12)
void game_encode(const Game *g,uint8_t *out);
bool game_decode(Game *g,const uint8_t *in,unsigned size);
#endif
