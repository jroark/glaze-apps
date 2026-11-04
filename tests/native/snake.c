#include "../../packages/snake/game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Game g;
/* Geometry is restated here so the test checks game.c, not itself. */
static const int tdx[4]={0,1,0,-1},tdy[4]={-1,0,1,0};
static int on_body(int cell){for(int i=0;i<g.len;i++)if(g.body[(g.head-i+SNAKE_MAX)%SNAKE_MAX]==cell)return 1;return 0;}
static void straight(int len,int dir,int cx,int cy,uint32_t seed)
{
    game_new(&g,seed,0);g.len=len;g.dir=g.next_dir=dir;g.head=len-1;g.tick=0;
    for(int i=0;i<len;i++)g.body[g.head-i]=(uint16_t)(((cy-tdy[dir]*i)*SW)+(cx-tdx[dir]*i));
    g.food=SNAKE_FOOD_NONE-1; /* a corner cell the snake never sits on here */
    if(on_body(g.food))g.food=SW+1;
}
int main(void)
{
    /* New game: three cells, head at (8,10) pointing right, food off the body. */
    game_new(&g,1,0);
    assert(g.len==3&&g.dir==SNAKE_RIGHT&&g.head==2);
    assert(g.body[2]==10*SW+8&&g.body[1]==10*SW+7&&g.body[0]==10*SW+6);
    assert(g.food<SNAKE_MAX&&!on_body(g.food)&&g.rng&&g.best==0);
    /* Same seed, same first food. */
    straight(3,SNAKE_RIGHT,10,10,7);Game a=g;straight(3,SNAKE_RIGHT,10,10,7);assert(a.food==g.food);
    /* Steering: one queued turn; reversing onto the neck is refused. */
    straight(3,SNAKE_RIGHT,10,10,5);
    assert(!game_turn(&g,SNAKE_RIGHT)&&!game_turn(&g,SNAKE_LEFT)&&!game_turn(&g,-1)&&!game_turn(&g,4));
    assert(game_turn(&g,SNAKE_UP)&&g.next_dir==SNAKE_UP);
    assert(!game_turn(&g,SNAKE_LEFT)); /* still refused while up is queued */
    game_step(&g);assert(g.dir==SNAKE_UP);
    /* Wall death: 14 right steps from x=8 on a 22-wide grid. */
    straight(3,SNAKE_RIGHT,8,10,5);
    for(int i=0;i<13;i++)assert(game_step(&g));
    assert(!game_step(&g)&&g.over);
    /* Eating: grows, scores 10, updates best, respawns food off the body. */
    straight(3,SNAKE_RIGHT,10,10,5);g.food=10*SW+11;
    assert(game_step(&g));
    assert(g.len==4&&g.score==10&&g.eaten==1&&g.best==10&&g.body[g.head]==10*SW+11);
    assert(g.food<SNAKE_MAX&&!on_body(g.food));
    /* Self hit: three turns into the row behind. */
    straight(6,SNAKE_RIGHT,10,10,5);
    assert(game_turn(&g,SNAKE_UP)&&game_step(&g)&&!g.over);
    assert(game_turn(&g,SNAKE_LEFT)&&game_step(&g)&&!g.over);
    assert(game_turn(&g,SNAKE_DOWN)&&!game_step(&g)&&g.over);
    /* Tail cell: the head may enter it as it leaves; eating there is fatal. */
    straight(4,SNAKE_RIGHT,10,10,5);
    game_turn(&g,SNAKE_DOWN);game_step(&g);
    game_turn(&g,SNAKE_LEFT);game_step(&g);
    assert(g.body[(g.head-(g.len-1)+SNAKE_MAX)%SNAKE_MAX]==10*SW+9);
    game_turn(&g,SNAKE_UP);assert(game_step(&g)&&!g.over&&g.body[g.head]==10*SW+9);
    straight(4,SNAKE_RIGHT,10,10,5);g.food=10*SW+9;
    game_turn(&g,SNAKE_DOWN);game_step(&g);
    game_turn(&g,SNAKE_LEFT);game_step(&g);
    game_turn(&g,SNAKE_UP);assert(!game_step(&g)&&g.over);
    /* Filling the board wins: one free cell right of the head. */
    game_new(&g,9,0);g.len=SNAKE_MAX-1;g.head=0;g.dir=g.next_dir=SNAKE_RIGHT;
    g.body[0]=(uint16_t)(19*SW+19);
    for(int i=1;i<g.len;i++)g.body[(g.head-i+SNAKE_MAX)%SNAKE_MAX]=(uint16_t)((i<438?i-1:439));
    g.food=(uint16_t)(19*SW+20);
    assert(game_step(&g)&&g.won&&g.over&&g.len==SNAKE_MAX&&g.food==SNAKE_FOOD_NONE);
    assert(!game_step(&g)&&!game_turn(&g,SNAKE_UP));
    /* Speed: 170 ms falling to 70; game_tick steps at the current speed. */
    game_new(&g,3,0);assert(game_level(&g)==1&&game_speed(&g)==170);
    g.eaten=5;assert(game_level(&g)==2&&game_speed(&g)==160);
    g.eaten=50;assert(game_speed(&g)==70);
    game_new(&g,3,0);g.food=0; /* top-left corner, away from the path */
    game_tick(&g,169);assert(g.body[g.head]%SW==8);
    game_tick(&g,1);assert(g.body[g.head]%SW==9);
    game_tick(&g,2000);assert(g.body[g.head]%SW==14&&g.tick==150&&!g.over);
    /* Arbitrary valid play survives encode/decode; the checksum catches edits. */
    uint8_t bytes[SAVE_BYTES+1];
    for(unsigned seed=1;seed<=50;seed++){
        game_new(&g,seed,100);
        for(int i=0;i<300&&!g.over;i++){
            switch(i%5){case 0:game_turn(&g,SNAKE_UP);break;case 1:game_turn(&g,SNAKE_RIGHT);break;
            case 2:game_turn(&g,SNAKE_DOWN);break;case 3:game_turn(&g,SNAKE_LEFT);break;
            case 4:game_tick(&g,250);break;}
            game_encode(&g,bytes);
            Game r;assert(game_decode(&r,bytes,SAVE_BYTES));
            assert(r.len==g.len&&r.score==g.score&&r.food==g.food);
            if(!g.over)assert(on_body(g.body[g.head]));
        }
    }
    game_new(&g,4,55);game_encode(&g,bytes);Game r;
    assert(game_decode(&r,bytes,SAVE_BYTES)&&r.best==55);
    assert(!game_decode(&r,bytes,SAVE_BYTES-1));assert(!game_decode(&r,bytes,SAVE_BYTES+1));
    bytes[200]^=1;assert(!game_decode(&r,bytes,SAVE_BYTES));
    game_encode(&g,bytes);uint32_t *h=(uint32_t*)bytes;h[0]^=1;assert(!game_decode(&r,bytes,SAVE_BYTES));
    /* Forged but checksum-valid states. */
    Game t;game_new(&t,2,0);
    #define forgery(mutate) do{game_encode(&t,bytes);Game bad;Game *p=(Game*)(bytes+12);mutate; \
        uint32_t *hh=(uint32_t*)bytes;hh[2]=0;for(unsigned n=0;n<sizeof(Game);n++)hh[2]=(hh[2]^bytes[12+n])*16777619u; \
        assert(!game_decode(&bad,bytes,SAVE_BYTES));game_new(&t,2,0);}while(0)
    forgery(p->len=2;);forgery(p->len=SNAKE_MAX+1;);forgery(p->head=-1;);forgery(p->head=SNAKE_MAX;);
    forgery(p->dir=4;);forgery(p->next_dir=4;);forgery(p->dir=SNAKE_RIGHT;p->next_dir=SNAKE_LEFT;);
    forgery(p->rng=0;);forgery(p->score=100000;);forgery(p->best=0;);forgery(p->tick=100000;);
    forgery(p->body[0]=p->body[1];);forgery(p->body[0]=SNAKE_MAX;);forgery(p->food=SNAKE_MAX;);
    forgery(p->food=p->body[0];);forgery(p->won=1;p->over=0;);forgery(p->won=1;);
    forgery(p->over=2;);forgery(p->eaten=100000;);
    #undef forgery
    puts("snake: all tests passed");
    return 0;
}
