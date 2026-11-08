/* A solvable sliding puzzle. Saving each move survives the Home button. */
#include <glaze/fs.h>
#include <glaze/sys.h>
#include <glaze/ui.h>
#include <stdio.h>
#include <string.h>
static struct {
    uint8_t tile[16];
    uint32_t moves;
} game;
static char path[FS_PATH_MAX];
static gfx_rect again;
static int empty(void)
{
    for (int i = 0; i < 16; i++)
        if (!game.tile[i])
            return i;
    return 15;
}
static bool solved(void)
{
    for (int i = 0; i < 15; i++)
        if (game.tile[i] != i + 1)
            return false;
    return true;
}
static void save(void)
{
    fs_write_file(path, &game, sizeof(game));
}
static bool move(int n)
{
    int e = empty();
    if (n < 0 || n > 15 || !((n / 4 == e / 4 && (n == e - 1 || n == e + 1)) || n == e - 4 || n == e + 4))
        return false;
    game.tile[e] = game.tile[n];
    game.tile[n] = 0;
    return true;
}
static void shuffle(void)
{
    for (int i = 0; i < 16; i++)
        game.tile[i] = (i + 1) % 16;
    uint32_t rng = gz_time_ms() | 1;
    for (int i = 0; i < 400; i++) {
        rng = rng * 1664525u + 1013904223u;
        move((rng >> 16) % 16);
    }
    if (solved())
        move(14);
    game.moves = 0;
    save();
}
/* Sprites from res/ (art/draw_art.py); without them flat tiles stand in. */
static bool art;
static gfx_image im_tile[4], im_tray, im_linen;
static void load_art(void)
{
    art = res_image(&im_tile[0], "tile0.gzi") && res_image(&im_tile[1], "tile1.gzi")
       && res_image(&im_tile[2], "tile2.gzi") && res_image(&im_tile[3], "tile3.gzi")
       && res_image(&im_tray, "tray.gzi") && res_image(&im_linen, "linen.gzi");
}
static void draw(void)
{
    gfx_surface *s = &ui.surf;
    if (art)
        for (int y = 30; y < UI_H; y += im_linen.h)
            for (int x = 0; x < UI_W; x += im_linen.w)
                gfx_draw_image(s, x, y, &im_linen, 255);
    else
        ui_draw_pinstripes(s, gfx_r(0, 0, UI_W, UI_H));
    ui_draw_navbar(s, "Fifteen", NULL, NULL, 0);
    again = ui_draw_bar_button(s, "New", true, false, false, 0);
    char text[40];
    snprintf(text, sizeof(text), solved() ? "Well done! %u moves" : "Moves: %u", game.moves);
    gfx_text_center(s, 0, 43, UI_W, ui_font(FONT_B14), text, art ? 0x3A2A1C : 0x385487, 255);
    if (art) {
        gfx_round_rect(s, gfx_r(16, 76, 212, 212), 10, 0x000000, 60);   /* the tray's shadow */
        gfx_draw_image(s, 14, 72, &im_tray, 255);
    } else {
        gfx_round_rect(s, gfx_r(14, 72, 212, 212), 9, 0x47546A, 255);
    }
    for (int i = 0; i < 16; i++) {
        int x = 19 + (i % 4) * 51, y = 77 + (i / 4) * 51;
        if (art) {
            if (!game.tile[i])
                continue;
            gfx_draw_image(s, x, y, &im_tile[game.tile[i] % 4], 255);
            snprintf(text, sizeof(text), "%u", game.tile[i]);
            /* engraved: a light lip under dark numerals */
            gfx_text_center(s, x, y + 13, 49, ui_font(FONT_B20), text, 0xFFF0D0, 150);
            gfx_text_center(s, x, y + 12, 49, ui_font(FONT_B20), text,
                            game.tile[i] == i + 1 ? 0x2E4A1E : 0x4A2E18, 255);
            continue;
        }
        if (!game.tile[i]) {
            gfx_round_rect(s, gfx_r(x, y, 49, 49), 5, 0x344154, 255);
            continue;
        }
        gfx_round_rect_vgradient(s, gfx_r(x, y, 49, 48), 5, 0xFFFBEC, 0xC4AE83, 255);
        snprintf(text, sizeof(text), "%u", game.tile[i]);
        gfx_text_center(s, x, y + 12, 49, ui_font(FONT_B20), text, 0x493827, 255);
    }
    ui_invalidate_all();
    ui_present();
}
int main(void)
{
    ui_init(SB_STATUS_GRAY);
    load_art();
    snprintf(path, sizeof(path), "%s/data", res_bundle());
    fs_mkdirs(path);
    snprintf(path, sizeof(path), "%s/data/board", res_bundle());
    unsigned mask = 0;
    bool valid = fs_read_file(path, &game, sizeof(game)) == sizeof(game);
    for (int i = 0; i < 16; i++) {
        if (game.tile[i] > 15 || (mask & (1u << game.tile[i])))
            valid = false;
        else
            mask |= 1u << game.tile[i];
    }
    if (!valid)
        shuffle();
    draw();
    struct gz_input_event e;
    int down = -1;
    for (;;) {
        if (!ui_next_event(&e, -1))
            continue;
        if (e.type == GZ_EV_TOUCH_CANCEL)
            down = -1;                  /* Home mid-touch: no move */
        if (e.type == GZ_EV_TOUCH_DOWN)
            down = (e.x >= 19 && e.x < 221 && e.y >= 77 && e.y < 279)
                       ? ((e.y - 77) / 51) * 4 + (e.x - 19) / 51
                       : -1;
        if (e.type == GZ_EV_TOUCH_UP) {
            if (gfx_rect_contains(again, e.x, e.y))
                shuffle();
            else if (down >= 0 && e.x >= 19 && e.x < 221 && e.y >= 77 && e.y < 279 &&
                     down == ((e.y - 77) / 51) * 4 + (e.x - 19) / 51 && move(down)) {
                game.moves++;
                save();
            }
            down = -1;
            draw();
        }
    }
}
