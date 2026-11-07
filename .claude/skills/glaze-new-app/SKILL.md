---
name: glaze-new-app
description: Create a new Glaze app or game package in glaze-apps from a working template (packages/<id>, icon, native test, make target). Use when starting any new Glaze app or game.
---

# New Glaze app

## 1. Scaffold

From the repo root:

```sh
python3 .claude/skills/glaze-new-app/scripts/new_app.py <id> "<Name>" [RRGGBB]
```

- `<id>`: a lowercase letter, then lowercase letters/digits, at most 24 chars (`pong`, `tictactoe`).
- `<Name>`: the home-screen label, at most 31 chars. Keep it short (≤ 10) so it fits under the icon.
- `RRGGBB`: icon color (default green `3C8F46`).

It creates the following and never overwrites anything:

| File | What it is |
| --- | --- |
| `packages/<id>/Info` | id, name, version, `exec=app`. Bump `version` on releases. |
| `packages/<id>/Makefile` | SDK build; all `*.c` files are compiled. Leave it alone. |
| `packages/<id>/game.h`, `game.c` | Pure rules: no `glaze/` includes. Host-testable. |
| `packages/<id>/main.c` | Drawing, keys, touch, pause menu, save/load, lifecycle. |
| `packages/<id>/icon.gzi` | 44x44 icon (a colored tile with a disc). |
| `packages/<id>/README.md` | Controls, rules, saving, tests. Update it. |
| `tests/native/<id>.c` | Asserts for game.c. |
| `tests/Makefile` | Gets an `<id>-test` target, added to `check:`. |
| `README.md` | `<id>` added to the Games row. Move it to its own row if it is not a game. |

The template is a complete game that works as generated: a cursor on a 10x10
grid; arrows move, OK or a tap collects the star, a 30 s round, ESC pause menu,
best score. Check it before changing anything:

```sh
make -C tests <id>-test
.claude/skills/glaze-build/scripts/build_app.sh <id>
```

## 2. Turn the template into your game

Change one layer at a time, building and testing after each.

1. **game.h**: replace the `Game` fields with your state. Rules:
   - fixed-size types (`uint8_t`, `int32_t`…), no pointers, so a save is a plain copy;
   - keep the `rng` field (xorshift; never 0);
   - when the layout changes, bump `SAVE_MAGIC` in game.c (`"GZG1"` → `"GZG2"`, i.e. `0x32475a47`).
2. **game.c**: write your rules as small functions (`game_new`, `game_move`,
   `game_tick(ms)`…). Make `game_decode` reject every impossible value a corrupt
   file could hold: out-of-range positions, a bad `over` flag, `score > best`.
3. **tests/native/<id>.c**: rewrite the asserts for your rules (see `glaze-native-test`).
4. **main.c**: change `draw()`, `play_key()`, `touch()`, the hint text and the
   `printf("<id>: ...")` checkpoints. Keep:
   - the save/load code, `GZ_EV_APP_DEACTIVATE`/`ACTIVATE` handling and the pause menu;
   - the event loop shape: wait `-1` (forever) when nothing moves, a timeout
     (16–100 ms) only while something animates.
5. **README.md** in the package: controls, rules, saving, test commands.

## Notes

- **Not a game?** Keep the same split: data/logic in a pure file, UI in `main.c`.
  A touch-only app looks like `packages/fifteen/main.c` (~120 lines).
  Buttons, lists and switches are in glaze-api `draw.md`.
- **Resources** (images, sounds, data) go in `packages/<id>/res/`. Load them with
  `res_map("name", &size)` or `res_image(&img, "name.gzi")`. Max 64 files per package.
- **Better icon**: run `python3 .claude/skills/glaze-new-app/scripts/mkicon.py packages/<id>/icon.gzi RRGGBB` again, or draw a
  44x44 PNG and run `python3 ../glaze-os/out/sdk/glaze-sdk/tools/mkgzi.py icon.png packages/<id>/icon.gzi`
  (needs Pillow).
- **Text that doesn't fit**: the 240 px width holds about 37 characters of
  `FONT_R11`, 30 of `FONT_B12`, 26 of `FONT_B14` or 18 of `FONT_B20`. Measure with
  `gfx_text_width()` (glaze-api `draw.md`).
