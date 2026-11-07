---
name: glaze-ui-test
description: Run a Glaze app in the BE-300 emulator with a scripted input file (install from card, launch, play), then check the screenshot and log. Use after the package builds, to verify drawing, input, saving and resume.
---

# Emulator UI test

```sh
.claude/skills/glaze-ui-test/scripts/ui_test.sh <id> [tests/ui/t_<id>.txt] [seconds]
```

It does these steps:
1. builds the package;
2. makes a card holding only `<id>.gzp`;
3. builds an OS image with your script baked in (about 40 s total);
4. boots it and takes ONE screenshot at the end;
5. prints:
   - `SCREENSHOT: <png>`: **open this image and check it** against what the script should leave on screen;
   - `LAUNCH:`: whether `<id>` actually launched;
   - `FAULTS:`: crashes (`none` is good);
   - `APP OUTPUT`: your `printf("<id>: ...")` lines, the best evidence of state.

`seconds` defaults to the last event + 8 s. A shorter run can end before the last event.
Each run is slow, so make each one count; don't loop blindly.

## Script format (`tests/ui/t_<id>.txt`)

One event per line: `<ms since boot> <command>`. Times must increase. `#` starts a comment.

| Command | Effect |
| --- | --- |
| `tap X Y` | Touch down and up (80 ms) at screen X, Y |
| `drag X0 Y0 X1 Y1 MS` | Swipe over MS milliseconds |
| `down X Y` / `move X Y` / `up X Y` | Raw touch events |
| `key NAME [held_ms]` | `ok esc up down left right home power enter backspace` |
| `text STRING` | Types the rest of the line (hardware-keyboard text) |

**Coordinates are screen coordinates**: the 16 px status bar is on top. App y = screen y − 16.
To tap app point (x, y), write `tap x y+16`.

## Template script (install + launch + play)

```
# <Name>: install from a card holding only <id>.gzp, play, pause.
#   .claude/skills/glaze-ui-test/scripts/ui_test.sh <id>
# unlock swipe
3000 drag 40 280 235 280 500
# Files icon on the home screen
4500 tap 30 120
# Card row in Files
6500 tap 120 64
# first file on the card: <id>.gzp
8500 tap 120 64
# Install on Flash
10500 tap 120 242
# OK on the "installed" message
15000 tap 120 203
# back to the home screen, then the new app's icon
16000 key home
18000 tap 90 192
# --- your app is in front from about 19000 ---
20000 key right
21000 key ok
22000 tap 120 170
23500 key esc
```

Copy these lines as they are; they work on a factory OS image. Leave ≥ 1 s
between steps, 2 s after a launch. Don't probe the layout up front: the
`LAUNCH:` line tells you if the icon tap missed.

**If `LAUNCH:` says your app never launched**, the OS image's layout differs.
For example, a pre-installed app moves Card down one row (`tap 120 100`) and moves your icon one
cell right (`tap 150 192`). Run this and look at the PNG:

```sh
.claude/skills/glaze-ui-test/scripts/home_shot.sh
```

Home icons are on a grid: x = 30, 90, 150, 210; y = 48, 120, 192. A new app takes the
first free cell. If Maps (or another unexpected app) shows, `rm ../glaze-os/out/fs.img`
rebuilds a clean factory flash on the next run.

## Checking results

- Plan the final screen. Only the last frame is captured, so end the script on
  the state you want to see: the pause menu, a game over, or a moved piece.
  To check an earlier moment, write a shorter copy of the script.
- Prefer `printf` evidence for numbers (score, position); use the screenshot for layout
  (overlaps, clipped text, colors, off-screen items).
- **Resume test** (save survives Home + reboot): after a run, boot its flash again with a
  script that only unlocks and taps the icon:

  ```sh
  FS_FROM=out/shots/t_<id>/glaze.bin .claude/skills/glaze-ui-test/scripts/ui_test.sh <id> tests/ui/t_<id>_resume.txt
  ```

  The app should print `ready ... paused=1` with the old score. (The path names the previous
  run's flash; `run.sh` finds it under `../glaze-os/out/shots/`, so type it exactly as shown.)
- Keep the final script as `tests/ui/t_<id>.txt`. Delete throwaway scripts.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| Screenshot shows the lock screen | The run was too short, or the unlock drag is missing. |
| Screenshot looks dimmed | Expected under your own pause/game-over overlay. Otherwise it was taken mid-transition: add 1–2 s after the last event. |
| Taps do nothing in the app | Wrong y (add 16), the tap is outside your hit rect, or the run ended before the tap. |
| `FAULTS:` shows `fault`/`exception` | A crash: a NULL pointer, out-of-bounds access, or too much stack. Reproduce it in the native test (`glaze-native-test`). |
| Build fails before booting | Fix it with `glaze-build` first. |
