/* Glaze: directory reading over the file service (glaze/compat.c). */
#ifndef GLAZE_VLC_DIRENT_H
#define GLAZE_VLC_DIRENT_H
#include <glaze/fs.h>
struct dirent {
    unsigned char d_type;
    char d_name[256];
};
#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4
typedef struct glaze_dir DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);
/* scandir and alphasort are VLC's own (src/extras/libc.c). */
#endif
