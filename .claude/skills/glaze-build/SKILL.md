---
name: glaze-build
description: Build one Glaze app package (out/pkg/<id>.gzp) warning-free and fix common compile, link and package errors. Use after changing any file in packages/<id>.
---

# Build a Glaze app

```sh
.claude/skills/glaze-build/scripts/build_app.sh <id>
```

The script:
- cross-compiles with the MIPS toolchain (in Docker via `./build.sh`, or natively with `make` when `mipsel-linux-gnu-gcc` is installed);
- packs the package and runs `glaze-sdk check`;
- prints only the important lines. The full log is `out/build-<id>.log`; search it with `grep`, don't `cat` it.

| Output | Meaning |
| --- | --- |
| `OK: out/pkg/<id>.gzp (...)` | Done. |
| `BUILD HAS WARNINGS` | It compiled, but warnings are not allowed. Fix each one. |
| `BUILD FAILED` | Read the **first** error line and fix it; later errors often follow from it. |
| `no SDK at ...` | The SDK is missing: `(cd ../glaze-os && ./build.sh sdk)` (Docker) or `make sdk`. |

Messages look like `game.c:42:7: warning: ...`, meaning file `packages/<id>/game.c`, line 42.

Build the host test first (`make -C tests <id>-test`): it compiles faster and
catches logic bugs with ASan/UBSan. Then build the package.

The device build uses `-Wall -Wextra -O2` (unused parameters allowed). That is the same
warning set as the native test, so -O2 can add warnings (e.g. `may be used uninitialized`).

## Error → fix

| Message | Fix |
| --- | --- |
| `implicit declaration of function 'X'` | Add the `#include`. If X is a desktop function (`fopen`, `rand`, `sqrt`…), it does not exist: see glaze-api `libc.md`. |
| `undefined reference to 'X'` | The function does not exist on Glaze, or is in a library not linked. `__adddf3`, `__mulsf3`… mean float math: use integers, or add `LDLIBS += -l:libsoftfp.a` to `packages/<id>/Makefile`. Never add `-lgcc`, `-lm` or `-lc`. |
| `unknown type name 'FILE'` / `'time_t'` | Not available. Use `fs_*` and `uint32_t` milliseconds. |
| `format '%d' expects argument of type 'int'` | Cast: `(int)x` or `(unsigned)x`. `%f` does not exist. |
| `comparison of integer expressions of different signedness` | Cast one side: `n != (int)sizeof(buf)`. |
| `unused variable` / `defined but not used` | Delete it. |
| `'X' may be used uninitialized` | Initialize it where it is declared. |
| `array subscript ... is above array bounds` | A real bug: fix the index or the array size. |
| `glaze-sdk check` fails on Info | `packages/<id>/Info` needs `id=`, `name=` (≤ 31 chars), `version=` (≤ 15), `exec=app`; id must equal the folder name. |
| check fails on the icon | `icon.gzi` must be 44x44. Run `python3 .claude/skills/glaze-new-app/scripts/mkicon.py packages/<id>/icon.gzi` again. |
| ELF too large (> 2 MiB) | Large static tables or data. Move data to `res/` files and load them with `res_map`. |
| `docker: ... Cannot connect` | Docker isn't running. Ask the user to start it. |

## Rules

- Do not change compiler flags in the package Makefile, except `LDLIBS += -l:libsoftfp.a` when floats are truly needed.
- `make clean` only after changing flags. The build already tracks headers.
