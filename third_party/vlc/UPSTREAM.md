# VLC media player 0.8.6i, libmad and libmpeg2

| Project | Release | Tarball | SHA-256 |
| --- | --- | --- | --- |
| VLC | 0.8.6i "Janus" | https://download.videolan.org/pub/videolan/vlc/0.8.6i/vlc-0.8.6i.tar.bz2 | `a866768f7dd8254c62e059327094073800ed968214b5b35e2682eb81f448214f` |
| libmad | 0.15.1b | https://downloads.sourceforge.net/project/mad/libmad/0.15.1b/libmad-0.15.1b.tar.gz | `bbfac3ed6bfbc2823d3775ebb931087371e142bb0e9bb1bee51a76a6e0078690` |
| libmpeg2 | 0.5.1 | https://sources.buildroot.net/libmpeg2/libmpeg2-0.5.1.tar.gz | `dee22e893cb5fc2b2b6ebd60b88478ab8556cb3b93f9a0d7ce8f3b61851871d4` |

`tools/vlc_vendor.sh [DIR]` re-creates `third_party/vlc`, `libmad` and `libmpeg2`
from these tarballs (checked against the hashes) and applies `patches/*.patch` in
order; the result matches what is committed.

VLC 0.8.6i is the last 0.8 release, the line of the 2005–2006 Windows CE builds.
Those were XScale (ARM) binaries, so this is a native build from source. libmad and
libmpeg2 are VLC 0.8's own non-ffmpeg MP3 and MPEG-1/2 decoders. libmpeg2 0.4.1, the
version VLC 0.8.6 was released against, is no longer on any mirror; 0.5.1 has the
same API for what `modules/codec/libmpeg2.c` uses.

All three are GPL-2.0-or-later (`COPYING` in each); the Glaze app elects GPLv3.
Copyright notices in the individual files remain in place.

## What is vendored

- VLC: `include/`, every `src/` source and header (not the `src/test` programs or
  the BeOS and Darwin glue), `COPYING`, `AUTHORS`, `THANKS`, and the cone icon. Only
  the modules `packages/vlc/Makefile` builds are kept; no build system, no other
  modules, no `po/`, no skins.
- libmad: the library sources, headers, tables and licence files.
- libmpeg2: `include/mpeg2.h`, `attributes.h`, the C library sources, and the
  licence files. No SIMD or video-output code.

## Local changes (`patches/`)

- `0001-glaze-threads-and-clock.patch`: a `GLAZE_THREADS` branch beside the
  pthread, Win32 and BeOS ones in `vlc_threads.h`, `vlc_threads_funcs.h` and
  `src/misc/threads.c` (mutexes are libc's `gz_mutex`; condition variables and
  joinable threads are built from kernel semaphores in
  `packages/vlc/glaze/threads.c`). The system has 256 semaphores and VLC gives
  each of its objects, modules included, two mutexes and a condition variable,
  so a mutex's semaphore is made on its first lock and a condition variable's on
  its first wait: VLC then holds about 70 rather than all of them. A Glaze
  case in `src/misc/mtime.c` gives `mdate`, `mwait` and `msleep` on the
  monotonic clock.
- `0002-glaze-builtins-and-files.patch`: `module_handle_t` for a build without a
  plugin loader; `unicode.c` treats Glaze's paths as UTF-8; `access/file.c`
  takes the FILE-stream path written for Windows CE, since Glaze's `read` and
  `close` are for sockets; the playlist demux module leaves out the formats that
  need an XML parser (B4S, podcast, XSPF, Shoutcast) and DVB channel lists, and
  the export module keeps only M3U.
- `0003-upstream-fixes.patch`: bugs in 0.8.6i itself, regenerated as one diff per
  file against the tarball (no other patch touches these files):
  - `charset.c` compared a pointer with `'\0'` instead of the character it
    points to; `vout_intf.c` defined `DIR_SEP` twice where a platform `#if` was
    lost; `avi.c` passed a `const` list where the configuration takes a mutable
    one.
  - `playlist/item-ext.c`: the all-items view's root shares the `pp_items`
    array, and deleting an item through the view moved it (`realloc`) without
    updating `pp_items`, so clearing a playlist that had held an M3U's or PLS's
    items read freed memory (found by ASan in the native test).
    `playlist_Delete` takes the array back from the view, and
    `playlist_ItemToNode` updates the view after removing from `pp_items`.
  - `s16tofixed32.c` scaled 16-bit samples by `FIXED32_ONE >> 16`, half of full
    scale, so every PCM and ADPCM sound played 6 dB quiet on the fixed-point
    path; `>> 15` makes the round trip through `fixed32tos16` exact.
  - `input/decoder.c`: an input ended once its decoders' queues were empty,
    while the last of the audio still waited in the output for its date, and
    ending deleted it: the end of every file was cut off. The decoder now
    records when its last audio ends, and is not empty before then.
  - `access/http.c`: the first read of an ICY stream did not stop at the first
    metadata block, so when more than one block's worth had arrived (a fast
    link) the metadata was played as sound and every later block misread.

Everything else Glaze needs is outside the vendored tree, in `packages/vlc/glaze/`:
`config.h` in place of autoconf's, the compat layer (`compat.h`, `printf.c`,
`stdio.c`, `libc.c`, `math.c`, `sysinc/`), stubs for the core parts left out
(stream output, OSD menu files), and the Glaze modules in `glaze/modules/`: the
interface (`intf_glaze`, on libui, run on the app's main thread), the audio
output (`aout_glaze`, the audio server's stream mode), the video output
(`vout_glaze`, the libui surface) and a Motion-JPEG decoder (`codec_mjpeg`, on
stb_image, since VLC 0.8 decoded MJPEG only through ffmpeg).

## Build notes

Modules are linked in as builtins (`-D__BUILTIN__`), with no plugin loader and no
FPU: audio takes 0.8's fixed-point path (libmad, fixed32, s16). libmad uses
`FPM_64BIT` (the VR4131's 32x32->64 multiply); its `FPM_MIPS` assembly uses `hi`/`lo`
constraints current GCC no longer accepts. libmad's sources also get
`SIZEOF_INT=4`, as the `mad.h` VLC includes has it; without it their
`mad_fixed_t` is a `long`, which is 64 bits on a 64-bit host. `OPTIMIZE_MEMORY` is set, as the CE
builds had it: otherwise every input allocates a 12 MiB stream cache. Flags are MIPS2/O32 soft-float PIC with
`-fno-strict-aliasing -fwrapv`; a few upstream warning categories are turned off
for vendor objects only.

The source lists and shared flags are in `packages/vlc/sources.mk`, which the
package's Makefile and the native test (`tests/Makefile`, `vlc-test`) both use.
The test builds the same tree for Linux with pthreads in place of Glaze's
threads (`tests/host/vlc/config.h`), with capture outputs, under ASan and UBSan.
