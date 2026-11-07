---
name: glaze-api
description: Compact, verified reference for the Glaze app API (events and keys, drawing and fonts, widgets, files and saving, libc limits). Use while writing main.c of a Glaze app instead of reading SDK headers.
---

# Glaze API cheat sheets

Open only the file you need:

| Need | Read |
| --- | --- |
| Event loop, keys, touch, Home/resume, timers | `references/events.md` |
| Drawing, colors, fonts, text width, buttons, lists, alerts, images | `references/draw.md` |
| Saving and loading, files, preferences, resources | `references/files.md` |
| Which C library functions exist; math, random, float | `references/libc.md` |

Paths are relative to this skill's folder: `.claude/skills/glaze-api/`.

The includes an app normally needs:

```c
#include <glaze/ui.h>    /* also gfx.h, res.h, sb.h: drawing, fonts, events */
#include <glaze/fs.h>    /* files */
#include <glaze/sys.h>   /* gz_time_ms, gz_sleep_ms */
#include <stdio.h>       /* snprintf, printf (printf goes to the emulator log) */
#include <string.h>
```

If something is not in these sheets, find its exact signature with one grep, then read only the
lines around the match:

```sh
grep -n "audio_play" ../glaze-os/out/sdk/glaze-sdk/sysroot/usr/include/glaze/*.h
```

Never guess a function name. Desktop functions (`fopen`, `rand`, `sin`, `time`,
`usleep`, `malloc_usable_size`) are not available; `libc.md` lists the replacements.
