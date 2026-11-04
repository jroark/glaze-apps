#include "../../packages/flappy/game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Game g;
/* Units are restated here so the test checks game.c, not itself: x and pipe tops
 * are 1/10 px, y and the fall speed 1/1000 px, and one step moves y by speed*10. */
#define X(px) ((px)*10)
#define Y(px) ((px)*1000)
#define MID Y(FL_H/2)                 /* game_new starts the bird here */
#define BX0 X(FL_BIRD_X-FL_BIRD_R)    /* the bird's collision box, in x */
#define BX1 X(FL_BIRD_X+FL_BIRD_R)
#define CX X(FL_BIRD_X)               /* a pair scores as its right edge clears this */
#define GAPLOW X(FL_H-FL_GAP-FL_BOT_MIN) /* the lowest a gap top may sit, 136 px */
#define SLOT(i) ((g.head+(i))%FL_PIPES)
#define PX(i) g.x[SLOT(i)]
#define TOP(i) g.top[SLOT(i)]
/* Park one pair on the bird with its gap top `margin` px below the bird box
 * (negative overlaps it), y steady, other pairs clear of the box on either side. */
static void parked(int margin)
{
    game_new(&g,5,0);
    g.x[0]=X(40);g.x[1]=X(160);g.x[2]=X(280);
    g.top[0]=(int16_t)X(FL_H/2-FL_BIRD_R-margin);
    g.y=MID;g.speed=0;
    assert(g.x[0]+X(FL_PIPE_W)>BX0&&g.x[0]<BX1);
    assert(g.x[1]>=BX1&&g.x[2]>=BX1);
    assert(g.top[0]>=X(FL_TOP_MIN)&&g.top[0]<=GAPLOW);
    assert(g.y-Y(FL_BIRD_R)>=Y(FL_TOP_MIN)&&g.y+Y(FL_BIRD_R)<=Y(FL_H-FL_BOT_MIN));
}
int main(void)
{
    /* New game: bird at the field's middle, speed 0, base scroll, best kept, and
     * three pairs queued off the right edge FL_SPACING apart, oldest at head. */
    game_new(&g,1,55);
    assert(g.y==MID&&g.speed==0&&g.speed_px==FL_SPEED_BASE&&g.score==0&&g.best==55);
    assert(!g.over&&g.head==0&&g.t==0&&g.acc==0);
    for(int i=0;i<FL_PIPES;i++){
        assert(PX(i)==X(FL_W)+i*X(FL_SPACING)&&!g.passed[SLOT(i)]);
        assert(TOP(i)>=X(FL_TOP_MIN)&&TOP(i)<=GAPLOW);
    }
    /* Same seed, same gap tops; seed 0 spawns anyway (rng is forced nonzero). */
    game_new(&g,4242,0);int16_t tops[FL_PIPES];
    for(int i=0;i<FL_PIPES;i++)tops[i]=g.top[i];
    game_new(&g,4242,0);
    for(int i=0;i<FL_PIPES;i++)assert(g.top[i]==tops[i]);
    game_new(&g,0,0);assert(g.rng&&g.top[0]>=X(FL_TOP_MIN));
    /* Flapping sets the rise; it does nothing once the run is over. */
    game_new(&g,7,0);
    game_flap(&g);assert(g.speed==FL_FLAP_SPEED);
    game_step(&g);assert(g.y==MID+FL_FLAP_SPEED*10&&g.speed==FL_FLAP_SPEED+FL_GRAVITY);
    g.over=1;game_flap(&g);assert(g.speed==FL_FLAP_SPEED+FL_GRAVITY);
    /* Gravity: the first step holds still, then the fall grows 6 px/s per step. */
    game_new(&g,2,0);
    game_step(&g);assert(g.y==MID&&g.speed==FL_GRAVITY);
    game_step(&g);assert(g.y==MID+60&&g.speed==2*FL_GRAVITY);
    /* Terminal fall is capped, and the ceiling ends a run that climbs into it. */
    g.speed=FL_MAX_FALL-4;game_step(&g);
    assert(g.speed==FL_MAX_FALL&&g.y==MID+60+(FL_MAX_FALL-4)*10&&!g.over);
    game_new(&g,2,7);
    for(int i=0;i<34;i++){game_flap(&g);game_step(&g);assert(!g.over);}
    game_flap(&g);game_step(&g);
    assert(g.over&&g.y==Y(FL_BIRD_R)&&g.speed==FL_FLAP_SPEED+FL_GRAVITY&&g.best==7);
    /* Floor: free fall from the middle covers 105 px, so step 60 lands it. */
    game_new(&g,3,7);
    for(int i=0;i<59;i++){game_step(&g);assert(!g.over);}
    assert(PX(0)<X(FL_W));
    game_step(&g);
    assert(g.over&&g.y==Y(FL_H-FL_BIRD_R)&&g.best==7);
    game_step(&g);assert(g.y==Y(FL_H-FL_BIRD_R)&&g.t==60);
    /* game_tick steps whole 10 ms and carries the remainder; t counts the steps. */
    game_new(&g,3,0);
    game_tick(&g,9);assert(g.t==0&&g.acc==9&&g.y==MID);
    game_tick(&g,1);assert(g.t==1&&g.acc==0);
    game_tick(&g,16);assert(g.t==2&&g.acc==6);
    game_tick(&g,500);assert(g.t==52&&g.acc==6&&!g.over);
    game_tick(&g,3);assert(g.t==52&&g.acc==9&&!g.over);
    /* Scroll: pairs move left by speed_px/10 px per step, and the pair that
     * leaves the left edge re-enters past the tail with a fresh gap, so slot
     * head always names the leftmost pair. */
    game_new(&g,11,0);int16_t x0[FL_PIPES];
    for(int i=0;i<FL_PIPES;i++)x0[i]=g.x[i];
    /* Hover at the gap centre of the pair that is on the bird - the head pair,
     * from step 322 to 384 - so the bird lives through the scroll; no other
     * pair reaches the bird before step 440, so the hover is harmless. The
     * head pair scores at step 374 and pops when its x reaches -FL_PIPE_W*10,
     * step 440. hit() tests the scrolled x, so lead the hover window by 1 px. */
    int hover=0;
    for(int i=0;i<439;i++){
        for(int j=0;j<FL_PIPES;j++)
            if(g.x[j]+X(FL_PIPE_W)>BX0-X(FL_SPEED_BASE/10)&&g.x[j]<BX1+X(FL_SPEED_BASE/10))hover=j;
        g.y=(g.top[hover]+X(FL_GAP/2))*100;g.speed=0;game_step(&g);
    }
    assert(g.head==0&&g.score==1&&g.t==439&&!g.over&&g.speed_px==FL_SPEED_BASE);
    for(int i=0;i<FL_PIPES;i++)assert(g.x[i]==x0[i]-439*(FL_SPEED_BASE/10));
    assert(g.x[0]+X(FL_PIPE_W)==FL_SPEED_BASE/10); /* one more unit and it pops */
    game_step(&g);
    assert(g.head==1&&g.t==440);
    assert(g.x[1]==x0[1]-440*(FL_SPEED_BASE/10)&&g.x[2]==x0[2]-440*(FL_SPEED_BASE/10));
    assert(g.x[0]==g.x[2]+X(FL_SPACING)&&!g.passed[0]&&g.top[0]>=X(FL_TOP_MIN)&&g.top[0]<=GAPLOW);
    game_step(&g); /* the re-entered pair lands 120 px past the tail: no second pop */
    assert(g.head==1&&g.t==441&&g.x[0]==g.x[2]+X(FL_SPACING));
    /* Scoring: a pair counts once its right edge clears the bird's centre line,
     * counts only once, and every tenth point steps the scroll up by 4 px/s. */
    parked(3);g.x[0]=X(FL_BIRD_X)-X(FL_PIPE_W)-6;g.score=8;
    game_step(&g);
    assert(g.score==9&&g.passed[0]==1&&g.speed_px==FL_SPEED_BASE&&game_level(&g)==0&&!g.over);
    g.passed[0]=0;g.x[0]=X(FL_BIRD_X)-X(FL_PIPE_W)-6;game_step(&g);
    assert(g.score==10&&g.speed_px==FL_SPEED_BASE+4&&game_level(&g)==1);
    g.score=150;g.passed[0]=0;g.x[0]=X(FL_BIRD_X)-X(FL_PIPE_W)-6;game_step(&g);
    assert(g.score==151&&g.speed_px==FL_SPEED_MAX&&game_level(&g)==15);
    g.score=400;g.passed[0]=0;g.x[0]=X(FL_BIRD_X)-X(FL_PIPE_W)-6;game_step(&g);
    assert(g.speed_px==FL_SPEED_MAX&&game_level(&g)==15&&!g.over);
    /* Pipes: only the shaft collides. A bird box flush with either end of the
     * gap survives, one unit (1/1000 px) of overlap is fatal, and a shaft hit
     * stops the bird dead, unlike the floor catch that ends a fall. */
    parked(0);game_step(&g);
    assert(!g.over&&g.y==MID&&g.speed==FL_GRAVITY); /* flush with the gap top */
    parked(0);g.y--;game_step(&g);
    assert(g.over&&g.speed==0);
    parked(58);game_step(&g);                       /* flush with the gap bottom */
    assert(!g.over);
    parked(58);g.y++;game_step(&g);
    assert(g.over&&g.speed==0);
    parked(-1);g.best=9;game_step(&g);
    assert(g.over&&g.speed==0&&g.best==9&&!g.score); /* a scoreless crash keeps best */
    /* Save record: a mid-play state round-trips through encode/decode; the
     * flap every 60 ticks keeps the bird in the air, and nothing has scored
     * yet (step 255 of 374). */
    game_new(&g,77,31);
    for(int i=0;i<150;i++){if(i%60==0)game_flap(&g);game_tick(&g,17);}
    assert(!g.over&&g.t==255&&g.acc==0&&g.score==0&&g.head==0&&g.best==31);
    uint8_t bytes[SAVE_BYTES+1];Game r;
    game_encode(&g,bytes);
    assert(game_decode(&r,bytes,SAVE_BYTES)&&memcmp(&r,&g,sizeof r)==0);
    assert(!game_decode(&r,bytes,SAVE_BYTES-1));assert(!game_decode(&r,bytes,SAVE_BYTES+1));
    for(unsigned i=0;i<SAVE_BYTES;i++){ /* any flipped byte breaks the checksum */
        bytes[i]^=0x40;assert(!game_decode(&r,bytes,SAVE_BYTES));bytes[i]^=0x40;
    }
    game_encode(&g,bytes);uint32_t *h=(uint32_t*)bytes;
    h[0]^=1;assert(!game_decode(&r,bytes,SAVE_BYTES)); /* wrong magic: not ours */
    game_encode(&g,bytes);h[1]^=4;
    assert(!game_decode(&r,bytes,SAVE_BYTES));         /* struct size must match */
    /* Forged but checksum-valid states: the bounds above are checked by a
     * game_decode of states only a hand-edited card could hold. */
    Game t;game_new(&t,2,0);
    #define forgery(mutate) do{game_encode(&t,bytes);Game bad;Game *p=(Game*)(bytes+8);mutate; \
        uint32_t c=2166136261u;for(unsigned n=0;n<sizeof(Game);n++)c=(c^bytes[8+n])*16777619u; \
        memcpy(bytes+8+sizeof(Game),&c,sizeof c); \
        assert(!game_decode(&bad,bytes,SAVE_BYTES));game_new(&t,2,0);}while(0)
    forgery(p->speed_px=FL_SPEED_MAX+1;);forgery(p->speed_px=FL_SPEED_BASE-1;);
    forgery(p->speed=FL_MAX_FALL+1;);forgery(p->speed=FL_FLAP_SPEED-1;);
    forgery(p->y=-1;);forgery(p->y=(int32_t)FL_H*1000+1;);
    forgery(p->head=FL_PIPES;);forgery(p->over=2;);forgery(p->acc=10;);
    forgery(p->passed[0]=2;);forgery(p->top[0]=X(FL_TOP_MIN)-1;);forgery(p->top[2]=GAPLOW+1;);
    forgery(for(int i=0;i<FL_PIPES;i++)p->x[i]-=X(265);); /* off the left, ring intact */
    forgery(for(int i=0;i<FL_PIPES;i++)p->x[i]+=X(253);); /* two screens out to the right */
    forgery(p->x[(p->head+1)%FL_PIPES]+=6;);              /* slots unevenly spaced */
    #undef forgery
    puts("flappy: all tests passed");
    return 0;
}
