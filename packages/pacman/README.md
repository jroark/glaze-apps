# Pac-Man

Installable Glaze game: a Pac-Man style maze chase. Build with
`.claude/skills/glaze-build/scripts/build_app.sh pacman`, then install
`out/pkg/pacman.gzp` from Files on a card.

## Controls

- Arrow keys or a swipe anywhere on the maze steer Pac-Man (turns apply at
  tile centres; the wanted direction is buffered, so a press during a
  corridor still takes effect at the next junction).
- ESC pauses (menu: Resume / New Game, keys or taps). Home saves and
  suspends; reopening a saved game waits for Resume.
- On "Level clear!" OK or a tap starts the next maze; after Game Over OK or
  a tap starts a new game.

## Rules

- 15x17 tile maze with a wrap-around tunnel in the middle row, four power
  pellets in the corners and 113 pellets.
- Blinky chases Pac-Man, Pinky aims four tiles ahead of him, Inky flanks
  through Blinky's position and Clyde hangs back when close. Ghosts
  alternate scatter and chase phases and never re-enter the ghost house.
- Pellet 10, power pellet 50, fruit 100 (a cherry appears below the ghost
  house after 20 and 50 pellets), frightened ghosts 200/400/800/1600.
- A power pellet frightens the ghosts (they reverse, slow down, choose
  randomly and blink near the end); touching a normal ghost costs a life.
  Clearing all pellets clears the level; power mode shortens each level.
- Pac-Man chomps while moving and shrinks when caught; ghosts wiggle their
  skirts. Square-wave chiptune cues (opening tune, wak-wak, power-up, ghost
  eaten, fruit, death, level fanfare) stream through the audio server at
  11025 Hz and follow the system volume; sound closes on pause and Home.

## Saving

State (positions, maze, mode, score, level, lives, rng) is a versioned,
checksummed 108-byte record in `<bundle>/data/state`, replaced atomically
through a temporary file. `game_decode` rejects any impossible value, so an
invalid save simply starts a fresh game.

## Tests

- `make -C tests pacman-test`: rules, maze connectivity, ghost AI safety,
  tunnel wrap, frightened chain, level/death flow, bounded house bounce and
  save/load round-trips (ASan/UBSan).
- Emulator: `.claude/skills/glaze-ui-test/scripts/ui_test.sh pacman`
  (install, play, pause; `tests/ui/t_pacman.txt`). The kernel log shows
  `audio: start 11025 Hz` once the game primes its sound stream.
