# Glaze apps

Installable apps for [Glaze](https://github.com/jroark/glaze-os), the
microkernel OS for the Casio BE-300. Each is a `.gzp` package, offered in the
App Store or installed from a CompactFlash card through Files.

| Package | App |
| --- | --- |
| `aim` | AIM client speaking OSCAR to a server you name (test against a self-hosted Open OSCAR Server) |
| `gmail` | Gmail over IMAPS/SMTPS with an App Password, or any IMAP/SMTP server |
| `chatgpt` | Chat: Codex with ChatGPT account sign-in ([guide](docs/CHATGPT.md)) |
| `somafm` | SomaFM internet radio ([guide](docs/SOMAFM.md)) |
| `doom` | DoomGeneric port; bring your own WAD ([guide](docs/DOOM.md)) |
| `minesweeper`, `solitaire`, `tetris`, `snake`, `flappy`, `fifteen`, `chess`, `twixt`, `breakout`, `sky1942`, `pacman` | Games |
| `sketch` | Drawing |

## Building

The apps build against the Glaze application SDK, which a sibling
[glaze-os](https://github.com/jroark/glaze-os) checkout exports:

```sh
git clone https://github.com/jroark/glaze-os ../glaze-os
(cd ../glaze-os && ./build.sh sdk)    # out/sdk/glaze-sdk
./build.sh                            # out/pkg/<app>.gzp, in the mips-cross-dev container
make                                  # the same, with mipsel-linux-gnu-gcc installed
```

Set `GLAZE_SDK` to use another SDK, or `GLAZE_DIR` to use another glaze-os
checkout. Each `packages/<app>` is an SDK project. The top-level Makefile packs
it with its icon using the SDK's `mkromfs.py` and checks it with `glaze-sdk check`.
Gmail and Chat link the system's `libwolfssl.so.1`. Chat also carries a copy of
the Browser's HTTP/TLS transport (`packages/chatgpt/http/`), and Doom links the
pinned engine in `third_party/doomgeneric` through `packages/doom/engine`.

## Art

Games draw their sprites and icons with `packages/<id>/art/draw_art.py`,
which uses Pillow and the shared helpers in `tools/gzart.py` (GZI writer,
supersampled shapes, shaded spheres, glows, tileable noise, wood, icon
tiles). The generated `res/*.gzi` and `icon.gzi` are committed, so building
needs neither Python nor Pillow; re-run a script after changing it.

## Testing

```sh
make test                                         # native tests (host cc, ASan/UBSan)
make -C tests doom-test DOOM_WAD=/abs/doom1.wad   # optional full-engine Doom checks
```

The native tests compile parts of glaze-os (libgfx, libui, test stubs, its
native wolfSSL build) from the sibling checkout (`GLAZE`, default `../glaze-os`).

Scripted UI tests in `tests/ui/` run through glaze-os's `tests/run.sh`, which
builds an OS image with the script baked in, boots it with a card, and saves the
final screen. Each script's header gives its card and command, for example:

```sh
../glaze-os/tools/mkcfimg.sh out/cf.img 16 16 ../glaze-os/out/pkg/hello.gzp out/pkg/minesweeper.gzp out/pkg/solitaire.gzp
CF=out/cf.img ../glaze-os/tests/run.sh tests/ui/t_minesweeper.txt 34
```

AIM's and Gmail's sounds are regenerated with glaze-os's
`tools/mksounds.py --aim packages/aim/res` and `--gmail packages/gmail/res`.

## License

Glaze code is GPLv3 (`LICENSE`). DoomGeneric is GPL-2.0-or-later, elected
GPLv3; see [third_party/README.md](third_party/README.md). No game data is included.
