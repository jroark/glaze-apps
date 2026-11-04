/* The Glaze side of game.h: draws the 240x224 field, reads the BE-300 hardware
 * buttons, and keeps the saved game. The nav bar is 30 px, so the field sits at
 * (0,FIELD_Y) with its floor at the top of the ground band; game.h's y maps onto
 * that directly. OK or Up flaps, Escape opens the menu, Home saves and suspends. */
#include "game.h"
#include <glaze/ui.h>
#include <glaze/fs.h>
#include <glaze/sys.h>
#include <stdio.h>
#include <string.h>
static Game game;
static char path[FS_PATH_MAX],temp[FS_PATH_MAX];
static bool paused,confirm,save_failed;
static int selection;
static uint32_t scrolled; /* px scrolled since launch, so the clouds drift */
#define FIELD_Y 36
#define GROUND_H 6
#define CAP_H 8 /* game.h: caps are scenery, outside the collision box */
#define GROUND_Y (FIELD_Y+FL_H-GROUND_H)
static void save(void)
{
    uint8_t bytes[SAVE_BYTES];game_encode(&game,bytes);
    save_failed=fs_write_file(temp,bytes,sizeof(bytes))!=sizeof(bytes);
    if(!save_failed)save_failed=fs_rename(temp,path)<0;
}
static void hud(int x,int y,const char *s)
{
    gfx_text_shadow(&ui.surf,x,y,ui_font(FONT_B12),s,0xE5ECF5,0x0E2A3E,1,255);
}
/* Three flat rects read as a cloud puff at this size. */
static void cloud(int x,int y)
{
    gfx_surface *s=&ui.surf;
    gfx_fill(s,gfx_r(x,y+3,26,7),0xFFFFFF);
    gfx_fill(s,gfx_r(x+4,y,14,6),0xFFFFFF);
    gfx_fill(s,gfx_r(x+11,y-4,9,7),0xFFFFFF);
    gfx_fill_alpha(s,gfx_r(x,y+8,26,2),0x8FBEDC,110);
}
/* One pipe cap: its lower edge sits at y for the upper pipe, its upper edge at y
 * for the lower, so it hugs the gap without reaching into it. */
static void cap(int x,int y,bool upper)
{
    gfx_surface *s=&ui.surf;
    int top=upper?y-CAP_H:y;
    gfx_fill(s,gfx_r(x-4,top,FL_PIPE_W+8,CAP_H),0x1E6F34);
    gfx_fill(s,gfx_r(x-3,top+1,FL_PIPE_W+6,CAP_H-2),0x63BE68);
    gfx_fill(s,gfx_r(x-4,top+(upper?0:CAP_H-1),FL_PIPE_W+8,1),upper?0x8FD896:0x14522A);
    gfx_fill_alpha(s,gfx_r(x-2,top+1,4,CAP_H-2),0xFFFFFF,70);
}
/* One pair: both shafts and their caps. x is the shaft's left edge, top the gap
 * top and bottom the gap bottom, all in screen px. */
static void pipe(int x,int top,int bottom)
{
    gfx_surface *s=&ui.surf;
    gfx_fill(s,gfx_r(x,FIELD_Y,FL_PIPE_W,top-FIELD_Y),0x1E6F34);
    gfx_fill(s,gfx_r(x+1,FIELD_Y,FL_PIPE_W-3,top-FIELD_Y-1),0x57B45E);
    gfx_fill_alpha(s,gfx_r(x+2,FIELD_Y,3,top-FIELD_Y),0xFFFFFF,55);
    gfx_fill(s,gfx_r(x+FL_PIPE_W-2,top-1,2,1),0x14522A);
    gfx_fill(s,gfx_r(x,bottom,FL_PIPE_W,GROUND_Y-bottom),0x1E6F34);
    gfx_fill(s,gfx_r(x+1,bottom+1,FL_PIPE_W-3,GROUND_Y-bottom-1),0x57B45E);
    gfx_fill_alpha(s,gfx_r(x+2,bottom+1,3,GROUND_Y-bottom-1),0xFFFFFF,55);
    gfx_fill(s,gfx_r(x+FL_PIPE_W-2,bottom,2,1),0x14522A);
    cap(x,top,true);cap(x,bottom,false);
}
/* The bird, drawn about its collision centre: a 14 px body at FL_BIRD_X, a wing
 * that beats while playing and holds up right after a flap, beak and eye. */
static void bird(void)
{
    gfx_surface *s=&ui.surf;
    int cx=FL_BIRD_X,cy=FIELD_Y+game.y/1000;
    bool up=game.speed<0&&game.y>FL_BIRD_R*1000;
    gfx_circle(s,cx,cy,7,0x1B2A44,255);
    gfx_circle(s,cx,cy,6,0xFFC93C,255);
    gfx_circle(s,cx-2,cy-2,3,0xFFE9A0,255);
    if(up){gfx_fill(s,gfx_r(cx-10,cy-6,9,4),0xF08A2C);gfx_fill(s,gfx_r(cx-9,cy-5,7,2),0xFFD27A);}
    else if(game.over){gfx_fill(s,gfx_r(cx-10,cy+1,9,4),0xF08A2C);}
    else if(game.t/6%2){gfx_fill(s,gfx_r(cx-10,cy-3,9,4),0xF08A2C);gfx_fill(s,gfx_r(cx-9,cy-2,7,2),0xFFD27A);}
    else{gfx_fill(s,gfx_r(cx-10,cy+2,9,4),0xF08A2C);gfx_fill(s,gfx_r(cx-9,cy+2,7,2),0xFFD27A);}
    gfx_fill(s,gfx_r(cx+4,cy-1,6,3),0xF0742C);
    gfx_fill(s,gfx_r(cx+9,cy,1,1),0xC85A1E);
    gfx_fill(s,gfx_r(cx+1,cy-5,4,4),0xFFFFFF);
    gfx_fill(s,gfx_r(cx+3,cy-4,2,2),0x1B2A44);
}
static void draw(void)
{
    gfx_surface *s=&ui.surf;
    gfx_fill(s,gfx_r(0,0,UI_W,UI_H),0x101B2B);
    ui_draw_navbar(s,"Flappy",NULL,NULL,0);
    gfx_set_clip(s,gfx_r(0,FIELD_Y,FL_W,FL_H)); /* the field clips sky, pipes, bird */
    gfx_vgradient(s,gfx_r(0,FIELD_Y,FL_W,FL_H),0x5EC8E8,0xC2E9F5,255);
    int off=(int)(scrolled/3%(FL_W+40)); /* clouds crawl, they are far away */
    for(int i=0;i<4;i++){
        int x=i*70+10-off;
        while(x<-30)x+=FL_W+40;
        while(x>FL_W+10)x-=FL_W+40;
        cloud(x,56+(i&1)*18);
    }
    for(int i=0;i<FL_PIPES;i++){ /* head slot is the leftmost pair, game.h */
        int slot=(game.head+i)%FL_PIPES;
        int top=FIELD_Y+game.top[slot]/10;
        pipe(game.x[slot]/10,top,top+FL_GAP);
    }
    gfx_fill(s,gfx_r(0,GROUND_Y,FL_W,GROUND_H),0x1E6F34);
    gfx_fill(s,gfx_r(0,GROUND_Y,FL_W,1),0x8FD896);
    for(int x=-(int)(scrolled%16);x<FL_W;x+=16)gfx_fill(s,gfx_r(x,GROUND_Y+2,8,2),0x2F8A44);
    gfx_fill_alpha(s,gfx_r(0,GROUND_Y+GROUND_H-2,FL_W,2),0x14522A,140);
    char b[32];
    const gfx_font *f=ui_font(FONT_B20);
    snprintf(b,sizeof(b),"%u",game.score);
    gfx_text_shadow(s,FL_W/2-gfx_text_width(f,b)/2,44,f,b,0xFFF6C8,0x123048,1,255);
    snprintf(b,sizeof(b),"Best %u",game.best);hud(6,FIELD_Y+4,b);
    snprintf(b,sizeof(b),"Level %u",game_level(&game));
    f=ui_font(FONT_B12);
    hud(FL_W-6-gfx_text_width(f,b),FIELD_Y+4,b);
    bird();
    gfx_reset_clip(s);
    if(paused||game.over){
        int count=game.over&&!confirm?1:2,iy=game.over?142:132;
        const char *items[2]={"Resume","New Game"};
        if(confirm){items[0]="Cancel";items[1]="Discard game";}
        else if(game.over)items[0]="New Game";
        gfx_fill_alpha(s,gfx_r(0,FIELD_Y,FL_W,FL_H),0x000000,185);
        gfx_round_rect(s,gfx_r(20,84,200,150),9,0x304763,255);
        gfx_text_center(s,20,95,200,ui_font(FONT_B20),confirm?"New game?":game.over?"Game over":"Paused",0xFFFFFF,255);
        if(game.over){
            snprintf(b,sizeof(b),"Score %u   Best %u",game.score,game.best);
            gfx_text_center(s,20,122,200,ui_font(FONT_B14),b,0xC8D8EB,255);
        }
        for(int i=0;i<count;i++){
            if(i==selection)gfx_round_rect(s,gfx_r(33,iy+i*30,174,26),4,0x52749C,255);
            gfx_text_center(s,20,iy+6+i*30,200,ui_font(FONT_B14),items[i],0xFFFFFF,255);
        }
        gfx_text_center(s,20,212,200,ui_font(FONT_B12),"Up/Down select   OK choose",0xC8D8EB,255);
    }
    gfx_text_center(s,0,266,FL_W,ui_font(FONT_B12),"OK / Up flap   ESC pauses",0xA6BAD2,255);
    if(save_failed)gfx_text_center(s,0,284,FL_W,ui_font(FONT_B12),"Could not save game",0xFFCF8A,255);
    else gfx_text_center(s,0,284,FL_W,ui_font(FONT_B12),"Home saves and suspends",0xA6BAD2,255);
    ui_invalidate_all();ui_present();
}
static void toggle(void){paused=!paused;confirm=false;selection=0;save();}
static void restart(void)
{
    game_new(&game,gz_time_ms(),game.best);
    scrolled=0;paused=confirm=false;selection=0;save();
}
int main(void)
{
    ui_init(SB_STATUS_GRAY);
    snprintf(path,sizeof(path),"%s/data",res_bundle());fs_mkdirs(path);
    snprintf(path,sizeof(path),"%s/data/state",res_bundle());
    snprintf(temp,sizeof(temp),"%s/data/state.tmp",res_bundle());
    uint8_t bytes[SAVE_BYTES+1];
    int size=fs_read_file(path,bytes,sizeof(bytes));
    if(size>0&&game_decode(&game,bytes,size))paused=true;
    else{game_new(&game,gz_time_ms(),0);save();}
    uint32_t last=gz_time_ms();draw();
    for(;;){
        struct gz_input_event e;
        int got=ui_next_event(&e,paused||game.over?-1:16);
        uint32_t now=gz_time_ms(),elapsed=now-last;last=now;
        if(paused||game.over)elapsed=0;
        if(got){
            if(e.type==GZ_EV_APP_DEACTIVATE){paused=true;confirm=false;selection=0;save();continue;}
            if(e.type==GZ_EV_APP_ACTIVATE){paused=true;selection=0;draw();continue;}
            if(e.type==GZ_EV_KEY_DOWN){
                if(e.code==GZ_KEY_ESC){if(confirm){confirm=false;selection=0;}else if(!game.over)toggle();}
                else if(paused||game.over){
                    int count=game.over&&!confirm?1:2;
                    if(e.code==GZ_KEY_UP)selection=(selection+count-1)%count;
                    if(e.code==GZ_KEY_DOWN)selection=(selection+1)%count;
                    if(e.code==GZ_KEY_OK||e.code==GZ_KEY_ENTER){
                        if(game.over)restart();
                        else if(confirm){if(selection==1)restart();else{confirm=false;selection=0;}}
                        else if(selection==0)toggle();
                        else{confirm=true;selection=0;}
                    }
                }else if(e.code==GZ_KEY_OK||e.code==GZ_KEY_UP||e.code==GZ_KEY_ENTER)game_flap(&game);
            }
        }
        if(!paused&&!game.over){
            uint8_t old_over=game.over;
            scrolled+=game.speed_px*elapsed/1000;
            game_tick(&game,elapsed);
            if(old_over!=game.over)save();
        }
        if(got||(!paused&&!game.over))draw();
    }
}
