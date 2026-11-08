# Breakout

Installable Glaze game. Build with
`.claude/skills/glaze-build/scripts/build_app.sh breakout`, then install
`out/pkg/breakout.gzp` from Files on a card.

Controls: drag a finger across the play field to slide the paddle (a tap also
serves the ball), or hold Left/Right to move it and press OK to serve. ESC
opens the pause menu (Up/Down and OK, or a tap). Home saves and suspends;
reopening a saved game requires Resume.

Rules: 11 x 6 brick grid, 3 balls, 9 levels that cycle through five layouts.
A brick is worth 7 points times the level; orange bricks take two hits and
grey steel never breaks (the layouts keep steel away from the bottom row).
Where the ball lands on the paddle sets its outgoing angle, from almost
straight up at the centre to about 55 degrees at the edge. The ball speeds up
every 8 bricks broken, up to a cap. If a ball bounces around without breaking
anything for about 10 seconds it is re-aimed, and it is aimed downward if it
has got above the bricks, so a level can never be stalled. Clearing a layout
starts the next one; the best score survives a new game.

State is a versioned, checksummed record in `data/state`, replaced through a
temporary file. An invalid save starts a fresh game.

Tests: `make -C tests breakout-test` (rules, physics and saves, ASan/UBSan).
For the emulator: `.claude/skills/glaze-ui-test/scripts/ui_test.sh breakout`.
