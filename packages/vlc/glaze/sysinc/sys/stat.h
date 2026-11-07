/* Glaze: file status over the file service (glaze/compat.c). Descriptors
 * from libc are sockets, so fstat only answers for those as FIFOs. */
#ifndef GLAZE_VLC_SYS_STAT_H
#define GLAZE_VLC_SYS_STAT_H
#include <sys/types.h>
#include <time.h>
#ifndef GLAZE_OFF_T
#define GLAZE_OFF_T
typedef long long off_t;
#endif
typedef unsigned int mode_t;
struct stat {
    mode_t st_mode;
    off_t st_size;
    time_t st_mtime;
};
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IFIFO  0010000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFSOCK 0140000
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
int stat(const char *path, struct stat *st);
int lstat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mkdir(const char *path, mode_t mode);
#endif
