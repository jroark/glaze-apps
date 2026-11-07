/* __NAME__: the Glaze side. Rules live in game.c; this file draws, reads
 * input, saves, and follows the app lifecycle. */
#include "game.h"
#include <glaze/fs.h>
#include <glaze/sys.h>
#include <glaze/ui.h>
#include <stdio.h>
#include <string.h>

/* Board: 22 px cells at (10,60). The app surface is UI_W x UI_H (240x304). */
#define CELL 22
#define BX 10
#define BY 60

static Game game;
static char path[FS_PATH_MAX], temp[FS_PATH_MAX];
static bool paused, save_failed;
static int selection;           /* pause menu row */
static int down_cell = -1;      /* cell under a touch, or -1 */

/* Write to a temp file, then rename, so a crash never leaves half a save. */
static void save(void)
{
    uint8_t bytes[SAVE_BYTES];
    game_encode(&game, bytes);
    save_failed = fs_write_file(temp, bytes, sizeof(bytes)) != (int)sizeof(bytes);
    if (!save_failed)
        save_failed = fs_rename(temp, path) < 0;
    printf("__ID__: saved score=%u ok=%d\n", (unsigned)game.score, !save_failed);
}

static int cell_at(int x, int y)
{
    if (x < BX || y < BY || x >= BX + GRID_W * CELL || y >= BY + GRID_H * CELL)
        return -1;
    return ((y - BY) / CELL) * GRID_W + (x - BX) / CELL;
}

static void draw(void)
{
    gfx_surface *s = &ui.surf;
    char b[40];
    gfx_fill(s, gfx_r(0, 0, UI_W, UI_H), 0x101B2B);
    ui_draw_navbar(s, "__NAME__", NULL, NULL, 0);
    snprintf(b, sizeof(b), "Score %u", (unsigned)game.score);
    gfx_text(s, 12, 36, ui_font(FONT_B14), b, 0xE5ECF5, 255);
    snprintf(b, sizeof(b), "Best %u", (unsigned)game.best);
    gfx_text_center(s, 80, 36, 80, ui_font(FONT_B14), b, 0xE5ECF5, 255);
    snprintf(b, sizeof(b), "%us", (unsigned)((game.ms_left + 999) / 1000));
    gfx_text(s, 196, 36, ui_font(FONT_B14), b, 0xE5ECF5, 255);

    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            gfx_fill(s, gfx_r(BX + x * CELL + 1, BY + y * CELL + 1, CELL - 2, CELL - 2), 0x223450);
    gfx_circle(s, BX + game.star_x * CELL + CELL / 2, BY + game.star_y * CELL + CELL / 2, 7, 0xF2C94C, 255);
    gfx_round_rect(s, gfx_r(BX + game.x * CELL + 3, BY + game.y * CELL + 3, CELL - 6, CELL - 6), 4, 0x57B45E, 255);

    gfx_text_center(s, 0, 286, UI_W, ui_font(FONT_R11), "Arrows move   OK collects   ESC pauses", 0xA6BAD2, 255);
    if (paused || game.over) {
        gfx_fill_alpha(s, gfx_r(0, 30, UI_W, UI_H - 30), 0x000000, 185);
        gfx_round_rect(s, gfx_r(20, 100, 200, 110), 9, 0x304763, 255);
        gfx_text_center(s, 20, 110, 200, ui_font(FONT_B20), game.over ? "Time up" : "Paused", 0xFFFFFF, 255);
        const char *items[2] = {game.over ? "New Game" : "Resume", "New Game"};
        int count = game.over ? 1 : 2;
        for (int i = 0; i < count; i++) {
            if (i == selection)
                gfx_round_rect(s, gfx_r(33, 142 + i * 30, 174, 26), 4, 0x52749C, 255);
            gfx_text_center(s, 20, 148 + i * 30, 200, ui_font(FONT_B14), items[i], 0xFFFFFF, 255);
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
    save();
}

/* Pause/game-over menu keys. */
static void menu_key(int code)
{
    int count = game.over ? 1 : 2;
    if (code == GZ_KEY_UP)
        selection = (selection + count - 1) % count;
    else if (code == GZ_KEY_DOWN)
        selection = (selection + 1) % count;
    else if (code == GZ_KEY_OK || code == GZ_KEY_ENTER) {
        if (game.over || selection == 1)
            restart();
        else
            paused = false;
    } else if (code == GZ_KEY_ESC && !game.over)
        paused = false;
}

static void play_key(int code)
{
    switch (code) {
    case GZ_KEY_UP: game_move(&game, DIR_UP); break;
    case GZ_KEY_RIGHT: game_move(&game, DIR_RIGHT); break;
    case GZ_KEY_DOWN: game_move(&game, DIR_DOWN); break;
    case GZ_KEY_LEFT: game_move(&game, DIR_LEFT); break;
    case GZ_KEY_OK:
    case GZ_KEY_ENTER:
        if (game_collect(&game)) {
            printf("__ID__: collect score=%u\n", (unsigned)game.score);
            save();
        }
        break;
    case GZ_KEY_ESC: paused = true; selection = 0; save(); break;
    }
}

static void touch(const struct gz_input_event *e)
{
    if (e->type == GZ_EV_TOUCH_DOWN) {
        down_cell = cell_at(e->x, e->y);
    } else if (e->type == GZ_EV_TOUCH_UP) {
        int c = cell_at(e->x, e->y);
        if (c >= 0 && c == down_cell && !paused && !game.over) {
            game.x = (uint8_t)(c % GRID_W);
            game.y = (uint8_t)(c / GRID_W);
            if (game_collect(&game)) {
                printf("__ID__: collect score=%u\n", (unsigned)game.score);
                save();
            }
        }
        down_cell = -1;
    }
}

int main(void)
{
    ui_init(SB_STATUS_GRAY);
    snprintf(path, sizeof(path), "%s/data", res_bundle());
    fs_mkdirs(path);
    snprintf(path, sizeof(path), "%s/data/state", res_bundle());
    snprintf(temp, sizeof(temp), "%s/data/state.tmp", res_bundle());

    uint8_t bytes[SAVE_BYTES + 1];  /* +1 so an oversized file fails decode */
    int size = fs_read_file(path, bytes, sizeof(bytes));
    if (size > 0 && game_decode(&game, bytes, (unsigned)size))
        paused = true;              /* a restored game waits for Resume */
    else
        restart();
    printf("__ID__: ready score=%u paused=%d\n", (unsigned)game.score, paused);

    uint32_t last = gz_time_ms();
    draw();
    for (;;) {
        struct gz_input_event e;
        bool running = !paused && !game.over;
        /* Wait forever when idle; wake every 100 ms while the clock runs. */
        int got = ui_next_event(&e, running ? 100 : -1);
        uint32_t now = gz_time_ms(), elapsed = now - last;
        last = now;
        if (got) {
            if (e.type == GZ_EV_APP_DEACTIVATE) {
                /* Home: save now; the app may be ended while suspended. */
                paused = true;
                selection = 0;
                save();
                continue;
            }
            if (e.type == GZ_EV_APP_ACTIVATE) {
                last = gz_time_ms();
                draw();
                continue;
            }
            if (e.type == GZ_EV_TOUCH_CANCEL)
                down_cell = -1;     /* touch ended by the system: do nothing */
            else if (e.type == GZ_EV_KEY_DOWN) {
                if (paused || game.over)
                    menu_key(e.code);
                else
                    play_key(e.code);
            } else
                touch(&e);
        }
        if (running && !paused) {
            uint8_t was_over = game.over;
            game_tick(&game, elapsed);
            if (game.over && !was_over) {
                printf("__ID__: over score=%u\n", (unsigned)game.score);
                save();
            }
        }
        draw();
    }
}
