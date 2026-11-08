# 1942

A 1942-style vertical scrolling shooter for Glaze. Build with
`.claude/skills/glaze-build/scripts/build_app.sh sky1942`, then install
`out/pkg/sky1942.gzp` from Files on a card.

Controls: hold the arrows (or drag a finger anywhere in the sea) to fly the
plane; the two wing guns fire on their own, so there is no fire button. The
plane stays in the lower half of the field. ESC opens the pause menu (Up/Down
and OK, or tap a row). Home saves and suspends; reopening a saved game
requires Resume.

Rules: waves of enemy planes fly in from the top: small weaving olive
fighters, fast red divers, and green twin-engine bombers that carry 4 hull
and drop bombs. A wave ends when all its queued planes are dead or gone:
bonus points, then a "GO!" intro before the next, faster wave. A hit (bomb
or ramming) costs one of three lives and clears the screen of bombs, with a
few invincible seconds
while the plane blinks. Fighters 100, divers 150, bombers 400, wave bonus
50 x wave; the best score survives.

State is a versioned, checksummed record in `data/state`, replaced through a
temporary file. The world runs on fixed 20 ms steps, so replays of one save
are identical. An invalid save starts a fresh game.

Art: the default sprites in `res/` are drawn for this app by
`packages/sky1942/art/draw_art.py` (pixel maps in the script; the generated
GZIs are committed, re-run the script after editing the maps). Optionally,
`python3 packages/sky1942/art/arcade_art.py` swaps in the original Capcom
arcade sprites from a fan remake's image dump
([kavros/1942-Arcade-Game](https://github.com/kavros/1942-Arcade-Game), code
Apache-2.0, but the sheets are Capcom's and not freely licensed, so never
commit that swap). Without `res/` at all the app draws simple placeholder
shapes and plays the same.

Tests: `make -C tests sky1942-test` (rules, determinism and saves,
ASan/UBSan). For the emulator:
`.claude/skills/glaze-ui-test/scripts/ui_test.sh sky1942` (install, fly,
pause) and the resume check
`FS_FROM=out/shots/t_sky1942/glaze.bin .claude/skills/glaze-ui-test/scripts/ui_test.sh sky1942 tests/ui/t_sky1942_resume.txt`.
