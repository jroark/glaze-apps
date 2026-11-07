# __NAME__

<!-- TEMPLATE TEXT: rewrite Controls and Rules for your game. -->
Installable Glaze game. Build with
`.claude/skills/glaze-build/scripts/build_app.sh __ID__`, then install
`out/pkg/__ID__.gzp` from Files on a card.

Controls: arrows move the cursor, OK collects the star under it, a tap moves
the cursor to the tapped cell and collects there, and ESC opens the pause menu
(Up/Down and OK). Home saves and suspends; reopening a saved game requires Resume.

Rules: a 10x10 board and a 30-second round. Each star is worth 10 points. The
best score survives a new game.

State is a versioned, checksummed record in `data/state`, replaced through a
temporary file. An invalid save starts a fresh game.

Tests: `make -C tests __ID__-test` (rules and saves, ASan/UBSan). For the
emulator: `.claude/skills/glaze-ui-test/scripts/ui_test.sh __ID__`.
