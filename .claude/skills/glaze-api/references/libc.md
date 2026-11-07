# Glaze libc: what exists

Glaze's C library is small. If a function is not listed here, assume it does
not exist. Check with:
`grep -rn "name" ../glaze-os/out/sdk/glaze-sdk/sysroot/usr/include/`

## Available

| Header | Functions |
| --- | --- |
| `<stdio.h>` | `snprintf`, `vsnprintf`, `printf`, `puts`. Nothing else: no `FILE`, `fopen`, `sprintf`, `scanf`. |
| `<string.h>` | `memset memcpy memmove memcmp memchr strlen strnlen strcmp strncmp strcpy strncpy strlcpy strcat strncat strlcat strchr strrchr strstr strspn strcspn strdup strndup` |
| `<stdlib.h>` | `malloc calloc realloc free exit abort abs atoi strtol strtoul qsort`. No `rand`, `atof`, `getenv`. |
| `<ctype.h>` | `isdigit isalpha isalnum isspace isupper islower isprint ispunct isxdigit tolower toupper` |
| `<stdint.h> <stdbool.h> <stddef.h> <limits.h> <inttypes.h> <stdarg.h>` | as usual |
| `<assert.h>` | `assert` |
| `<glaze/sys.h>` | `uint32_t gz_time_ms(void)`, `void gz_sleep_ms(uint32_t)` (do not sleep in the UI loop; use the `ui_next_event` timeout) |
| `<glaze/thread.h>` | `gz_thread_*`, `gz_mutex_*` (rarely needed; grep the header) |

`snprintf`/`printf` support `%d %i %u %x %X %p %s %c %%`, a width, `-` (left
align), `0` padding, `%.Ns` and the `l` modifier. There is **no `%f`** and no
`%lld` (64-bit). To print a `uint32_t`, cast it: `printf("%u", (unsigned)v)`.

## Missing, and what to use instead

| Want | Use |
| --- | --- |
| `rand()` | xorshift in your Game struct: `r ^= r << 13; r ^= r >> 17; r ^= r << 5;` seeded with `gz_time_ms()` (never 0) |
| `time()`, clocks | `gz_time_ms()` for durations; `ui_now_secs()` for wall-clock seconds |
| `float`/`double`, `sqrt`, `sin` | Integers. Use fixed point (e.g. positions × 16, or × 256). For angles, a small sine table. For distance, compare squared lengths. If floats are unavoidable, add `LDLIBS += -l:libsoftfp.a` to the package Makefile (slow; there is still no `math.h`). |
| `fopen`/`fread` | `fs_read_file`/`fs_write_file` (`files.md`) |
| `sleep`/`usleep` | `ui_next_event(&e, ms)` timeout |
| pthreads | `glaze/thread.h` (avoid threads in simple apps) |
| C++ / VLAs of big size / recursion deep | Not supported or unsafe: 64 KiB stack. Make big arrays `static`. |

64-bit integer divide (`uint64_t / x`) works (libc has `__divdi3`), but is slow; prefer 32-bit.

## Typical warnings and fixes

- `implicit declaration of function 'X'`: a missing `#include`, or X does not exist on Glaze.
- `format '%d' expects 'int' but argument has 'uint32_t'`: cast, e.g. `(int)v` or `(unsigned)v`.
- `comparison of integer expressions of different signedness`: cast one side, e.g. `n != (int)sizeof(buf)`.
- `unused variable`/`unused function`: delete it. Unused parameters are already allowed.
