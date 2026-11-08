/* Pac-Man: the Glaze side. Rules live in game.c; this file draws (with the
 * mouth/skirt animations driven by game.step_cnt), reads input, plays
 * square-wave chiptune sounds through the audio server, saves, and follows
 * the app lifecycle. */
#include "game.h"
#include <glaze/audio.h>
#include <glaze/fs.h>
#include <glaze/sys.h>
#include <glaze/ui.h>
#include <stdio.h>
#include <string.h>

/* Maze: 15x17 tiles of TILE_PX px at (BX,BY). Surface is 240x304. */
#define BX 15
#define BY 44
#define MW (MAZE_W * TILE_PX)
#define MH (MAZE_H * TILE_PX)
static const int UDX[4] = {0, 1, 0, -1}, UDY[4] = {-1, 0, 1, 0}; /* unit dirs */

static Game game;
static char path[FS_PATH_MAX], temp[FS_PATH_MAX];
static bool paused, save_failed;
static int selection;        /* pause/over menu row */
static int tdx = -1, tdy;    /* swipe origin, -1 = no swipe */

/* ---- sound: square-wave notes streamed to the audio server ------------- */

#define SR 11025u /* sample rate */
#define NOTE_N 48
static struct { uint16_t hz, ms, amp; } notes[NOTE_N];
static int qn, qe;                    /* queue head / tail */
static uint32_t g_hz, g_amp, g_left, g_phase; /* note being generated */
static bool sound_off, audio_on;      /* server unusable / stream open */

static void sfx(uint16_t hz, uint16_t ms, uint16_t amp)
{
    int next = (qe + 1) % NOTE_N;
    if (sound_off || next == qn)
        return; /* queue full: drop new sounds, never stall the game */
    notes[qe].hz = hz;
    notes[qe].ms = ms;
    notes[qe].amp = amp;
    qe = next;
}

/* A ramp of n short notes from hz a to hz b. */
static void sfx_ramp(uint16_t a, uint16_t b, int n, uint16_t ms, uint16_t amp)
{
    for (int i = 0; i < n; i++)
        sfx((uint16_t)(a + (int32_t)(b - a) * i / (n - 1)), ms, amp);
}

static void sound_reset(void)
{
    qn = qe = 0;
    g_left = 0;
    g_phase = 0;
}

static bool primed; /* the server starts output once ~16 KiB is queued */

static void sound_close(void)
{
    if (audio_on)
        audio_stream_close();
    audio_on = false;
    primed = false;
    sound_reset();
}

static void sound_ensure(void)
{
    if (sound_off || audio_on)
        return;
    if (audio_stream_open(SR, 1) >= 0)
        audio_on = true;
    else
        sound_off = true;
}

/* Generate n samples of the queued notes (silence when the queue is dry). */
static void gen_samples(int16_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        while (!g_left) {
            if (qn == qe)
                break;
            g_hz = notes[qn].hz;
            g_amp = notes[qn].amp;
            g_left = (uint32_t)notes[qn].ms * SR / 1000u;
            qn = (qn + 1) % NOTE_N;
            g_phase = 0;
        }
        if (!g_left) {
            out[i] = 0;
            continue;
        }
        g_phase += g_hz;
        if (g_phase >= SR)
            g_phase -= SR;
        out[i] = g_phase < SR / 2 ? (int16_t)g_amp : (int16_t)(-(int16_t)g_amp);
    }
}

static void sound_pump(void)
{
    static int16_t pcm[1024]; /* 2048 bytes = 92 ms at 11025 Hz */
    uint32_t played, queued;
    if (!audio_on || paused)
        return;
    /* Burst until the server's PRIME_BYTES (~740 ms) are queued, then just
     * top up so the DMA never runs dry. */
    for (int i = 0; i < (primed ? 1 : 10); i++) {
        if (audio_stream_clock(&played, &queued) < 0)
            return;
        if (queued > (primed ? 300u : 800u)) {
            primed = true;
            return;
        }
        gen_samples(pcm, (int)(sizeof(pcm) / sizeof(pcm[0])));
        if (audio_stream_write(pcm, sizeof(pcm)) < 0) {
            audio_on = false;
            sound_off = true;
            printf("pacman: sound off\n");
            return;
        }
        if (queued + 92 >= 740u)
            primed = true;
    }
}

/* Cues for game events (original chiptune blips). */
static void cue_pellet(int n)
{
    sfx(n & 1 ? 360 : 520, 30, 3600);
}
static void cue_power(void)
{
    sfx_ramp(180, 760, 8, 30, 7000);
}
static void cue_ghost(void)
{
    sfx_ramp(300, 1300, 6, 40, 8000);
}
static void cue_fruit(void)
{
    sfx(1000, 60, 7000);
    sfx(1400, 70, 7000);
}
static void cue_dying(void)
{
    sfx_ramp(900, 150, 11, 60, 8000);
}
static void cue_win(void)
{
    static const uint16_t f[] = {523, 659, 784, 1047, 784, 1047};
    for (unsigned i = 0; i < sizeof(f) / sizeof(f[0]); i++)
        sfx(f[i], i == 5 ? 380 : 110, 7000);
}
static void cue_over(void)
{
    sfx_ramp(520, 80, 12, 80, 8000);
}
static void cue_start(void)
{
    static const uint16_t f[] = {659, 784, 1047, 1319};
    for (unsigned i = 0; i < sizeof(f) / sizeof(f[0]); i++)
        sfx(f[i], 90, 6000);
}

/* Write to a temp file, then rename, so a crash never leaves half a save. */
static void save(void)
{
    uint8_t bytes[SAVE_BYTES];
    game_encode(&game, bytes);
    save_failed = fs_write_file(temp, bytes, sizeof(bytes)) != (int)sizeof(bytes);
    if (!save_failed)
        save_failed = fs_rename(temp, path) < 0;
    printf("pacman: saved score=%u ok=%d\n", (unsigned)game.score, !save_failed);
}

/* Wall look-up for drawing (mirrors game.c's compiled-in map). */
static char mchar(int x, int y)
{
    static const char *rows[MAZE_H] = {
        "###############", "#o...........o#", "#.#.##.#.##.#.#", "#.####.#.####.#",
        "#.#.........#.#", "#.#.##.#.##.#.#", "#.#.........#.#", "#.#.###D###.#.#",
        "..####HHH####..", "#.#.#######.#.#", "#.#.... ....#.#", "#.#.##.#.##.#.#",
        "#.#.........#.#", "#.####.#.####.#", "#.#.##.#.##.#.#", "#o...........o#",
        "###############"};
    if (x < 0 || x >= MAZE_W || y < 0 || y >= MAZE_H)
        return '#';
    return rows[y][x];
}

static void ent_px(int tile, int sub, int dir, int *px, int *py)
{
    int x, y;
    game_abs(tile, sub, dir, &x, &y);
    *px = BX + x / 16;
    *py = BY + y / 16;
}

static void draw_ghost(gfx_surface *s, int x, int y, int gi)
{
    static const uint32_t body[GHOST_COUNT] = {0xFF3030, 0xFFA8C8, 0x30D0FF, 0xFFA030};
    uint32_t col = body[gi];
    bool fright = game.frightened != 0;
    bool blink = fright && game.fright_timer < 120 && (game.fright_timer / 8) % 2;
    if (fright)
        col = blink ? 0xE8ECFF : 0x2850F0;
    gfx_circle(s, x, y - 1, 6, col, 255);
    gfx_fill(s, gfx_r(x - 6, y - 1, 12, 7), col);
    /* skirt wiggle: three dark scallops swap sides ~4 times a second */
    static const int sk[2][3] = {{-5, -1, 3}, {-4, 0, 4}};
    int wob = (game.step_cnt / 6) & 1;
    for (int i = 0; i < 3; i++)
        gfx_fill(s, gfx_r(x + sk[wob][i], y + 4, 2, 2), 0x08081C);
    if (fright && !blink) { /* scared face */
        gfx_fill(s, gfx_r(x - 4, y - 4, 2, 2), 0xFFD0D0);
        gfx_fill(s, gfx_r(x + 2, y - 4, 2, 2), 0xFFD0D0);
        gfx_fill(s, gfx_r(x - 4, y + 1, 8, 2), 0xFFD0D0);
    } else { /* eyes looking along dir */
        gfx_circle(s, x - 3, y - 3, 2, 0xFFFFFF, 255);
        gfx_circle(s, x + 3, y - 3, 2, 0xFFFFFF, 255);
        gfx_circle(s, x - 3 + UDX[game.g[gi].dir], y - 3 + UDY[game.g[gi].dir], 1, 0x203060, 255);
        gfx_circle(s, x + 3 + UDX[game.g[gi].dir], y - 3 + UDY[game.g[gi].dir], 1, 0x203060, 255);
    }
}

static void draw(void)
{
    gfx_surface *s = &ui.surf;
    char b[40];
    gfx_fill(s, gfx_r(0, 0, UI_W, UI_H), 0x0B0B1E);
    gfx_text(s, 10, 8, ui_font(FONT_B14), "Pac-Man", 0xF2C03C, 255);
    snprintf(b, sizeof(b), "%u", (unsigned)game.score);
    gfx_text(s, 10, 26, ui_font(FONT_B12), b, 0xE5ECF5, 255);
    snprintf(b, sizeof(b), "Best %u", (unsigned)game.best);
    gfx_text_center(s, 80, 26, 80, ui_font(FONT_B12), b, 0xA6BAD2, 255);
    snprintf(b, sizeof(b), "Lv %u", (unsigned)game.level);
    gfx_text(s, 196, 8, ui_font(FONT_B12), b, 0xA6BAD2, 255);
    for (int i = 0; i < game.lives; i++)
        gfx_circle(s, 202 + i * 13, 28, 5, 0xF2C03C, 255);

    gfx_fill(s, gfx_r(BX, BY, MW, MH), 0x08081C);
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++) {
            int px = BX + x * TILE_PX, py = BY + y * TILE_PX;
            if (mchar(x, y) == '#') { /* outline shared edges with open tiles */
                if (mchar(x, y - 1) != '#')
                    gfx_fill(s, gfx_r(px, py, TILE_PX, 2), 0x2848C8);
                if (mchar(x, y + 1) != '#')
                    gfx_fill(s, gfx_r(px, py + TILE_PX - 2, TILE_PX, 2), 0x2848C8);
                if (mchar(x - 1, y) != '#')
                    gfx_fill(s, gfx_r(px, py, 2, TILE_PX), 0x2848C8);
                if (mchar(x + 1, y) != '#')
                    gfx_fill(s, gfx_r(px + TILE_PX - 2, py, 2, TILE_PX), 0x2848C8);
            } else if (mchar(x, y) == 'D') {
                gfx_fill(s, gfx_r(px + 1, py + 5, TILE_PX - 2, 4), 0xF0B860);
            }
        }
    for (int t = 0; t < MAZE_W * MAZE_H; t++) {
        if (!game_dot_at(&game, t))
            continue;
        int x = BX + (t % MAZE_W) * TILE_PX + TILE_PX / 2;
        int y = BY + (t / MAZE_W) * TILE_PX + TILE_PX / 2;
        if (game_power_at(&game, t)) {
            bool off = game.frightened && game.fright_timer < 120 &&
                       (game.fright_timer / 8) % 2;
            if (!off)
                gfx_circle(s, x, y, 5, 0xFFE8B0, 255);
        } else {
            gfx_circle(s, x, y, 2, 0xFFD8A0, 255);
        }
    }
    if (game.tofu) { /* cherries below the ghost house */
        int x = BX + (TOFU_TILE % MAZE_W) * TILE_PX + TILE_PX / 2;
        int y = BY + (TOFU_TILE / MAZE_W) * TILE_PX + TILE_PX / 2;
        gfx_line(s, x - 3, y + 1, x + 1, y - 5, 0x40A040, 255);
        gfx_line(s, x + 3, y + 1, x + 1, y - 5, 0x40A040, 255);
        gfx_circle(s, x - 3, y + 2, 3, 0xE03848, 255);
        gfx_circle(s, x + 3, y + 2, 3, 0xC02838, 255);
    }

    int px, py;
    ent_px(game.pac_tile, game.pac_sub, game.pac_dir, &px, &py);
    if (game.state == ST_DYING) { /* shrink away as it dies */
        int r = (int)(6 * (uint32_t)game.state_timer / DYING_STEPS);
        if (r > 0)
            gfx_circle(s, px, py, r, 0xF2C03C, 255);
    } else {
        gfx_circle(s, px, py, 6, 0xF2C03C, 255);
        /* chomp: the mouth wedge cycles while playing */
        bool open = (game.step_cnt / 5) & 1;
        int ox = px + UDX[game.pac_dir] * 4, oy = py + UDY[game.pac_dir] * 4;
        int w = open ? 5 : 3;
        gfx_fill(s, gfx_r(ox - w / 2, oy - w / 2, w, w), 0x08081C);
    }
    for (int i = 0; i < GHOST_COUNT; i++) {
        if (game.g[i].in_house && game.g[i].release)
            continue; /* hidden until released */
        ent_px(game.g[i].tile, game.g[i].sub, game.g[i].dir, &px, &py);
        draw_ghost(s, px, py, i);
    }

    gfx_text_center(s, 0, 288, UI_W, ui_font(FONT_R11),
                    "Arrows / swipe steer   ESC pauses", 0xA6BAD2, 255);
    if (save_failed)
        gfx_text_center(s, 0, 272, UI_W, ui_font(FONT_B12), "Could not save game",
                        0xFFCF8A, 255);

    bool menu = paused || game.state == ST_OVER;
    if (menu || game.state == ST_WIN) {
        gfx_fill_alpha(s, gfx_r(0, BY, UI_W, MH), 0x000000, 185);
        gfx_round_rect(s, gfx_r(20, 100, 200, game.state == ST_WIN ? 70 : 110), 9,
                       0x304763, 255);
        const char *title = game.state == ST_OVER ? "Game Over"
                        : game.state == ST_WIN ? "Level clear!" : "Paused";
        gfx_text_center(s, 20, 110, 200, ui_font(FONT_B20), title, 0xFFFFFF, 255);
        int count = 1;
        const char *items[2] = {"New Game", "Resume"};
        if (game.state == ST_WIN)
            items[0] = "Next Level";
        if (paused) {
            items[0] = "Resume";
            items[1] = "New Game";
            count = 2;
        }
        for (int i = 0; i < count; i++) {
            if (menu && i == selection)
                gfx_round_rect(s, gfx_r(33, 142 + i * 30, 174, 26), 4, 0x52749C, 255);
            gfx_text_center(s, 20, 148 + i * 30, 200, ui_font(FONT_B14), items[i],
                            0xFFFFFF, 255);
        }
        if (game.state == ST_WIN)
            gfx_text_center(s, 20, 148, 200, ui_font(FONT_B14), "OK / tap for next level", 0xC8D8F0, 255);
    }
    ui_invalidate_all();
    ui_present();
}

static void next_level(void)
{
    game_next_level(&game);
    cue_start();
    printf("pacman: level=%u score=%u\n", (unsigned)game.level, (unsigned)game.score);
    save();
}

static void restart(void)
{
    game_new(&game, gz_time_ms(), game.best);
    paused = false;
    selection = 0;
    cue_start();
    save();
}

/* Pause/game-over menu keys. */
static void menu_key(int code)
{
    int count = paused ? 2 : 1;
    if ((code == GZ_KEY_UP || code == GZ_KEY_DOWN) && count == 2)
        selection ^= 1;
    else if (code == GZ_KEY_OK || code == GZ_KEY_ENTER) {
        if (game.state == ST_WIN)
            next_level();
        else if (!paused || selection == 1)
            restart();
        else
            paused = false;
    } else if (code == GZ_KEY_ESC && paused)
        paused = false;
}

static void play_key(int code)
{
    switch (code) {
    case GZ_KEY_UP: game_set_dir(&game, DIR_UP); break;
    case GZ_KEY_RIGHT: game_set_dir(&game, DIR_RIGHT); break;
    case GZ_KEY_DOWN: game_set_dir(&game, DIR_DOWN); break;
    case GZ_KEY_LEFT: game_set_dir(&game, DIR_LEFT); break;
    case GZ_KEY_ESC:
        paused = true;
        selection = 0;
        sound_close();
        save();
        break;
    }
}

static void maze_touch(int x, int y) /* swipe: set Pac's desired direction */
{
    int dx = x - tdx, dy = y - tdy;
    if (dx < 24 && dx > -24 && dy < 24 && dy > -24)
        return;
    if (dx > dy || dx > -dy)
        game_set_dir(&game, dx > 0 ? DIR_RIGHT : DIR_LEFT);
    else
        game_set_dir(&game, dy > 0 ? DIR_DOWN : DIR_UP);
    tdx = x;
    tdy = y; /* the swipe can continue from here */
}

static void touch(const struct gz_input_event *e)
{
    if (e->type == GZ_EV_TOUCH_DOWN) {
        tdx = e->x;
        tdy = e->y;
    } else if (e->type == GZ_EV_TOUCH_MOVE) {
        if (!paused && game.state == ST_PLAY)
            maze_touch(e->x, e->y);
    } else if (e->type == GZ_EV_TOUCH_UP) {
        if (game.state == ST_WIN)
            next_level();
        else if (paused) {
            int row = (e->y - 142) / 30;
            if (e->y >= 142 && row < 2) {
                if (row == 1)
                    restart();
                else
                    paused = false;
            }
        }
        tdx = -1;
    }
}

/* Compare state with the previous frame and queue the matching cue. */
static void cues(uint8_t pdots, uint8_t pfright, uint8_t ptofu,
                 uint32_t pscore, uint8_t pstate)
{
    if (game.state == ST_PLAY) {
        uint32_t d = game.score - pscore;
        if (game.frightened && !pfright)
            cue_power();
        else if (game.frightened && d >= 200)
            cue_ghost();
        else if (ptofu && !game.tofu)
            cue_fruit();
        else if (game.dots_eaten != pdots)
            cue_pellet(game.dots_eaten);
    }
    if (game.state != pstate) {
        if (game.state == ST_DYING)
            cue_dying();
        else if (game.state == ST_WIN)
            cue_win();
        else if (game.state == ST_OVER)
            cue_over();
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
    printf("pacman: ready score=%u level=%u paused=%d\n", (unsigned)game.score,
           (unsigned)game.level, paused);

    uint32_t last = gz_time_ms();
    draw();
    for (;;) {
        struct gz_input_event e;
        bool running = !paused && (game.state == ST_PLAY || game.state == ST_DYING);
        /* Wait forever when idle; wake every 16 ms while entities move and
         * every 100 ms while a queued sound still needs streaming. */
        int got = ui_next_event(&e,
            running ? 16 : (audio_on && !paused ? 150 : -1));
        uint32_t now = gz_time_ms(), elapsed = now - last;
        last = now;
        if (got) {
            if (e.type == GZ_EV_APP_DEACTIVATE) {
                /* Home: save now; the app may be ended while suspended. */
                paused = true;
                selection = 0;
                sound_close();
                save();
                continue;
            }
            if (e.type == GZ_EV_APP_ACTIVATE) {
                last = gz_time_ms();
                draw();
                continue;
            }
            if (e.type == GZ_EV_TOUCH_CANCEL)
                tdx = -1;           /* touch ended by the system: do nothing */
            else if (e.type == GZ_KEY_DOWN) {
                if (paused || game.state == ST_OVER || game.state == ST_WIN)
                    menu_key(e.code);
                else
                    play_key(e.code);
            } else
                touch(&e);
        }
        if (!paused && !sound_off) {
            sound_ensure();
            sound_pump();
        }
        if (running) {
            uint8_t pdots = game.dots_eaten, pfright = game.frightened;
            uint8_t ptofu = game.tofu, pstate = game.state;
            uint32_t pscore = game.score;
            game_tick(&game, elapsed);
            cues(pdots, pfright, ptofu, pscore, pstate);
            if (game.state != pstate) {
                if (game.state == ST_OVER)
                    printf("pacman: over score=%u\n", (unsigned)game.score);
                else if (game.state == ST_WIN)
                    printf("pacman: win level=%u score=%u\n", (unsigned)game.level,
                           (unsigned)game.score);
                else if (game.state == ST_DYING)
                    printf("pacman: dying lives=%u score=%u\n", (unsigned)game.lives,
                           (unsigned)game.score);
                save();
            }
        }
        draw();
    }
}
