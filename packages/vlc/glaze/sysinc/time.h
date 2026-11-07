/* Glaze: calendar time for VLC (glaze/compat.c); clocks come from glaze/sys.h. */
#ifndef GLAZE_VLC_TIME_H
#define GLAZE_VLC_TIME_H
#include <sys/types.h>
#include <sys/time.h>
#ifndef GLAZE_TIME_T
#define GLAZE_TIME_T
typedef long time_t;
#endif
typedef long clock_t;
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};
time_t time(time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *tm);
struct tm *localtime_r(const time_t *t, struct tm *tm);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
char *ctime_r(const time_t *t, char *buf);
#endif
