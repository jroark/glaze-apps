/* Maps: offline tile packs (.gzmap in /Maps or /Card/Maps) with the GPS
 * receiver's position (glaze/gps.h), satellite status and track recording.
 *
 * Positions are Web Mercator "world" coordinates, 2^32 across; at zoom z a
 * screen pixel is 2^(24 - z) world units. Past a pack's deepest zoom its
 * tiles are magnified; a missing tile is drawn from a coarser one.
 */
#include "gzmap.h"
#include "track.h"
#include <glaze/fs.h>
#include <glaze/gps.h>
#include <glaze/sys.h>
#include <glaze/ui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAP_Y       UI_NAV_H
#define BAR_H       38
#define MAP_H       (UI_H - MAP_Y - BAR_H)
#define MAP_CX      (UI_W / 2)
#define MAP_CY      (MAP_Y + MAP_H / 2)
#define ZOOM_MIN    2
#define ZOOM_MAX    20
#define OVERZOOM    2           /* zoom levels past a pack's deepest */
#define PACKS_MAX   16
#define LOADS_PER_FRAME 2       /* tile decodes per redraw, to stay responsive */

enum { V_MAP, V_GPS, V_PACKS };
enum { B_NONE = -1, B_LEFT, B_RIGHT, B_FOLLOW, B_OUT, B_IN, B_REC, B_BACK };

static int view;
static struct gzmap map;
static bool have_map;
static struct gzmap_cache cache;
static char pack_path[FS_PATH_MAX];
static struct { char path[FS_PATH_MAX]; char name[64]; uint32_t size; } packs[PACKS_MAX];
static int npacks;
static struct ui_list pack_list;

static uint32_t cx = 0x80000000u, cy = 0x80000000u;
static int zoom = ZOOM_MIN;
static bool follow = true, tiles_pending;

static struct gps_fix fix;
static bool fix_valid;
static uint32_t last_poll, last_seq;
static struct track track;
static char notice[96];
static uint32_t notice_until;

static gfx_rect btn[B_BACK + 1];
static int armed = B_NONE;
static bool dragging, touching;
static int touch_x, touch_y, last_x, last_y;
static uint32_t last_tap_ms;
static int last_tap_x, last_tap_y;

static void say(const char *text)
{
    snprintf(notice, sizeof(notice), "%s", text);
    notice_until = gz_time_ms() + 4000;
}

static uint32_t upp(void) { return 1u << (24 - zoom); }   /* world units per pixel */

/* --- packs --- */

static void find_packs(const char *dir)
{
    struct fs_dirent ents[16];
    uint32_t skip = 0;
    for (;;) {
        int n = fs_list(dir, skip, ents, 16);
        if (n <= 0) return;
        for (int i = 0; i < n && npacks < PACKS_MAX; i++) {
            const char *dot = strrchr(ents[i].name, '.');
            if (ents[i].type != FS_FILE || !dot || strcasecmp(dot, ".gzmap")) continue;
            snprintf(packs[npacks].path, sizeof(packs[npacks].path), "%s/%s", dir, ents[i].name);
            snprintf(packs[npacks].name, sizeof(packs[npacks].name), "%.*s", (int)(dot - ents[i].name),
                     ents[i].name);
            packs[npacks].size = ents[i].size;
            npacks++;
        }
        skip += (uint32_t)n;
        if (n < 16) return;
    }
}

static void scan_packs(void)
{
    npacks = 0;
    find_packs("/Maps");
    find_packs("/Card/Maps");
    ui_list_reset(&pack_list, npacks);
}

static void center_on(int32_t lat, int32_t lon) { gps_project(lat, lon, &cx, &cy); }

static bool open_pack(const char *path, bool recenter)
{
    if (have_map) { gzmap_close(&map); have_map = false; }
    gzmap_cache_flush(&cache);
    pack_path[0] = 0;
    int rv = gzmap_open(&map, path);
    if (rv) {
        char text[96];
        snprintf(text, sizeof(text), rv == GZ_ENOMEM ? "Not enough memory for that map." : "Can't read that map.");
        say(text);
        return false;
    }
    have_map = true;
    snprintf(pack_path, sizeof(pack_path), "%s", path);
    if (recenter) {
        center_on((int32_t)(((int64_t)map.south + map.north) / 2), (int32_t)(((int64_t)map.west + map.east) / 2));
        zoom = (map.min_zoom + map.max_zoom + 1) / 2;
    }
    if (zoom < map.min_zoom) zoom = map.min_zoom;
    if (zoom > map.max_zoom + OVERZOOM) zoom = map.max_zoom + OVERZOOM;
    return true;
}

static bool pack_contains(int i, int32_t lat, int32_t lon)
{
    struct gzmap m;
    if (gzmap_open(&m, packs[i].path)) return false;
    bool in = lat >= m.south && lat <= m.north && lon >= m.west && lon <= m.east;
    gzmap_close(&m);
    return in;
}

/* --- saved state --- */

static void state_path(char *out, size_t n) { snprintf(out, n, "%s/data/state", res_bundle()); }

static void save_state(void)
{
    char path[FS_PATH_MAX], dir[FS_PATH_MAX], data[FS_PATH_MAX + 96];
    int n = snprintf(data, sizeof(data), "pack=%s\nzoom=%d\nx=%u\ny=%u\nfollow=%d\n", pack_path, zoom,
                     (unsigned)cx, (unsigned)cy, follow);
    snprintf(dir, sizeof(dir), "%s/data", res_bundle());
    fs_mkdirs(dir);
    state_path(path, sizeof(path));
    fs_write_file(path, data, (uint32_t)n);
}

static bool load_state(void)
{
    char path[FS_PATH_MAX], data[FS_PATH_MAX + 96];
    state_path(path, sizeof(path));
    int n = fs_read_file(path, data, sizeof(data) - 1);
    if (n <= 0) return false;
    data[n] = 0;
    char pack[FS_PATH_MAX] = "";
    for (char *s = data; s && *s;) {
        char *end = strchr(s, '\n');
        if (end) *end++ = 0;
        if (!strncmp(s, "pack=", 5)) snprintf(pack, sizeof(pack), "%s", s + 5);
        else if (!strncmp(s, "zoom=", 5)) zoom = atoi(s + 5);
        else if (!strncmp(s, "x=", 2)) cx = (uint32_t)strtoul(s + 2, NULL, 10);
        else if (!strncmp(s, "y=", 2)) cy = (uint32_t)strtoul(s + 2, NULL, 10);
        else if (!strncmp(s, "follow=", 7)) follow = atoi(s + 7) != 0;
        s = end;
    }
    if (zoom < ZOOM_MIN || zoom > ZOOM_MAX) zoom = ZOOM_MIN;
    if (pack[0]) open_pack(pack, false);
    return true;
}

/* --- GPS --- */

static bool poll_gps(bool force)
{
    uint32_t now = gz_time_ms();
    if (!force && now - last_poll < 1000) return false;
    last_poll = now;
    struct gps_fix old = fix;
    fix_valid = gps_read(&fix) && fix.age_ms < 5000;
    bool changed = memcmp(&old, &fix, sizeof(fix)) != 0;
    if (fix.sequence != last_seq) {
        last_seq = fix.sequence;
        if (fix_valid) {
            if (track.recording && track_add(&track, &fix)) changed = true;
            if (track.full && track.recording) say("Track is full; stop to save it.");
            /* No map here yet: switch to a pack that covers the position. */
            if (!have_map || fix.lat < map.south || fix.lat > map.north || fix.lon < map.west || fix.lon > map.east)
                for (int i = 0; i < npacks; i++)
                    if (strcmp(packs[i].path, pack_path) && pack_contains(i, fix.lat, fix.lon)) {
                        open_pack(packs[i].path, false);
                        break;
                    }
            if (follow) center_on(fix.lat, fix.lon);
        }
    }
    return changed;
}

/* --- drawing --- */

static int screen_x(uint32_t wx) { return MAP_CX + (int)((int32_t)(wx - cx) / (int32_t)upp()); }
static int screen_y(uint32_t wy) { return MAP_CY + (int)((int32_t)(wy - cy) / (int32_t)upp()); }

static void draw_blank(gfx_surface *s, gfx_rect r)
{
    gfx_fill(s, r, 0xE9E5DC);
    /* A faint 32-pixel grid, fixed to the world so it pans with the map. */
    uint32_t u = upp();
    uint32_t wx0 = cx - (uint32_t)(MAP_CX - r.x) * u, wy0 = cy - (uint32_t)(MAP_CY - r.y) * u;
    int ox = (int)((32u - (wx0 / u) % 32u) % 32u), oy = (int)((32u - (wy0 / u) % 32u) % 32u);
    for (int x = r.x + ox; x < r.x + r.w; x += 32) gfx_fill(s, gfx_r(x, r.y, 1, r.h), 0xD6D1C6);
    for (int y = r.y + oy; y < r.y + r.h; y += 32) gfx_fill(s, gfx_r(r.x, y, r.w, 1), 0xD6D1C6);
}

/* Draws tile (tz, tx, ty) at screen rect d (256 << magnify pixels), from
 * the tile itself or the nearest coarser one in the cache or the pack. */
static void draw_tile(gfx_surface *s, unsigned tz, uint32_t tx, uint32_t ty, gfx_rect d, int *loads)
{
    for (unsigned k = 0; k <= tz - map.min_zoom && k < 8; k++) {
        unsigned z = tz - k;
        uint32_t x = tx >> k, y = ty >> k;
        const gfx_surface *t = gzmap_tile_cached(&cache, &map, z, x, y);
        if (!t && gzmap_find(&map, z, x, y)) {
            if (*loads < LOADS_PER_FRAME) { (*loads)++; t = gzmap_tile_load(&cache, &map, z, x, y); }
            else tiles_pending = true;
        }
        if (!t) continue;
        int sub = GZMAP_TILE >> k;
        gfx_rect sr = gfx_r((int)(tx & ((1u << k) - 1)) * sub, (int)(ty & ((1u << k) - 1)) * sub, sub, sub);
        if (sr.w == d.w) gfx_blit(s, d.x, d.y, t, sr);
        else gfx_blit_scaled(s, d, t, sr, 255);
        return;
    }
    draw_blank(s, d);
}

static void draw_tiles(gfx_surface *s, gfx_rect area)
{
    tiles_pending = false;
    if (!have_map || zoom < map.min_zoom) { draw_blank(s, area); return; }
    unsigned tz = (unsigned)(zoom > map.max_zoom ? map.max_zoom : zoom);
    int size = GZMAP_TILE << (zoom - (int)tz);              /* screen pixels per tile */
    unsigned shift = 32 - tz;                               /* world units per tile, log2 */
    uint32_t wx0 = cx - (uint32_t)MAP_CX * upp(), wy0 = cy - (uint32_t)(MAP_CY - MAP_Y) * upp();
    uint32_t tx0 = (uint32_t)((uint64_t)wx0 >> shift), ty0 = (uint32_t)((uint64_t)wy0 >> shift);
    int sx0 = -(int)(((uint64_t)wx0 - ((uint64_t)tx0 << shift)) >> (24 - zoom));
    int sy0 = MAP_Y - (int)(((uint64_t)wy0 - ((uint64_t)ty0 << shift)) >> (24 - zoom));
    uint32_t tiles = tz ? 1u << tz : 1;
    int loads = 0;
    /* Middle out, so the tiles near the position decode first. */
    int cols = (UI_W - sx0 + size - 1) / size, rows = (MAP_Y + MAP_H - sy0 + size - 1) / size;
    for (int ring = 0; ring <= cols + rows; ring++)
        for (int j = 0; j < rows; j++)
            for (int i = 0; i < cols; i++) {
                int di = i - cols / 2, dj = j - rows / 2;
                if ((di < 0 ? -di : di) + (dj < 0 ? -dj : dj) != ring) continue;
                uint32_t tx = (tx0 + (uint32_t)i) & (tiles - 1), ty = ty0 + (uint32_t)j;
                gfx_rect d = gfx_r(sx0 + i * size, sy0 + j * size, size, size);
                if (ty >= tiles) draw_blank(s, d);
                else draw_tile(s, tz, tx, ty, d, &loads);
            }
}

static void draw_track(gfx_surface *s)
{
    if (track.count < 2) return;
    uint32_t px, py;
    gps_project(track.points[0].lat, track.points[0].lon, &px, &py);
    int x0 = screen_x(px), y0 = screen_y(py);
    for (unsigned i = 1; i < track.count; i++) {
        gps_project(track.points[i].lat, track.points[i].lon, &px, &py);
        int x1 = screen_x(px), y1 = screen_y(py);
        if ((x0 != x1 || y0 != y1) && x0 > -500 && x0 < 740 && y0 > -500 && y0 < 800)
            gfx_line_aa(s, x0 << 4, y0 << 4, x1 << 4, y1 << 4, 3 << 4, 0xD63E36, 220);
        x0 = x1; y0 = y1;
    }
}

static void draw_position(gfx_surface *s)
{
    if (!fix_valid && !fix.sequence) return;
    if (fix.lat == 0 && fix.lon == 0) return;
    uint32_t px, py;
    gps_project(fix.lat, fix.lon, &px, &py);
    int x = screen_x(px), y = screen_y(py);
    uint32_t colour = fix_valid ? 0x1E78E6 : 0x8A8F98;
    /* Accuracy: HDOP times a typical 6 m user range error. */
    uint32_t mm = gps_mm_per_pixel(fix.lat, (unsigned)zoom);
    if (fix_valid && fix.hdop_c && mm) {
        int r = (int)((uint64_t)fix.hdop_c * 60u / mm);     /* hdop/100 * 6000 mm */
        if (r > 10 && r < 400) gfx_circle(s, x, y, r, colour, 50);
    }
    if (fix_valid && fix.course_valid && fix.speed_cms > 50) {
        /* Heading: a short line ahead of the dot. sin/cos from a table of eighths is enough here. */
        static const int dx[16] = { 0, 6, 11, 15, 16, 15, 11, 6, 0, -6, -11, -15, -16, -15, -11, -6 };
        static const int dy[16] = { -16, -15, -11, -6, 0, 6, 11, 15, 16, 15, 11, 6, 0, -6, -11, -15 };
        unsigned k = ((fix.course_cdeg + 1125) / 2250) % 16;
        gfx_line_aa(s, x << 4, y << 4, (x + dx[k] + dx[k] / 2) << 4, (y + dy[k] + dy[k] / 2) << 4, 3 << 4,
                    colour, 255);
    }
    gfx_circle(s, x, y, 8, 0xFFFFFF, 255);
    gfx_circle(s, x, y, 6, colour, 255);
}

static void draw_scale(gfx_surface *s)
{
    int32_t lat = fix_valid ? fix.lat : 0;
    if (!fix_valid) {
        /* The latitude at the centre, from the world y: good enough via the pack's middle. */
        if (have_map) lat = (int32_t)(((int64_t)map.south + map.north) / 2);
    }
    uint32_t mm = gps_mm_per_pixel(lat, (unsigned)zoom);
    if (!mm) return;
    static const uint32_t steps[] = { 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000,
                                      100000, 200000, 500000, 1000000 };
    uint32_t metres = steps[0];
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
        if ((uint64_t)steps[i] * 1000 / mm <= 80) metres = steps[i];
    int w = (int)((uint64_t)metres * 1000 / mm);
    int x = 8, y = MAP_Y + MAP_H - 14;
    char label[24];
    if (metres >= 1000) snprintf(label, sizeof(label), "%u km", metres / 1000);
    else snprintf(label, sizeof(label), "%u m", metres);
    gfx_fill_alpha(s, gfx_r(x - 3, y - 13, w + 6, 18), 0xFFFFFF, 150);
    gfx_fill(s, gfx_r(x, y, w, 2), 0x333333);
    gfx_fill(s, gfx_r(x, y - 4, 1, 6), 0x333333);
    gfx_fill(s, gfx_r(x + w - 1, y - 4, 1, 6), 0x333333);
    gfx_text(s, x + 1, y - 12, ui_font(FONT_B9), label, 0x333333, 255);
}

static void draw_bar(gfx_surface *s)
{
    gfx_rect bar = gfx_r(0, UI_H - BAR_H, UI_W, BAR_H);
    gfx_vgradient(s, bar, UI_NAV_TOP, UI_NAV_BOTTOM, 255);
    gfx_fill(s, gfx_r(0, bar.y, UI_W, 1), UI_NAV_LINE);
    int y = bar.y + 6, h = BAR_H - 12;
    btn[B_FOLLOW] = gfx_r(6, y, 64, h);
    btn[B_OUT] = gfx_r(76, y, 40, h);
    btn[B_IN] = gfx_r(122, y, 40, h);
    btn[B_REC] = gfx_r(168, y, 66, h);
    ui_draw_button(s, btn[B_FOLLOW], follow ? "Following" : "Locate", armed == B_FOLLOW || follow);
    ui_draw_button(s, btn[B_OUT], "-", armed == B_OUT);
    ui_draw_button(s, btn[B_IN], "+", armed == B_IN);
    ui_draw_button(s, btn[B_REC], track.recording ? "Stop" : "Record", armed == B_REC || track.recording);
}

static void draw_map(gfx_surface *s)
{
    gfx_rect area = gfx_r(0, MAP_Y, UI_W, MAP_H);
    gfx_set_clip(s, area);
    draw_tiles(s, area);
    draw_track(s);
    draw_position(s);
    draw_scale(s);
    const gfx_font *small = ui_font(FONT_R9);
    if (have_map && map.attribution[0]) {
        int w = gfx_text_width(small, map.attribution);
        if (w > UI_W - 8) w = UI_W - 8;
        gfx_fill_alpha(s, gfx_r(UI_W - w - 6, MAP_Y + MAP_H - 13, w + 6, 13), 0xFFFFFF, 170);
        gfx_text_fit(s, UI_W - w - 3, MAP_Y + MAP_H - 12, w, small, map.attribution, 0x333333, 255);
    }
    const char *status = NULL;
    if (fix.state == GPS_ABSENT) status = "No GPS card";
    else if (fix.state == GPS_SEARCHING) status = "Finding the GPS receiver...";
    else if (!fix_valid) status = "Acquiring satellites...";
    if (notice[0] && gz_time_ms() < notice_until) status = notice;
    else if (!have_map && !status) status = npacks ? "Pick a map" : "No maps: copy a .gzmap to /Maps";
    if (status) {
        const gfx_font *f = ui_font(FONT_B11);
        int w = gfx_text_width(f, status) + 16;
        if (w > UI_W - 12) w = UI_W - 12;
        gfx_round_rect(s, gfx_r((UI_W - w) / 2, MAP_Y + 6, w, 20), 9, 0x000000, 150);
        gfx_text_center(s, (UI_W - w) / 2 + 4, MAP_Y + 9, w - 8, f, status, 0xFFFFFF, 255);
    }
    if (track.recording) {
        char text[48];
        snprintf(text, sizeof(text), "REC %u.%02u km", track.length_m / 1000, track.length_m % 1000 / 10);
        gfx_round_rect(s, gfx_r(UI_W - 92, MAP_Y + 30, 86, 18), 8, 0xD63E36, 220);
        gfx_text_center(s, UI_W - 92, MAP_Y + 32, 86, ui_font(FONT_B10), text, 0xFFFFFF, 255);
    }
    gfx_set_clip(s, gfx_r(0, 0, UI_W, UI_H));
    draw_bar(s);
}

static void degrees_text(char *out, size_t n, int32_t v, char pos, char neg)
{
    uint32_t a = v < 0 ? (uint32_t)-(int64_t)v : (uint32_t)v;
    snprintf(out, n, "%u.%05u %c", a / 10000000u, a % 10000000u / 100u, v < 0 ? neg : pos);
}

static void draw_gps(gfx_surface *s)
{
    ui_draw_pinstripes(s, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H));
    char rows[9][2][40];
    int n = 0;
#define ROW(k, ...) do { snprintf(rows[n][0], 40, "%s", k); snprintf(rows[n][1], 40, __VA_ARGS__); n++; } while (0)
    if (fix.state == GPS_ABSENT) ROW("Receiver", "No GPS card");
    else if (fix.state == GPS_SEARCHING) ROW("Receiver", "Searching");
    else ROW("Receiver", "NMEA, %u baud", (unsigned)fix.baud);
    ROW("Fix", "%s", fix.fix == GPS_FIX_3D ? "3D" : fix.fix == GPS_FIX_2D ? "2D" : "None");
    ROW("Satellites", "%u used, %u in view", fix.sats_used, fix.sats_in_view);
    if (fix.fix != GPS_FIX_NONE) {
        char a[24];
        degrees_text(a, sizeof(a), fix.lat, 'N', 'S'); ROW("Latitude", "%s", a);
        degrees_text(a, sizeof(a), fix.lon, 'E', 'W'); ROW("Longitude", "%s", a);
        int32_t alt = fix.altitude_dm;
        ROW("Altitude", "%s%d m", alt < 0 && alt > -10 ? "-" : "", (int)(alt / 10));
        uint32_t kmh10 = fix.speed_cms * 36 / 100;
        ROW("Speed", "%u.%u km/h", kmh10 / 10, kmh10 % 10);
        if (fix.hdop_c) ROW("Accuracy", "HDOP %u.%02u", (unsigned)fix.hdop_c / 100, (unsigned)fix.hdop_c % 100);
    }
    if (fix.year) ROW("Time (UTC)", "%04u-%02u-%02u %02u:%02u:%02u", fix.year, fix.month, fix.day, fix.hour,
                      fix.minute, fix.second);
#undef ROW
    int y = UI_NAV_H + 8;
    for (int i = 0; i < n; i++) {
        gfx_rect r = gfx_r(8, y + i * 22, UI_W - 16, 22);
        ui_draw_group_cell(s, r, ui_group_pos(i, n), false);
        gfx_text(s, r.x + 8, r.y + 4, ui_font(FONT_B11), rows[i][0], UI_TEXT, 255);
        int w = gfx_text_width(ui_font(FONT_R11), rows[i][1]);
        gfx_text(s, r.x + r.w - 8 - w, r.y + 4, ui_font(FONT_R11), rows[i][1], UI_TEXT_DETAIL, 255);
    }
    /* Signal strength per satellite, green when used in the fix. */
    int top = y + n * 22 + 10, base = UI_H - 18, nsat = 0;
    while (nsat < (int)GPS_SATS_MAX && fix.sats[nsat].prn) nsat++;
    if (!nsat) {
        gfx_text_center(s, 0, top + 20, UI_W, ui_font(FONT_R11), "No satellites in view", UI_TEXT_DETAIL, 255);
        return;
    }
    int shown = nsat > 12 ? 12 : nsat, w = (UI_W - 16) / shown;
    for (int i = 0; i < shown; i++) {
        const struct gps_satellite *sat = &fix.sats[i];
        int snr = sat->snr < 0 ? 0 : sat->snr, h = snr * (base - top - 4) / 50;
        if (h > base - top) h = base - top;
        int x = 8 + i * w;
        gfx_fill(s, gfx_r(x + 2, base - h, w - 4, h ? h : 1), sat->used ? 0x3FA34D : sat->snr < 0 ? 0xB0B4BA : 0x7A8796);
        char prn[8];
        snprintf(prn, sizeof(prn), "%u", sat->prn);
        gfx_text_center(s, x, base + 3, w, ui_font(FONT_R9), prn, UI_TEXT, 255);
    }
}

static void draw_pack_row(gfx_surface *s, int i, gfx_rect r, bool selected, void *ctx)
{
    (void)ctx;
    bool current = !strcmp(packs[i].path, pack_path);
    ui_draw_list_row(s, r, selected);
    uint32_t text = selected ? 0xFFFFFF : UI_TEXT;
    gfx_text_fit(s, 10, r.y + 6, UI_W - 40, ui_font(FONT_B14), packs[i].name, text, 255);
    char detail[96];
    snprintf(detail, sizeof(detail), "%s, %u KB", packs[i].path, (unsigned)(packs[i].size / 1024));
    gfx_text_fit(s, 10, r.y + 27, UI_W - 40, ui_font(FONT_R11), detail, selected ? 0xFFFFFF : UI_TEXT_DETAIL, 255);
    if (current) gfx_text(s, UI_W - 24, r.y + 14, ui_font(FONT_B14), GFX_CH_CHECK, selected ? 0xFFFFFF : UI_BLUE_BUTTON, 255);
}

static void draw_packs(gfx_surface *s)
{
    gfx_fill(s, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H), 0xFFFFFF);
    if (!npacks) {
        gfx_text_center(s, 10, 110, UI_W - 20, ui_font(FONT_B14), "No maps", UI_TEXT, 255);
        gfx_text_center(s, 10, 134, UI_W - 20, ui_font(FONT_R11), "Copy .gzmap tile packs to /Maps", UI_TEXT_DETAIL, 255);
        gfx_text_center(s, 10, 150, UI_W - 20, ui_font(FONT_R11), "or /Card/Maps (tools/mkmaptiles.py).", UI_TEXT_DETAIL, 255);
        return;
    }
    ui_list_draw(s, &pack_list, draw_pack_row, NULL);
}

static void draw(void)
{
    gfx_surface *s = &ui.surf;
    gfx_set_clip(s, gfx_r(0, 0, UI_W, UI_H));
    for (int i = 0; i <= B_BACK; i++) btn[i] = gfx_r(0, 0, 0, 0);
    if (view == V_MAP) {
        draw_map(s);
        /* The pack's name, if it fits between the bar buttons (libui's titles are B14). */
        const char *title = have_map && map.name[0] ? map.name : "Maps";
        if (gfx_text_width(ui_font(FONT_B14), title) > UI_W - 2 * 64) title = "Maps";
        ui_draw_navbar(s, title, NULL, NULL, 0);
        btn[B_LEFT] = ui_draw_bar_button(s, "Maps", false, armed == B_LEFT, false, 0);
        btn[B_RIGHT] = ui_draw_bar_button(s, "GPS", true, armed == B_RIGHT, false, 0);
    } else if (view == V_GPS) {
        draw_gps(s);
        ui_draw_navbar(s, "GPS", "Map", &btn[B_BACK], 0);
    } else {
        draw_packs(s);
        ui_draw_navbar(s, "Maps", "Map", &btn[B_BACK], 0);
        btn[B_RIGHT] = ui_draw_bar_button(s, "Refresh", true, armed == B_RIGHT, false, 0);
    }
    ui_invalidate_all();
    ui_present();
}

/* --- input --- */

static void set_zoom(int z, int fx, int fy)
{
    int lo = have_map ? map.min_zoom : ZOOM_MIN, hi = have_map ? map.max_zoom + OVERZOOM : ZOOM_MAX;
    if (lo < ZOOM_MIN) lo = ZOOM_MIN;
    if (z < lo) z = lo;
    if (z > hi) z = hi;
    if (z == zoom) return;
    /* Keep the world point under (fx, fy) where it is. */
    uint32_t wx = cx + (uint32_t)((fx - MAP_CX) * (int32_t)upp()), wy = cy + (uint32_t)((fy - MAP_CY) * (int32_t)upp());
    zoom = z;
    cx = wx - (uint32_t)((fx - MAP_CX) * (int32_t)upp());
    cy = wy - (uint32_t)((fy - MAP_CY) * (int32_t)upp());
}

static void pan(int dx, int dy)
{
    cx -= (uint32_t)(dx * (int32_t)upp());
    cy -= (uint32_t)(dy * (int32_t)upp());
    follow = false;
}

static void toggle_record(void)
{
    if (!track.recording) {
        if (!track_start(&track)) { say("Not enough memory to record."); return; }
        if (fix_valid) track_add(&track, &fix);
        ui_set_background_running(true);
        say("Recording track");
        return;
    }
    track_stop(&track);
    ui_set_background_running(false);
    char path[FS_PATH_MAX], text[96];
    if (track.count < 2) { say("Track too short to save."); return; }
    int rv = track_save(&track, path, sizeof(path));
    if (rv) snprintf(text, sizeof(text), "Couldn't save the track (%d).", rv);
    else snprintf(text, sizeof(text), "Saved %s", strrchr(path, '/') + 1);
    say(text);
}

static void action(int b)
{
    switch (b) {
    case B_LEFT: view = V_PACKS; scan_packs(); break;
    case B_RIGHT:
        if (view == V_PACKS) { scan_packs(); break; }
        view = V_GPS; poll_gps(true); break;
    case B_BACK: view = V_MAP; break;
    case B_FOLLOW:
        follow = true;
        if (fix_valid) center_on(fix.lat, fix.lon);
        else say(fix.state == GPS_ABSENT ? "Insert the GPS card." : "Waiting for a position fix.");
        break;
    case B_OUT: set_zoom(zoom - 1, MAP_CX, MAP_CY); break;
    case B_IN: set_zoom(zoom + 1, MAP_CX, MAP_CY); break;
    case B_REC: toggle_record(); break;
    }
}

static int hit(int x, int y)
{
    for (int i = 0; i <= B_BACK; i++)
        if (btn[i].w && gfx_rect_contains(btn[i], x, y)) return i;
    return B_NONE;
}

static bool key(const struct gz_input_event *e)
{
    if (view != V_MAP) {
        if (e->code == GZ_KEY_ESC) { view = V_MAP; return true; }
        if (view == V_PACKS && e->code == GZ_KEY_OK && npacks) {
            open_pack(packs[0].path, true); view = V_MAP; return true;
        }
        return false;
    }
    switch (e->code) {
    case GZ_KEY_UP: pan(0, 48); return true;
    case GZ_KEY_DOWN: pan(0, -48); return true;
    case GZ_KEY_LEFT: pan(48, 0); return true;
    case GZ_KEY_RIGHT: pan(-48, 0); return true;
    case GZ_KEY_OK: action(B_FOLLOW); return true;
    case GZ_KEY_PAGEUP: set_zoom(zoom + 1, MAP_CX, MAP_CY); return true;
    case GZ_KEY_PAGEDOWN: set_zoom(zoom - 1, MAP_CX, MAP_CY); return true;
    }
    return false;
}

static bool input(const struct gz_input_event *e)
{
    if (e->type == GZ_EV_TOUCH_CANCEL || e->type == GZ_EV_APP_DEACTIVATE) {
        armed = B_NONE; touching = dragging = false;
        if (view == V_PACKS) { bool d; struct gz_input_event c = { .type = GZ_EV_TOUCH_CANCEL }; ui_list_event(&pack_list, &c, &d); }
        if (e->type == GZ_EV_APP_DEACTIVATE) save_state();
        return true;
    }
    if (e->type == GZ_EV_KEY_DOWN) return key(e);
    bool dirty = false;
    if (view == V_PACKS) {
        int row = ui_list_event(&pack_list, e, &dirty);
        if (row >= 0 && row < npacks) {
            open_pack(packs[row].path, true);
            follow = false;
            view = V_MAP;
            return true;
        }
    }
    if (e->type == GZ_EV_TOUCH_DOWN) {
        armed = hit(e->x, e->y);
        touching = armed == B_NONE && view == V_MAP && e->y >= MAP_Y && e->y < MAP_Y + MAP_H;
        dragging = false;
        touch_x = last_x = e->x; touch_y = last_y = e->y;
        return armed != B_NONE || dirty;
    }
    if (e->type == GZ_EV_TOUCH_MOVE && touching) {
        if (!dragging && (abs(e->x - touch_x) > 6 || abs(e->y - touch_y) > 6)) dragging = true;
        if (dragging) { pan(e->x - last_x, e->y - last_y); last_x = e->x; last_y = e->y; return true; }
        return dirty;
    }
    if (e->type == GZ_EV_TOUCH_UP) {
        int b = armed;
        armed = B_NONE;
        if (b != B_NONE && b == hit(e->x, e->y)) action(b);
        if (touching && !dragging) {
            /* Double tap: zoom in there. */
            uint32_t now = e->time_ms;
            if (now - last_tap_ms < 400 && abs(e->x - last_tap_x) < 24 && abs(e->y - last_tap_y) < 24) {
                set_zoom(zoom + 1, e->x, e->y);
                follow = false;
                last_tap_ms = 0;
            } else {
                last_tap_ms = now; last_tap_x = e->x; last_tap_y = e->y;
            }
        }
        touching = dragging = false;
        return true;
    }
    return dirty;
}

int main(void)
{
    ui_init(SB_STATUS_GRAY);
    if (!gzmap_cache_init(&cache)) return 1;
    map.fd = -1;
    ui_list_init(&pack_list, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H), 50);
    scan_packs();
    if (!load_state() || !have_map) {
        if (npacks) open_pack(packs[0].path, true);
    }
    poll_gps(true);
    if (fix_valid && follow) center_on(fix.lat, fix.lon);
    draw();
    for (;;) {
        struct gz_input_event e;
        bool animating = view == V_PACKS && pack_list.sc.animating;
        int got = ui_next_event(&e, animating || tiles_pending ? 16 : 250);
        bool dirty = false;
        if (got) {
            if (e.type == GZ_EV_APP_ACTIVATE) { poll_gps(true); scan_packs(); dirty = true; }
            else dirty = input(&e);
        }
        if (poll_gps(false) && view != V_PACKS) dirty = true;
        if (ui_in_background()) continue;
        if (view == V_PACKS && ui_scroll_step(&pack_list.sc)) dirty = true;
        if (notice[0] && gz_time_ms() >= notice_until) { notice[0] = 0; dirty = true; }
        if (tiles_pending) dirty = true;
        if (dirty) draw();
    }
}
