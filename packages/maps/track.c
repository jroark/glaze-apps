/* Track recording and GPX 1.1 (https://www.topografix.com/GPX/1/1/). */
#include "track.h"
#include <glaze/fs.h>
#include <glaze/sys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool track_start(struct track *t)
{
    if (!t->points) t->points = malloc(TRACK_MAX * sizeof(struct track_point));
    if (!t->points) return false;
    t->count = 0; t->length_m = 0; t->full = false;
    t->recording = true;
    return true;
}

bool track_add(struct track *t, const struct gps_fix *fix)
{
    if (!t->recording || fix->fix == GPS_FIX_NONE) return false;
    if (t->count == TRACK_MAX) { t->full = true; return false; }
    uint32_t step = 0;
    if (t->count) {
        const struct track_point *last = &t->points[t->count - 1];
        step = gps_distance_m(last->lat, last->lon, fix->lat, fix->lon);
        if (step < TRACK_MIN_STEP_M) return false;
    }
    struct track_point *p = &t->points[t->count++];
    *p = (struct track_point){ fix->lat, fix->lon, fix->altitude_dm, fix->year, fix->month, fix->day,
                               fix->hour, fix->minute, fix->second, 0 };
    t->length_m += step;
    return true;
}

void track_stop(struct track *t) { t->recording = false; }

void track_free(struct track *t)
{
    free(t->points);
    memset(t, 0, sizeof(*t));
}

/* 1e-7 degrees as a decimal: "-122.3937000". */
static int degrees(char *out, size_t cap, int32_t v)
{
    uint32_t a = v < 0 ? (uint32_t)-(int64_t)v : (uint32_t)v;
    return snprintf(out, cap, "%s%u.%07u", v < 0 ? "-" : "", a / 10000000u, a % 10000000u);
}

int track_gpx(const struct track *t, char *buf, unsigned cap)
{
    unsigned n = 0;
#define PUT(...) do { int k = snprintf(buf + n, cap - n, __VA_ARGS__); \
                      if (k < 0 || (unsigned)k >= cap - n) return -1; n += (unsigned)k; } while (0)
    PUT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"Glaze Maps\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        "<trk><name>Glaze track</name><trkseg>\n");
    for (unsigned i = 0; i < t->count; i++) {
        const struct track_point *p = &t->points[i];
        char lat[24], lon[24];
        degrees(lat, sizeof(lat), p->lat);
        degrees(lon, sizeof(lon), p->lon);
        int32_t alt = p->altitude_dm;
        uint32_t aa = alt < 0 ? (uint32_t)-alt : (uint32_t)alt;
        PUT("<trkpt lat=\"%s\" lon=\"%s\"><ele>%s%u.%u</ele>", lat, lon, alt < 0 ? "-" : "", aa / 10, aa % 10);
        if (p->year)
            PUT("<time>%04u-%02u-%02uT%02u:%02u:%02uZ</time>", p->year, p->month, p->day, p->hour, p->minute,
                p->second);
        PUT("</trkpt>\n");
    }
    PUT("</trkseg></trk>\n</gpx>\n");
#undef PUT
    return (int)n;
}

int track_save(const struct track *t, char *path_out, unsigned path_cap)
{
    if (!t->count) return GZ_EINVAL;
    unsigned cap = 256 + t->count * 140u;
    char *buf = malloc(cap);
    if (!buf) return GZ_ENOMEM;
    int len = track_gpx(t, buf, cap);
    int rv = len < 0 ? GZ_EINVAL : 0;
    if (!rv) {
        const struct track_point *p = &t->points[0];
        fs_mkdirs("/Documents/Tracks");
        if (p->year)
            snprintf(path_out, path_cap, "/Documents/Tracks/%04u%02u%02u-%02u%02u%02u.gpx", p->year, p->month,
                     p->day, p->hour, p->minute, p->second);
        else
            snprintf(path_out, path_cap, "/Documents/Tracks/track-%u.gpx", (unsigned)gz_time_ms());
        int w = fs_write_file(path_out, buf, (uint32_t)len);
        rv = w == len ? 0 : (w < 0 ? w : GZ_EIO);
    }
    free(buf);
    return rv;
}
