/* 1942: the Glaze side. Rules live in game.c; this file draws, reads
 * input, saves, and follows the app lifecycle. */
#include "game.h"
#include <glaze/fs.h>
#include <glaze/sys.h>
#include <glaze/ui.h>
#include <stdio.h>
#include <string.h>

/* Field: game coords (0,0)..(220,230) drawn at (FX,FY) on the 240x304 surface. */
#define FX 10
#define FY 60

static Game game;
static char path[FS_PATH_MAX], temp[FS_PATH_MAX];
static bool paused, save_failed, art;
static int selection;           /* pause menu row */
static int held;                /* held arrows: 1 up, 2 right, 4 down, 8 left */
static int down_row = -1;       /* menu row under a touch, or -1 */

/* Sprites from res/ (our own art via art/draw_art.py, or arcade_art.py's
 * swap). Without them draw() falls back to the placeholder shapes. */
static gfx_image im_player, im_fighter, im_diver, im_bomber, im_shot, im_bomb;
static gfx_image im_boom[6];

static void load_art(void)
{
    art = res_image(&im_player, "player.gzi")
       && res_image(&im_fighter, "fighter.gzi")
       && res_image(&im_diver, "diver.gzi")
       && res_image(&im_bomber, "bomber.gzi")
       && res_image(&im_shot, "shot.gzi")
       && res_image(&im_bomb, "bomb.gzi")
       && res_image(&im_boom[0], "boom0.gzi") && res_image(&im_boom[1], "boom1.gzi")
       && res_image(&im_boom[2], "boom2.gzi") && res_image(&im_boom[3], "boom3.gzi")
       && res_image(&im_boom[4], "boom4.gzi") && res_image(&im_boom[5], "boom5.gzi");
    printf("sky1942: art=%d\n", art);
}

static void draw_sprite(gfx_surface *s, const gfx_image *im, int cx, int cy)
{
    gfx_draw_image(s, cx - im->w / 2, cy - im->h / 2, im, 255);
}

/* Write to a temp file, then rename, so a crash never leaves half a save. */
static void save(void)
{
    uint8_t bytes[SAVE_BYTES];
    game_encode(&game, bytes);
    save_failed = fs_write_file(temp, bytes, sizeof(bytes)) != (int)sizeof(bytes);
    if (!save_failed)
        save_failed = fs_rename(temp, path) < 0;
    printf("sky1942: saved score=%u ok=%d\n", (unsigned)game.score, !save_failed);
}

/* Pause menu rows; shared by draw() and the touch handler. */
static int menu_count(void)
{
    return game.over ? 1 : 2;
}

static gfx_rect menu_rect(int i)
{
    return gfx_r(33, 152 + i * 30, 174, 26);
}

static int menu_row_at(int x, int y)
{
    for (int i = 0; i < menu_count(); i++)
        if (gfx_rect_contains(menu_rect(i), x, y))
            return i;
    return -1;
}

/* A tiny top-down plane: wings, fuselage, tail. ws = half wing span,
 * fh = half fuselage length. ws 5 = HUD icon, ws 9 = the player. */
static void draw_plane(gfx_surface *s, int x, int y, int ws, int fh, uint32_t body, uint32_t wing)
{
    gfx_fill(s, gfx_r(x - ws, y - 1, 2 * ws + 1, 3), wing);
    gfx_fill(s, gfx_r(x - 1, y - fh, 3, 2 * fh + 1), body);
    gfx_fill(s, gfx_r(x - ws * 2 / 3, y + fh - 3, ws * 4 / 3 + 1, 3), wing);
}

static void draw(void)
{
    gfx_surface *s = &ui.surf;
    char b[40];
    gfx_fill(s, gfx_r(0, 0, UI_W, UI_H), 0x0E1420);
    ui_draw_navbar(s, "Sky Raid", NULL, NULL, 0);

    snprintf(b, sizeof(b), "Score %u", (unsigned)game.score);
    gfx_text(s, 10, 38, ui_font(FONT_B12), b, 0xE5ECF5, 255);
    snprintf(b, sizeof(b), "WAVE %u", (unsigned)game.wave);
    gfx_text_center(s, 70, 38, 100, ui_font(FONT_B12), b, 0xE5ECF5, 255);
    for (int i = 0; i < game.lives; i++)
        draw_plane(s, 218 - i * 14, 45, 5, 4, 0xD8E2EE, 0x9FB2C8);

    /* Sea with scrolling foam rows and cloud puffs. */
    gfx_fill(s, gfx_r(FX, FY, FIELD_W, FIELD_H), 0x17395C);
    gfx_set_clip(s, gfx_r(FX, FY, FIELD_W, FIELD_H));
    int r0 = game.scroll / 16, off = game.scroll % 16;
    for (int i = 0; i < 16; i++) {
        int r = r0 + i;
        int y = FY + i * 16 - off;
        if (r % 7 == 3) {
            int cx = FX + (int)(((unsigned)r * 89u + 53u) % (unsigned)(FIELD_W - 70)) + 20;
            gfx_circle(s, cx, y + 8, 13, 0xE6EEF6, 150);
            gfx_circle(s, cx + 14, y + 10, 9, 0xE6EEF6, 150);
        } else {
            int cx = FX + (int)(((unsigned)r * 53u + 29u) % (unsigned)(FIELD_W - 40)) + 10;
            gfx_fill(s, gfx_r(cx, y, 20, 2), 0x1F4A76);
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &game.enemies[i];
        if (!e->alive)
            continue;
        int x = FX + e->x, y = FY + e->y;
        if (art) {
            const gfx_image *im = e->type == E_BOMBER ? &im_bomber
                                : e->type == E_DIVER ? &im_diver : &im_fighter;
            draw_sprite(s, im, x, y);
        } else if (e->type == E_BOMBER) {
            gfx_fill(s, gfx_r(x - 13, y - 2, 27, 5), 0x5D7C3C);
            gfx_fill(s, gfx_r(x - 2, y - 8, 5, 18), 0x71934B);
            gfx_fill(s, gfx_r(x - 9, y - 1, 3, 3), 0x445C2C);
            gfx_fill(s, gfx_r(x + 7, y - 1, 3, 3), 0x445C2C);
        } else if (e->type == E_DIVER) {
            gfx_fill(s, gfx_r(x - 6, y - 1, 13, 3), 0xD0533F);
            gfx_fill(s, gfx_r(x - 1, y - 7, 3, 14), 0x8E352B);
        } else {
            gfx_fill(s, gfx_r(x - 7, y - 1, 15, 3), 0xB9C2CE);
            gfx_fill(s, gfx_r(x - 1, y - 7, 3, 14), 0x93A0B0);
            gfx_fill(s, gfx_r(x - 1, y + 5, 3, 2), 0xC8502F);
        }
    }
    for (int i = 0; i < MAX_BOMBS; i++) {
        const Bomb *b2 = &game.bombs[i];
        if (!b2->alive)
            continue;
        if (art)
            draw_sprite(s, &im_bomb, FX + b2->x, FY + b2->y);
        else
            gfx_circle(s, FX + b2->x, FY + b2->y, 2, 0xE8EAEE, 255);
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        const Shot *sh = &game.shots[i];
        if (!sh->alive)
            continue;
        if (art)
            draw_sprite(s, &im_shot, FX + sh->x, FY + sh->y);
        else
            gfx_fill(s, gfx_r(FX + sh->x - 1, FY + sh->y - 3, 3, 7), 0xFFE38A);
    }

    /* Player plane: blinks while invincible. */
    if (!(game.inv && (game.inv / 4) % 2) && !game.over) {
        int x = FX + game.px, y = FY + game.py;
        if (art) {
            draw_sprite(s, &im_player, x, y);
        } else {
            draw_plane(s, x, y, 9, 8, 0xF4F8FC, 0xDCE6F2);
            gfx_fill(s, gfx_r(x - 1, y - 4, 3, 4), 0x3E6FA3);
        }
    }

    for (int i = 0; i < MAX_FX; i++) {
        const Fx *f = &game.fx[i];
        if (!f->alive)
            continue;
        int x = FX + f->x, y = FY + f->y;
        if (art) {
            int fr = f->age / 2;
            const gfx_image *im = &im_boom[fr > 5 ? 5 : fr];
            if (f->big)
                gfx_draw_image_scaled(s, gfx_r(x - im->w * 3 / 4, y - im->h * 3 / 4,
                                               im->w * 3 / 2, im->h * 3 / 2), im, 255);
            else
                draw_sprite(s, im, x, y);
        } else {
            int rad = 2 + f->age / 2;
            uint32_t col = f->age < 5 ? 0xFFD54D : (f->age < 9 ? 0xE8793C : 0x9AA0A6);
            gfx_circle(s, x, y, f->big ? rad * 3 / 2 : rad, col, 255);
        }
    }
    gfx_reset_clip(s);

    gfx_fill(s, gfx_r(FX - 1, FY - 1, FIELD_W + 2, 1), 0x2C4258);
    gfx_fill(s, gfx_r(FX - 1, FY + FIELD_H, FIELD_W + 2, 1), 0x2C4258);
    gfx_fill(s, gfx_r(FX - 1, FY, 1, FIELD_H), 0x2C4258);
    gfx_fill(s, gfx_r(FX + FIELD_W, FY, 1, FIELD_H), 0x2C4258);

    if (!paused && !game.over) {
        if (game.state == W_INTRO) {
            if (game.state_timer > 30) {
                snprintf(b, sizeof(b), "WAVE %u", (unsigned)game.wave);
                gfx_text_center(s, 0, 130, UI_W, ui_font(FONT_B20), b, 0xFFFFFF, 255);
            } else {
                gfx_text_center(s, 0, 130, UI_W, ui_font(FONT_B20), "GO!", 0xFFE38A, 255);
            }
        } else if (game.state == W_CLEAR) {
            snprintf(b, sizeof(b), "WAVE %u CLEAR", (unsigned)game.wave);
            gfx_text_center(s, 0, 130, UI_W, ui_font(FONT_B14), b, 0xFFE38A, 255);
        }
    }

    gfx_text_center(s, 0, 291, UI_W, ui_font(FONT_R10), "Arrows/drag fly   Guns fire auto", 0xA6BAD2, 255);
    if (paused || game.over) {
        gfx_fill_alpha(s, gfx_r(0, 30, UI_W, UI_H - 30), 0x000000, 185);
        gfx_round_rect(s, gfx_r(20, 100, 200, 110), 9, 0x304763, 255);
        gfx_text_center(s, 20, 106, 200, ui_font(FONT_B20), game.over ? "Shot down" : "Paused", 0xFFFFFF, 255);
        snprintf(b, sizeof(b), "Score %u   Best %u", (unsigned)game.score, (unsigned)game.best);
        gfx_text_center(s, 20, 132, 200, ui_font(FONT_R11), b, 0xA6BAD2, 255);
        const char *items[2] = {game.over ? "New Game" : "Resume", "New Game"};
        int count = menu_count();
        for (int i = 0; i < count; i++) {
            if (i == selection)
                gfx_round_rect(s, menu_rect(i), 4, 0x52749C, 255);
            gfx_text_center(s, 20, 157 + i * 30, 200, ui_font(FONT_B14), items[i], 0xFFFFFF, 255);
        }
    }
    if (save_failed)
        gfx_text_center(s, 0, 270, UI_W, ui_font(FONT_B12), "Could not save game", 0xFFCF8A, 255);
    ui_invalidate_all();
    ui_present();
}

static void restart(void)
{
    game_new(&game, gz_time_ms(), game.best);
    paused = false;
    selection = 0;
    held = 0;
    save();
}

static void menu_pick(int i)
{
    if (game.over || i == 1)
        restart();
    else
        paused = false;
}

/* Pause/game-over menu keys. */
static void menu_key(int code)
{
    int count = menu_count();
    if (code == GZ_KEY_UP)
        selection = (selection + count - 1) % count;
    else if (code == GZ_KEY_DOWN)
        selection = (selection + 1) % count;
    else if (code == GZ_KEY_OK || code == GZ_KEY_ENTER)
        menu_pick(selection);
    else if (code == GZ_KEY_ESC && !game.over)
        paused = false;
}

static Stick stick(void)
{
    Stick s;
    s.dx = (int8_t)((held & 2 ? 1 : 0) - (held & 8 ? 1 : 0));
    s.dy = (int8_t)((held & 4 ? 1 : 0) - (held & 1 ? 1 : 0));
    return s;
}

static void touch(const struct gz_input_event *e)
{
    if (e->type == GZ_EV_TOUCH_DOWN) {
        if (paused || game.over)
            down_row = menu_row_at(e->x, e->y);
        else
            game_drag(&game, e->x - FX, e->y - FY);
    } else if (e->type == GZ_EV_TOUCH_MOVE) {
        if (!paused && !game.over)
            game_drag(&game, e->x - FX, e->y - FY);
    } else if (e->type == GZ_EV_TOUCH_UP) {
        if (paused || game.over) {
            int row = menu_row_at(e->x, e->y);
            if (row >= 0 && row == down_row) {
                selection = row;
                menu_pick(row);
            }
            down_row = -1;
        }
    }
}

int main(void)
{
    ui_init(SB_STATUS_GRAY);
    snprintf(path, sizeof(path), "%s/data", res_bundle());
    fs_mkdirs(path);
    snprintf(path, sizeof(path), "%s/data/state", res_bundle());
    snprintf(temp, sizeof(temp), "%s/data/state.tmp", res_bundle());
    load_art();

    uint8_t bytes[SAVE_BYTES + 1];  /* +1 so an oversized file fails decode */
    int size = fs_read_file(path, bytes, sizeof(bytes));
    if (size > 0 && game_decode(&game, bytes, (unsigned)size))
        paused = true;              /* a restored game waits for Resume */
    else
        restart();
    printf("sky1942: ready score=%u wave=%u paused=%d\n",
           (unsigned)game.score, (unsigned)game.wave, paused);

    uint32_t last = gz_time_ms();
    draw();
    for (;;) {
        struct gz_input_event e;
        bool running = !paused && !game.over;
        /* Wake every 16 ms while the world moves; sleep forever when it does not. */
        int got = ui_next_event(&e, running ? 16 : -1);
        uint32_t now = gz_time_ms(), elapsed = now - last;
        last = now;
        if (got) {
            if (e.type == GZ_EV_APP_DEACTIVATE) {
                /* Home: save now; the app may be ended while suspended. */
                paused = true;
                selection = 0;
                held = 0;
                save();
                continue;
            }
            if (e.type == GZ_EV_APP_ACTIVATE) {
                last = gz_time_ms();
                draw();
                continue;
            }
            if (e.type == GZ_EV_TOUCH_CANCEL)
                down_row = -1;      /* touch ended by the system: do nothing */
            else if (e.type == GZ_EV_KEY_DOWN) {
                if (paused || game.over) {
                    menu_key(e.code);
                } else if (e.code == GZ_KEY_ESC) {
                    paused = true;
                    selection = 0;
                    held = 0;
                    save();
                } else if (e.code == GZ_KEY_UP) {
                    held |= 1;
                } else if (e.code == GZ_KEY_RIGHT) {
                    held |= 2;
                } else if (e.code == GZ_KEY_DOWN) {
                    held |= 4;
                } else if (e.code == GZ_KEY_LEFT) {
                    held |= 8;
                }
            } else if (e.type == GZ_EV_KEY_UP) {
                if (e.code == GZ_KEY_UP) held &= ~1;
                else if (e.code == GZ_KEY_RIGHT) held &= ~2;
                else if (e.code == GZ_KEY_DOWN) held &= ~4;
                else if (e.code == GZ_KEY_LEFT) held &= ~8;
            } else
                touch(&e);
        }
        if (running) {
            uint32_t was_score = game.score;
            uint8_t was_lives = game.lives, was_wave = game.wave, was_over = game.over;
            game_tick(&game, stick(), elapsed);
            if (game.over && !was_over) {
                printf("sky1942: over score=%u\n", (unsigned)game.score);
                save();
            } else if (game.wave != was_wave) {
                printf("sky1942: wave=%u score=%u\n", (unsigned)game.wave, (unsigned)game.score);
                save();
            } else if (game.lives != was_lives) {
                printf("sky1942: hit lives=%u\n", (unsigned)game.lives);
                save();
            } else if (game.score != was_score) {
                printf("sky1942: score=%u\n", (unsigned)game.score);
                save();
            }
        }
        draw();
    }
}
