---
name: glaze-app
description: Start here to write, change, build or test a Glaze app or small game in glaze-apps (Casio BE-300, C). Gives the hard platform rules, the step-by-step workflow, and which glaze-* skill to load for each step.
---

# Building a Glaze app or game

Glaze is a small OS for the Casio BE-300 handheld. Apps are C programs built
against the Glaze SDK into installable `.gzp` packages. Each app lives in
`packages/<id>/`. Run every command from the glaze-apps repo root.

## Hard rules (break one and the build or the device fails)

- **C only** (gnu11). Freestanding MIPS II, soft float. Never link libgcc or a Linux libc.
- **Small libc**: no `fopen`/`FILE`, `rand`, `time`, pthreads, `sqrt`, or `math.h`.
  Use `fs_*` for files and an xorshift RNG seeded from `gz_time_ms()`. Use integers;
  `float` needs `LDLIBS += -l:libsoftfp.a` and is slow. See glaze-api `libc.md`.
- **Screen**: the app draws a 240x304 surface (`UI_W`x`UI_H`). Colors are `0xRRGGBB`.
  Touch coordinates are relative to the app surface.
- **Memory**: 16 MiB shared by everything; thread stacks are 64 KiB. Use no big locals:
  make large arrays `static`.
- **Lifecycle**: on `GZ_EV_APP_DEACTIVATE`, save state. The app may then be
  killed without warning. Data goes in `<res_bundle()>/data/`.
- **Warning-free**: the build must have zero warnings.
- **Split logic from UI**: rules go in `game.c`/`game.h` with no `glaze/` includes, so
  they can be tested on the host. `main.c` does drawing, input and saving.
- **Never commit** credentials, WADs, ROMs or `out/`.

## Workflow

| Step | Do | Skill |
| --- | --- | --- |
| 1 | Scaffold `packages/<id>` + test + make target | `glaze-new-app` |
| 2 | Write the rules in `game.c`/`game.h`, with asserts in `tests/native/<id>.c` | `glaze-native-test` |
| 3 | Run `make -C tests <id>-test` until it passes | `glaze-native-test` |
| 4 | Write the screen and input in `main.c` | `glaze-api` |
| 5 | Build the package warning-free | `glaze-build` |
| 6 | Run in the emulator, look at the screenshot | `glaze-ui-test` |
| 7 | Update `packages/<id>/README.md`, then the done checklist | this file |

Work in small steps: change one thing, then build or test. Fix the first error
first; later errors are often caused by it.

## Saving context (important: your context is small)

- Do not read whole SDK headers. Use the cheat sheets in `glaze-api/references/`.
  For one exact signature:
  `grep -n "name" ../glaze-os/out/sdk/glaze-sdk/sysroot/usr/include/glaze/*.h`
- One example is enough:
  - `packages/snake/` is keys + timer + save (the template is based on it);
  - `packages/fifteen/main.c` is touch-only and very short.
- Never open `third_party/`, `packages/doom`, `packages/chatgpt`, `packages/gmail`,
  `packages/aim` or `packages/maps`: they are large and unrelated.
- Build/test scripts print only the important lines; full logs are in `out/`.
  Read a log with `grep` or `tail`, never `cat`.

## Done checklist

- [ ] `make -C tests <id>-test` prints `<id>-test: ok`, and `make test` passes (all apps)
- [ ] `.claude/skills/glaze-build/scripts/build_app.sh <id>` prints `OK`
- [ ] `ui_test.sh <id>` shows `<id> launched` and no faults, and the screenshot looks right
- [ ] `packages/<id>/README.md` describes the controls, rules and saving
- [ ] The app is listed in the README.md table (the scaffold adds it to Games)
- [ ] Compared with `git status` before you started, the only new changes are your app's
      files, `tests/native/<id>.c`, `tests/ui/t_<id>.txt`, `tests/Makefile` and `README.md`
