#!/bin/sh
# Re-creates tests/fixtures/vlc, the small media files the native VLC test
# (tests/native/vlc.c) plays: one of each format the port decodes. Tones and
# flat colours, so the test can check what comes out without reference
# decodes: a tone's zero crossings give its pitch, a colour is a colour.
#
#   tools/mkvlcfixtures.sh      # needs ffmpeg
set -eu
out=$(cd "$(dirname "$0")/.." && pwd)/tests/fixtures/vlc
mkdir -p "$out"
cd "$out"
ff() { ffmpeg -loglevel error -y -bitexact "$@"; }

# PCM WAV: 1 s of 440 Hz, 22.05 kHz mono.
ff -f lavfi -i "sine=f=440:d=1:r=22050" -c:a pcm_s16le pcm.wav
# IMA ADPCM WAV: 1 s of 550 Hz, 22.05 kHz mono.
ff -f lavfi -i "sine=f=550:d=1:r=22050" -c:a adpcm_ima_wav adpcm.wav
# MP3: 1 s, 44.1 kHz stereo, 440 Hz left and 660 Hz right.
ff -f lavfi -i "sine=f=440:d=1" -f lavfi -i "sine=f=660:d=1" \
    -filter_complex "[0][1]join=inputs=2:channel_layout=stereo" -c:a libmp3lame -b:a 64k \
    -write_xing 0 -id3v2_version 0 tone.mp3
# Motion-JPEG AVI: 1 s of 64x48 at 10 fps in one colour, with 8 kHz PCM.
ff -f lavfi -i "color=c=0x3060C0:s=64x48:r=10:d=1" -f lavfi -i "sine=f=440:d=1:r=8000" \
    -c:v mjpeg -q:v 2 -pix_fmt yuvj420p -c:a pcm_s16le mjpeg.avi
# MPEG-1 program stream: 1 s of 64x48 at 24 fps in another colour, with MP2.
ff -f lavfi -i "color=c=0xC03020:s=64x48:r=24:d=1" -f lavfi -i "sine=f=440:d=1" \
    -c:v mpeg1video -q:v 2 -c:a mp2 -b:a 64k -ac 1 -f mpeg mpeg1.mpg
# A playlist of two of them, by relative name.
printf '#EXTM3U\n#EXTINF:1,PCM\npcm.wav\n#EXTINF:1,ADPCM\nadpcm.wav\n' > list.m3u
ls -l "$out"
