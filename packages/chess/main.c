/* Chess for Glaze: touch or hardware-key play against the vendored
 * micro-Max engine (engine/ -> ../../third_party/micromax).
 *
 * The rules model (game.c) is the sole authority on the board: every move,
 * human or engine, goes through c_do() first.  The engine board is a
 * follower kept in step upstream's way: each human move is queued with
 * mm_pending(), and the next engine_play() lets mm_search() apply it and
 * then search-and-play the engine's reply on the engine board.  A search
 * that cannot prove the engine board still agrees (or an engine move the
 * model rejects) retires the engine for the rest of the game in favour of
 * c_fallback_move().  mm_apply() immediate-replay is only used to rebuild
 * the engine board from a save's recorded history (engine_sync).
 *
 * The human always plays white.  24 px cells at (24,54); ranks are labelled
 * left of the board, files below it. */
#include "engine.h"
#include "game.h"

#include <glaze/fs.h>
#include <glaze/sys.h>
#include <glaze/ui.h>

#include <stdio.h>
#include <string.h>

static Cg g;
static char path[FS_PATH_MAX], temp[FS_PATH_MAX];
static bool paused, save_failed, engine_ok = true, thinking;
static int confirm = -1; /* -1 none, else the menu row awaiting confirmation */
static int selection, strength;
static int cursor = 0x64, sel = -1; /* keyboard cursor, selected source (0x88) */
static int last_from = -1, last_to = -1;

static const int cell0 = 24, row0 = 54, CELL = 24;
static const int budgets[] = { 3000, 40000, 100000 };
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

static bool my_turn(void) { return g.side == C_WHITE; }

static int king_square(int side)
{
    int s;

    for (s = 0; s < 128; s++)
        if (!(s & 0x88) && (g.sq[s] & 7) == C_K && (g.sq[s] & (C_WHITE | C_BLACK)) == side)
            return s;
    return -1;
}

static int legal_at(int from, int to)
{
    Cmove ms[96];
    int n, i;

    if (g.result != C_ONGOING || !my_turn())
        return 0;
    n = c_gen(ms, &g);
    for (i = 0; i < n; i++)
        if (ms[i].from == (unsigned char)from && ms[i].to == (unsigned char)to)
            return 1;
    return 0;
}

/* first legal move from -> to (engine moves arrive as bare squares; the
 * model only promotes to queen, so the coordinates pin the move down) */
static int pick_move(Cmove *out, int from, int to)
{
    Cmove ms[96];
    int n, i;

    n = c_gen(ms, &g);
    for (i = 0; i < n; i++)
        if (ms[i].from == (unsigned char)from && ms[i].to == (unsigned char)to) {
            *out = ms[i];
            return 1;
        }
    return 0;
}

/* ---------- engine ---------- */

static void engine_new(void)
{
    mm_set_budget(budgets[strength]);
    mm_new_game();
    engine_ok = true;
}

/* rebuild the engine board by replaying the model's accepted history */
static void engine_sync(void)
{
    int i;

    if (g.nplies > C_MAX_PLIES)
        return; /* model hit the length cap: history no longer replayable */
    mm_set_budget(budgets[strength]);
    mm_new_game();
    engine_ok = true;
    for (i = 0; i < g.nplies; i++)
        if (!mm_apply(g.hist[i].from, g.hist[i].to)) {
            engine_ok = false;
            return;
        }
}

static void engine_play(void)
{
    Cmove m;
    int f = 0, t = 0;

    /* mm_search applies the queued human move, then thinks AND plays on the
     * engine board (upstream semantics), so here we only validate its move
     * against the model and mirror it */
    if (engine_ok && mm_search(&f, &t, g.side) && pick_move(&m, f, t) &&
        c_do(&g, &m)) {
        last_from = f;
        last_to = t;
        return;
    }
    /* the engine produced nothing usable (and its board may even have
     * advanced by the human move): retire it and let the model's
     * deterministic picker carry the rest of this game */
    engine_ok = false;
    if (c_fallback_move(&g, &m, 0) && c_do(&g, &m)) {
        last_from = m.from;
        last_to = m.to;
    }
}

/* ---------- save / restore (checksummed record, replayed by c_restore) ----------
 * data/state is one strength byte (0..2) followed by the c_save() record. */

static void save(void)
{
    unsigned char bytes[1 + C_REC_CAP];
    int len = c_save(&g, bytes + 1, (int)sizeof bytes - 1);

    if (!len) {
        save_failed = true;
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
    cursor = 0x64; /* e2 */
    sel = -1;
    last_from = last_to = -1;
    thinking = false;
    paused = false;
    confirm = -1;
    selection = 0;
}

static void restore(void)
{
    unsigned char bytes[1 + C_REC_CAP];
    int len = fs_read_file(path, bytes, sizeof bytes);

    reset_ui_state();
    if (len > 1 && bytes[0] < 3 && c_restore(&g, bytes + 1, len - 1)) {
        strength = bytes[0];
        engine_sync();
        paused = true; /* open paused so the resumed position gets a look */
    } else {
        c_init(&g);
        engine_new();
        save();
    }
}

static void restart(void)
{
    c_init(&g);
    engine_new();
    reset_ui_state();
    save();
}

static void resign(void)
{
    if (g.result != C_ONGOING)
        return;
    g.result = C_BLACK_WIN; /* the human always plays White */
    g.reason = C_R_RESIGN;
    save();
}

/* ---------- menu plumbing (labels and geometry shared by draw and input) ---------- */

static int menu_count(void) { return confirm < 0 ? 4 : 2; }

static int menu_row_y(int i) { return 110 + i * 30; }

static const char *menu_label(int i)
{
    static char b[40];

    if (confirm >= 0)
        return i == 0 ? "Cancel" : (confirm == 1 ? "Start new game" : "Resign game");
    switch (i) {
    case 0:  return "Resume";
    case 1:  return "New Game";
    case 2:  return "Resign";
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
        paused = false;
        break;
    case 1:
        confirm = 1;
        selection = 0;
        break;
    case 2:
        confirm = 2;
        selection = 0;
        break;
    case 3:
        strength = (strength + 1) % 3;
        mm_set_budget(budgets[strength]);
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

/* glyph centred on (cx, cy) */
static void put_char(int cx, int cy, const gfx_font *f, const char *ch, uint32_t rgb)
{
    gfx_text(&ui.surf, cx - gfx_text_width(f, ch) / 2, cy - f->line_height / 2, f, ch,
             rgb, 255);
}

/* 2 px outline just inside a cell (gfx has filled shapes only) */
static void frame(int x, int y, uint32_t rgb)
{
    gfx_surface *s = &ui.surf;

    gfx_fill(s, gfx_r(x, y, CELL, 2), rgb);
    gfx_fill(s, gfx_r(x, y + CELL - 2, CELL, 2), rgb);
    gfx_fill(s, gfx_r(x, y + 2, 2, CELL - 4), rgb);
    gfx_fill(s, gfx_r(x + CELL - 2, y + 2, 2, CELL - 4), rgb);
}

static void draw_piece(int x, int y, int p)
{
    static const char names[] = "?PNBRQK"; /* kind nibble indexes names[] */
    char ch[2] = { names[p & 7], 0 };

    if (p & C_WHITE) {
        gfx_round_rect(&ui.surf, gfx_r(x + 2, y + 2, 20, 20), 10, 0xB9C6DA, 255);
        gfx_round_rect(&ui.surf, gfx_r(x + 3, y + 3, 18, 18), 9, 0xEBF1F9, 255);
        put_char(x + CELL / 2, y + CELL / 2, font(FONT_B17), ch, 0x14202F);
    } else {
        gfx_round_rect(&ui.surf, gfx_r(x + 2, y + 2, 20, 20), 10, 0x8C9EB8, 255);
        gfx_round_rect(&ui.surf, gfx_r(x + 3, y + 3, 18, 18), 9, 0x131D2B, 255);
        put_char(x + CELL / 2, y + CELL / 2, font(FONT_B17), ch, 0xE9EFF8);
    }
}

static const char *result_text(void)
{
    switch (g.reason) {
    case C_R_MATE:     return my_turn() ? "Checkmate - you lose" : "Checkmate - you win!";
    case C_R_STALE:    return "Stalemate - draw";
    case C_R_FIFTY:    return "Draw by 50-move rule";
    case C_R_REP:      return "Draw by repetition";
    case C_R_MATERIAL: return "Draw by material";
    case C_R_RESIGN:   return "You resigned";
    case C_R_LENGTH:   return "Drawn: game too long";
    default:           return "Checkmate";
    }
}

static void draw(void)
{
    gfx_surface *s = &ui.surf;
    int file, rank, check_sq = -1;
    char b[48];

    gfx_fill(s, gfx_r(0, 0, UI_W, UI_H), 0x101B2B);
    ui_draw_navbar(s, "Chess", NULL, NULL, 0);

    text(12, 35, thinking ? "Black thinking..." :
                 (g.result == C_ONGOING ? (my_turn() ? "White to move" : "White waits")
                                        : "Game over"));
    snprintf(b, sizeof b, "%s", engine_ok ? strength_names[strength] : "simple");
    gfx_text_center(s, 128, 35, 100, font(FONT_B14), b, 0xE5ECF5, 255);

    if (g.result == C_ONGOING && c_in_check(&g, g.side))
        check_sq = king_square(g.side);
    gfx_fill(s, gfx_r(cell0 - 1, row0 - 1, 8 * CELL + 2, 8 * CELL + 2), 0x40536D);
    for (rank = 0; rank < 8; rank++)
        for (file = 0; file < 8; file++) {
            int sq = rank * 16 + file, x = cell0 + file * CELL, y = row0 + rank * CELL;

            gfx_fill(s, gfx_r(x, y, CELL, CELL), ((rank + file) & 1) ? 0x243652 : 0x46608A);
            if (sq == last_from || sq == last_to)
                gfx_fill_alpha(s, gfx_r(x, y, CELL, CELL), 0x52749C, 110);
            if (sq == check_sq)
                gfx_fill_alpha(s, gfx_r(x, y, CELL, CELL), 0xC0453C, 120);
            if (sel >= 0 && sq != sel && legal_at(sel, sq))
                gfx_fill_alpha(s, gfx_r(x + 8, y + 8, 8, 8),
                               g.sq[sq] ? 0xD06A5E : 0xCBAA5E, 200);
            if (g.sq[sq])
                draw_piece(x, y, g.sq[sq]);
            if (sq == sel)
                frame(x, y, 0xE8C86A);
            else if (sq == cursor && !paused && g.result == C_ONGOING)
                frame(x, y, 0xDCE7F5);
        }
    for (file = 0; file < 8; file++) {
        char ch[2] = { (char)('a' + file), 0 };

        gfx_text_center(s, cell0 + file * CELL, 250, CELL, font(FONT_B12), ch, 0xA6BAD2, 255);
    }
    for (rank = 0; rank < 8; rank++) {
        char ch[3];

        snprintf(ch, sizeof ch, "%d", 8 - rank);
        gfx_text_center(s, 6, row0 + rank * CELL + 6, 14, font(FONT_B12), ch, 0xA6BAD2, 255);
    }

    if (g.result != C_ONGOING) {
        text_center(266, result_text(), 0xFFCF8A, FONT_B14);
        text_center(284, "ESC for the menu", 0xA6BAD2, FONT_B12);
    } else if (!thinking) {
        snprintf(b, sizeof b, "Move %d", g.nplies / 2 + 1);
        text_center(266, b, 0xA6BAD2, FONT_B12);
        if (!save_failed)
            text_center(284, "Tap or arrows + OK   ESC menu", 0xA6BAD2, FONT_B12);
    }
    if (save_failed)
        text_center(284, "Could not save game", 0xFFCF8A, FONT_B12);

    if (paused) {
        gfx_fill_alpha(s, gfx_r(0, 36, 240, 266), 0x000000, 185);
        gfx_round_rect(s, gfx_r(20, 66, 200, 194), 9, 0x304763, 255);
        gfx_text_center(s, 20, 77, 200, font(FONT_B20), confirm < 0 ? "Paused" : "Sure?",
                        0xFFFFFF, 255);
        for (int i = 0; i < menu_count(); i++) {
            int ry = menu_row_y(i);

            if (i == selection)
                gfx_round_rect(s, gfx_r(33, ry, 174, 26), 4, 0x52749C, 255);
            gfx_text_center(s, 20, ry + 6, 200, font(FONT_B14), menu_label(i), 0xFFFFFF, 255);
        }
        gfx_text_center(s, 20, 246, 200, font(FONT_B12), "Up/Down select   OK choose",
                        0xC8D8EB, 255);
    }
    ui_invalidate_all();
    ui_present();
}

/* ---------- board input ---------- */

static void try_move(int from, int to)
{
    Cmove m;

    if (!pick_move(&m, from, to) || !c_do(&g, &m))
        return;
    last_from = from;
    last_to = to;
    sel = -1;
    cursor = to;
    if (engine_ok && g.result == C_ONGOING)
        mm_pending(from, to); /* applied by the next mm_search() */
    save();
}

static void board_touch(const struct gz_input_event *e)
{
    int file = (e->x - cell0) / CELL, rank = (e->y - row0) / CELL;
    int sq;

    if (file < 0 || file > 7 || rank < 0 || rank > 7)
        return;
    sq = rank * 16 + file;
    cursor = sq;
    if (sel >= 0 && legal_at(sel, sq))
        try_move(sel, sq);
    else if ((g.sq[sq] & C_WHITE) && my_turn())
        sel = sq;
    else
        sel = -1;
}

static void board_key(int code)
{
    int f = cursor & 15, r = cursor >> 4;

    switch (code) {
    case GZ_KEY_LEFT:  if (f > 0) cursor--; break;
    case GZ_KEY_RIGHT: if (f < 7) cursor++; break;
    case GZ_KEY_UP:    if (r > 0) cursor -= 16; break;
    case GZ_KEY_DOWN:  if (r < 7) cursor += 16; break;
    case GZ_KEY_OK:
    case GZ_KEY_ENTER:
        if (sel >= 0 && legal_at(sel, cursor))
            try_move(sel, cursor);
        else if (my_turn() && (g.sq[cursor] & C_WHITE))
            sel = cursor;
        else
            sel = -1;
        break;
    default: break;
    }
}

static bool board_key_relevant(int code)
{
    return code == GZ_KEY_UP || code == GZ_KEY_DOWN || code == GZ_KEY_LEFT ||
           code == GZ_KEY_RIGHT || code == GZ_KEY_OK || code == GZ_KEY_ENTER;
}

/* Black's reply: let the repaint land before the search blocks the loop.
 * Also runs when a save was taken after White's move but before the reply
 * (the app was ended mid-search), so a restored game never stalls. */
static void black_reply(void)
{
    if (paused || g.result != C_ONGOING || my_turn())
        return;
    thinking = true;
    draw();
    engine_play();
    thinking = false;
    save();
}

/* ---------- main ---------- */

int main(void)
{
    ui_init(SB_STATUS_GRAY);
    mm_init_once();
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
                paused = true;
                confirm = -1;
                selection = 0;
                sel = -1;
            }
            save();
            continue;
        }
        if (e.type == GZ_EV_APP_ACTIVATE) {
            paused = true;
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
                        paused = false;
                    }
                }
                else if (e.code == GZ_KEY_UP)
                    selection = (selection + menu_count() - 1) % menu_count();
                else if (e.code == GZ_KEY_DOWN)
                    selection = (selection + 1) % menu_count();
                else if (e.code == GZ_KEY_OK || e.code == GZ_KEY_ENTER)
                    menu_activate();
            }
            black_reply();
            draw();
            continue;
        }
        if (e.type == GZ_EV_KEY_DOWN && e.code == GZ_KEY_ESC) {
            paused = true;
            confirm = -1;
            selection = 0;
            sel = -1;
            save();
            draw();
            continue;
        }
        if (g.result == C_ONGOING && my_turn() &&
            (e.type == GZ_EV_TOUCH_UP ||
             (e.type == GZ_EV_KEY_DOWN && board_key_relevant(e.code)))) {
            if (e.type == GZ_EV_TOUCH_UP)
                board_touch(&e);
            else
                board_key(e.code);
            black_reply();
            draw();
            continue;
        }
        draw();
    }
    return 0;
}
