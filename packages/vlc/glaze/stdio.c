/*
 * FILE stdio for VLC over the file service (glaze/fs.h): its configuration,
 * playlists and the file access module read and write through it. Each FILE
 * has one buffer that holds either read-ahead or pending writes, so the
 * service sees requests of up to FS_IO_MAX bytes. stdout and stderr go to
 * the debug log a line at a time.
 */
#include <stdbool.h>
#include <glaze/fs.h>
#include <glaze/sys.h>

#define BUF_SIZE    FS_IO_MAX

struct glaze_file {
    int fd;                     /* fs handle; -1 for the log streams */
    int flags;                  /* FS_O_* it was opened with */
    bool eof, err, writing;
    long pos;                   /* file position of buf[0] */
    int len, off;               /* bytes in buf; next byte to use */
    char *buf;
};

static char log_line[256];
static int log_len;
static struct glaze_file log_out = { .fd = -1, .flags = FS_O_WRITE };
static struct glaze_file log_err = { .fd = -1, .flags = FS_O_WRITE };
static struct glaze_file no_in = { .fd = -1, .flags = FS_O_READ, .eof = true };
FILE *const glaze_stdin = &no_in;
FILE *const glaze_stdout = &log_out;
FILE *const glaze_stderr = &log_err;

static void log_flush(void)
{
    if (log_len)
        gz_log(log_line, (uint32_t)log_len);
    log_len = 0;
}

static void log_put(char c)
{
    log_line[log_len++] = c;
    if (c == '\n' || log_len == (int)sizeof(log_line))
        log_flush();
}

FILE *fopen(const char *path, const char *mode)
{
    int flags;
    bool plus = strchr(mode, '+') != NULL;

    switch (mode[0]) {
    case 'r': flags = FS_O_READ | (plus ? FS_O_WRITE : 0); break;
    case 'w': flags = FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC | (plus ? FS_O_READ : 0); break;
    case 'a': flags = FS_O_WRITE | FS_O_CREATE | FS_O_APPEND | (plus ? FS_O_READ : 0); break;
    default: errno = EINVAL; return NULL;
    }
    struct glaze_file *f = calloc(1, sizeof(*f));
    if (!f || !(f->buf = malloc(BUF_SIZE))) {
        free(f);
        errno = ENOMEM;
        return NULL;
    }
    f->fd = fs_open(path, flags);
    if (f->fd < 0) {
        glaze_fail(f->fd);
        free(f->buf);
        free(f);
        return NULL;
    }
    f->flags = flags;
    if (flags & FS_O_APPEND) {
        int end = fs_seek(f->fd, 0, FS_SEEK_END);
        f->pos = end < 0 ? 0 : end;
    }
    return f;
}

static int flush_writes(FILE *f)
{
    int done = 0;

    while (done < f->len) {
        int r = fs_write(f->fd, f->buf + done, (uint32_t)(f->len - done));
        if (r <= 0) {
            f->err = true;
            return glaze_fail(r);
        }
        done += r;
    }
    f->pos += f->len;
    f->len = f->off = 0;
    f->writing = false;
    return 0;
}

int fflush(FILE *f)
{
    if (!f || f->fd < 0) {
        log_flush();
        return 0;
    }
    if (f->writing)
        return flush_writes(f);
    return 0;
}

int fclose(FILE *f)
{
    if (!f || f->fd < 0)
        return fflush(f);
    int r = fflush(f);
    fs_close(f->fd);
    free(f->buf);
    free(f);
    return r;
}

/* Drops read-ahead so the handle's position is the logical one again. */
static int drop_reads(FILE *f)
{
    if (f->writing || f->len == f->off)
        goto done;
    if (fs_seek(f->fd, (int32_t)(f->pos + f->off), FS_SEEK_SET) < 0) {
        f->err = true;
        return -1;
    }
done:
    if (!f->writing) {
        f->pos += f->off;
        f->len = f->off = 0;
    }
    return 0;
}

static bool fill(FILE *f)
{
    if (f->writing && flush_writes(f) < 0)
        return false;
    f->pos += f->len;
    f->len = f->off = 0;
    int r = fs_read(f->fd, f->buf, BUF_SIZE);
    if (r < 0) {
        f->err = true;
        glaze_fail(r);
        return false;
    }
    if (r == 0) {
        f->eof = true;
        return false;
    }
    f->len = r;
    return true;
}

size_t fread(void *p, size_t size, size_t n, FILE *f)
{
    size_t want = size * n, got = 0;
    char *d = p;

    if (!want || f->fd < 0 || !(f->flags & FS_O_READ))
        return 0;
    while (got < want) {
        if (f->off == f->len) {
            /* Large reads skip the buffer. */
            if (want - got >= BUF_SIZE && !f->writing) {
                f->pos += f->len;
                f->len = f->off = 0;
                uint32_t chunk = want - got > FS_IO_MAX ? FS_IO_MAX : (uint32_t)(want - got);
                int r = fs_read(f->fd, d + got, chunk);
                if (r <= 0) {
                    if (r < 0)
                        f->err = true;
                    else
                        f->eof = true;
                    break;
                }
                f->pos += r;
                got += (size_t)r;
                continue;
            }
            if (!fill(f))
                break;
        }
        size_t k = (size_t)(f->len - f->off);
        if (k > want - got)
            k = want - got;
        memcpy(d + got, f->buf + f->off, k);
        f->off += (int)k;
        got += k;
    }
    return got / size;
}

size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    size_t want = size * n;
    const char *s = p;

    if (!want)
        return 0;
    if (f->fd < 0) {
        for (size_t i = 0; i < want; i++)
            log_put(s[i]);
        return n;
    }
    if (!(f->flags & FS_O_WRITE))
        return 0;
    if (!f->writing) {
        if (drop_reads(f) < 0)
            return 0;
        f->writing = true;
    }
    for (size_t done = 0; done < want;) {
        if (f->len == BUF_SIZE && flush_writes(f) < 0)
            return done / size;
        f->writing = true;
        size_t k = BUF_SIZE - (size_t)f->len;
        if (k > want - done)
            k = want - done;
        memcpy(f->buf + f->len, s + done, k);
        f->len += (int)k;
        done += k;
    }
    return n;
}

int fgetc(FILE *f)
{
    unsigned char c;
    return fread(&c, 1, 1, f) == 1 ? c : EOF;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0;

    if (n <= 0)
        return NULL;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF)
            break;
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return NULL;
    s[i] = 0;
    return s;
}

int fputc(int c, FILE *f)
{
    unsigned char b = (unsigned char)c;
    return fwrite(&b, 1, 1, f) == 1 ? b : EOF;
}

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char small[256];
    va_list copy;

    va_copy(copy, ap);
    int n = glaze_vsnprintf(small, sizeof(small), fmt, copy);
    va_end(copy);
    if (n < 0)
        return n;
    if (n < (int)sizeof(small))
        return fwrite(small, 1, (size_t)n, f) == (size_t)n ? n : -1;
    char *big = malloc((size_t)n + 1);
    if (!big)
        return -1;
    glaze_vsnprintf(big, (size_t)n + 1, fmt, ap);
    size_t w = fwrite(big, 1, (size_t)n, f);
    free(big);
    return w == (size_t)n ? n : -1;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

void perror(const char *s)
{
    fprintf(stderr, "%s%s%s\n", s ? s : "", s ? ": " : "", strerror(errno));
}

long ftell(FILE *f)
{
    if (f->fd < 0)
        return -1;
    return f->writing ? f->pos + f->len : f->pos + f->off;
}

int fseek(FILE *f, long off, int whence)
{
    if (f->fd < 0)
        return -1;
    if (f->writing && flush_writes(f) < 0)
        return -1;
    long target;
    switch (whence) {
    case SEEK_SET: target = off; break;
    case SEEK_CUR: target = f->pos + f->off + off; break;
    case SEEK_END: {
        int end = fs_seek(f->fd, 0, FS_SEEK_END);
        if (end < 0)
            return glaze_fail(end);
        target = end + off;
        break;
    }
    default: errno = EINVAL; return -1;
    }
    if (target < 0) {
        errno = EINVAL;
        return -1;
    }
    /* A seek inside the read-ahead keeps it. */
    if (target >= f->pos && target <= f->pos + f->len && whence != SEEK_END) {
        f->off = (int)(target - f->pos);
    } else {
        int r = fs_seek(f->fd, (int32_t)target, FS_SEEK_SET);
        if (r < 0)
            return glaze_fail(r);
        f->pos = target;
        f->len = f->off = 0;
    }
    f->eof = false;
    return 0;
}

void rewind(FILE *f)
{
    fseek(f, 0, SEEK_SET);
    f->err = false;
}

int feof(FILE *f) { return f->eof && f->off == f->len; }
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) { f->eof = f->err = false; }

int remove(const char *path)
{
    int r = fs_remove(path);
    return r < 0 ? glaze_fail(r) : 0;
}

int unlink(const char *path)
{
    return remove(path);
}

int rename(const char *from, const char *to)
{
    int r = fs_rename(from, to);
    return r < 0 ? glaze_fail(r) : 0;
}
