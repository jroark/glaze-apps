# TwixT

Installable Glaze TwixT game. Build with `./build.sh out/pkg/twixt.gzp`;
install the package from Files on a card. No extra resources beyond the icon.

You always play Red and connect the top and bottom red bands; the computer
answers as Black, connecting left and right. Both play on a 12x12 dot grid
whose four corner dots are dead holes. A turn is a single peg: it
automatically builds every knight-move rod it has earned, and a rod is
refused only if another live rod crosses it -- crossings are first come,
first served for both colors, there is no BLOCKED move, and turns simply
alternate. Red may not end its chain on the leftmost or rightmost column,
Black not on the top or bottom row; that is all the end-hole rule keeps.

Play by touch or with the BE-300 hardware buttons: tap near a dot to drop a
peg on the closest one (a far-from-any-dot tap just moves the cursor), or
drive the white cursor with the arrow keys and drop with OK. Your last peg
and the computer's are flagged, the cursor marks where the next one lands.

Escape pauses and opens a menu: Up/Down selects Resume, Undo Move, New Game,
Resign or Strength; OK chooses or a tap picks the row. Undo pops one full
pair so you are on turn again. Strength cycles Weak, Normal, Strong; the
setting shows beside the navbar title. New Game and Resign ask for
confirmation (default Cancel). Escape closes the confirmation or resumes.
Home saves and suspends. A reopened saved game starts paused so you can
look at the position first.

The computer plays Black with the search in `game.c`: take an instant win,
otherwise plug the single hole that would lose for it right now, otherwise
a positional score over new rods built, rods denied to you, and home-band
proximity; Strong also looks one reply deeper so it never hands back an
instant win. The game ends when either chain joins its home edges, when the
side to move has no legal dot, or at the 128-ply record cap.

Game state is a strength byte followed by a versioned checksum-protected
record in `data/state`, replaced through a temporary file: the whole peg
history, one byte per peg. Because live rods depend on play order (two
crossing rods compete), a save restores by replaying the history through
the real rules, so a corrupt or forged record can never produce an
impossible board: it is refused and a fresh game starts instead. A save
taken while Black is thinking gets Black's reply once the game resumes.
Save failures appear on screen. The record is private to this
package/version, not a public format.

Run `make -C tests twixt-test` for deterministic rules and save checks with
ASan and UBSan: legality and end holes, auto-linking, competing crossings
in both play orders, both chain wins, the board-full draw, undo rebuild,
save roundtrips and forged-record rejects, and the three AI levels (win
taken, mate threatened blocked, deterministic). For the emulator smoke
test, put only this package on a card:

```
../glaze-os/tools/mkcfimg.sh out/twixt-cf.img 16 out/pkg/twixt.gzp
CF=out/twixt-cf.img ../glaze-os/tests/run.sh tests/ui/t_twixt.txt 40
```

To check persistence across a fresh boot after the smoke run (the file
system comes from the run above):

```
FS_FROM=out/shots/t_twixt/glaze.bin ../glaze-os/tests/run.sh tests/ui/t_twixt_resume.txt 20
```

The resume run launches the installed package from the previous filesystem,
decodes the saved mid-game history onto a fresh board, resumes, plays two
more pegs on row 0 (forbidden ground for Black, so the script never races
the AI's replies), then opens the pause menu.

## Art

`res/` and `icon.gzi` are drawn for this game by `art/draw_art.py`: a wooden pegboard, shaded red and black pegs, and a felt backdrop; links are drawn as anti-aliased rods.
It needs Pillow and the shared helpers in `tools/gzart.py`; re-run it after
editing (`--sheet out.png` writes a contact sheet). The GZIs are committed.
Without `res/` the game draws its original flat shapes.
