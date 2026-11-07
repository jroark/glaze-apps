# Events, input and lifecycle

## Startup

```c
int main(void)               /* no arguments */
{
    ui_init(SB_STATUS_GRAY);  /* connect to SpringBoard; must come first. Or SB_STATUS_BLACK. */
    ...
}
```

`res_bundle()` (the installed folder) works only after `ui_init`.

## Waiting for events

```c
int ui_next_event(struct gz_input_event *ev, int timeout_ms);  /* 1 = event, 0 = timeout; -1 waits forever */

struct gz_input_event {
    uint16_t type;
    uint16_t code;      /* GZ_KEY_* for key events, the ASCII char for GZ_EV_TEXT */
    int16_t x, y;       /* touch position, relative to the app surface (0..239, 0..303) */
    uint32_t time_ms;
};
```

- Wait with `-1` when nothing animates; that saves battery.
- Use a timeout only while something moves: 16-33 ms for smooth motion (falling objects, a ball),
  100 ms when only a countdown changes (the template uses 100).
- Measure real elapsed time with `gz_time_ms()` (`<glaze/sys.h>`, a wrapping `uint32_t`), not by counting loops.

| type | When |
| --- | --- |
| `GZ_EV_TOUCH_DOWN` / `GZ_EV_TOUCH_MOVE` / `GZ_EV_TOUCH_UP` | Finger down / drag / lift (1, 2, 3) |
| `GZ_EV_KEY_DOWN` / `GZ_EV_KEY_UP` | Hardware button (4, 5) |
| `GZ_EV_TEXT` | A typed character in `code` (hardware keyboard) |
| `GZ_EV_TOUCH_CANCEL` | A touch ended without a real lift (Home pressed mid-touch). Clear pressed state; do NOT act. |
| `GZ_EV_APP_DEACTIVATE` | Home pressed. **Save now**; release audio streams. The app is then suspended and may be killed. |
| `GZ_EV_APP_ACTIVATE` | Back in front. Reset your timer base (`last = gz_time_ms()`), redraw. |

Act on a tap at `TOUCH_UP`, and only if `TOUCH_DOWN` was on the same control.

## Keys (`e.code` on KEY_DOWN/KEY_UP)

The BE-300 has no keyboard: just a d-pad, OK, Esc (Cancel) and Home.

`GZ_KEY_UP`, `GZ_KEY_DOWN`, `GZ_KEY_LEFT`, `GZ_KEY_RIGHT`, `GZ_KEY_OK`, `GZ_KEY_ESC`.
Also handle `GZ_KEY_ENTER` like OK, for docked keyboards.
`GZ_KEY_HOME` (the Rocket button) belongs to SpringBoard; the app sees `GZ_EV_APP_DEACTIVATE` instead.

Others: `GZ_KEY_BACKSPACE`, `GZ_KEY_TAB`, `GZ_KEY_DELETE`, `GZ_KEY_PAGEUP`, `GZ_KEY_PAGEDOWN`.

Optional: the 7 printed icons under the screen can become keys while the app is in front:
`ui_claim_hard_icons(0x7F)` (bit n = icon n+1). They then arrive as `GZ_KEY_ICON1` … `GZ_KEY_ICON1 + 6`.

## Held keys and drags

Keys do **not** auto-repeat: holding an arrow gives one `KEY_DOWN`, then a `KEY_UP` on release.
For continuous movement, remember what is held and move in `game_tick`:

```c
static int held;                         /* -1 left, 0 none, +1 right */
if (e.type == GZ_EV_KEY_DOWN && e.code == GZ_KEY_LEFT)  held = -1;
if (e.type == GZ_EV_KEY_DOWN && e.code == GZ_KEY_RIGHT) held = +1;
if (e.type == GZ_EV_KEY_UP && (e.code == GZ_KEY_LEFT || e.code == GZ_KEY_RIGHT)) held = 0;
/* also set held = 0 on GZ_EV_APP_DEACTIVATE */
```

To follow a finger: on `TOUCH_DOWN` set `dragging = true`; on `TOUCH_DOWN` and `TOUCH_MOVE`
set the position from `e.x`; on `TOUCH_UP` or `GZ_EV_TOUCH_CANCEL` set `dragging = false`.

## Game loop pattern (from the template)

```c
uint32_t last = gz_time_ms();
draw();
for (;;) {
    struct gz_input_event e;
    bool running = !paused && !game.over;
    int got = ui_next_event(&e, running ? 16 : -1);
    uint32_t now = gz_time_ms(), elapsed = now - last;
    last = now;
    if (got) {
        if (e.type == GZ_EV_APP_DEACTIVATE) { paused = true; save(); continue; }
        if (e.type == GZ_EV_APP_ACTIVATE) { last = gz_time_ms(); draw(); continue; }
        if (e.type == GZ_EV_KEY_DOWN) handle_key(e.code);
        else handle_touch(&e);
    }
    if (running && !paused) game_tick(&game, elapsed);   /* see the clamp note below */
    draw();
}
```

If `game_tick` moves objects, clamp `ms` first (e.g. `if (ms > 100) ms = 100;`) so a stall
does not teleport them. A plain countdown (the template) needs no clamp.
The loop above uses 16 ms; the template uses 100 ms because only its clock moves.

## Debug output

`printf("<id>: score=%u\n", (unsigned)score);` appears in the emulator log
(`stdout.log`), and `ui_test.sh` prints lines that start with `<id>:`. Print at
key moments (ready, level up, game over, save) so tests can check them.
