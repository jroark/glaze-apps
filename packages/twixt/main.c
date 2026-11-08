/* TwixT for Glaze: the human plays Red (top-bottom) against the
 * computer's Black (left-right) on a 12x12 dot grid.
 *
 * All rules live in game.c: every peg, human or computer, goes through
 * tw_play(). A peg automatically builds its knight-move links, so the
 * only choice is where to drop it.
 *
 * Touch: tap the dot nearest your finger. Keys: arrows move the white
 * cursor, OK drops a peg, ESC opens the menu. Dots sit on a 16 px grid
 * at (32,64); home-edge bands are red above/below, dark left/right. */
#include "game.h"

#include <glaze/fs.h>
#include <glaze/ui.h>

#include <stdio.h>

static char path[FS_PATH_MAX], temp[FS_PATH_MAX];
static int paused, save_failed, thinking;
static int confirm = -1; /* -1 none, else the menu row awaiting confirmation */
static int selection, strength = TW_NORMAL;
static int cursor = 5 * TW_DIM + 5, last_pos = -1;

static const int OX = 32, OY = 64, PITCH = 16;
static const char *const strength_names[] = { "Weak", "Normal", "Strong" };

/* ---------- small view helpers ---------- */

static const gfx_font *font(int id) { return ui_font(id); }

static void text(int x, int y, const char *s)
{
    gfx_text(&ui.surf, x, y, font(FONT_B14), s, 0xE5ECF5, 255);
}

static void text_center(int y, const char *s, uint32_t rgb, int fid)
{
    gfx_text_center(&ui.surf, 0, y, UI_W, font(fid), s, rgb, 255);
}

static int dot_x(int pos) { return OX + (pos % TW_DIM) * PITCH; }
static int dot_y(int pos) { return OY + (pos / TW_DIM) * PITCH; }

/* ---------- save / restore ----------
 * data/state is one strength byte followed by the checksummed tw_save()
 * record; restore replays the ply log, so a corrupt file cannot wedge
 * the board into an impossible position. */

static void save(void)
{
    unsigned char bytes[1 + TW_REC_CAP];
    int len = tw_save(bytes + 1, (int)sizeof bytes - 1);

    if (len < 0) {
        save_failed = 1;
        return;
    }
    bytes[0] = (unsigned char)strength;
    len++;
    save_failed = fs_write_file(temp, bytes, (unsigned)len) != len;
    if (!save_failed)
        save_failed = fs_rename(temp, path) < 0;
}

static void reset_ui_state(void)
{
    cursor = 5 * TW_DIM + 5;
    last_pos = -1;
    thinking = 0;
    paused = 0;
    confirm = -1;
    selection = 0;
}

static void restore(void)
{
    unsigned char bytes[1 + TW_REC_CAP];
    int len = fs_read_file(path, bytes, sizeof bytes);

    reset_ui_state();
    if (len > 1 && bytes[0] <= TW_STRONG && tw_restore(bytes + 1, len - 1) == 0) {
        strength = bytes[0];
        paused = 1; /* open paused so the resumed position gets a look */
    } else {
        tw_new(1);
        save();
    }
}

static void restart(void)
{
    tw_new(1);
    reset_ui_state();
    save();
}

static void resign(void)
{
    if (tw_result() != TW_ONGOING)
        return;
    tw_resign();
    save();
}

/* step back one full pair so the human is on turn again */
static void undo_pair(void)
{
    if (tw_nplies() == 0)
        return;
    tw_undo();
    while (tw_nplies() > 0 && tw_to_move() == TW_BLACK)
        tw_undo();
    last_pos = -1;
    save();
}

/* ---------- menu plumbing (labels and geometry shared by draw and input) ---------- */

static int menu_count(void) { return confirm < 0 ? 5 : 2; }

static int menu_row_y(int i) { return 92 + i * 30; }

static const char *menu_label(int i)
{
    static char b[40];

    if (confirm >= 0)
        return i == 0 ? "Cancel" : (confirm == 1 ? "Start new game" : "Resign game");
    switch (i) {
    case 0:  return "Resume";
    case 1:  return "Undo Move";
    case 2:  return "New Game";
    case 3:  return "Resign";
    default: snprintf(b, sizeof b, "Strength: %s", strength_names[strength]); return b;
    }
}

static void menu_activate(void)
{
    if (confirm >= 0) {
        int act = confirm;

        confirm = -1;
        selection = 0;
        if (act == 1) {
            restart();
            return;
        }
        if (act == 2)
            resign();
        return;
    }
    switch (selection) {
    case 0:
        paused = 0;
        break;
    case 1:
        undo_pair();
        break;
    case 2:
        confirm = 1;
        selection = 0;
        break;
    case 3:
        confirm = 2;
        selection = 0;
        break;
    case 4:
        strength = (strength + 1) % 3;
        save();
        break;
    }
}

/* tap inside a menu row selects and activates it */
static int menu_touch(const struct gz_input_event *e)
{
    int i;

    for (i = 0; i < menu_count(); i++)
        if (e->x >= 33 && e->x < 207 && e->y >= menu_row_y(i) &&
            e->y < menu_row_y(i) + 26) {
            selection = i;
            menu_activate();
            return 1;
        }
    return 0;
}

/* ---------- board drawing ---------- */

/* Sprites from res/ (art/draw_art.py); without them flat shapes stand in. */
static bool art;
static gfx_image im_board, im_peg_red, im_peg_black, im_felt;
static void load_art(void)
{
    art = res_image(&im_board, "board.gzi") && res_image(&im_peg_red, "peg_red.gzi")
       && res_image(&im_peg_black, "peg_black.gzi") && res_image(&im_felt, "felt.gzi");
}

/* A link as a shaded rod: dark outline, body, then a thin highlight. */
static void draw_rod_aa(int x0, int y0, int x1, int y1, bool red)
{
    gfx_surface *s = &ui.surf;
    gfx_line_aa(s, x0 * 16 + 16, y0 * 16 + 24, x1 * 16 + 16, y1 * 16 + 24, 72, 0x000000, 70);
    gfx_line_aa(s, x0 * 16, y0 * 16, x1 * 16, y1 * 16, 64, red ? 0x7A1A14 : 0x0C0E14, 255);
    gfx_line_aa(s, x0 * 16, y0 * 16, x1 * 16, y1 * 16, 40, red ? 0xD4483A : 0x3A4050, 255);
    gfx_line_aa(s, x0 * 16 - 8, y0 * 16 - 8, x1 * 16 - 8, y1 * 16 - 8, 14, red ? 0xFFA090 : 0x9AA4BC, 200);
}

static void draw_board_art(void)
{
    gfx_surface *s = &ui.surf;
    int la[TW_MAX_LINKS], lb[TW_MAX_LINKS];
    int bx = OX - 8, by = OY - 8, bw = 12 * PITCH + 16;
    /* home-edge bands: red top/bottom, black left/right */
    gfx_round_rect_vgradient(s, gfx_r(bx + 6, by - 5, bw - 12, 4), 2, 0xF06A5A, 0x9A2E24, 255);
    gfx_round_rect_vgradient(s, gfx_r(bx + 6, by + bw + 1, bw - 12, 4), 2, 0xF06A5A, 0x9A2E24, 255);
    gfx_round_rect(s, gfx_r(bx - 5, by + 6, 4, bw - 12), 2, 0x20242E, 255);
    gfx_round_rect(s, gfx_r(bx + bw + 1, by + 6, 4, bw - 12), 2, 0x20242E, 255);
    gfx_round_rect(s, gfx_r(bx + 2, by + 2, bw, bw), 7, 0x000000, 55);   /* drop shadow */
    gfx_draw_image(s, bx, by, &im_board, 255);
    for (int i = 0; i < tw_get_links(la, lb, 2 * TW_NPOS); i++)
        draw_rod_aa(dot_x(la[i]), dot_y(la[i]), dot_x(lb[i]), dot_y(lb[i]), tw_who_at(la[i]) == TW_RED);
    for (int pos = 0; pos < TW_NPOS; pos++) {
        int x = dot_x(pos), y = dot_y(pos), who = tw_who_at(pos);
        if (pos == last_pos)
            gfx_circle(s, x, y, 8, 0xFFE08A, who == TW_EMPTY ? 120 : 170);
        if (who != TW_EMPTY) {
            const gfx_image *im = who == TW_RED ? &im_peg_red : &im_peg_black;
            gfx_draw_image(s, x - im->w / 2, y - im->h / 2, im, 255);
        }
        if (pos == cursor && !paused && tw_result() == TW_ONGOING) {
            gfx_circle(s, x, y, 8, 0xFFFFFF, 70);
            for (int k = 0; k < 4; k++) {     /* corner brackets */
                int sx = k & 1 ? 1 : -1, sy = k & 2 ? 1 : -1;
                int cx = (x + 7 * sx) * 16, cy = (y + 7 * sy) * 16;
                gfx_line_aa(s, cx, cy, cx - 4 * 16 * sx, cy, 24, 0xFFFFFF, 255);
                gfx_line_aa(s, cx, cy, cx, cy - 4 * 16 * sy, 24, 0xFFFFFF, 255);
            }
        }
    }
}

static void draw_rod(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    gfx_surface *s = &ui.surf;

    /* gfx_line is one pixel wide; three shifted passes make it a rod */
    gfx_line(s, x0 - 1, y0, x1 - 1, y1, rgb, 255);
    gfx_line(s, x0, y0, x1, y1, rgb, 255);
    gfx_line(s, x0 + 1, y0, x1 + 1, y1, rgb, 255);
}

static const char *result_text(void)
{
    switch (tw_result()) {
    case TW_RED_WIN:
        return tw_reason() == TW_R_RESIGN ? "You won - they gave up?"
                                          : "You connected top-bottom!";
    case TW_BLACK_WIN:
        return tw_reason() == TW_R_RESIGN ? "You resigned"
                                          : "Computer connected left-right";
    default:
        return "Draw - no room left";
    }
}

static void draw(void)
{
    gfx_surface *s = &ui.surf;
    int la[TW_MAX_LINKS], lb[TW_MAX_LINKS];
    int i, pos, x, y;

    gfx_fill(s, gfx_r(0, 0, UI_W, UI_H), 0x101B2B);
    if (art)
        for (y = 30; y < UI_H; y += im_felt.h)
            for (x = 0; x < UI_W; x += im_felt.w)
                gfx_draw_image(s, x, y, &im_felt, 255);
    ui_draw_navbar(s, "TwixT", NULL, NULL, 0);

    text(12, 35, thinking ? "Computer thinking..."
                          : (tw_result() == TW_ONGOING
                                 ? (tw_to_move() == TW_RED ? "You are Red, tap a dot"
                                                           : "Computer's turn")
                                 : "Game over"));

    if (art) {
        draw_board_art();
    } else {
        /* home-edge bands: red top/bottom, dark left/right */
        gfx_fill(s, gfx_r(OX - 8, OY - 13, 12 * PITCH + 16, 5), 0xB84A40);
        gfx_fill(s, gfx_r(OX - 8, OY + 11 * PITCH + 8, 12 * PITCH + 16, 5), 0xB84A40);
        gfx_fill(s, gfx_r(OX - 13, OY - 8, 5, 12 * PITCH + 16), 0x22324A);
        gfx_fill(s, gfx_r(OX + 11 * PITCH + 8, OY - 8, 5, 12 * PITCH + 16), 0x22324A);

        /* board panel with the four dead corner holes notched out */
        gfx_fill(s, gfx_r(OX - 8, OY - 8, 12 * PITCH + 16, 12 * PITCH + 16), 0x3A5170);
        gfx_fill(s, gfx_r(OX - 8, OY - 8, 16, 16), 0x101B2B);
        gfx_fill(s, gfx_r(OX + 11 * PITCH, OY - 8, 16, 16), 0x101B2B);
        gfx_fill(s, gfx_r(OX - 8, OY + 11 * PITCH, 16, 16), 0x101B2B);
        gfx_fill(s, gfx_r(OX + 11 * PITCH, OY + 11 * PITCH, 16, 16), 0x101B2B);

        /* rods first, pegs sit on top of their ends */
        for (i = 0; i < tw_get_links(la, lb, 2 * TW_NPOS); i++) {
            int color = tw_who_at(la[i]);
            draw_rod(dot_x(la[i]), dot_y(la[i]), dot_x(lb[i]), dot_y(lb[i]),
                     color == TW_RED ? 0xD25446 : 0x93A6C0);
        }

        for (pos = 0; pos < TW_NPOS; pos++) {
            x = dot_x(pos);
            y = dot_y(pos);
            int who_at = tw_who_at(pos);
            int r = pos / TW_DIM, c = pos % TW_DIM;

            if ((r == 0 || r == TW_DIM - 1) && (c == 0 || c == TW_DIM - 1))
                continue; /* corner holes stay blank notches */
            if (who_at == TW_EMPTY) {
                gfx_circle(s, x, y, 2, 0x9DB4D2, 255);
                if (pos == last_pos)
                    gfx_circle(s, x, y, 5, 0xE8C86A, 255);
            } else if (who_at == TW_RED) {
                if (pos == last_pos)
                    gfx_circle(s, x, y, 6, 0xE8C86A, 255);
                gfx_circle(s, x, y, 5, 0xC8503F, 255);
                gfx_circle(s, x, y, 2, 0xEF9A8B, 255);
            } else {
                if (pos == last_pos)
                    gfx_circle(s, x, y, 6, 0xE8C86A, 255);
                gfx_circle(s, x, y, 5, 0x93A6C0, 255);
                gfx_circle(s, x, y, 4, 0x18222F, 255);
            }
            if (pos == cursor && !paused && tw_result() == TW_ONGOING) {
                gfx_fill(s, gfx_r(x - 7, y - 7, 14, 2), 0xDCE7F5);
                gfx_fill(s, gfx_r(x - 7, y + 5, 14, 2), 0xDCE7F5);
                gfx_fill(s, gfx_r(x - 7, y - 5, 2, 10), 0xDCE7F5);
                gfx_fill(s, gfx_r(x + 5, y - 5, 2, 10), 0xDCE7F5);
            }
        }
    }

    if (tw_result() != TW_ONGOING) {
        text_center(266, result_text(), 0xFFCF8A, FONT_B14);
        text_center(284, "ESC for the menu", 0xA6BAD2, FONT_B12);
    } else if (!thinking) {
        char b[24];

        snprintf(b, sizeof b, "Move %d   %s", tw_nplies() / 2 + 1,
                 strength_names[strength]);
        text_center(266, b, 0xA6BAD2, FONT_B12);
        if (!save_failed)
            text_center(284, "Tap or arrows + OK   ESC menu", 0xA6BAD2, FONT_B12);
    }
    if (save_failed)
        text_center(284, "Could not save game", 0xFFCF8A, FONT_B12);

    if (paused) {
        gfx_fill_alpha(s, gfx_r(0, 36, 240, 266), 0x000000, 185);
        gfx_round_rect(s, gfx_r(20, 56, 200, 210), 9, 0x304763, 255);
        gfx_text_center(s, 20, 67, 200, font(FONT_B20),
                        confirm < 0 ? "Paused" : "Sure?", 0xFFFFFF, 255);
        for (i = 0; i < menu_count(); i++) {
            int ry = menu_row_y(i);

            if (i == selection)
                gfx_round_rect(s, gfx_r(33, ry, 174, 26), 4, 0x52749C, 255);
            gfx_text_center(s, 20, ry + 6, 200, font(FONT_B14), menu_label(i),
                            0xFFFFFF, 255);
        }
        gfx_text_center(s, 20, 248, 200, font(FONT_B12),
                        "Up/Down select   OK choose", 0xC8D8EB, 255);
    }
    ui_invalidate_all();
    ui_present();
}

/* ---------- board input ---------- */

static void try_peg(int pos)
{
    if (tw_play(pos) < 0)
        return;
    last_pos = pos;
    cursor = pos;
    save();
}

/* tap: play the dot nearest the finger, if close enough */
static void board_touch(const struct gz_input_event *e)
{
    int c = (e->x - OX + PITCH / 2) / PITCH;
    int r = (e->y - OY + PITCH / 2) / PITCH;
    int pos;

    if (c < 0 || c >= TW_DIM || r < 0 || r >= TW_DIM)
        return;
    pos = r * TW_DIM + c;
    cursor = pos;
    if (e->x - dot_x(pos) > 9 || dot_x(pos) - e->x > 9 ||
        e->y - dot_y(pos) > 9 || dot_y(pos) - e->y > 9)
        return; /* tapped closer to a panel edge than to a dot */
    try_peg(pos);
}

static void board_key(int code)
{
    int f = cursor % TW_DIM, r = cursor / TW_DIM;

    switch (code) {
    case GZ_KEY_LEFT:  if (f > 0) cursor--; break;
    case GZ_KEY_RIGHT: if (f < TW_DIM - 1) cursor++; break;
    case GZ_KEY_UP:    if (r > 0) cursor -= TW_DIM; break;
    case GZ_KEY_DOWN:  if (r < TW_DIM - 1) cursor += TW_DIM; break;
    case GZ_KEY_OK:
    case GZ_KEY_ENTER: try_peg(cursor); break;
    default: break;
    }
}

static int board_key_relevant(int code)
{
    return code == GZ_KEY_UP || code == GZ_KEY_DOWN || code == GZ_KEY_LEFT ||
           code == GZ_KEY_RIGHT || code == GZ_KEY_OK || code == GZ_KEY_ENTER;
}

/* Black's reply: let the repaint land before the search blocks the loop.
 * Also runs when the save was taken after Red's move but before the
 * reply (the app was ended mid-reply), so a restored game never stalls. */
static void computer_move(void)
{
    int pos;

    if (paused || tw_result() != TW_ONGOING || tw_to_move() != TW_BLACK)
        return;
    thinking = 1;
    draw();
    if (tw_ai(strength, &pos) >= 0 && tw_play(pos) >= 0)
        last_pos = pos;
    thinking = 0;
    save();
}

/* ---------- main ---------- */

int main(void)
{
    ui_init(SB_STATUS_GRAY);
    load_art();
    snprintf(path, sizeof path, "%s/data", res_bundle());
    fs_mkdirs(path);
    snprintf(path, sizeof path, "%s/data/state", res_bundle());
    snprintf(temp, sizeof temp, "%s/data/state.tmp", res_bundle());
    restore();
    draw();

    for (;;) {
        struct gz_input_event e;
        int got = ui_next_event(&e, -1);

        if (!got)
            continue;
        if (e.type == GZ_EV_APP_DEACTIVATE) {
            if (!paused) {
                paused = 1;
                confirm = -1;
                selection = 0;
            }
            save();
            continue;
        }
        if (e.type == GZ_EV_APP_ACTIVATE) {
            paused = 1;
            confirm = -1;
            selection = 0;
            draw();
            continue;
        }
        if (paused) {
            if (e.type == GZ_EV_TOUCH_UP)
                menu_touch(&e);
            else if (e.type == GZ_EV_KEY_DOWN) {
                if (e.code == GZ_KEY_ESC) {
                    if (confirm >= 0) { /* back out of the confirmation */
                        confirm = -1;
                        selection = 0;
                    } else {
                        paused = 0;
                    }
                } else if (e.code == GZ_KEY_UP)
                    selection = (selection + menu_count() - 1) % menu_count();
                else if (e.code == GZ_KEY_DOWN)
                    selection = (selection + 1) % menu_count();
                else if (e.code == GZ_KEY_OK || e.code == GZ_KEY_ENTER)
                    menu_activate();
            }
            computer_move();
            draw();
            continue;
        }
        if (e.type == GZ_EV_KEY_DOWN && e.code == GZ_KEY_ESC) {
            paused = 1;
            confirm = -1;
            selection = 0;
            save();
            draw();
            continue;
        }
        if (tw_result() == TW_ONGOING && tw_to_move() == TW_RED &&
            (e.type == GZ_EV_TOUCH_UP ||
             (e.type == GZ_EV_KEY_DOWN && board_key_relevant(e.code)))) {
            if (e.type == GZ_EV_TOUCH_UP)
                board_touch(&e);
            else
                board_key(e.code);
            computer_move();
            draw();
            continue;
        }
        draw();
    }
    return 0;
}
