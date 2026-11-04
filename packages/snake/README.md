# Snake

Installable Glaze snake game. Build with `./build.sh out/pkg/snake.gzp`;
install the package from Files on a card. No extra resources are needed.

Use the BE-300 hardware buttons: the arrow keys steer the snake, and Escape
opens a menu: Up/Down selects Resume or New Game; OK chooses. New Game asks for
confirmation (default Cancel). Escape also resumes or closes the confirmation.
Home saves and suspends. Reopening a saved game requires Resume. Best score
survives starting over.

Rules: a 22x20 grid of 10 px cells. Each move queues at most one turn, and a
turn straight back into the neck is refused. Hitting a wall or the body ends the
game; following the cell the tail leaves is safe, but eating into the tail kills.
Food is worth 10 points and every 5 pieces advances a level, so speed runs from
170 ms per move down to a 70 ms floor at level 11. Filling the board wins.

Game state uses a versioned checksum-protected record in `data/state`, replaced
through a temporary file. Invalid saves start a fresh game. Save failures appear
on screen. The record is private to this package/version, not a public format.

Run `make -C tests snake-test` for deterministic rules/save checks with ASan and
UBSan. For the emulator smoke test, put only this package on a card:

```
../glaze-os/tools/mkcfimg.sh out/snake-cf.img 16 16 out/pkg/snake.gzp
CF=out/snake-cf.img ../glaze-os/tests/run.sh tests/ui/t_snake.txt 40
```

To check persistence across a fresh boot after the smoke run (the image is
rebuilt with the resume script; the file system comes from the run above):

```
FS_FROM=out/shots/t_snake/glaze.bin ../glaze-os/tests/run.sh tests/ui/t_snake_resume.txt 14
```

The resume run launches the installed package from the previous filesystem,
resumes the saved snake, steers it with arrows, then opens the pause menu.
