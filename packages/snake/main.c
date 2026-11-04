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
/* 10 px cells at (10,54); the frame adds one pixel on each side. */
static void save(void)
{
    uint8_t bytes[SAVE_BYTES];game_encode(&game,bytes);
    save_failed=fs_write_file(temp,bytes,sizeof(bytes))!=sizeof(bytes);
    if(!save_failed)save_failed=fs_rename(temp,path)<0;
}
static void block(int x,int y,int outer,int inner)
{
    gfx_fill(&ui.surf,gfx_r(x,y,10,10),outer);
    gfx_fill(&ui.surf,gfx_r(x+1,y+1,8,8),inner);
}
static void text(int x,int y,const char *s){gfx_text(&ui.surf,x,y,ui_font(FONT_B14),s,0xE5ECF5,255);}
static void draw(void)
{
    gfx_surface *s=&ui.surf;
    gfx_fill(s,gfx_r(0,0,UI_W,UI_H),0x101B2B);
    ui_draw_navbar(s,"Snake",NULL,NULL,0);
    char b[32];
    snprintf(b,sizeof(b),"Score %u",game.score);text(12,35,b);
    snprintf(b,sizeof(b),"Best %u",game.best);gfx_text_center(s,128,35,100,ui_font(FONT_B14),b,0xE5ECF5,255);
    gfx_fill(s,gfx_r(9,53,222,202),0x40536D);
    gfx_fill(s,gfx_r(10,54,SW*10,SH*10),0x182539);
    if(game.food<SNAKE_MAX){
        int fx=10+(game.food%SW)*10,fy=54+(game.food/SW)*10;
        block(fx,fy,0x8F2F2C,0xF07482);
        gfx_fill_alpha(s,gfx_r(fx+2,fy+2,4,2),0xFFFFFF,90);
    }
    for(int i=game.len-1;i>=0;i--){
        int c=game.body[(game.head-i+SNAKE_MAX)%SNAKE_MAX];
        bool headcell=i==0;
        block(10+(c%SW)*10,54+(c/SW)*10,headcell?0x3C8F46:0x1E6F34,headcell?0xA6E87B:0x57B45E);
        if(headcell)gfx_fill_alpha(s,gfx_r(11+(c%SW)*10,55+(c/SW)*10,8,3),0xFFFFFF,70);
    }
    gfx_text_center(s,0,262,240,ui_font(FONT_B12),"Arrows steer the snake   ESC pauses",0xA6BAD2,255);
    gfx_text_center(s,0,280,240,ui_font(FONT_B12),"Rocket saves and suspends",0xA6BAD2,255);
    if(paused||game.over){
        gfx_fill_alpha(s,gfx_r(0,36,240,266),0x000000,185);
        gfx_round_rect(s,gfx_r(20,88,200,146),9,0x304763,255);
        gfx_text_center(s,20,99,200,ui_font(FONT_B20),confirm?"New game?":game.over?(game.won?"You win!":"Game over"):"Paused",0xFFFFFF,255);
        const char *items[2]={"Resume","New Game"};
        int count=game.over&&!confirm?1:2;
        if(confirm){items[0]="Cancel";items[1]="Discard game";}
        else if(game.over)items[0]="New Game";
        for(int i=0;i<count;i++){
            if(i==selection)gfx_round_rect(s,gfx_r(33,132+i*30,174,26),4,0x52749C,255);
            gfx_text_center(s,20,138+i*30,200,ui_font(FONT_B14),items[i],0xFFFFFF,255);
        }
        gfx_text_center(s,20,212,200,ui_font(FONT_B12),"Up/Down select   OK choose",0xC8D8EB,255);
    }
    if(save_failed)gfx_text_center(s,0,291,240,ui_font(FONT_B12),"Could not save game",0xFFCF8A,255);
    ui_invalidate_all();ui_present();
}
static void toggle(void){paused=!paused;confirm=false;selection=0;save();}
static void restart(void)
{
    game_new(&game,gz_time_ms(),game.best);
    paused=confirm=false;selection=0;save();
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
                }else{
                    int dir=e.code==GZ_KEY_UP?SNAKE_UP:e.code==GZ_KEY_DOWN?SNAKE_DOWN:
                            e.code==GZ_KEY_LEFT?SNAKE_LEFT:e.code==GZ_KEY_RIGHT?SNAKE_RIGHT:4;
                    game_turn(&game,dir);
                }
            }
        }
        if(!paused&&!game.over){
            unsigned old_eaten=game.eaten;uint8_t old_over=game.over;
            game_tick(&game,elapsed);
            if(old_eaten!=game.eaten||old_over!=game.over)save();
        }
        if(got||(!paused&&!game.over))draw();
    }
}
