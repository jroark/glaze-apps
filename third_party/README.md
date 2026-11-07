# Third-party code

| Directory | Project | Version | License |
| --- | --- | --- | --- |
| `doomgeneric/` | [DoomGeneric](https://github.com/ozkl/doomgeneric), original Doom software engine | `e257a09527062` | GPL-2.0-or-later, elected GPLv3 for the Glaze app; `COPYING` and `UPSTREAM.md`; no game data |
| `vlc/` | [VLC media player](https://www.videolan.org/vlc/) core and the modules VLC's package builds | 0.8.6i | GPL-2.0-or-later, elected GPLv3 for the Glaze app; `COPYING`; pins and patches in `vlc/UPSTREAM.md` |
| `libmad/` | [MAD](https://www.underbit.com/products/mad/), fixed-point MPEG audio decoder | 0.15.1b | GPL-2.0-or-later; `COPYING`; see `vlc/UPSTREAM.md` |
| `libmpeg2/` | [libmpeg2](https://libmpeg2.sourceforge.io/), MPEG-1/2 video decoder | 0.5.1 | GPL-2.0-or-later; `COPYING`; see `vlc/UPSTREAM.md` |
| `micromax/` | [micro-Max](http://home.hccnet.nl/h.g.muller/max-src2.html), H.G. Muller's minimalist chess engine | 4.8 | no license text upstream; public-source practice, not relicensed; `UPSTREAM.md` |

Doom's package links the engine as `packages/doom/engine`; VLC's links `vlc`, `libmad`
and `libmpeg2` the same way, and `tools/vlc_vendor.sh` re-creates all three. Chat's `packages/chatgpt/http/`
holds copies of glaze-os code and stb_image.h; see its README.
Maps' `packages/maps/puff.c` and `puff.h` are Mark Adler's puff 2.3 (zlib licence, from
glaze-os `third_party/puff`), changed only to use GCC's setjmp builtins on the device.
