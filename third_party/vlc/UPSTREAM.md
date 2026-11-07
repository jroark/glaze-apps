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
  `packages/vlc/glaze/threads.c`), and a Glaze case in `src/misc/mtime.c` for
  `mdate`, `mwait` and `msleep` on the monotonic clock.
- `0002-glaze-builtins-and-files.patch`: `module_handle_t` for a build without a
  plugin loader; `unicode.c` treats Glaze's paths as UTF-8; `access/file.c`
  takes the FILE-stream path written for Windows CE, since Glaze's `read` and
  `close` are for sockets; the playlist demux module leaves out the formats that
  need an XML parser (B4S, podcast, XSPF, Shoutcast) and DVB channel lists, and
  the export module keeps only M3U.
- `0003-upstream-fixes.patch`: bugs in 0.8.6i itself. `charset.c` compared a
  pointer with `'\0'` instead of the character it points to; `vout_intf.c`
  defined `DIR_SEP` twice where a platform `#if` was lost; `avi.c` passed a
  `const` list where the configuration takes a mutable one.

Everything else Glaze needs is outside the vendored tree, in `packages/vlc/glaze/`:
`config.h` in place of autoconf's, the compat layer (`compat.h`, `printf.c`,
`stdio.c`, `libc.c`, `math.c`, `sysinc/`), stubs for the core parts left out
(stream output, OSD menu files), and the Glaze modules.

## Build notes

Modules are linked in as builtins (`-D__BUILTIN__`), with no plugin loader and no
FPU: audio takes 0.8's fixed-point path (libmad, fixed32, s16). libmad uses
`FPM_64BIT` (the VR4131's 32x32->64 multiply); its `FPM_MIPS` assembly uses `hi`/`lo`
constraints current GCC no longer accepts. Flags are MIPS2/O32 soft-float PIC with
`-fno-strict-aliasing -fwrapv`; a few upstream warning categories are turned off
for vendor objects only.
