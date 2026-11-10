# VLC 0.8.6i's core and the modules this port builds, relative to
# packages/vlc (the vlc, libmad and libmpeg2 links reach third_party). Used by
# this package's Makefile and by tests/Makefile's native build. Each module is
# a builtin named after its MOD_ variable.

# ---- the core (libvlc) -------------------------------------------------------
VLC_CORE := libvlc.c \
    interface/interface.c interface/interaction.c \
    playlist/playlist.c playlist/sort.c playlist/loadsave.c playlist/view.c playlist/item.c \
    playlist/item-ext.c playlist/services_discovery.c \
    input/access.c input/clock.c input/control.c input/decoder.c input/demux.c input/es_out.c \
    input/input.c input/stream.c input/mem_stream.c input/subtitles.c input/var.c \
    video_output/video_output.c video_output/vout_pictures.c video_output/video_text.c \
    video_output/video_widgets.c video_output/vout_subpictures.c video_output/vout_synchro.c \
    video_output/vout_intf.c \
    audio_output/common.c audio_output/dec.c audio_output/filters.c audio_output/input.c \
    audio_output/mixer.c audio_output/output.c audio_output/intf.c \
    osd/osd.c osd/osd_text.c osd/osd_widgets.c \
    network/getaddrinfo.c network/io.c network/error.c network/tcp.c network/tls.c \
    misc/charset.c misc/md5.c misc/mtime.c misc/block.c misc/modules.c misc/threads.c \
    misc/stats.c misc/strings.c misc/unicode.c misc/cpu.c misc/configuration.c misc/image.c \
    misc/iso_lang.c misc/messages.c misc/objects.c misc/variables.c misc/error.c \
    misc/hashtables.c misc/xml.c misc/version.c \
    extras/libc.c extras/getopt.c extras/getopt1.c \
    control/core.c control/playlist.c control/input.c control/audio.c control/video.c
VLC_CORE_SRC := $(addprefix vlc/src/,$(VLC_CORE))

# ---- builtin modules: name := sources ---------------------------------------
MOD_dummy := $(addprefix vlc/modules/misc/dummy/,dummy.c interface.c input.c decoder.c encoder.c aout.c vout.c renderer.c)
MOD_memcpy := vlc/modules/misc/memcpy/memcpy.c
MOD_access_file := vlc/modules/access/file.c
MOD_access_http := vlc/modules/access/http.c
MOD_mpga := vlc/modules/demux/mpeg/mpga.c
MOD_mpgv := vlc/modules/demux/mpeg/mpgv.c
MOD_wav := vlc/modules/demux/wav.c
MOD_avi := vlc/modules/demux/avi/avi.c vlc/modules/demux/avi/libavi.c
MOD_ps := vlc/modules/demux/ps.c
MOD_playlist := $(addprefix vlc/modules/demux/playlist/,playlist.c m3u.c pls.c old.c)
MOD_export := vlc/modules/misc/playlist/export.c vlc/modules/misc/playlist/m3u.c
MOD_mpeg_audio := vlc/modules/codec/mpeg_audio.c
MOD_araw := vlc/modules/codec/araw.c
MOD_adpcm := vlc/modules/codec/adpcm.c
MOD_libmpeg2 := vlc/modules/codec/libmpeg2.c
MOD_packetizer_mpegvideo := vlc/modules/packetizer/mpegvideo.c
MOD_mpgatofixed32 := vlc/modules/audio_filter/converter/mpgatofixed32.c
MOD_fixed32tos16 := vlc/modules/audio_filter/converter/fixed32tos16.c
MOD_s16tofixed32 := vlc/modules/audio_filter/converter/s16tofixed32.c
MOD_trivial_channel_mixer := vlc/modules/audio_filter/channel_mixer/trivial.c
MOD_trivial_resampler := vlc/modules/audio_filter/resampler/trivial.c
MOD_linear_resampler := vlc/modules/audio_filter/resampler/linear.c
MOD_trivial_mixer := vlc/modules/audio_mixer/trivial.c
MOD_i420_rgb := $(addprefix vlc/modules/video_chroma/,i420_rgb.c i420_rgb8.c i420_rgb16.c)
VLC_MODULES := $(patsubst MOD_%,%,$(filter MOD_%,$(.VARIABLES)))
VLC_MODULES_SRC := $(foreach m,$(VLC_MODULES),$(MOD_$(m)))

VLC_MAD_SRC := $(addprefix libmad/,bit.c decoder.c fixed.c frame.c huffman.c layer12.c layer3.c \
    stream.c synth.c timer.c version.c)
VLC_MPEG2_SRC := $(addprefix libmpeg2/libmpeg2/,alloc.c cpu_accel.c cpu_state.c decode.c header.c \
    idct.c motion_comp.c slice.c)

# Preprocessor flags every VLC object takes (after the configuration's -I).
VLC_CPPFLAGS := -DHAVE_CONFIG_H -D__VLC__ -DSYS_GLAZE -Ivlc/include -Ivlc/src -Ilibmad \
    -Ilibmpeg2/include
# Upstream's warning policy, for the vendored objects only.
VLC_VENDOR_WARNINGS := -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
    -Wno-unused-but-set-variable -Wno-missing-field-initializers -Wno-implicit-fallthrough \
    -Wno-type-limits -Wno-pointer-sign -Wno-old-style-declaration -Wno-empty-body \
    -Wno-unused-value -Wno-parentheses -Wno-format-truncation -Wno-misleading-indentation \
    -Wno-unused-but-set-parameter -Wno-cast-function-type -Wno-unused-label -Wno-multichar \
    -Wno-discarded-qualifiers
# SIZEOF_INT as the mad.h VLC includes has it: libmad's own fixed.h otherwise
# takes long for mad_fixed_t, 64 bits on a 64-bit host.
VLC_MAD_CPPFLAGS := -DFPM_64BIT -DOPT_SPEED -DSIZEOF_INT=4

# The list the core registers at start-up (src/misc/modules_builtin.h), for
# the given module names.
define VLC_BUILTIN_H
{ echo '#define ALLOCATE_BUILTIN( NAME ) AllocateBuiltinModule( p_this, vlc_entry__ ## NAME );'; \
  echo 'int vlc_entry__main( module_t* );'; \
  $(foreach m,$(1),echo 'int vlc_entry__$(m)( module_t* );';) \
  echo '#define ALLOCATE_ALL_BUILTINS() do { \'; \
  $(foreach m,$(1),echo '    ALLOCATE_BUILTIN($(m)); \';) \
  echo '} while( 0 )'; }
endef
