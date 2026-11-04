# Flappy

Installable Glaze side-scrolling flying game. Build with `./build.sh out/pkg/flappy.gzp`;
install the package from Files on a card. No extra resources are needed.

Use the BE-300 hardware buttons: OK or Up flaps, Escape opens a menu: Up/Down
selects Resume, New Game or Quit to Home; OK chooses. New Game asks for
confirmation (default Cancel). Escape also resumes or closes the confirmation.
Home saves and suspends. Reopening a saved game requires Resume. Best score
survives starting over.

Rules: a 240x224 field with gravity pulling the bird down and a flap setting it
going up; the fall speed caps out. Pipes scroll left in pairs with a 72 px gap,
spaced 120 px apart; passing a pair scores a point, and touching a shaft, the
ceiling or the floor ends the game. Scroll speed runs from 60 px/s to 120 px/s
by score 150. The pipe rims are drawn outside the collision box, so clipping a
lip is survivable.

Game state uses a versioned checksum-protected record in `data/state`, replaced
through a temporary file. Invalid saves start a fresh game. Save failures appear
on screen. The record is private to this package/version, not a public format.

Run `make -C tests flappy-test` for deterministic rules/save checks with ASan
and UBSan. For the emulator smoke test, put only this package on a card:

```
../glaze-os/tools/mkcfimg.sh out/flappy-cf.img 16 16 out/pkg/flappy.gzp
CF=out/flappy-cf.img ../glaze-os/tests/run.sh tests/ui/t_flappy.txt 40
```

To check persistence across a fresh boot after the smoke run (the image is
rebuilt with the resume script; the file system comes from the run above):

```
FS_FROM=out/shots/t_flappy/glaze.bin ../glaze-os/tests/run.sh tests/ui/t_flappy_resume.txt 14
```

The resume run launches the installed package from the previous filesystem,
resumes the saved bird mid-field, flaps it past the pair ahead, then opens the
pause menu.
