# VLC for Glaze

VLC media player 0.8.6i, the last of the 0.8 line that ran on Windows CE
handhelds, built from source for Glaze. The Windows CE builds were XScale (ARM)
binaries, which Glaze's MIPS runtime cannot run. It plays music, video and
internet radio from the memory card, the Music and Movies folders and the
network.

## Install

Build `out/pkg/vlc.gzp` (`./build.sh`), copy it to a CompactFlash card and open
it in Files to install it. VLC opens to its menu:

- **Music**, **Movies** and **Memory Card** browse `/Music`, `/Movies` and
  `/Card`. Only files VLC can play are listed, with folders first. Tap a file to
  add it to the playlist and play it.
- **Open Network Stream** takes a URL on the on-screen keyboard (or a docked
  keyboard). A name with no `http://` gets one.
- **Playlist** lists what has been opened. Tap an item to play it; **Clear**
  empties the list.
- **Now Playing** shows the title, the station's current song (ICY) or the
  artist, a scrubber, previous/play/next and the volume. Video plays full
  screen; tap it for the controls, which hide again after 3 s.

| Key | Action |
| --- | --- |
| OK | Play / pause |
| Left / Right | Seek back / forward 10 s |
| Up / Down | Volume |
| Esc | Back (from the video, to Now Playing) |

Pressing Home pauses VLC and closes its audio stream, so the Music player can
use the speaker. VLC saves the playlist, the current item, its position and the
volume in `/Applications/vlc.app/data`. When you come back it is still paused;
after a restart, OK resumes where it was. If the Music player starts while VLC
is playing, VLC pauses.

## What plays

| Kind | Formats |
| --- | --- |
| Audio files | MP3 and MP2 (libmad, fixed point), WAV (PCM and IMA ADPCM) |
| Video files | MPEG-1 program streams (`.mpg`, libmpeg2; it decodes MPEG-2 too, but not fast enough for DVD-size video), Motion-JPEG AVI with PCM or ADPCM sound |
| Playlists | M3U and PLS, on the card or over HTTP |
| Network | HTTP files and SHOUTcast/Icecast (ICY) radio, with stream titles |

VLC 0.8 decoded everything else through ffmpeg, which this port leaves out:
MPEG-4, H.264, AAC, Ogg, WMA and the rest. There is no HTTPS: VLC 0.8 needs
GnuTLS for it. For radio with HTTPS-only streams, use the SomaFM app.

### Making files that play well

The CPU is a 166 MHz VR4131 with no FPU and no SIMD. Measured in the emulator:

- **MPEG-1** at 320×240 (shown at 240×180), 24 fps, 600 kbit/s, MP2 audio: every
  frame, 24 fps.
- **Motion-JPEG** at 320×240: about 11 fps. Each frame takes about 90 ms to
  decode, so frames that would show late are skipped and the sound keeps time.
- MP3 and WAV play with a few audio underruns at most.

```sh
# MPEG-1 video for the BE-300's 240x320 screen
ffmpeg -i in.mp4 -vf scale=320:240 -r 24 -c:v mpeg1video -b:v 600k \
    -c:a mp2 -b:a 128k -ar 44100 -f mpeg out.mpg
# Motion-JPEG AVI, as the Videos app takes it (plays at about 11 fps)
ffmpeg -i in.mp4 -vf scale=320:240 -r 12 -c:v mjpeg -q:v 6 -pix_fmt yuvj420p \
    -c:a pcm_s16le -ar 22050 -ac 1 out.avi
# MP3
ffmpeg -i in.flac -c:a libmp3lame -b:a 128k out.mp3
```

## Limits

- **Memory:** VLC runs with about 9.5 MiB free and is built with
  `OPTIMIZE_MEMORY`, as the Windows CE builds were: a 128 KiB stream cache
  instead of 12 MiB.
- **Network speed:** in the emulator, HTTP arrives at 11–12 KB/s. That carries
  radio up to about 80 kbit/s; a 128 kbit/s stream (16 KB/s) runs late and VLC
  0.8 drops its audio rather than rebuffering. Glaze's TCP receive window (4 ×
  1460 bytes, `glaze-os/servers/net/lwipopts.h`) bounds the rate. Real hardware
  has not been measured.
- **MPEG program streams** lose about the first quarter second of their sound,
  in VLC 0.8's program-stream path (the native test measures it).
- **The end of a file** can lose up to about 100 ms of sound: the audio output
  works in whole buffers.

## Source and licence

The vendored sources are VLC 0.8.6i, libmad 0.15.1b and libmpeg2 0.5.1,
GPL-2.0-or-later, elected GPLv3 for this app. Pins, hashes, what was pruned
and every local patch are in
[third_party/vlc/UPSTREAM.md](../third_party/vlc/UPSTREAM.md), and
`tools/vlc_vendor.sh` re-creates the tree from the release tarballs. The Glaze
parts are in `packages/vlc/glaze`: the configuration and compat layer, the
thread backend, and the interface, audio output, video output and
Motion-JPEG decoder modules.

## Tests

- `make -C tests vlc-test` builds the same core, modules and configuration for
  Linux with ASan and UBSan (in the build container on other hosts). It plays
  `tests/fixtures/vlc` (made by `tools/mkvlcfixtures.sh`) through capture
  outputs. It checks PCM sample for sample, each tone's pitch, picture
  counts, sizes and colours, an M3U playlist, and HTTP, ICY (with its stream
  title) and M3U over HTTP from a local server.
- `tests/ui/t_vlc.txt` installs VLC, opens the card, plays an MPEG-1 and pauses
  it; the screenshot shows the video with its controls. Its header has the card
  and command.
- A `/Card/autoplay.m3u` present at launch plays by itself (URL entries once
  the network is up) and reports progress, the end of the list and audio
  underruns to the debug log as `vlc: ...` lines. It is for tests: leave it off
  everyday cards.
