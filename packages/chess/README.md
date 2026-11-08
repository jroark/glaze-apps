# Chess

Installable Glaze chess game. Build with `./build.sh out/pkg/chess.gzp`;
install the package from Files on a card. No extra resources are needed: the
engine is vendored in `third_party/micromax` and compiled into the package
(see `third_party/micromax/UPSTREAM.md` for its provenance and patches).

You always play White; the engine answers as Black. Play by touch or with the
BE-300 hardware buttons: tap a white piece, then tap an empty or enemy square
(legal destinations get a dot, captures a red one), or drive the cursor with
the arrow keys and choose with OK: once on the source piece, OK picks it, then
step to the destination and OK again. Last move squares are tinted and a
king in check is flagged red.

Escape pauses and opens a menu: Up/Down selects Resume, New Game, Resign or
Strength; OK chooses. Strength cycles Weak, Normal, Strong (3000, 40000 or
100000 search nodes per reply; about 0.6 s, 2 s and 4 s on the emulator); the
current setting shows beside the navbar title, or `simple` when the engine has
been retired for that game. New Game and Resign ask for confirmation (default
Cancel). Escape closes the confirmation or resumes. Home saves and suspends. A
reopened saved game starts paused so you can look at the position first; New
Game starts a fresh game at the chosen strength.

Rules: standard chess with castling, en passant and queen promotion, checked
against a full model in `game.c` that pins every move to the board before
accepting it. Micro-Max (searching within the strength budget) plays Black,
but the model stays the authority: each engine reply is re-validated, and if
the engine board ever disagrees with the model the engine retires for the
rest of the game and a simple deterministic responder takes over, so play
never stalls. The game ends on checkmate, stalemate, the 50-move rule, three
repetitions of a position, bare-king-vs-bare-king type material draws, or the
512-ply record cap.

Game state uses a strength byte and then a versioned checksum-protected record
in `data/state`, replaced through a temporary file: the whole accepted move
history, three bytes per move. Invalid saves start a fresh game, and a valid
save rebuilds both the model board and the engine board by replay, so a
resumed game picks the engine up mid-position, at the saved strength. A game
saved with Black to move (the app ended mid-search) gets Black's reply once it
resumes. Save failures appear on screen. The record is private to this
package/version, not a public format.

Run `make -C tests chess-test` for deterministic rules, engine glue and
save checks with ASan and UBSan: perft counts against known position tree
sizes, engine self-play and human-move-pending flows, forged-save rejects,
and castling/en-passant/promotion replays. For the emulator smoke test, put
only this package on a card:

```
../glaze-os/tools/mkcfimg.sh out/chess-cf.img 16 16 out/pkg/chess.gzp
CF=out/chess-cf.img ../glaze-os/tests/run.sh tests/ui/t_chess.txt 40
```

To check persistence across a fresh boot after the smoke run (the image is
rebuilt with the resume script; the file system comes from the run above):

```
FS_FROM=out/shots/t_chess/glaze.bin ../glaze-os/tests/run.sh tests/ui/t_chess_resume.txt 20
```

The resume run launches the installed package from the previous filesystem,
decodes the saved mid-game history into a fresh model and engine board,
resumes, plays a3 with the touch picker, then opens the pause menu.

## Art

`res/` and `icon.gzi` are drawn for this game by `art/draw_art.py`: Staunton-style ivory and ebony pieces and a wooden board.
It needs Pillow and the shared helpers in `tools/gzart.py`; re-run it after
editing (`--sheet out.png` writes a contact sheet). The GZIs are committed.
Without `res/` the game draws its original flat shapes.
