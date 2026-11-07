#!/bin/sh
# Re-creates the pristine part of third_party/{vlc,libmad,libmpeg2} from the
# pinned release tarballs (see third_party/vlc/UPSTREAM.md). Local changes are
# then applied from third_party/vlc/patches/ in order.
#
#   tools/vlc_vendor.sh [DIR]   # DIR holds the tarballs; otherwise they are fetched
#
# The tarballs are checked against the pinned SHA-256 before anything is copied.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TP=$ROOT/third_party
DL=${1:-}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

VLC=vlc-0.8.6i.tar.bz2
VLC_URL=https://download.videolan.org/pub/videolan/vlc/0.8.6i/$VLC
VLC_SHA=a866768f7dd8254c62e059327094073800ed968214b5b35e2682eb81f448214f
MAD=libmad-0.15.1b.tar.gz
MAD_URL=https://downloads.sourceforge.net/project/mad/libmad/0.15.1b/$MAD
MAD_SHA=bbfac3ed6bfbc2823d3775ebb931087371e142bb0e9bb1bee51a76a6e0078690
MPEG2=libmpeg2-0.5.1.tar.gz
MPEG2_URL=https://sources.buildroot.net/libmpeg2/$MPEG2
MPEG2_SHA=dee22e893cb5fc2b2b6ebd60b88478ab8556cb3b93f9a0d7ce8f3b61851871d4

fetch() {   # name url sha
    if [ -n "$DL" ] && [ -f "$DL/$1" ]; then
        cp "$DL/$1" "$WORK/$1"
    else
        curl -sSfL --max-time 600 -o "$WORK/$1" "$2"
    fi
    echo "$3  $WORK/$1" | shasum -a 256 -c - >/dev/null || { echo "$1: checksum mismatch" >&2; exit 1; }
}
fetch $VLC "$VLC_URL" $VLC_SHA
fetch $MAD "$MAD_URL" $MAD_SHA
fetch $MPEG2 "$MPEG2_URL" $MPEG2_SHA
mkdir "$WORK/x"
tar -xjf "$WORK/$VLC" -C "$WORK/x"
tar -xzf "$WORK/$MAD" -C "$WORK/x"
tar -xzf "$WORK/$MPEG2" -C "$WORK/x"
V=$WORK/x/vlc-0.8.6i
M=$WORK/x/libmad-0.15.1b
P=$WORK/x/libmpeg2-0.5.1

copy() {    # srcroot dstroot files...
    s=$1; d=$2; shift 2
    for f in "$@"; do
        mkdir -p "$d/$(dirname "$f")"
        cp "$s/$f" "$d/$f"
    done
}

# ---- VLC: the core, its headers, and the modules Glaze builds in ----------
rm -rf "$TP/vlc/include" "$TP/vlc/src" "$TP/vlc/modules"
mkdir -p "$TP/vlc"
copy "$V" "$TP/vlc" COPYING AUTHORS THANKS share/vlc48x48.png
cp -R "$V/include" "$TP/vlc/include"
rm -f "$TP/vlc/include/Makefile.am" "$TP/vlc/include/Makefile.in"
( cd "$V" && find src -name '*.c' -o -name '*.h' -o -name '*.h.in' ) | while read -r f; do
    case $f in
    src/test/*|src/misc/beos_specific.cpp|src/misc/darwin_specific.m) ;;
    *) copy "$V" "$TP/vlc" "$f" ;;
    esac
done
copy "$V" "$TP/vlc" \
    modules/misc/dummy/dummy.c modules/misc/dummy/dummy.h modules/misc/dummy/interface.c \
    modules/misc/dummy/input.c modules/misc/dummy/decoder.c modules/misc/dummy/encoder.c \
    modules/misc/dummy/aout.c modules/misc/dummy/vout.c modules/misc/dummy/renderer.c \
    modules/misc/memcpy/memcpy.c \
    modules/access/file.c modules/access/http.c \
    modules/demux/mpeg/mpga.c modules/demux/mpeg/mpgv.c modules/demux/wav.c \
    modules/demux/avi/avi.c modules/demux/avi/libavi.c modules/demux/avi/libavi.h \
    modules/demux/ps.c modules/demux/ps.h \
    modules/demux/playlist/playlist.c modules/demux/playlist/playlist.h \
    modules/demux/playlist/m3u.c modules/demux/playlist/pls.c modules/demux/playlist/old.c \
    modules/misc/playlist/export.c modules/misc/playlist/m3u.c \
    modules/codec/mpeg_audio.c modules/codec/araw.c modules/codec/adpcm.c modules/codec/libmpeg2.c \
    modules/packetizer/mpegvideo.c \
    modules/audio_filter/converter/mpgatofixed32.c modules/audio_filter/converter/fixed32tos16.c \
    modules/audio_filter/converter/s16tofixed32.c \
    modules/audio_filter/channel_mixer/trivial.c \
    modules/audio_filter/resampler/linear.c modules/audio_filter/resampler/trivial.c \
    modules/audio_mixer/trivial.c \
    modules/video_chroma/i420_rgb.c modules/video_chroma/i420_rgb.h \
    modules/video_chroma/i420_rgb8.c modules/video_chroma/i420_rgb16.c modules/video_chroma/i420_rgb_c.h

# ---- libmad: fixed-point MPEG audio ---------------------------------------
rm -rf "$TP/libmad"
mkdir -p "$TP/libmad"
copy "$M" "$TP/libmad" COPYING COPYRIGHT CREDITS README \
    bit.c decoder.c fixed.c frame.c huffman.c layer12.c layer3.c stream.c synth.c timer.c version.c \
    bit.h decoder.h fixed.h frame.h global.h huffman.h layer12.h layer3.h stream.h synth.h timer.h \
    version.h mad.h D.dat imdct_s.dat qc_table.dat rq_table.dat sf_table.dat

# ---- libmpeg2: MPEG-1/2 video, portable C paths only ----------------------
rm -rf "$TP/libmpeg2"
mkdir -p "$TP/libmpeg2"
copy "$P" "$TP/libmpeg2" COPYING AUTHORS README \
    include/mpeg2.h include/attributes.h \
    libmpeg2/alloc.c libmpeg2/cpu_accel.c libmpeg2/cpu_state.c libmpeg2/decode.c \
    libmpeg2/header.c libmpeg2/idct.c libmpeg2/motion_comp.c libmpeg2/slice.c \
    libmpeg2/mpeg2_internal.h libmpeg2/vlc.h

# ---- local changes ---------------------------------------------------------
for p in "$TP"/vlc/patches/*.patch; do
    [ -e "$p" ] || continue
    patch -s -p1 -d "$TP" < "$p"
done
echo "vendored VLC 0.8.6i, libmad 0.15.1b and libmpeg2 0.5.1 into $TP"
