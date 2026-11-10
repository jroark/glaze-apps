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
| `vlc` | VLC media player 0.8: MP3, WAV, MPEG-1 and Motion-JPEG video, HTTP and ICY radio, M3U/PLS ([guide](docs/VLC.md)) |
| `minesweeper`, `solitaire`, `tetris`, `snake`, `flappy`, `fifteen`, `chess` | Games |
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
pinned engine in `third_party/doomgeneric` through `packages/doom/engine`. VLC
builds the pinned VLC, libmad and libmpeg2 in `third_party` (lists in
`packages/vlc/sources.mk`).

## Testing

```sh
make test                                         # native tests (host cc, ASan/UBSan)
make -C tests doom-test DOOM_WAD=/abs/doom1.wad   # optional full-engine Doom checks
```

The native tests compile parts of glaze-os (libgfx, libui, test stubs, its
native wolfSSL build) from the sibling checkout (`GLAZE`, default `../glaze-os`).
VLC's (`vlc-test`) builds for Linux, VLC 0.8's own platform, and runs in the
mips-cross-dev container on other hosts.

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

Glaze code is GPLv3 (`LICENSE`). DoomGeneric, VLC, libmad and libmpeg2 are
GPL-2.0-or-later, elected GPLv3; see [third_party/README.md](third_party/README.md).
No game data is included.
