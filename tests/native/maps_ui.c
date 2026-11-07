/* Maps against a host directory as its file system and a fake GPS: pack
 * discovery and reading, tile drawing and fallback, following the fix,
 * panning and zooming, the GPS page, and recording a track to GPX.
 * MAPS_FS is a directory holding Maps/demo.gzmap (glaze-os
 * tests/maptiles_test.py --keep makes one). */
#define main maps_app_main
#include "../../packages/maps/main.c"
#undef main
#include "hostres.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

struct ui_app ui;
static uint16_t pixels[UI_W * UI_H];
static const char *root;
static uint32_t now_ms;
static bool background_running;
uint32_t gz_time_ms(void) { return now_ms += 16; }
int gz_log(const char *s, uint32_t n) { return (int)n; }
void ui_init(int style) { (void)style; }
bool ui_in_background(void) { return false; }
void ui_set_background_running(bool on) { background_running = on; }
int ui_next_event(struct gz_input_event *e, int timeout) { return 0; }
void ui_invalidate_all(void) {}
void ui_present(void) {}
int ui_ease_out(uint32_t t, uint32_t duration) { return t >= duration ? 1024 : (int)(t * 1024 / duration); }
const char *res_bundle(void) { return "/Applications/maps.app"; }

/* --- the file system: paths under root --- */
static void host_path(char *out, const char *path) { snprintf(out, 1024, "%s%s", root, path); }
int fs_open(const char *path, int flags)
{
    char p[1024]; host_path(p, path);
    int fd = open(p, (flags & FS_O_WRITE ? O_RDWR : O_RDONLY) | (flags & FS_O_CREATE ? O_CREAT : 0) |
                     (flags & FS_O_TRUNC ? O_TRUNC : 0), 0644);
    return fd < 0 ? GZ_ENOENT : fd;
}
int fs_read(int fd, void *buf, uint32_t len) { ssize_t n = read(fd, buf, len); return n < 0 ? GZ_EIO : (int)n; }
int fs_seek(int fd, int32_t off, int whence) { off_t r = lseek(fd, off, whence); return r < 0 ? GZ_EINVAL : (int)r; }
int fs_close(int fd) { return close(fd) ? GZ_EINVAL : 0; }
int fs_stat(const char *path, struct fs_stat *st)
{
    char p[1024]; host_path(p, path);
    struct stat s;
    if (stat(p, &s)) return GZ_ENOENT;
    st->type = S_ISDIR(s.st_mode) ? FS_DIR : FS_FILE; st->size = (uint32_t)s.st_size;
    return 0;
}
int fs_list(const char *path, uint32_t skip, struct fs_dirent *out, int max)
{
    char p[1024]; host_path(p, path);
    DIR *d = opendir(p);
    if (!d) return GZ_ENOENT;
    int n = 0; uint32_t seen = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.' || seen++ < skip) continue;
        char q[2048]; snprintf(q, sizeof(q), "%s/%s", p, e->d_name);
        struct stat s; stat(q, &s);
        memset(&out[n], 0, sizeof(out[n]));
        out[n].type = S_ISDIR(s.st_mode) ? FS_DIR : FS_FILE; out[n].size = (uint32_t)s.st_size;
        snprintf(out[n].name, sizeof(out[n].name), "%s", e->d_name);
        out[n].name_len = (uint8_t)strlen(out[n].name);
        n++;
    }
    closedir(d);
    return n;
}
int fs_mkdirs(const char *path)
{
    char p[1024]; host_path(p, path);
    for (char *s = p + strlen(root) + 1; (s = strchr(s, '/')); s++) { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
    return 0;
}
int fs_read_file(const char *path, void *buf, uint32_t max)
{
    int fd = fs_open(path, FS_O_READ);
    if (fd < 0) return fd;
    int n = fs_read(fd, buf, max); fs_close(fd); return n;
}
int fs_write_file(const char *path, const void *buf, uint32_t len)
{
    int fd = fs_open(path, FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    if (fd < 0) return fd;
    int n = (int)write(fd, buf, len); fs_close(fd); return n;
}

/* --- the GPS: a snapshot built from sentences --- */
static struct gps_snapshot gps;
static bool gps_present;
int gz_gps_io(uint32_t op, void *buffer, uint32_t size)
{
    if (op != GPS_IO_SNAPSHOT || size != sizeof(gps)) return GZ_EINVAL;
    struct gps_snapshot s = gps;
    if (!gps_present) { memset(&s, 0, sizeof(s)); s.version = GPS_SNAPSHOT_VERSION; s.size = sizeof(s); }
    memcpy(buffer, &s, size);
    return 0;
}
static void sentence(char *out, const char *body)
{
    uint8_t x = 0;
    for (const char *p = body; *p; p++) x ^= (uint8_t)*p;
    snprintf(out, GPS_SENTENCE_MAX, "$%s*%02X", body, x);
}
/* A fix at lat/lon (decimal strings in NMEA ddmm.mmmm form). */
static void set_fix(const char *lat, const char *lon, unsigned second)
{
    char b[128];
    gps.version = GPS_SNAPSHOT_VERSION; gps.size = sizeof(gps);
    gps.state = GPS_STREAMING; gps.baud = 4800; gps.sequence += 5;
    gps.rmc_age = gps.gga_age = gps.gsa_age = gps.gsv_age = 200;
    snprintf(b, sizeof(b), "GPRMC,1800%02u.00,A,%s,N,%s,W,2.50,45.00,061126,,,A", second, lat, lon);
    sentence(gps.rmc, b);
    snprintf(b, sizeof(b), "GPGGA,1800%02u.00,%s,N,%s,W,1,06,1.2,12.3,M,-28.0,M,,0000", second, lat, lon);
    sentence(gps.gga, b);
    sentence(gps.gsa, "GPGSA,A,3,07,02,26,27,09,04,,,,,,,2.0,1.2,1.6");
    sentence(gps.gsv[0], "GPGSV,2,1,08,07,79,048,42,02,51,062,43,26,36,256,42,27,27,138,40");
    sentence(gps.gsv[1], "GPGSV,2,2,08,09,23,313,39,04,19,159,37,15,12,041,,10,08,290,");
    gps.gsv_count = 2;
    gps_present = true;
}

static void snapshot(const char *name)
{
    char file[128];
    snprintf(file, sizeof(file), "../out/host/maps-%s.bmp", name);
    draw();
    host_write_bmp(file, &ui.surf);
}
static void tap(gfx_rect r)
{
    struct gz_input_event e = { .type = GZ_EV_TOUCH_DOWN, .x = (int16_t)(r.x + r.w / 2), .y = (int16_t)(r.y + r.h / 2) };
    input(&e); draw(); e.type = GZ_EV_TOUCH_UP; input(&e); draw();
}
static uint16_t at(int x, int y) { return pixels[y * UI_W + x]; }

int main(void)
{
    root = getenv("MAPS_FS");
    assert(root && "MAPS_FS: a directory with Maps/demo.gzmap");
    char p[1024];
    snprintf(p, sizeof(p), "rm -rf '%s/Applications' '%s/Documents'", root, root);
    assert(system(p) == 0);     /* a clean start: no saved state or tracks */
    gfx_surface_init(&ui.surf, pixels, UI_W, UI_H, UI_W);
    assert(gzmap_cache_init(&cache));
    map.fd = -1;
    ui_list_init(&pack_list, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H), 50);

    /* No receiver: the pack opens centred on its middle. */
    scan_packs();
    assert(npacks == 1 && !strcmp(packs[0].name, "demo"));
    assert(!load_state());
    assert(open_pack(packs[0].path, true) && have_map && map.min_zoom == 14 && map.max_zoom == 17);
    assert(!strcmp(map.name, "demo") && !strcmp(map.attribution, "Synthetic test map"));
    assert(gzmap_find(&map, 14, 2621, 6331) || map.count);
    assert(!gzmap_find(&map, 13, 0, 0) && !gzmap_find(&map, 17, 0, 0));
    poll_gps(true);
    assert(fix.state == GPS_ABSENT && !fix_valid);
    for (int i = 0; i < 8; i++) draw();     /* tiles decode a few per frame */
    assert(!tiles_pending);
    snapshot("no-gps");
    /* The map's land colour fills the middle (tests/maptiles_test.py's street tile). */
    uint16_t land = RGB565(0xF2EFE9), road = RGB565(0xFFFFFF), water = RGB565(0xAAD3DF);
    int painted = 0;
    for (int y = MAP_Y + 30; y < MAP_Y + MAP_H - 20; y += 7)
        for (int x = 0; x < UI_W; x += 7) painted += at(x, y) == land || at(x, y) == road || at(x, y) == water;
    assert(painted > 400);

    /* A fix in the pack: followed, a blue dot in the middle. */
    set_fix("3747.7300", "12223.6220", 0);
    now_ms += 2000; poll_gps(false);
    assert(fix_valid && fix.fix == GPS_FIX_3D && follow);
    for (int i = 0; i < 8; i++) draw();
    snapshot("fix");
    assert(at(MAP_CX, MAP_CY) == RGB565(0x1E78E6));

    /* Drag: follow stops, the map moves under the finger. */
    uint32_t x0 = cx;
    struct gz_input_event e = { .type = GZ_EV_TOUCH_DOWN, .x = 100, .y = 150 };
    input(&e); e.type = GZ_EV_TOUCH_MOVE; e.x = 140; input(&e); e.type = GZ_EV_TOUCH_UP; input(&e);
    assert(!follow && cx == x0 - 40 * upp());
    /* Zoom buttons clamp to the pack's range plus overzoom. */
    int z = zoom;
    for (int i = 0; i < 10; i++) action(B_IN);
    assert(zoom == map.max_zoom + OVERZOOM);
    for (int i = 0; i < 10; i++) action(B_OUT);
    assert(zoom == map.min_zoom);
    zoom = z;
    /* Double tap zooms in at the tap. */
    e = (struct gz_input_event){ .type = GZ_EV_TOUCH_DOWN, .x = 60, .y = 100, .time_ms = 1000 };
    input(&e); e.type = GZ_EV_TOUCH_UP; input(&e);
    e.type = GZ_EV_TOUCH_DOWN; e.time_ms = 1200; input(&e); e.type = GZ_EV_TOUCH_UP; input(&e);
    assert(zoom == z + 1);
    /* Locate: follows again. */
    draw(); tap(btn[B_FOLLOW]);
    assert(follow && zoom == z + 1);

    /* Record a few seconds of walking north, then stop: GPX in Documents/Tracks. */
    draw(); tap(btn[B_REC]);
    assert(track.recording && background_running && track.count == 1);
    static const char *const lats[] = { "3747.7310", "3747.7330", "3747.7360", "3747.7400" };
    /* The first step (1.9 m) is under TRACK_MIN_STEP_M, so it is dropped. */
    for (unsigned i = 0; i < 4; i++) { set_fix(lats[i], "12223.6220", 1 + i); now_ms += 1100; poll_gps(false); }
    assert(track.count == 4 && track.length_m > 15 && track.length_m < 22);
    for (int i = 0; i < 8; i++) draw();
    snapshot("recording");
    draw(); tap(btn[B_REC]);
    assert(!track.recording && !background_running);
    char gpx[4096];
    int n = fs_read_file("/Documents/Tracks/20261106-180000.gpx", gpx, sizeof(gpx) - 1);
    assert(n > 0);
    gpx[n] = 0;
    assert(strstr(gpx, "<trkpt lat=\"37.7955000\" lon=\"-122.3937000\"><ele>12.3</ele><time>2026-11-06T18:00:00Z</time>"));
    assert(strstr(gpx, "</gpx>"));

    /* The GPS page, and the pack list. */
    draw(); tap(btn[B_RIGHT]);
    assert(view == V_GPS);
    snapshot("gps");
    tap(btn[B_BACK]);
    assert(view == V_MAP);
    draw(); tap(btn[B_LEFT]);
    assert(view == V_PACKS);
    snapshot("packs");
    struct gz_input_event k = { .type = GZ_EV_KEY_DOWN, .code = GZ_KEY_ESC };
    input(&k);
    assert(view == V_MAP);

    /* Lost receiver. */
    gps_present = false;
    now_ms += 2000; poll_gps(false);
    assert(fix.state == GPS_ABSENT && !fix_valid);
    snapshot("card-removed");

    /* State survives a restart. */
    save_state();
    uint32_t sx = cx, sy = cy; int sz = zoom;
    gzmap_close(&map); have_map = false; cx = cy = 0; zoom = 2;
    assert(load_state() && have_map && cx == sx && cy == sy && zoom == sz);

    /* A corrupt pack is refused. */
    snprintf(p, sizeof(p), "%s/Maps/bad.gzmap", root);
    FILE *f = fopen(p, "wb"); fwrite("GZMP\1\0\0\0", 1, 8, f); fclose(f);
    struct gzmap bad;
    assert(gzmap_open(&bad, "/Maps/bad.gzmap") < 0);
    unlink(p);
    puts("Maps UI: packs, tiles and fallback, following, pan and zoom, GPS page, GPX recording, saved state PASS");
    return 0;
}
