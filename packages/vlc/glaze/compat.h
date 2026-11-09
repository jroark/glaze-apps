/*
 * The parts of a hosted C library that VLC's core and modules use and Glaze's
 * minimal libc does not have: a small FILE stdio over the file service, a
 * complete printf and scanf, time, the float conversions, select over poll,
 * and do-nothing process calls. Force-included before every source
 * (Makefile), with config.h; glaze/sysinc adds the missing headers.
 *
 * Nothing here changes a Glaze ABI: FILE is private to the app, the printf
 * family is renamed to the app's own copy (libc's has no %ll or %f), and
 * sockets stay the libc's own descriptors.
 */
#ifndef GLAZE_VLC_COMPAT_H
#define GLAZE_VLC_COMPAT_H

#include "config.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>

/* ---- types --------------------------------------------------------------- */
typedef int pid_t;
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

static inline div_t div(int n, int d) { div_t r = { n / d, n % d }; return r; }
static inline ldiv_t ldiv(long n, long d) { ldiv_t r = { n / d, n % d }; return r; }
lldiv_t lldiv(long long n, long long d);

#define PRId64  "lld"
#define PRIi64  "lli"
#define PRIu64  "llu"
#define PRIx64  "llx"
#define PRIX64  "llX"
#define PRId8   "d"
#define PRIu8   "u"

/* errno values libc does not name. */
#define EEXIST      17
#define ENOTDIR     20
#define EISDIR      21
#define ENOTEMPTY   39
#define ESPIPE      29
#define EROFS       30
#define ENAMETOOLONG 36

/* ---- printf and scanf (glaze/printf.c) ------------------------------------
 * Function-like, so `format(printf, ...)` attributes in headers keep working. */
int glaze_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int glaze_snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int glaze_sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int glaze_vsprintf(char *buf, const char *fmt, va_list ap);
int glaze_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int glaze_vsscanf(const char *s, const char *fmt, va_list ap);
int glaze_sscanf(const char *s, const char *fmt, ...) __attribute__((format(scanf, 2, 3)));
#define vsnprintf(...)  glaze_vsnprintf(__VA_ARGS__)
#define snprintf(...)   glaze_snprintf(__VA_ARGS__)
#define sprintf(...)    glaze_sprintf(__VA_ARGS__)
#define vsprintf(...)   glaze_vsprintf(__VA_ARGS__)
#define printf(...)     glaze_printf(__VA_ARGS__)
#define vsscanf(...)    glaze_vsscanf(__VA_ARGS__)
#define sscanf(...)     glaze_sscanf(__VA_ARGS__)

/* ---- FILE stdio over glaze/fs.h (glaze/stdio.c) --------------------------- */
typedef struct glaze_file FILE;
extern FILE *const glaze_stdin, *const glaze_stdout, *const glaze_stderr;
#define stdin   glaze_stdin
#define stdout  glaze_stdout     /* both go to the debug log */
#define stderr  glaze_stderr
#define EOF     (-1)
#define BUFSIZ  4096
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int fgetc(FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vfprintf(FILE *f, const char *fmt, va_list ap);
int fflush(FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int putchar(int c);
void perror(const char *s);
int remove(const char *path);
int rename(const char *from, const char *to);
int unlink(const char *path);
#define getc(f)     fgetc(f)
#define putc(c, f)  fputc(c, f)

/* No processes or shell: these always fail. */
static inline FILE *popen(const char *cmd, const char *mode) { (void)cmd; (void)mode; errno = ENOSYS; return NULL; }
static inline int pclose(FILE *f) { (void)f; return -1; }
static inline pid_t fork(void) { errno = ENOSYS; return -1; }
static inline pid_t getpid(void) { return 1; }
static inline char *getcwd(char *buf, size_t n) { (void)buf; (void)n; errno = ENOSYS; return NULL; }
static inline int isatty(int fd) { (void)fd; return 0; }

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2
#define F_SETFD         2
#define O_RDONLY        0x0
#define O_WRONLY        0x1
#define O_CREAT         0x40
#define O_TRUNC         0x200
#define O_APPEND        0x400
/* Files are FILE streams here (stdio.c); libc descriptors are sockets. */
static inline int open(const char *path, int flags, ...) { (void)path; (void)flags; errno = ENOSYS; return -1; }
#define FD_CLOEXEC      1

/* ---- strings and numbers (glaze/libc.c) ----------------------------------- */
/* Sets errno from a Glaze error (GZ_E*) and returns -1. */
int glaze_fail(int gz_err);
char *strerror(int err);
char *strpbrk(const char *s, const char *accept);
char *strtok_r(char *s, const char *delim, char **save);
char *strsep(char **sp, const char *delim);
#define strcoll strcmp
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
double atof(const char *s);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
long long atoll(const char *s);
long atol(const char *s);
long labs(long v);
long long llabs(long long v);
int rand(void);
void srand(unsigned seed);
#define RAND_MAX 0x7fffffff
char *getenv(const char *name);
void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *));

int gettimeofday(struct timeval *tv, void *tz);

/* ---- select over poll (glaze/libc.c) -------------------------------------- */
#define FD_SETSIZE 64
typedef struct { uint32_t bits[FD_SETSIZE / 32]; } fd_set;
#define FD_ZERO(s)      memset((s), 0, sizeof(fd_set))
#define FD_SET(fd, s)   ((s)->bits[(fd) / 32] |= 1u << ((fd) % 32))
#define FD_CLR(fd, s)   ((s)->bits[(fd) / 32] &= ~(1u << ((fd) % 32)))
#define FD_ISSET(fd, s) (((s)->bits[(fd) / 32] >> ((fd) % 32)) & 1u)
int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv);

/* ---- network helpers libc does not have (glaze/libc.c) -------------------- */
#define NI_MAXHOST      1025
#define NI_MAXSERV      32
#define NI_NUMERICHOST  0x1
#define NI_NUMERICSERV  0x2
#define NI_NOFQDN       0x4
#define NI_NAMEREQD     0x8
#define NI_DGRAM        0x10
#define EAI_OVERFLOW    (-12)
struct sockaddr;
int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);
unsigned int inet_addr(const char *s);
#define IN6_IS_ADDR_MULTICAST(a) (((const uint8_t *)(a))[0] == 0xff)

/* VLC's getopt is its own (src/extras/getopt.c). */
extern int optind;

#endif
