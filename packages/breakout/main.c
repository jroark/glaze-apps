/* Breakout: the Glaze side. Rules live in game.c; this file draws, reads
 * input, saves, and follows the app lifecycle. */
#include "game.h"
#include <glaze/fs.h>
#include <glaze/sys.h>
#include <glaze/ui.h>
#include <stdio.h>
#include <string.h>

static Game game;
static char path[FS_PATH_MAX], temp[FS_PATH_MAX];
static bool paused, save_failed;
static int selection;  /* pause menu row */
static int held;       /* -1 left, +1 right, 0 none: keys do not repeat */
static bool dragging;   /* a finger is steering the paddle */
static int pressed = -1; /* pause-menu row held down by a finger, or -1 */

/* Write to a temp file, then rename, so a crash never leaves half a save. */
static void save(void)
{
    uint8_t bytes[SAVE_BYTES];
    game_encode(&game, bytes);
    save_failed = fs_write_file(temp, bytes, sizeof(bytes)) != (int)sizeof(bytes);
    if (!save_failed)
        save_failed = fs_rename(temp, path) < 0;
    printf("breakout: saved score=%u ok=%d\n", (unsigned)game.score, !save_failed);
}

static void draw(void)
{
    gfx_surface *s = &ui.surf;
    char b[40];
    gfx_fill(s, gfx_r(0, 0, UI_W, UI_H), 0x0B1220);
    ui_draw_navbar(s, "Breakout", NULL, NULL, 0);

    /* the walls the ball bounces off, then the field contents; the HUD row
     * (y 33..47) sits clear above FIELD_T, and is drawn last anyway */
    gfx_fill(s, gfx_r(FIELD_L - 3, FIELD_T - 3, 3, FIELD_BOTTOM - FIELD_T + 6), 0x1E3050);
    gfx_fill(s, gfx_r(FIELD_R, FIELD_T - 3, 3, FIELD_BOTTOM - FIELD_T + 6), 0x1E3050);
    gfx_fill(s, gfx_r(FIELD_L - 3, FIELD_T - 3, FIELD_R - FIELD_L + 6, 3), 0x1E3050);

    /* bricks */
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++) {
            uint8_t hp = game.bricks[r][c];
            if (!hp)
                continue;
            int x, y;
            game_brick_rect(r, c, &x, &y);
            uint32_t col = hp == STEEL ? 0x6E7A8C : (hp > 1 ? 0xD08341 : 0x4C8FD6);
            gfx_fill(s, gfx_r(x, y, BRICK_W, BRICK_H), col);
        }

    /* paddle and ball */
    int px = SUB_PX(game.paddle_x);
    gfx_round_rect(s, gfx_r(px - PADDLE_W / 2, PADDLE_Y, PADDLE_W, PADDLE_H), 3, 0xE8EEF7, 255);
    gfx_circle(s, SUB_PX(game.ball_x), SUB_PX(game.ball_y), BALL_HALF, 0xF2F5FA, 255);

    if (!game.launch && !game.over && !paused)
        gfx_text_center(s, 0, 252, UI_W, ui_font(FONT_R11), "OK or tap to launch", 0x8FA6C4, 255);
    gfx_text_center(s, 0, 294, UI_W, ui_font(FONT_R10), "Drag, or hold Left/Right", 0x62789C, 255);

    /* status row: score, level, balls left */
    snprintf(b, sizeof(b), "Score %u", (unsigned)game.score);
    gfx_text(s, 12, 33, ui_font(FONT_B12), b, 0xE5ECF5, 255);
    snprintf(b, sizeof(b), "Level %u", game.level);
    gfx_text_center(s, 80, 33, 80, ui_font(FONT_B12), b, 0xE5ECF5, 255);
    for (int i = 0; i < MAX_LIVES; i++)
        gfx_circle(s, 200 + i * 12, 38, 4, i < game.lives ? 0xF2C94C : 0x27384F, 255);

    if (paused || game.over) {
        gfx_fill_alpha(s, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H), 0x000000, 185);
        gfx_round_rect(s, gfx_r(20, 100, 200, 110), 9, 0x304763, 255);
        gfx_text_center(s, 20, 108, 200, ui_font(FONT_B20), game.over ? "Game over" : "Paused", 0xFFFFFF, 255);
        snprintf(b, sizeof(b), "Score %u   Best %u", (unsigned)game.score, (unsigned)game.best);
        gfx_text_center(s, 20, 130, 200, ui_font(FONT_B12), b, 0xBBD0EA, 255);
        const char *items[2] = {game.over ? "New Game" : "Resume", "New Game"};
        int count = game.over ? 1 : 2;
        int sel = pressed >= 0 ? pressed : selection;
        for (int i = 0; i < count; i++) {
            if (i == sel)
                gfx_round_rect(s, gfx_r(33, 156 + i * 30, 174, 26), 4, 0x52749C, 255);
            gfx_text_center(s, 20, 162 + i * 30, 200, ui_font(FONT_B14), items[i], 0xFFFFFF, 255);
        }
    }
    if (save_failed)
        gfx_text_center(s, 0, 272, UI_W, ui_font(FONT_B12), "Could not save game", 0xFFCF8A, 255);
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

/* Which pause-menu row is at (x, y)? -1 for none. Must match draw(). */
static int menu_row_at(int x, int y)
{
    int count = game.over ? 1 : 2;
    if (x < 33 || x >= 33 + 174)
        return -1;
    for (int i = 0; i < count; i++)
        if (y >= 156 + i * 30 && y < 156 + i * 30 + 26)
            return i;
    return -1;
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

static void menu_touch(const struct gz_input_event *e)
{
    if (e->type == GZ_EV_TOUCH_DOWN) {
        pressed = menu_row_at(e->x, e->y);
        if (pressed >= 0)
            selection = pressed;
    } else if (e->type == GZ_EV_TOUCH_UP) {
        int row = menu_row_at(e->x, e->y);
        if (pressed >= 0 && row == pressed) {
            if (game.over || pressed == 1)
                restart();
            else
                paused = false;
        }
        pressed = -1;
    } else if (e->type == GZ_EV_TOUCH_CANCEL) {
        pressed = -1;
    }
}

static void play_key(int code)
{
    switch (code) {
    case GZ_KEY_LEFT:
    case GZ_KEY_RIGHT:
        held = code == GZ_KEY_LEFT ? -1 : 1;
        game_paddle_input(&game, held);
        break;
    case GZ_KEY_OK:
    case GZ_KEY_ENTER:
        if (!game.launch) {
            game_launch(&game);
            printf("breakout: launch level=%u lives=%u\n", game.level, game.lives);
        }
        break;
    case GZ_KEY_ESC:
        paused = true;
        selection = 0;
        held = 0;
        game_paddle_input(&game, 0);
        save();
        break;
    }
}

static void key_up(int code)
{
    if (code == GZ_KEY_LEFT && held < 0)
        held = 0;
    if (code == GZ_KEY_RIGHT && held > 0)
        held = 0;
    game_paddle_input(&game, held);
}

static void touch(const struct gz_input_event *e)
{
    if (e->y < UI_NAV_H)
        return; /* the nav bar does nothing here */
    if (e->type == GZ_EV_TOUCH_DOWN) {
        dragging = true;
        game_set_paddle(&game, e->x);
        if (!game.launch) {
            game_launch(&game);
            printf("breakout: launch level=%u lives=%u\n", game.level, game.lives);
        }
    } else if (e->type == GZ_EV_TOUCH_MOVE && dragging) {
        game_set_paddle(&game, e->x);
    } else if (e->type == GZ_EV_TOUCH_UP || e->type == GZ_EV_TOUCH_CANCEL) {
        dragging = false;
    }
}

/* Report what a step did, and save at the moments worth keeping. */
static void report(int ev)
{
    if (ev & STEP_LIFE) {
        printf("breakout: ball lost lives=%u\n", game.lives);
        save();
    }
    if (ev & STEP_LEVEL) {
        printf("breakout: level cleared level=%u score=%u\n", game.level, (unsigned)game.score);
        save();
    }
    if (ev & STEP_STALL)
        printf("breakout: ball re-aimed\n");
    if (ev & STEP_OVER) {
        printf("breakout: game over score=%u best=%u\n", (unsigned)game.score, (unsigned)game.best);
        save();
    }
}

int main(void)
{
    ui_init(SB_STATUS_GRAY);
    snprintf(path, sizeof(path), "%s/data", res_bundle());
    fs_mkdirs(path);
    snprintf(path, sizeof(path), "%s/data/state", res_bundle());
    snprintf(temp, sizeof(temp), "%s/data/state.tmp", res_bundle());

    uint8_t bytes[SAVE_BYTES + 1]; /* +1 so an oversized file fails decode */
    int size = fs_read_file(path, bytes, sizeof(bytes));
    if (size > 0 && game_decode(&game, bytes, (unsigned)size))
        paused = true; /* a restored game waits for Resume */
    else
        restart();
    printf("breakout: ready score=%u level=%u lives=%u paused=%d\n", (unsigned)game.score, game.level,
           game.lives, paused);

    uint32_t last = gz_time_ms(), acc = 0;
    draw();
    for (;;) {
        struct gz_input_event e;
        bool running = !paused && !game.over;
        /* The ball moves continuously, so wake every STEP_MS while playing. */
        int got = ui_next_event(&e, running ? (int)STEP_MS : -1);
        uint32_t now = gz_time_ms(), elapsed = now - last;
        last = now;
        if (got) {
            if (e.type == GZ_EV_APP_DEACTIVATE) {
                /* Home: save now; the app may be ended while suspended. */
                paused = true;
                selection = 0;
                held = 0;
                dragging = false;
                game_paddle_input(&game, 0);
                save();
                continue;
            }
            if (e.type == GZ_EV_APP_ACTIVATE) {
                last = gz_time_ms();
                acc = 0;
                draw();
                continue;
            }
            if (e.type == GZ_EV_KEY_DOWN) {
                if (paused || game.over)
                    menu_key(e.code);
                else
                    play_key(e.code);
            } else if (e.type == GZ_EV_KEY_UP) {
                key_up(e.code);
            } else if (paused || game.over) {
                menu_touch(&e);
            } else {
                touch(&e);
            }
        }
        if (running) {
            if (elapsed > 100)
                elapsed = 100; /* a stall must not teleport the ball */
            acc += elapsed;
            while (acc >= STEP_MS) {
                acc -= STEP_MS;
                report(game_step(&game));
            }
        }
        draw();
    }
}
