#include "game.h"
#include <string.h>
static const int8_t dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
static uint32_t random32(Game *g){g->rng^=g->rng<<13;g->rng^=g->rng>>17;g->rng^=g->rng<<5;return g->rng;}
static void spawn_food(Game *g)
{
    uint8_t used[SNAKE_MAX];
    int free_cells[SNAKE_MAX],n=0;
    memset(used,0,sizeof(used));
    for(int i=0;i<g->len;i++)used[g->body[(g->head-i+SNAKE_MAX)%SNAKE_MAX]]=1;
    for(int c=0;c<SNAKE_MAX;c++)if(!used[c])free_cells[n++]=c;
    if(!n){if(g->len==SNAKE_MAX){g->won=1;g->over=1;}g->food=SNAKE_FOOD_NONE;return;}
    g->food=(uint16_t)free_cells[random32(g)%n];
}
void game_new(Game *g,uint32_t seed,uint32_t best)
{
    memset(g,0,sizeof(*g));g->rng=seed?seed:1;g->best=best;
    g->dir=g->next_dir=SNAKE_RIGHT;g->head=2;g->len=3;
    int cell=(SH/2)*SW+8;
    g->body[0]=(uint16_t)(cell-2);g->body[1]=(uint16_t)(cell-1);g->body[2]=(uint16_t)cell;
    spawn_food(g);
}
bool game_turn(Game *g,int dir)
{
    /* One queued turn; reversing onto the neck is refused. */
    if(g->over||dir<0||dir>3)return false;
    if(dir==g->dir||(dir+2)%4==g->dir)return false;
    g->next_dir=(uint8_t)dir;return true;
}
bool game_step(Game *g)
{
    if(g->over)return false;
    g->dir=g->next_dir;
    int cell=g->body[g->head],x=cell%SW,y=cell/SW;
    x+=dx[g->dir];y+=dy[g->dir];
    if(x<0||x>=SW||y<0||y>=SH){g->over=1;return false;}
    cell=y*SW+x;
    bool eat=cell==g->food;
    for(int i=0;i<g->len;i++){
        if(g->body[(g->head-i+SNAKE_MAX)%SNAKE_MAX]!=cell)continue;
        if(i==g->len-1&&!eat)break; /* the tail leaves as the head arrives */
        g->over=1;return false;
    }
    g->head=(g->head+1)%SNAKE_MAX;g->body[g->head]=(uint16_t)cell;
    if(eat){
        g->len++;g->score+=10;g->eaten++;
        if(g->score>g->best)g->best=g->score;
        spawn_food(g);
    }
    return true;
}
unsigned game_level(const Game *g){return 1+g->eaten/5;}
unsigned game_speed(const Game *g){unsigned l=game_level(g);return l>=11?70:170-10*(l-1);}
void game_tick(Game *g,uint32_t ms)
{
    /* Bound work after stalls. UI never catches up time spent in the background. */
    if(ms>1000)ms=1000;
    while(ms--&&!g->over){if(++g->tick>=game_speed(g)){g->tick=0;game_step(g);}}
}
static uint32_t checksum(const uint8_t *p,unsigned n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
void game_encode(const Game *g,uint8_t *out)
{
    uint32_t header[3]={0x314e4b53,sizeof(Game),0};
    header[2]=checksum((const uint8_t *)g,sizeof(*g));memcpy(out,header,12);memcpy(out+12,g,sizeof(*g));
}
bool game_decode(Game *g,const uint8_t *in,unsigned size)
{
    uint32_t h[3];Game t;uint8_t used[SNAKE_MAX];
    if(size!=SAVE_BYTES)return false;
    memcpy(h,in,12);memcpy(&t,in+12,sizeof(t));
    if(h[0]!=0x314e4b53||h[1]!=sizeof(Game)||h[2]!=checksum(in+12,sizeof(t)))return false;
    if(t.len<3||t.len>SNAKE_MAX||t.head<0||t.head>=SNAKE_MAX||t.dir>3||t.next_dir>3||
       (t.dir+2)%4==t.next_dir||t.over>1||t.won>1||(t.won&&(!t.over||t.len!=SNAKE_MAX))||
       !t.rng||t.eaten>SNAKE_MAX-3||t.score>t.eaten*10||t.best<t.score||t.tick>=game_speed(&t))return false;
    memset(used,0,sizeof(used));
    for(int i=0;i<t.len;i++){
        int c=t.body[(t.head-i+SNAKE_MAX)%SNAKE_MAX];
        if(c>=SNAKE_MAX||used[c])return false;
        used[c]=1;
    }
    if(t.won){if(t.food!=SNAKE_FOOD_NONE)return false;}
    else if(t.food>=SNAKE_MAX||used[t.food])return false;
    *g=t;return true;
}
