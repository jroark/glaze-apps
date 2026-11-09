/*
 * intf_glaze: VLC's interface on libui, run on the app's main thread
 * (main.c starts it with VLC_AddIntf, blocking).
 *
 * Views: a home menu (Music, Movies, the memory card, a network stream, the
 * playlist), a file browser that lists what the built-in demuxers play, a
 * URL field on the on-screen keyboard, the playlist, Now Playing (title,
 * stream title or artist, scrubber, controls, volume) and the video, full
 * screen, with controls that a tap brings up over the picture. The video
 * output draws only while the video view is shown (vout_glaze.h); it opens
 * when an input with video starts, and Now Playing then switches to it.
 *
 * Keys follow Music and Videos: OK plays and pauses, Left and Right seek
 * 10 s, Up and Down change the volume, Esc goes back.
 *
 * The audio output is held (its stream closed, aout_glaze.h) while paused
 * and while the app is in the background, so the Music player can use it.
 * Home pauses, and the app stays paused when it comes back. If the Music
 * player takes the output during playback, VLC pauses. The playlist, the
 * current item, its position and the volume are saved in <bundle>/data on
 * Home and restored at launch, ready to resume where they were.
 *
 * Test hook: a /Card/autoplay.m3u present at launch is played (its URL
 * entries once the network is up), with progress and the end of the
 * playlist reported to the debug log as "vlc: ..." lines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/intf.h>
#include <vlc/input.h>
#include <vlc/aout.h>

#include <glaze/audio.h>
#include <glaze/fs.h>
#include <glaze/net.h>
#include <glaze/res.h>
#include <glaze/sys.h>
#include <glaze/ui.h>

#include "aout_glaze.h"
#include "vout_glaze.h"

static int  Open(vlc_object_t *);
static void Run(intf_thread_t *);

vlc_module_begin();
    set_shortname("Glaze");
    set_description(_("Glaze interface"));
    set_capability("interface", 0);
    set_category(CAT_INTERFACE);
    set_subcategory(SUBCAT_INTERFACE_MAIN);
    add_shortcut("glaze");
    set_callbacks(Open, NULL);
vlc_module_end();

static int Open(vlc_object_t *p_this)
{
    ((intf_thread_t *)p_this)->pf_run = Run;
    return VLC_SUCCESS;
}

/* ---- state --------------------------------------------------------------- */

#define POLL_MS         250
#define SEEK_STEP       10              /* seconds, Left and Right */
#define OVERLAY_MS      3000            /* video controls hide after this */
#define MAX_FILES       128
#define URL_MAX         256
#define TITLE_MAX       128
#define AUTOPLAY        "/Card/autoplay.m3u"
#define AUTO_MAX        16
#define NET_WAIT_MS     60000

enum view { V_HOME, V_FILES, V_URL, V_LIST, V_NOW, V_VIDEO };

/* What the views show, from the playlist and its input. The overlay is
 * drawn from the video output's thread, so `st` changes under the draw lock. */
struct now {
    int status;                         /* PLAYLIST_RUNNING ... */
    int state;                          /* input state, END_S without one */
    int index, count;
    int64_t time_us, length_us;
    char title[TITLE_MAX];
    char sub[TITLE_MAX];                /* stream title, artist */
};

static intf_thread_t *intf;
static playlist_t *pl;
static struct now st;
static enum view view = V_HOME, player_back = V_HOME;
static struct ui_list list;
static bool in_background;

/* Files. */
struct entry {
    char name[FS_NAME_MAX];
    bool dir;
};
static char dir[FS_PATH_MAX], root[FS_PATH_MAX];
static struct entry files[MAX_FILES];
static int nfiles;

/* The URL field. */
static char url[URL_MAX] = "http://";
static int url_cursor;
static struct ui_keyboard kb;

/* Touch state. */
static gfx_rect back_rect, right_rect, done_rect;
static bool pressed_back, pressed_right, pressed_done, pressed_play;
static int pressed_ctrl = -1;           /* 0 previous, 1 play/pause, 2 next */
static bool scrubbing, voluming;
static int64_t scrub_us;

/* Video. */
static bool overlay;
static uint32_t overlay_until;
static bool video_was_active, video_dismissed;

/* Resume after a launch: the item and where it was. */
static int resume_index = -1;
static int64_t resume_us;
static int seek_index = -1;             /* seek there once it plays */
static int64_t seek_us;

/* The test hook. */
static char autoplay[AUTO_MAX][URL_MAX];
static int nautoplay;
static uint32_t autoplay_since;
static bool autoplay_running;
static uint32_t underruns_at_start;

static char data_dir[FS_PATH_MAX];

/* ---- helpers ------------------------------------------------------------- */

static bool ext_in(const char *name, const char *const *exts)
{
    const char *dot = strrchr(name, '.');

    if (!dot)
        return false;
    for (; *exts; exts++) {
        const char *a = dot + 1, *b = *exts;
        while (*a && *b && (*a | 0x20) == *b)
            a++, b++;
        if (!*a && !*b)
            return true;
    }
    return false;
}

static const char *const media_exts[] = {
    "mp3", "mp2", "mpa", "mpga", "wav", "avi", "mpg", "mpeg", "mpe", "vob",
    "m1v", "m2v", "mpv", NULL,
};
static const char *const playlist_exts[] = { "m3u", "pls", NULL };

static bool is_url(const char *s)
{
    return strstr(s, "://") != NULL;
}

/* The name without its folder and extension. */
static void display_name(char *out, size_t n, const char *path)
{
    const char *base = strrchr(path, '/');

    base = base ? base + 1 : path;
    snprintf(out, n, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot && dot != out && !is_url(path))
        *dot = 0;
}

static void fmt_time(char *buf, size_t n, int64_t us, bool minus)
{
    unsigned sec = (unsigned)(us / 1000000);

    if (sec >= 3600)
        snprintf(buf, n, "%s%u:%02u:%02u", minus ? "-" : "", sec / 3600, sec / 60 % 60, sec % 60);
    else
        snprintf(buf, n, "%s%u:%02u", minus ? "-" : "", sec / 60, sec % 60);
}

static void triangle(gfx_surface *s, int x, int y, int w, int h, int dir_, uint32_t rgb)
{
    for (int i = 0; i < w; i++) {
        int inset = i * h / (2 * w);
        int col = dir_ > 0 ? x + i : x + w - 1 - i;
        gfx_fill(s, gfx_r(col, y + inset, 1, h - 2 * inset), rgb);
    }
}

static void draw_play_pause(gfx_surface *s, int cx, int cy, bool playing, uint32_t rgb)
{
    if (playing) {
        gfx_fill(s, gfx_r(cx - 7, cy - 9, 5, 18), rgb);
        gfx_fill(s, gfx_r(cx + 2, cy - 9, 5, 18), rgb);
    } else {
        triangle(s, cx - 7, cy - 10, 17, 20, 1, rgb);
    }
}

static void draw_skip(gfx_surface *s, int cx, int cy, int d, uint32_t rgb)
{
    int x = d > 0 ? cx - 11 : cx - 8;

    triangle(s, x, cy - 7, 10, 14, d, rgb);
    triangle(s, x + 9, cy - 7, 10, 14, d, rgb);
    gfx_fill(s, gfx_r(d > 0 ? cx + 8 : cx - 11, cy - 7, 3, 14), rgb);
}

/* ---- the playlist and its input -------------------------------------------- */

/* The current input, held; release with vlc_object_release. */
static input_thread_t *get_input(void)
{
    input_thread_t *in;

    vlc_mutex_lock(&pl->object_lock);
    in = pl->p_input;
    if (in)
        vlc_object_yield(in);
    vlc_mutex_unlock(&pl->object_lock);
    return in;
}

static bool playing(void)
{
    return st.state == PLAYING_S || st.state == INIT_S;
}

static bool loaded(void)
{
    return st.count > 0 && (st.index >= 0 || resume_index >= 0);
}

static const char *state_name(int s)
{
    switch (s) {
    case INIT_S: return "opening";
    case PLAYING_S: return "playing";
    case PAUSE_S: return "paused";
    case ERROR_S: return "error";
    default: return "stopped";
    }
}

/* Re-reads what is playing; true if anything shown changed. */
static bool poll_state(void)
{
    struct now n;
    input_thread_t *in;

    memset(&n, 0, sizeof(n));
    n.state = END_S;
    vlc_mutex_lock(&pl->object_lock);
    n.status = pl->status.i_status;
    n.index = pl->i_index;
    n.count = pl->i_size;
    in = pl->p_input;
    if (in)
        vlc_object_yield(in);
    int shown = n.index >= 0 ? n.index : resume_index;
    if (!in && shown >= 0 && shown < pl->i_size && pl->pp_items[shown]->input.psz_name)
        snprintf(n.title, sizeof(n.title), "%s", pl->pp_items[shown]->input.psz_name);
    vlc_mutex_unlock(&pl->object_lock);

    if (in) {
        input_item_t *it = in->input.p_item;
        /* A finished input's variables are being torn down: "state" then
         * reads 0, which is INIT_S. */
        n.state = in->b_die || in->b_dead || in->b_eof ? END_S : var_GetInteger(in, "state");
        n.time_us = var_GetTime(in, "time");
        n.length_us = var_GetTime(in, "length");
        vlc_mutex_lock(&it->lock);
        snprintf(n.title, sizeof(n.title), "%s", it->psz_name ? it->psz_name : "");
        vlc_mutex_unlock(&it->lock);
        char *np = vlc_input_item_GetInfo(it, _(VLC_META_INFO_CAT), _(VLC_META_NOW_PLAYING));
        char *ar = vlc_input_item_GetInfo(it, _(VLC_META_INFO_CAT), _(VLC_META_ARTIST));
        snprintf(n.sub, sizeof(n.sub), "%s", np && *np ? np : ar && *ar ? ar : "");
        free(np);
        free(ar);
        if (n.state == PLAYING_S && seek_index >= 0 && seek_index == n.index) {
            var_SetTime(in, "time", seek_us);
            seek_index = -1;
        }
        vlc_object_release(in);
    }
    if (n.state == ERROR_S && !n.sub[0])
        snprintf(n.sub, sizeof(n.sub), "Can't play this");
    if (n.title[0] && strstr(n.title, "://")) {
        /* A stream without a name: show its address without the scheme. */
        char t[TITLE_MAX];
        snprintf(t, sizeof(t), "%s", strstr(n.title, "://") + 3);
        snprintf(n.title, sizeof(n.title), "%s", t);
    }
    if (shown < 0 && resume_index < 0 && n.index < 0)
        n.title[0] = 0;

    bool changed = n.state != st.state || n.index != st.index || n.count != st.count ||
                   n.status != st.status || n.time_us / 1000000 != st.time_us / 1000000 ||
                   n.length_us / 1000000 != st.length_us / 1000000 ||
                   strcmp(n.title, st.title) || strcmp(n.sub, st.sub);
    if (n.state != st.state || n.index != st.index || strcmp(n.sub, st.sub))
        printf("vlc: %s %d/%d at %d/%d s: %s%s%s\n", state_name(n.state), n.index + 1, n.count,
               (int)(st.time_us / 1000000), (int)(st.length_us / 1000000), n.title,
               n.sub[0] ? " - " : "", n.sub);
    if (autoplay_running && st.status == PLAYLIST_RUNNING && n.status == PLAYLIST_STOPPED) {
        struct audio_state as;
        unsigned u = audio_get_state(&as) == 0 ? (unsigned)(as.underruns - underruns_at_start) : 0;
        printf("vlc: playlist done, %u underruns\n", u);
        autoplay_running = false;
    }
    vout_glaze_lock();
    st = n;
    vout_glaze_unlock();
    return changed;
}

static void hold_audio(void)
{
    aout_glaze_hold(in_background || !playing());
}

static void set_state(int s)
{
    input_thread_t *in = get_input();

    if (in) {
        var_SetInteger(in, "state", s);
        vlc_object_release(in);
    }
}

static void play_pause(void)
{
    if (playing()) {
        set_state(PAUSE_S);
    } else if (st.state == PAUSE_S) {
        aout_glaze_hold(0);
        set_state(PLAYING_S);
    } else if (st.count > 0) {
        int i = st.index >= 0 ? st.index : resume_index >= 0 ? resume_index : 0;
        if (i == resume_index && resume_us > 0) {
            seek_index = i;
            seek_us = resume_us;
        }
        resume_index = -1;
        aout_glaze_hold(0);
        playlist_Goto(pl, i);
    }
    poll_state();
}

static void seek_by(int sec)
{
    input_thread_t *in = get_input();

    if (in) {
        var_SetTime(in, "time-offset", (int64_t)sec * 1000000);
        vlc_object_release(in);
    }
}

static void seek_to(int64_t us)
{
    input_thread_t *in = get_input();

    if (in) {
        var_SetTime(in, "time", us);
        vlc_object_release(in);
    }
}

static int volume(void)
{
    audio_volume_t v = AOUT_VOLUME_DEFAULT;

    aout_VolumeGet(intf, &v);
    return v;
}

static void set_volume(int v)
{
    v = v < AOUT_VOLUME_MIN ? AOUT_VOLUME_MIN : v > AOUT_VOLUME_DEFAULT * 2 ? AOUT_VOLUME_DEFAULT * 2 : v;
    aout_VolumeSet(intf, (audio_volume_t)v);
}

/* Adds a file, playlist or stream and plays it. */
static void add_and_play(const char *mrl)
{
    char name[TITLE_MAX];

    display_name(name, sizeof(name), mrl);
    resume_index = -1;
    aout_glaze_hold(0);
    playlist_Add(pl, mrl, name, PLAYLIST_APPEND | PLAYLIST_GO, PLAYLIST_END);
    printf("vlc: open %s\n", mrl);
}

/* ---- saving ----------------------------------------------------------------- */

/* The playlist as an M3U with names, and "index time_ms volume". */
static void save_state(void)
{
    char path[FS_PATH_MAX], tmp[FS_PATH_MAX];
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);

    if (!buf)
        return;
    len = (size_t)snprintf(buf, cap, "#EXTM3U\n");
    vlc_mutex_lock(&pl->object_lock);
    for (int i = 0; i < pl->i_size; i++) {
        input_item_t *it = &pl->pp_items[i]->input;
        const char *name = it->psz_name ? it->psz_name : "";
        const char *uri = it->psz_uri ? it->psz_uri : "";
        size_t need = strlen(name) + strlen(uri) + 32;
        if (len + need > cap) {
            char *nb = realloc(buf, cap = (len + need) * 2);
            if (!nb)
                break;
            buf = nb;
        }
        len += (size_t)snprintf(buf + len, cap - len, "#EXTINF:-1,%s\n%s\n", name, uri);
    }
    vlc_mutex_unlock(&pl->object_lock);

    snprintf(path, sizeof(path), "%s/playlist.m3u", data_dir);
    snprintf(tmp, sizeof(tmp), "%s/playlist.tmp", data_dir);
    if (fs_write_file(tmp, buf, (uint32_t)len) == (int)len)
        fs_rename(tmp, path);
    free(buf);

    char line[64];
    int index = st.index >= 0 ? st.index : resume_index;
    int64_t at = st.index >= 0 ? st.time_us : resume_us;
    int n = snprintf(line, sizeof(line), "%d %d %d\n", index, (int)(at / 1000), volume());
    snprintf(path, sizeof(path), "%s/resume", data_dir);
    snprintf(tmp, sizeof(tmp), "%s/resume.tmp", data_dir);
    if (fs_write_file(tmp, line, (uint32_t)n) == n)
        fs_rename(tmp, path);
    printf("vlc: saved %d items, item %d at %d s\n", st.count, index + 1, (int)(at / 1000000));
}

static void load_state(void)
{
    char path[FS_PATH_MAX];
    static char buf[32 * 1024];

    snprintf(path, sizeof(path), "%s/playlist.m3u", data_dir);
    int n = fs_read_file(path, buf, sizeof(buf) - 1);
    if (n > 0) {
        char name[TITLE_MAX] = "";
        buf[n] = 0;
        for (char *line = buf, *next; line && *line; line = next) {
            next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            line[strcspn(line, "\r")] = 0;
            if (!strncmp(line, "#EXTINF:", 8)) {
                const char *comma = strchr(line, ',');
                snprintf(name, sizeof(name), "%s", comma ? comma + 1 : "");
            } else if (line[0] && line[0] != '#') {
                if (!name[0])
                    display_name(name, sizeof(name), line);
                playlist_Add(pl, line, name, PLAYLIST_APPEND, PLAYLIST_END);
                name[0] = 0;
            }
        }
    }
    snprintf(path, sizeof(path), "%s/resume", data_dir);
    n = fs_read_file(path, buf, 63);
    if (n > 0) {
        int index = -1, ms = 0, vol = AOUT_VOLUME_DEFAULT;
        buf[n] = 0;
        if (sscanf(buf, "%d %d %d", &index, &ms, &vol) >= 2 && index >= 0 && index < pl->i_size) {
            resume_index = index;
            resume_us = (int64_t)ms * 1000;
        }
        set_volume(vol);
    }
    printf("vlc: restored %d items, item %d at %d s\n", pl->i_size, resume_index + 1,
           (int)(resume_us / 1000000));
}

/* ---- the test hook ----------------------------------------------------------- */

static void open_player(void);

static void read_autoplay(void)
{
    static char buf[4096];
    int n = fs_read_file(AUTOPLAY, buf, sizeof(buf) - 1);

    if (n <= 0)
        return;
    buf[n] = 0;
    for (char *line = buf, *next; line && *line && nautoplay < AUTO_MAX; line = next) {
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        line[strcspn(line, "\r")] = 0;
        if (!line[0] || line[0] == '#')
            continue;
        snprintf(autoplay[nautoplay++], URL_MAX, "%s%s",
                 is_url(line) || line[0] == '/' ? "" : "/Card/", line);
    }
    autoplay_since = gz_time_ms();
    printf("vlc: autoplay %d items\n", nautoplay);
}

/* Starts the autoplay list once any network it needs is up. */
static void start_autoplay(void)
{
    bool net = false;

    for (int i = 0; i < nautoplay; i++)
        net |= is_url(autoplay[i]);
    if (net && net_wait_up(0) != 0 && gz_time_ms() - autoplay_since < NET_WAIT_MS)
        return;
    if (net)
        printf("vlc: network %s\n", net_wait_up(0) == 0 ? "up" : "not up");

    struct audio_state as;
    underruns_at_start = audio_get_state(&as) == 0 ? as.underruns : 0;
    playlist_Stop(pl);
    playlist_LockClear(pl);
    resume_index = -1;
    aout_glaze_hold(0);
    for (int i = 0; i < nautoplay; i++) {
        char name[TITLE_MAX];
        display_name(name, sizeof(name), autoplay[i]);
        playlist_Add(pl, autoplay[i], name, PLAYLIST_APPEND | (i ? 0 : PLAYLIST_GO), PLAYLIST_END);
    }
    nautoplay = 0;
    autoplay_running = true;
    poll_state();
    open_player();
}

/* ---- files ----------------------------------------------------------------- */

static int entry_cmp(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;

    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    for (const char *p = x->name, *q = y->name;; p++, q++) {
        int c = (*p | 0x20) - (*q | 0x20);
        if (c || !*p)
            return c;
    }
}

static void list_dir(void)
{
    struct fs_dirent ents[8];
    uint32_t skip = 0;
    int n;

    nfiles = 0;
    while (nfiles < MAX_FILES && (n = fs_list(dir, skip, ents, 8)) > 0) {
        for (int i = 0; i < n && nfiles < MAX_FILES; i++) {
            bool d = ents[i].type == FS_DIR;
            if (ents[i].name[0] == '.')
                continue;
            if (!d && !ext_in(ents[i].name, media_exts) && !ext_in(ents[i].name, playlist_exts))
                continue;
            snprintf(files[nfiles].name, sizeof(files[nfiles].name), "%s", ents[i].name);
            files[nfiles++].dir = d;
        }
        skip += (uint32_t)n;
    }
    qsort(files, (size_t)nfiles, sizeof(files[0]), entry_cmp);
}

/* ---- drawing ----------------------------------------------------------------- */

#define ROW_H       44
#define ART         112
#define ART_Y       (UI_NAV_H + 10)
#define TITLE_Y     (ART_Y + ART + 10)
#define BAR_Y       (TITLE_Y + 48)
#define BAR_X0      44
#define BAR_X1      (UI_W - 44)
#define CTRL_Y      (BAR_Y + 14)
#define CTRL_H      46
#define VOL_Y       (CTRL_Y + CTRL_H + 6)
#define VOL_RECT    gfx_r(30, VOL_Y, UI_W - 60, 20)
#define PANEL       gfx_r((UI_W - 64) / 2, (UI_H - 48) / 2, 64, 48)
#define OV_H        (UI_NAV_H + 28)
#define OV_BAR_Y    (UI_NAV_H + 14)

static const char *home_rows[] = {
    "Music", "Movies", "Memory Card", "Open Network Stream", "Playlist",
};
#define HOME_ROWS 5

static int list_count(void)
{
    switch (view) {
    case V_HOME: return HOME_ROWS;
    case V_FILES: return nfiles;
    case V_LIST: return st.count;
    default: return 0;
    }
}

static void row_text(gfx_surface *s, gfx_rect r, int x, const char *text, bool sel, bool chevron)
{
    const gfx_font *f = ui_font(FONT_B17);

    gfx_text_fit(s, x, r.y + (ROW_H - f->line_height) / 2, UI_W - x - (chevron ? 28 : 10), f,
                 text, sel ? 0xFFFFFF : UI_TEXT, 255);
    if (chevron)
        ui_draw_chevron(s, UI_W - 18, r.y + ROW_H / 2, sel ? 0xFFFFFF : 0x808080);
}

static void draw_row(gfx_surface *s, int i, gfx_rect r, bool sel, void *ctx)
{
    (void)ctx;
    ui_draw_list_row(s, r, sel);
    if (view == V_HOME) {
        char text[40];
        if (i == 4)
            snprintf(text, sizeof(text), "Playlist (%d)", st.count);
        row_text(s, r, 12, i == 4 ? text : home_rows[i], sel, true);
    } else if (view == V_FILES) {
        /* Whole names: test.mp3 and test.wav are different files. */
        row_text(s, r, 12, files[i].name, sel, files[i].dir);
    } else if (view == V_LIST) {
        char name[TITLE_MAX] = "";
        vlc_mutex_lock(&pl->object_lock);
        if (i < pl->i_size && pl->pp_items[i]->input.psz_name)
            snprintf(name, sizeof(name), "%s", pl->pp_items[i]->input.psz_name);
        vlc_mutex_unlock(&pl->object_lock);
        int x = 12;
        if (i == (st.index >= 0 ? st.index : resume_index)) {
            triangle(s, 8, r.y + ROW_H / 2 - 6, 10, 12, 1, sel ? 0xFFFFFF : UI_BLUE_BUTTON);
            x = 24;
        }
        row_text(s, r, x, name, sel, false);
    }
}

static const char *dir_title(void)
{
    if (!strcmp(dir, root)) {
        if (!strcmp(root, "/Card"))
            return "Card";      /* "Memory Card" runs under Now Playing */
        return root + 1;
    }
    const char *slash = strrchr(dir, '/');
    return slash ? slash + 1 : dir;
}

static void draw_lists(gfx_surface *s)
{
    gfx_rect body = list.area;
    const gfx_font *sf = ui_font(FONT_R14);
    const char *title = "VLC", *back = NULL, *right = NULL;

    gfx_set_clip(s, body);
    gfx_fill(s, body, 0xFFFFFF);
    ui_list_draw(s, &list, draw_row, NULL);
    if (!list_count()) {
        const char *a = "", *b = "";
        if (view == V_FILES) {
            a = "No Media";
            b = "MP3, WAV, AVI and MPEG files";
        } else if (view == V_LIST) {
            a = "Playlist Empty";
            b = "Files and streams you open";
        }
        gfx_text_center(s, 0, UI_NAV_H + 100, UI_W, ui_font(FONT_B17), a, 0x666666, 255);
        gfx_text_center(s, 0, UI_NAV_H + 126, UI_W, sf, b, 0x888888, 255);
        if (view == V_LIST)
            gfx_text_center(s, 0, UI_NAV_H + 144, UI_W, sf, "appear here.", 0x888888, 255);
    }
    gfx_reset_clip(s);
    if (view == V_FILES) {
        title = dir_title();
        back = strcmp(dir, root) ? "Back" : "VLC";
    } else if (view == V_LIST) {
        title = "Playlist";
        back = "VLC";
        right = st.count ? "Clear" : NULL;
    }
    back_rect = gfx_r(0, 0, 0, 0);
    ui_draw_navbar(s, title, back, &back_rect, 0);
    if (!right && loaded())
        right = "Now Playing";
    right_rect = right ? ui_draw_bar_button(s, right, true, pressed_right, false, 0) : gfx_r(0, 0, 0, 0);
}

#define FIELD   gfx_r(8, UI_NAV_H + 14, UI_W - 16, 32)
#define KB_Y    (UI_H - UI_KB_H)

static void draw_url(gfx_surface *s)
{
    const gfx_font *f = ui_font(FONT_R14);
    gfx_rect fr = FIELD;
    char head[URL_MAX];

    ui_draw_pinstripes(s, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H));
    gfx_text(s, 10, UI_NAV_H + 56, ui_font(FONT_R12), "HTTP streams, files and M3U or PLS", 0x4C566C, 255);
    gfx_text(s, 10, UI_NAV_H + 72, ui_font(FONT_R12), "playlists. No HTTPS.", 0x4C566C, 255);
    gfx_round_rect(s, fr, 6, 0x7F8790, 255);
    gfx_round_rect(s, gfx_r(fr.x + 1, fr.y + 1, fr.w - 2, fr.h - 2), 5, 0xFFFFFF, 255);

    /* Show the text up to the cursor, scrolled so the cursor is in view. */
    int inner = fr.w - 16, start = 0;
    for (;;) {
        snprintf(head, sizeof(head), "%.*s", url_cursor - start, url + start);
        if (gfx_text_width(f, head) <= inner || start >= url_cursor)
            break;
        start++;
    }
    int ty = fr.y + (fr.h - f->line_height) / 2;
    gfx_set_clip(s, gfx_r(fr.x + 6, fr.y, fr.w - 12, fr.h));
    gfx_text(s, fr.x + 8, ty, f, url + start, UI_TEXT, 255);
    gfx_fill(s, gfx_r(fr.x + 8 + gfx_text_width(f, head), ty, 2, f->line_height), UI_BLUE_BUTTON);
    gfx_reset_clip(s);

    back_rect = gfx_r(0, 0, 0, 0);
    ui_draw_navbar(s, "Open Stream", "VLC", &back_rect, 0);
    right_rect = ui_draw_bar_button(s, "Play", true, pressed_right, true, 0);
    if (!ui_has_hardware_keyboard())
        ui_draw_keyboard(s, &kb, KB_Y);
}

static int64_t shown_time(void)
{
    return scrubbing ? scrub_us : st.time_us;
}

static int knob_x(int64_t t)
{
    if (st.length_us <= 0)
        return BAR_X0;
    if (t > st.length_us)
        t = st.length_us;
    return BAR_X0 + (int)(t * (BAR_X1 - BAR_X0) / st.length_us);
}

/* Elapsed, the track and remaining (or "Live"), centred on y. */
static void draw_scrubber(gfx_surface *s, int y)
{
    const gfx_font *nf = ui_font(FONT_B11);
    char a[16], b[16];
    int64_t t = shown_time();

    fmt_time(a, sizeof(a), t, false);
    if (st.length_us > 0)
        fmt_time(b, sizeof(b), st.length_us - (t < st.length_us ? t : st.length_us), true);
    else
        snprintf(b, sizeof(b), "%s", st.state == END_S ? "" : "Live");
    gfx_text(s, 6, y - nf->line_height / 2, nf, a, 0xFFFFFF, 255);
    gfx_text(s, UI_W - 6 - gfx_text_width(nf, b), y - nf->line_height / 2, nf, b, 0xFFFFFF, 255);
    int kx = knob_x(t);
    gfx_round_rect(s, gfx_r(BAR_X0, y - 2, BAR_X1 - BAR_X0, 5), 2, 0x4A4A4A, 255);
    if (st.length_us > 0) {
        gfx_round_rect(s, gfx_r(BAR_X0, y - 2, kx - BAR_X0 + 2, 5), 2, 0xD8D8D8, 255);
        gfx_circle(s, kx, y, scrubbing ? 8 : 6, 0xFFFFFF, 255);
        gfx_circle(s, kx, y, scrubbing ? 6 : 4, 0xD0D0D0, 255);
    }
}

static gfx_rect ctrl_rect(int i)
{
    return gfx_r(30 + i * 60, CTRL_Y, 60, CTRL_H);
}

static void draw_now(gfx_surface *s)
{
    const gfx_font *tf = ui_font(FONT_B17), *af = ui_font(FONT_R13);
    gfx_rect art = gfx_r((UI_W - ART) / 2, ART_Y, ART, ART);
    static gfx_image icon;
    static bool have_icon, tried;

    gfx_fill(s, gfx_r(0, 0, UI_W, UI_H), 0x000000);
    gfx_vgradient(s, art, 0x5C6168, 0x1C1E21, 255);
    gfx_fill(s, gfx_r(art.x, art.y, art.w, 1), 0x8A9098);
    if (!tried) {
        have_icon = res_image(&icon, "icon.gzi");
        tried = true;
    }
    if (have_icon)
        gfx_draw_image_scaled(s, gfx_r(art.x + 16, art.y + 16, art.w - 32, art.h - 32), &icon, 255);
    gfx_vgradient(s, gfx_r(art.x, art.y + art.h, art.w, 6), 0x2A2D30, 0x000000, 255);

    const char *title = st.title[0] ? st.title : "Not Playing";
    if (gfx_text_width(tf, title) <= UI_W - 20)
        gfx_text_center(s, 0, TITLE_Y, UI_W, tf, title, 0xFFFFFF, 255);
    else
        gfx_text_fit(s, 10, TITLE_Y, UI_W - 20, tf, title, 0xFFFFFF, 255);
    const char *sub = st.sub[0] ? st.sub : st.state == INIT_S ? "Opening..." :
                      st.state == END_S && resume_index >= 0 ? "Paused" : "";
    if (gfx_text_width(af, sub) <= UI_W - 20)
        gfx_text_center(s, 0, TITLE_Y + 22, UI_W, af, sub, 0xA0A0A0, 255);
    else
        gfx_text_fit(s, 10, TITLE_Y + 22, UI_W - 20, af, sub, 0xA0A0A0, 255);

    if (st.state != END_S || resume_index >= 0) {
        struct now saved = st;
        if (st.state == END_S && !scrubbing) {
            st.time_us = resume_us;
            st.length_us = 0;
        }
        draw_scrubber(s, BAR_Y);
        st = saved;
    }

    gfx_vgradient(s, gfx_r(0, CTRL_Y, UI_W, UI_H - CTRL_Y), 0x2E2E2E, 0x0A0A0A, 255);
    gfx_fill(s, gfx_r(0, CTRL_Y, UI_W, 1), 0x4A4A4A);
    for (int i = 0; i < 3; i++) {
        gfx_rect r = ctrl_rect(i);
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        if (i == pressed_ctrl)
            gfx_circle(s, cx, cy, 18, 0xFFFFFF, 60);
        if (i == 1)
            draw_play_pause(s, cx, cy, playing(), 0xFFFFFF);
        else
            draw_skip(s, cx, cy, i == 0 ? -1 : 1, 0xFFFFFF);
    }
    ui_draw_slider(s, VOL_RECT, volume(), AOUT_VOLUME_DEFAULT * 2);

    back_rect = gfx_r(0, 0, 0, 0);
    ui_draw_navbar(s, "Now Playing", player_back == V_LIST ? "Playlist" : "Back", &back_rect, 1);
    right_rect = vout_glaze_active() ? ui_draw_bar_button(s, "Video", true, pressed_right, false, 1)
                                     : gfx_r(0, 0, 0, 0);
}

/* The controls over the video; also called from the video output's thread
 * (draw lock held). */
static void draw_overlay(gfx_surface *s)
{
    static uint16_t barbuf[UI_W * OV_H];
    gfx_surface bar;

    gfx_surface_init(&bar, barbuf, UI_W, OV_H, UI_W);
    gfx_vgradient(&bar, gfx_r(0, 0, UI_W, OV_H), 0x3A3A3A, 0x0A0A0A, 255);
    done_rect = ui_draw_bar_button(&bar, "Done", false, pressed_done, true, 1);
    gfx_text_fit(&bar, 64, (UI_NAV_H - ui_font(FONT_B14)->line_height) / 2, UI_W - 72,
                 ui_font(FONT_B14), st.title, 0xFFFFFF, 255);
    draw_scrubber(&bar, OV_BAR_Y);
    gfx_blit_alpha(s, 0, 0, &bar, gfx_r(0, 0, UI_W, OV_H), 190);

    gfx_rect p = PANEL;
    gfx_round_rect(s, p, 8, 0x000000, 150);
    gfx_round_rect(s, gfx_r(p.x + 1, p.y + 1, p.w - 2, p.h / 2), 7, 0xFFFFFF, 25);
    int cx = p.x + p.w / 2, cy = p.y + p.h / 2;
    if (pressed_play)
        gfx_circle(s, cx, cy, 18, 0xFFFFFF, 60);
    draw_play_pause(s, cx, cy, playing(), 0xFFFFFF);
}

static void draw_view(gfx_surface *s)
{
    switch (view) {
    case V_URL:
        draw_url(s);
        break;
    case V_NOW:
        draw_now(s);
        break;
    case V_VIDEO:
        vout_glaze_repaint();
        if (overlay)
            draw_overlay(s);
        break;
    default:
        draw_lists(s);
        break;
    }
}

static void redraw(void)
{
    vout_glaze_lock();
    draw_view(&ui.surf);
    ui_invalidate_all();
    ui_present();
    vout_glaze_unlock();
}

static void transition(bool forward)
{
    vout_glaze_lock();
    ui_transition(draw_view, forward);
    vout_glaze_unlock();
}

/* ---- navigation ------------------------------------------------------------- */

static void set_view(enum view v, int dir_)
{
    enum view old = view;

    view = v;
    pressed_back = pressed_right = pressed_done = pressed_play = false;
    pressed_ctrl = -1;
    scrubbing = voluming = false;
    if (v == V_HOME || v == V_FILES || v == V_LIST) {
        ui_list_reset(&list, list_count());
        list.footer_h = 0;
    }
    if (v == V_VIDEO) {
        overlay = true;
        overlay_until = gz_time_ms() + OVERLAY_MS;
        vout_glaze_set_overlay(draw_overlay);
        vout_glaze_show(1);
    } else if (old == V_VIDEO) {
        vout_glaze_show(0);
        vout_glaze_set_overlay(NULL);
    }
    if (dir_ && v != V_VIDEO && old != V_VIDEO)
        transition(dir_ > 0);
    else
        redraw();
}

static void open_player(void)
{
    if (view != V_NOW && view != V_VIDEO)
        player_back = view;
    video_dismissed = false;
    set_view(vout_glaze_active() ? V_VIDEO : V_NOW, 1);
}

static void leave_player(void)
{
    set_view(player_back, -1);
}

static void open_dir(const char *path, int dir_)
{
    snprintf(dir, sizeof(dir), "%s", path);
    list_dir();
    set_view(V_FILES, dir_);
}

static void go_back(void)
{
    switch (view) {
    case V_FILES:
        if (strcmp(dir, root)) {
            char *slash = strrchr(dir, '/');
            if (slash)
                *slash = 0;
            open_dir(dir, -1);
        } else {
            set_view(V_HOME, -1);
        }
        break;
    case V_URL:
    case V_LIST:
        set_view(V_HOME, -1);
        break;
    case V_NOW:
        leave_player();
        break;
    case V_VIDEO:
        video_dismissed = true;
        set_view(V_NOW, 0);
        break;
    default:
        break;
    }
}

static void show_overlay(void)
{
    overlay = true;
    overlay_until = gz_time_ms() + OVERLAY_MS;
    vout_glaze_set_overlay(draw_overlay);
}

static void play_url(void)
{
    char mrl[URL_MAX + 8];

    if (!url[0] || !strcmp(url, "http://"))
        return;
    snprintf(mrl, sizeof(mrl), "%s%s", is_url(url) ? "" : "http://", url);
    add_and_play(mrl);
    player_back = V_HOME;
    view = V_HOME;
    open_player();
}

/* ---- events ------------------------------------------------------------------- */

/* Nav bar buttons: true if the event was theirs. */
static bool bar_event(const struct gz_input_event *e)
{
    if (e->type == GZ_EV_TOUCH_DOWN && e->y < UI_NAV_H) {
        pressed_back = gfx_rect_contains(back_rect, e->x, e->y);
        pressed_right = gfx_rect_contains(right_rect, e->x, e->y);
        if (pressed_right)
            redraw();
        return true;
    }
    if (!pressed_back && !pressed_right)
        return false;
    if (e->type == GZ_EV_TOUCH_UP) {
        bool back = pressed_back && gfx_rect_contains(back_rect, e->x, e->y);
        bool right = pressed_right && gfx_rect_contains(right_rect, e->x, e->y);
        pressed_back = pressed_right = false;
        if (back) {
            go_back();
        } else if (right) {
            if (view == V_LIST && st.count) {
                playlist_Stop(pl);
                playlist_LockClear(pl);
                resume_index = -1;
                poll_state();
                ui_list_reset(&list, list_count());
                redraw();
            } else if (view == V_URL) {
                play_url();
            } else if (view == V_NOW) {
                video_dismissed = false;
                set_view(V_VIDEO, 0);
            } else {
                open_player();
            }
        } else {
            redraw();
        }
    }
    return true;
}

static void list_event(const struct gz_input_event *e)
{
    if (bar_event(e))
        return;
    bool dirty = false;
    int i = ui_list_event(&list, e, &dirty);
    if (i < 0) {
        if (dirty)
            redraw();
        return;
    }
    if (view == V_HOME) {
        static const char *const roots[] = { "/Music", "/Movies", "/Card" };
        if (i < 3) {
            snprintf(root, sizeof(root), "%s", roots[i]);
            open_dir(root, 1);
        } else if (i == 3) {
            url_cursor = (int)strlen(url);
            ui_keyboard_edit_init(&kb, UI_EDIT_URL);
            kb.url = true;
            kb.shift = false;
            set_view(V_URL, 1);
        } else {
            set_view(V_LIST, 1);
        }
    } else if (view == V_FILES) {
        char path[FS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, files[i].name);
        if (files[i].dir) {
            open_dir(path, 1);
        } else {
            add_and_play(path);
            open_player();
        }
    } else if (view == V_LIST) {
        resume_index = -1;
        aout_glaze_hold(0);
        playlist_Goto(pl, i);
        poll_state();
        open_player();
    }
}

static void url_key(int ch)
{
    if (ch == '\n') {
        play_url();
        return;
    }
    if (ch == UI_KEY_DOTCOM) {
        for (const char *p = ".com"; *p; p++)
            ui_edit_key(url, sizeof(url), &url_cursor, *p, UI_EDIT_URL);
    } else {
        ui_edit_key(url, sizeof(url), &url_cursor, ch, UI_EDIT_URL);
    }
}

static void url_event(const struct gz_input_event *e)
{
    if (bar_event(e))
        return;
    if (e->type == GZ_EV_TEXT || e->type == GZ_EV_KEY_DOWN) {
        if (ui_input_char(e) == '\n' || (e->type == GZ_EV_KEY_DOWN && e->code == GZ_KEY_ENTER)) {
            play_url();
            return;
        }
        if (ui_edit_event(url, sizeof(url), &url_cursor, e, UI_EDIT_URL))
            redraw();
        return;
    }
    if (ui_has_hardware_keyboard())
        return;
    bool changed = false;
    int ch = ui_keyboard_event(&kb, e, KB_Y, &changed);
    if (ch)
        url_key(ch);
    if (ch || changed)
        redraw();
}

static int64_t scrub_at(int x)
{
    x = x < BAR_X0 ? BAR_X0 : x > BAR_X1 ? BAR_X1 : x;
    return (int64_t)(x - BAR_X0) * st.length_us / (BAR_X1 - BAR_X0);
}

static void control(int i)
{
    if (i == 0)
        playlist_Prev(pl);
    else if (i == 2)
        playlist_Next(pl);
    else
        play_pause();
    poll_state();
}

static void now_event(const struct gz_input_event *e)
{
    if (bar_event(e))
        return;
    gfx_rect vr = VOL_RECT;
    if (e->type == GZ_EV_TOUCH_DOWN) {
        if (abs(e->y - BAR_Y) < 14 && e->x >= BAR_X0 - 12 && e->x <= BAR_X1 + 12 && st.length_us > 0) {
            scrubbing = true;
            scrub_us = scrub_at(e->x);
        } else if (gfx_rect_contains(gfx_r(vr.x - 10, vr.y - 6, vr.w + 20, vr.h + 12), e->x, e->y)) {
            voluming = true;
            set_volume(ui_slider_value(vr, AOUT_VOLUME_DEFAULT * 2, e->x));
        } else {
            for (int i = 0; i < 3; i++)
                if (gfx_rect_contains(ctrl_rect(i), e->x, e->y))
                    pressed_ctrl = i;
        }
        redraw();
    } else if (e->type == GZ_EV_TOUCH_MOVE) {
        if (scrubbing)
            scrub_us = scrub_at(e->x);
        else if (voluming)
            set_volume(ui_slider_value(vr, AOUT_VOLUME_DEFAULT * 2, e->x));
        if (scrubbing || voluming)
            redraw();
    } else if (e->type == GZ_EV_TOUCH_UP) {
        if (scrubbing) {
            scrubbing = false;
            seek_to(scrub_at(e->x));
            poll_state();
        } else if (voluming) {
            voluming = false;
        } else if (pressed_ctrl >= 0) {
            int i = pressed_ctrl;
            pressed_ctrl = -1;
            if (gfx_rect_contains(ctrl_rect(i), e->x, e->y))
                control(i);
        }
        redraw();
    }
}

static void video_event(const struct gz_input_event *e)
{
    if (e->type == GZ_EV_TOUCH_DOWN) {
        if (!overlay) {
            show_overlay();
            redraw();
            return;
        }
        overlay_until = gz_time_ms() + OVERLAY_MS;
        if (gfx_rect_contains(done_rect, e->x, e->y)) {
            pressed_done = true;
        } else if (abs(e->y - OV_BAR_Y) < 12 && e->x >= BAR_X0 - 12 && e->x <= BAR_X1 + 12 &&
                   st.length_us > 0) {
            scrubbing = true;
            scrub_us = scrub_at(e->x);
        } else if (gfx_rect_contains(PANEL, e->x, e->y)) {
            pressed_play = true;
        } else {
            overlay = false;
            vout_glaze_set_overlay(NULL);
        }
        redraw();
    } else if (e->type == GZ_EV_TOUCH_MOVE) {
        if (scrubbing) {
            scrub_us = scrub_at(e->x);
            overlay_until = gz_time_ms() + OVERLAY_MS;
            redraw();
        }
    } else if (e->type == GZ_EV_TOUCH_UP) {
        if (pressed_done) {
            pressed_done = false;
            if (gfx_rect_contains(done_rect, e->x, e->y)) {
                go_back();
                return;
            }
        } else if (scrubbing) {
            scrubbing = false;
            seek_to(scrub_at(e->x));
        } else if (pressed_play) {
            pressed_play = false;
            if (gfx_rect_contains(PANEL, e->x, e->y))
                play_pause();
        }
        overlay_until = gz_time_ms() + OVERLAY_MS;
        redraw();
    }
}

static void key_event(const struct gz_input_event *e)
{
    if (e->type != GZ_EV_KEY_DOWN)
        return;
    bool player = view == V_NOW || view == V_VIDEO;
    switch (e->code) {
    case GZ_KEY_OK:
    case GZ_KEY_ENTER:
        if (st.count) {
            play_pause();
            if (!player)
                open_player();
        }
        break;
    case GZ_KEY_LEFT:
    case GZ_KEY_RIGHT:
        if (player)
            seek_by(e->code == GZ_KEY_LEFT ? -SEEK_STEP : SEEK_STEP);
        break;
    case GZ_KEY_UP:
    case GZ_KEY_DOWN:
        if (player)
            set_volume(volume() + (e->code == GZ_KEY_UP ? AOUT_VOLUME_STEP : -AOUT_VOLUME_STEP));
        break;
    case GZ_KEY_ESC:
        go_back();
        return;
    default:
        return;
    }
    if (view == V_VIDEO)
        show_overlay();
    poll_state();
    redraw();
}

/* The video output appeared or went away. */
static void follow_video(void)
{
    bool active = vout_glaze_active();

    if (active == video_was_active)
        return;
    video_was_active = active;
    if (active && view == V_NOW && !video_dismissed && !in_background)
        set_view(V_VIDEO, 0);
    else if (!active && view == V_VIDEO)
        set_view(V_NOW, 0);
    else if (!active)
        video_dismissed = false;
    else if (view == V_NOW)
        redraw();       /* the Video button */
}

static void deactivate(void)
{
    in_background = true;
    if (playing())
        set_state(PAUSE_S);
    aout_glaze_hold(1);
    vout_glaze_show(0);
    pressed_back = pressed_right = pressed_done = pressed_play = false;
    pressed_ctrl = -1;
    scrubbing = voluming = false;
    poll_state();
    save_state();
}

static void activate(void)
{
    in_background = false;
    poll_state();
    if (view == V_VIDEO) {
        if (vout_glaze_active()) {
            show_overlay();
            vout_glaze_show(1);
        } else {
            view = V_NOW;
        }
    }
    redraw();
}

static void Run(intf_thread_t *p_intf)
{
    intf = p_intf;
    pl = vlc_object_find(p_intf, VLC_OBJECT_PLAYLIST, FIND_ANYWHERE);
    if (!pl) {
        msg_Err(p_intf, "no playlist");
        return;
    }
    snprintf(data_dir, sizeof(data_dir), "%s/data", res_bundle());
    fs_mkdirs(data_dir);
    ui_list_init(&list, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H), ROW_H);
    aout_glaze_hold(1);
    load_state();
    read_autoplay();
    poll_state();
    set_view(V_HOME, 0);
    printf("vlc: interface ready\n");

    uint32_t last_poll = gz_time_ms();
    while (!p_intf->b_die) {
        struct gz_input_event ev;
        bool animating = list.sc.animating && (view == V_HOME || view == V_FILES || view == V_LIST);
        int wait = animating ? 30 : nautoplay ? 100 : POLL_MS;

        if (ui_next_event(&ev, wait)) {
            if (ev.type == GZ_EV_APP_DEACTIVATE) {
                deactivate();
            } else if (ev.type == GZ_EV_APP_ACTIVATE) {
                activate();
            } else if (ev.type == GZ_EV_TOUCH_CANCEL) {
                bool changed;
                pressed_back = pressed_right = pressed_done = pressed_play = false;
                pressed_ctrl = -1;
                scrubbing = voluming = false;
                ui_list_event(&list, &ev, &changed);
                redraw();
            } else if (view == V_URL) {
                url_event(&ev);
            } else if (ev.type == GZ_EV_KEY_DOWN || ev.type == GZ_EV_KEY_UP) {
                key_event(&ev);
            } else if (view == V_NOW) {
                now_event(&ev);
            } else if (view == V_VIDEO) {
                video_event(&ev);
            } else {
                list_event(&ev);
            }
        } else if (animating && ui_scroll_step(&list.sc)) {
            redraw();
        }

        if (nautoplay)
            start_autoplay();
        if (gz_time_ms() - last_poll >= (animating ? 100u : (unsigned)POLL_MS)) {
            last_poll = gz_time_ms();
            bool changed = poll_state();
            if (aout_glaze_taken() && st.state == PLAYING_S) {
                printf("vlc: the music player took the audio output\n");
                set_state(PAUSE_S);
                poll_state();
                changed = true;
            }
            hold_audio();
            follow_video();
            if (view == V_VIDEO && overlay && playing() && gz_time_ms() >= overlay_until && !scrubbing &&
                !pressed_done && !pressed_play) {
                overlay = false;
                vout_glaze_set_overlay(NULL);
                redraw();       /* clear where the controls were */
            }
            if (view == V_LIST && list.count != st.count) {
                ui_list_set_count(&list, st.count);
                changed = true;
            }
            /* The video view redraws with each picture; while paused, here. */
            if (changed && !scrubbing && !voluming && (view != V_VIDEO || !playing()))
                redraw();
        }
    }
    vlc_object_release(pl);
}
