# Files, saving and resources

## Paths

- `res_bundle()` is the installed app folder, e.g. `/Applications/<id>.app`. It is valid after `ui_init`.
- Write only in `<res_bundle()>/data/`. Updates keep `data/`; uninstall deletes it.
- `FS_PATH_MAX` is 128.

```c
static char path[FS_PATH_MAX], temp[FS_PATH_MAX];
snprintf(path, sizeof(path), "%s/data", res_bundle());
fs_mkdirs(path);                                   /* create once at startup */
snprintf(path, sizeof(path), "%s/data/state", res_bundle());
snprintf(temp, sizeof(temp), "%s/data/state.tmp", res_bundle());
```

## Whole-file helpers (`#include <glaze/fs.h>`)

All of these return a negative `GZ_E*` code on error.

```c
int fs_read_file(const char *path, void *buf, uint32_t max);         /* bytes read */
int fs_write_file(const char *path, const void *buf, uint32_t len);  /* bytes written; creates/replaces */
int fs_rename(const char *from, const char *to);                     /* replaces `to` */
int fs_remove(const char *path);
int fs_mkdirs(const char *path);
const char *fs_strerror(int err);
```

Lower level: `fs_open(path, FS_O_READ|FS_O_WRITE|FS_O_CREATE|FS_O_TRUNC|FS_O_APPEND)`,
`fs_read`, `fs_write`, `fs_seek`, `fs_close`, `fs_stat`, `fs_list`. Grep `fs.h` for details.

## Safe save pattern (use this)

```c
static void save(void)
{
    uint8_t bytes[SAVE_BYTES];
    game_encode(&game, bytes);                      /* header + checksum + struct */
    save_failed = fs_write_file(temp, bytes, sizeof(bytes)) != (int)sizeof(bytes);
    if (!save_failed)
        save_failed = fs_rename(temp, path) < 0;    /* atomic replace */
}
/* load: read one byte more than expected, so an oversized file fails decode */
uint8_t bytes[SAVE_BYTES + 1];
int n = fs_read_file(path, bytes, sizeof(bytes));
if (n > 0 && game_decode(&game, bytes, (unsigned)n)) paused = true;  /* restored */
else new_game();                                                      /* missing or corrupt */
```

- Save at moments that matter (score change, game over, pause) and on `GZ_EV_APP_DEACTIVATE`.
  Do not save every frame: flash wears out and writes are slow.
- Show a message on screen when `save_failed` is set. Never crash on a bad file.

## Preferences (small settings) (`#include <glaze/prefs.h>`)

```c
int pref_get(const char *key, void *buf, uint32_t len);   /* bytes, or GZ_ENOENT if unset */
int pref_put(const char *key, const void *buf, uint32_t len);
```

Prefix keys with your app id: `"<id>.difficulty"`. For game state, use the data file instead.

## Bundled resources (read-only)

Files in `packages/<id>/res/` are packed into the app.

```c
uint32_t size;
const void *p = res_map("levels.bin", &size);   /* NULL if missing; do not free */
gfx_image img; bool ok = res_image(&img, "ball.gzi");
```

Limits: at most 64 files per package; entry paths at most 47 chars; no hidden files.
