/* A recorded track: GPS points kept while recording, saved as GPX 1.1. */
#ifndef MAPS_TRACK_H
#define MAPS_TRACK_H
#include <glaze/gps.h>

#define TRACK_MAX 8000          /* about two hours at a point a second */
#define TRACK_MIN_STEP_M 3      /* closer points are dropped */

struct track_point {
    int32_t lat, lon, altitude_dm;
    uint16_t year; uint8_t month, day, hour, minute, second, pad;
};

struct track {
    struct track_point *points;
    unsigned count;
    uint32_t length_m;
    bool recording, full;
};

bool track_start(struct track *t);
/* Adds a fix if it moved far enough from the last point. Returns true if
 * the point was added. */
bool track_add(struct track *t, const struct gps_fix *fix);
void track_stop(struct track *t);
void track_free(struct track *t);
/* Writes the GPX document to buf; returns its length, or -1 if cap is too
 * small. track_save writes it to Documents/Tracks under a name from the
 * first point's UTC time, returning the path in path_out. */
int track_gpx(const struct track *t, char *buf, unsigned cap);
int track_save(const struct track *t, char *path_out, unsigned path_cap);

#endif
