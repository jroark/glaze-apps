/*
 * The rest of compat.h: string and number conversions, calendar time from
 * the RTC and the time zone, select over poll, numeric getnameinfo, and
 * directories and file status over the file service.
 */
#include <ctype.h>
#include <stdbool.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <glaze/fs.h>
#include <glaze/notify.h>
#include <glaze/res.h>
#include <glaze/sys.h>
#include <glaze/tz.h>

int glaze_fail(int gz_err)
{
    switch (gz_err) {
    case GZ_ENOENT: errno = ENOENT; break;
    case GZ_ENOMEM: errno = ENOMEM; break;
    case GZ_EEXIST: errno = EEXIST; break;
    case GZ_ENOTEMPTY: errno = ENOTEMPTY; break;
    case GZ_ENOSPC: errno = ENOSPC; break;
    case GZ_EISDIR: errno = EISDIR; break;
    case GZ_ENOTDIR: errno = ENOTDIR; break;
    case GZ_EINVAL: errno = EINVAL; break;
    case GZ_EPERM: errno = EACCES; break;
    default: errno = EIO; break;
    }
    return -1;
}

/* ---- strings ------------------------------------------------------------- */
char *strerror(int err)
{
    static char unknown[24];

    switch (err) {
    case 0: return "Success";
    case EPERM: return "Operation not permitted";
    case ENOENT: return "No such file or directory";
    case EINTR: return "Interrupted";
    case EIO: return "I/O error";
    case EBADF: return "Bad file descriptor";
    case EAGAIN: return "Try again";
    case ENOMEM: return "Out of memory";
    case EACCES: return "Permission denied";
    case EBUSY: return "Busy";
    case EEXIST: return "File exists";
    case ENOTDIR: return "Not a directory";
    case EISDIR: return "Is a directory";
    case EINVAL: return "Invalid argument";
    case ENOSPC: return "No space left";
    case EPIPE: return "Broken pipe";
    case ERANGE: return "Out of range";
    case ENOSYS: return "Not supported";
    case ENOTEMPTY: return "Directory not empty";
    case ENETDOWN: return "Network is down";
    case ENETUNREACH: return "Network is unreachable";
    case ECONNRESET: return "Connection reset";
    case ENOTCONN: return "Not connected";
    case ETIMEDOUT: return "Timed out";
    case ECONNREFUSED: return "Connection refused";
    case EHOSTUNREACH: return "Host is unreachable";
    case EINPROGRESS: return "In progress";
    }
    snprintf(unknown, sizeof(unknown), "Error %d", err);
    return unknown;
}

char *strpbrk(const char *s, const char *accept)
{
    s += strcspn(s, accept);
    return *s ? (char *)s : NULL;
}

char *strsep(char **sp, const char *delim)
{
    char *s = *sp;

    if (!s)
        return NULL;
    char *e = s + strcspn(s, delim);
    if (*e)
        *e++ = 0;
    else
        e = NULL;
    *sp = e;
    return s;
}

char *strtok_r(char *s, const char *delim, char **save)
{
    if (!s)
        s = *save;
    s += strspn(s, delim);
    if (!*s) {
        *save = s;
        return NULL;
    }
    char *e = s + strcspn(s, delim);
    if (*e)
        *e++ = 0;
    *save = e;
    return s;
}

/* HOME is the app's data folder, so VLC keeps its .vlc directory there. */
char *getenv(const char *name)
{
    static char home[FS_PATH_MAX];

    if (strcmp(name, "HOME") != 0)
        return NULL;
    if (!home[0]) {
        /* Empty until ui_init: never fall back to the root's /data. */
        const char *bundle = res_bundle();
        if (!bundle || !bundle[0] || snprintf(home, sizeof(home), "%s/data", bundle) >= (int)sizeof(home))
            return NULL;
        fs_mkdirs(home);
    }
    return home;
}

/* ---- numbers ------------------------------------------------------------- */
static int digit_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return 99;
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    const char *p = s;
    bool neg = false, any = false, over = false;
    unsigned long long v = 0;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        digit_value((unsigned char)p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    for (int d; (d = digit_value((unsigned char)*p)) < base; p++) {
        any = true;
        if (v > (~0ull - (unsigned)d) / (unsigned)base)
            over = true;
        v = v * (unsigned)base + (unsigned)d;
    }
    if (end)
        *end = (char *)(any ? p : s);
    if (over) {
        errno = ERANGE;
        return ~0ull;
    }
    return neg ? 0ull - v : v;
}

long long strtoll(const char *s, char **end, int base)
{
    const char *p = s;

    while (isspace((unsigned char)*p))
        p++;
    bool neg = *p == '-';
    unsigned long long v = strtoull(p + (neg || *p == '+'), end, base);
    if (end && *end == p + (neg || *p == '+'))
        *end = (char *)s;
    if (!neg && v > (unsigned long long)LLONG_MAX) {
        errno = ERANGE;
        return LLONG_MAX;
    }
    if (neg && v > (unsigned long long)LLONG_MAX + 1) {
        errno = ERANGE;
        return LLONG_MIN;
    }
    return neg ? (long long)(0ull - v) : (long long)v;
}

long long atoll(const char *s) { return strtoll(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
long labs(long v) { return v < 0 ? -v : v; }
long long llabs(long long v) { return v < 0 ? -v : v; }

lldiv_t lldiv(long long n, long long d)
{
    lldiv_t r = { n / d, n % d };
    return r;
}

static const double pow10_bin[] = { 1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256 };

static double scale10(double v, int e)
{
    bool neg = e < 0;
    double f = 1.0;

    if (neg)
        e = -e;
    for (int i = 0; e && i < 9; i++, e >>= 1)
        if (e & 1)
            f *= pow10_bin[i];
    if (e)                      /* beyond 1e511 */
        return neg ? 0.0 : __builtin_huge_val();
    return neg ? v / f : v * f;
}

/* Decimal only (and inf/nan): VLC reads rates, ratios and the like. Up to
 * 19 significant digits are kept; the rest only move the exponent. */
double strtod(const char *s, char **end)
{
    const char *p = s;
    bool neg = false, any = false;
    unsigned long long m = 0;
    int digits = 0, e = 0;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if (strncasecmp(p, "inf", 3) == 0) {
        p += strncasecmp(p, "infinity", 8) == 0 ? 8 : 3;
        if (end)
            *end = (char *)p;
        return neg ? -__builtin_huge_val() : __builtin_huge_val();
    }
    if (strncasecmp(p, "nan", 3) == 0) {
        if (end)
            *end = (char *)p + 3;
        return __builtin_nan("");
    }
    for (; isdigit((unsigned char)*p); p++) {
        any = true;
        if (digits < 19) {
            m = m * 10 + (unsigned)(*p - '0');
            if (m)
                digits++;
        } else {
            e++;
        }
    }
    if (*p == '.') {
        p++;
        for (; isdigit((unsigned char)*p); p++) {
            any = true;
            if (digits < 19) {
                m = m * 10 + (unsigned)(*p - '0');
                if (m)
                    digits++;
                e--;
            }
        }
    }
    if (!any) {
        if (end)
            *end = (char *)s;
        return 0.0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        bool eneg = false;
        int x = 0;
        if (*q == '+' || *q == '-')
            eneg = *q++ == '-';
        if (isdigit((unsigned char)*q)) {
            for (; isdigit((unsigned char)*q); q++)
                if (x < 10000)
                    x = x * 10 + (*q - '0');
            e += eneg ? -x : x;
            p = q;
        }
    }
    if (end)
        *end = (char *)p;
    double v = scale10((double)m, e);
    return neg ? -v : v;
}

float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s) { return strtod(s, NULL); }

static unsigned long rand_state = 1;

int rand(void)
{
    rand_state = rand_state * 1103515245ul + 12345ul;
    return (int)((rand_state >> 1) & RAND_MAX);
}

void srand(unsigned seed)
{
    rand_state = seed;
}

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *))
{
    const char *lo = base;

    while (n) {
        const char *mid = lo + (n / 2) * size;
        int c = cmp(key, mid);
        if (c == 0)
            return (void *)mid;
        if (c > 0) {
            lo = mid + size;
            n -= n / 2 + 1;
        } else {
            n /= 2;
        }
    }
    return NULL;
}

/* ---- time ---------------------------------------------------------------- */
#define UNIX_2000   946684800L      /* 2000-01-01 00:00 UTC in Unix time */

time_t time(time_t *t)
{
    struct gz_datetime dt;
    time_t now = UNIX_2000;

    if (gz_rtc_get(&dt) >= 0)
        now = (time_t)tz_local_to_utc(tz_current(), gz_dt_to_secs(&dt)) + UNIX_2000;
    if (t)
        *t = now;
    return now;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    tv->tv_sec = time(NULL);
    tv->tv_usec = (long)(gz_time_us() % 1000000u);
    return 0;
}

static struct tm *to_tm(uint32_t secs2000, struct tm *tm)
{
    struct gz_datetime dt;

    gz_secs_to_dt(secs2000, &dt);
    memset(tm, 0, sizeof(*tm));
    tm->tm_year = dt.year - 1900;
    tm->tm_mon = dt.month - 1;
    tm->tm_mday = dt.day;
    tm->tm_hour = dt.hour;
    tm->tm_min = dt.minute;
    tm->tm_sec = dt.second;
    tm->tm_wday = (int)((secs2000 / 86400u + 6) % 7);   /* 2000-01-01 was a Saturday */
    tm->tm_yday = gz_days_from_civil(dt.year, dt.month, dt.day) -
                  gz_days_from_civil(dt.year, 1, 1);
    return tm;
}

static uint32_t since_2000(time_t t)
{
    return t < UNIX_2000 ? 0 : (uint32_t)(t - UNIX_2000);
}

struct tm *gmtime_r(const time_t *t, struct tm *tm)
{
    return to_tm(since_2000(*t), tm);
}

struct tm *localtime_r(const time_t *t, struct tm *tm)
{
    return to_tm(tz_utc_to_local(tz_current(), since_2000(*t)), tm);
}

struct tm *gmtime(const time_t *t)
{
    static struct tm tm;
    return gmtime_r(t, &tm);
}

struct tm *localtime(const time_t *t)
{
    static struct tm tm;
    return localtime_r(t, &tm);
}

time_t mktime(struct tm *tm)
{
    int32_t days = gz_days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, 1) + tm->tm_mday - 1;
    int64_t local = (int64_t)days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;

    if (local < 0)
        local = 0;
    uint32_t utc = tz_local_to_utc(tz_current(), (uint32_t)local);
    to_tm(tz_utc_to_local(tz_current(), utc), tm);
    return (time_t)utc + UNIX_2000;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    static const char *const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    size_t n = 0;

    for (; *fmt; fmt++) {
        char part[32];
        if (*fmt != '%') {
            part[0] = *fmt;
            part[1] = 0;
        } else {
            switch (*++fmt) {
            case 'Y': snprintf(part, sizeof(part), "%d", tm->tm_year + 1900); break;
            case 'y': snprintf(part, sizeof(part), "%02d", tm->tm_year % 100); break;
            case 'm': snprintf(part, sizeof(part), "%02d", tm->tm_mon + 1); break;
            case 'd': snprintf(part, sizeof(part), "%02d", tm->tm_mday); break;
            case 'e': snprintf(part, sizeof(part), "%2d", tm->tm_mday); break;
            case 'H': snprintf(part, sizeof(part), "%02d", tm->tm_hour); break;
            case 'M': snprintf(part, sizeof(part), "%02d", tm->tm_min); break;
            case 'S': snprintf(part, sizeof(part), "%02d", tm->tm_sec); break;
            case 'j': snprintf(part, sizeof(part), "%03d", tm->tm_yday + 1); break;
            case 'a': snprintf(part, sizeof(part), "%s", days[tm->tm_wday % 7]); break;
            case 'b': case 'h': snprintf(part, sizeof(part), "%s", months[tm->tm_mon % 12]); break;
            case 'F': snprintf(part, sizeof(part), "%d-%02d-%02d", tm->tm_year + 1900,
                               tm->tm_mon + 1, tm->tm_mday); break;
            case 'T': snprintf(part, sizeof(part), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min,
                               tm->tm_sec); break;
            case '%': snprintf(part, sizeof(part), "%%"); break;
            case 0: fmt--; part[0] = 0; break;
            default: snprintf(part, sizeof(part), "%%%c", *fmt); break;
            }
        }
        size_t k = strlen(part);
        if (n + k >= max)
            return 0;
        memcpy(s + n, part, k);
        n += k;
    }
    if (max)
        s[n] = 0;
    return n;
}

/* ---- select over poll ---------------------------------------------------- */
int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    struct pollfd pfd[FD_SETSIZE];
    int np = 0;

    if (n > FD_SETSIZE)
        n = FD_SETSIZE;
    for (int fd = 0; fd < n; fd++) {
        short ev = 0;
        if (r && FD_ISSET(fd, r))
            ev |= POLLIN;
        if (w && FD_ISSET(fd, w))
            ev |= POLLOUT;
        if (ev || (e && FD_ISSET(fd, e))) {
            pfd[np].fd = fd;
            pfd[np].events = ev;
            pfd[np].revents = 0;
            np++;
        }
    }
    int ms = tv ? (int)(tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000) : -1;
    int rv = np ? poll(pfd, (nfds_t)np, ms) : (ms > 0 ? (gz_sleep_ms((uint32_t)ms), 0) : 0);
    if (rv < 0)
        return rv;
    if (r)
        FD_ZERO(r);
    if (w)
        FD_ZERO(w);
    if (e)
        FD_ZERO(e);
    int count = 0;
    for (int i = 0; i < np; i++) {
        short re = pfd[i].revents;
        if (r && (re & (POLLIN | POLLHUP | POLLERR))) {
            FD_SET(pfd[i].fd, r);
            count++;
        }
        if (w && (re & (POLLOUT | POLLERR))) {
            FD_SET(pfd[i].fd, w);
            count++;
        }
        if (e && (re & (POLLERR | POLLNVAL | POLLPRI))) {
            FD_SET(pfd[i].fd, e);
            count++;
        }
    }
    return count;
}

/* ---- network ------------------------------------------------------------- */
/* Numeric only: Glaze has no reverse DNS. */
int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    unsigned port;

    if (flags & NI_NAMEREQD)
        return EAI_NONAME;
    if (sa->sa_family == AF_INET && salen >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        if (host && hostlen && !inet_ntop(AF_INET, &in->sin_addr, host, hostlen))
            return EAI_OVERFLOW;
        port = ntohs(in->sin_port);
    } else if (sa->sa_family == AF_INET6 && salen >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        if (host && hostlen && !inet_ntop(AF_INET6, &in6->sin6_addr, host, hostlen))
            return EAI_OVERFLOW;
        port = ntohs(in6->sin6_port);
    } else {
        return EAI_FAMILY;
    }
    if (serv && servlen && snprintf(serv, servlen, "%u", port) >= (int)servlen)
        return EAI_OVERFLOW;
    return 0;
}

unsigned int inet_addr(const char *s)
{
    struct in_addr a;
    return inet_pton(AF_INET, s, &a) == 1 ? a.s_addr : INADDR_NONE;
}

/* ---- directories and file status ----------------------------------------- */
#define DIR_BATCH   16

struct glaze_dir {
    char path[FS_PATH_MAX];
    uint32_t skip;              /* entries listed before this batch */
    int n, i;
    bool done;
    struct fs_dirent batch[DIR_BATCH];
    struct dirent cur;
};

DIR *opendir(const char *path)
{
    struct fs_stat st;
    int r = fs_stat(path, &st);

    if (r < 0) {
        glaze_fail(r);
        return NULL;
    }
    if (st.type != FS_DIR) {
        errno = ENOTDIR;
        return NULL;
    }
    DIR *d = calloc(1, sizeof(*d));
    if (!d) {
        errno = ENOMEM;
        return NULL;
    }
    strlcpy(d->path, path, sizeof(d->path));
    return d;
}

struct dirent *readdir(DIR *d)
{
    if (d->i == d->n) {
        if (d->done)
            return NULL;
        d->skip += (uint32_t)d->n;
        int r = fs_list(d->path, d->skip, d->batch, DIR_BATCH);
        d->n = r < 0 ? 0 : r;
        d->i = 0;
        if (d->n < DIR_BATCH)
            d->done = true;
        if (d->n == 0)
            return NULL;
    }
    const struct fs_dirent *e = &d->batch[d->i++];
    size_t len = e->name_len < FS_NAME_MAX ? e->name_len : FS_NAME_MAX;
    memcpy(d->cur.d_name, e->name, len);
    d->cur.d_name[len] = 0;
    d->cur.d_type = e->type == FS_DIR ? DT_DIR : e->type == FS_FILE ? DT_REG : DT_UNKNOWN;
    return &d->cur;
}

void rewinddir(DIR *d)
{
    d->skip = 0;
    d->n = d->i = 0;
    d->done = false;
}

int closedir(DIR *d)
{
    free(d);
    return 0;
}

int stat(const char *path, struct stat *st)
{
    struct fs_stat fst;
    int r = fs_stat(path, &fst);

    if (r < 0)
        return glaze_fail(r);
    memset(st, 0, sizeof(*st));
    st->st_mode = (fst.type == FS_DIR ? S_IFDIR : S_IFREG) | 0644;
    st->st_size = fst.size;
    return 0;
}

int lstat(const char *path, struct stat *st)
{
    return stat(path, st);
}

/* libc descriptors are sockets. */
int fstat(int fd, struct stat *st)
{
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFSOCK;
    return 0;
}

int mkdir(const char *path, mode_t mode)
{
    (void)mode;
    int r = fs_mkdir(path);
    return r < 0 ? glaze_fail(r) : 0;
}
