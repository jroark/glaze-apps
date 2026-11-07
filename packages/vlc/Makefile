# An SDK project. The top-level Makefile packs it with its icon.
#
# VLC 0.8.6i's core and a fixed set of modules, linked statically as builtins
# (no plugins), with libmad and libmpeg2. The vendored sources are reached
# through the vlc, libmad and libmpeg2 links to ../../third_party; see
# third_party/vlc/UPSTREAM.md for the pins and local changes.
GLAZE_SDK ?= $(abspath ../../../glaze-os/out/sdk/glaze-sdk)

# ---- the core (libvlc) -------------------------------------------------------
CORE := libvlc.c \
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
CORE_SRC := $(addprefix vlc/src/,$(CORE))

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
MODULES := $(patsubst MOD_%,%,$(filter MOD_%,$(.VARIABLES)))

MAD_SRC := $(addprefix libmad/,bit.c decoder.c fixed.c frame.c huffman.c layer12.c layer3.c \
    stream.c synth.c timer.c version.c)
MPEG2_SRC := $(addprefix libmpeg2/libmpeg2/,alloc.c cpu_accel.c cpu_state.c decode.c header.c \
    idct.c motion_comp.c slice.c)
GLUE_SRC := $(wildcard glaze/*.c)

SOURCES := $(GLUE_SRC) $(CORE_SRC) $(foreach m,$(MODULES),$(MOD_$(m))) $(MAD_SRC) $(MPEG2_SRC)

include $(GLAZE_SDK)/share/app.mk

# Every object sees the Glaze configuration and compat layer first.
CPPFLAGS += -Iglaze -include glaze/compat.h -DHAVE_CONFIG_H -D__VLC__ -DSYS_GLAZE \
    -Ivlc/include -Ivlc/src -Ilibmad -Ilibmpeg2/include -Iglaze/sysinc \
    -fno-strict-aliasing -fwrapv -Wno-unused-parameter
# Soft-float builtins for the core's few float paths (exported by the SDK).
LDLIBS += -l:libsoftfp.a

VENDOR_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(CORE_SRC) $(foreach m,$(MODULES),$(MOD_$(m))) $(MAD_SRC) $(MPEG2_SRC))
# Upstream's warning policy is kept apart from the Glaze glue.
$(VENDOR_OBJ): CFLAGS += -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
    -Wno-unused-but-set-variable -Wno-missing-field-initializers -Wno-implicit-fallthrough \
    -Wno-type-limits -Wno-pointer-sign -Wno-old-style-declaration -Wno-empty-body \
    -Wno-unused-value -Wno-parentheses -Wno-format-truncation -Wno-misleading-indentation \
    -Wno-unused-but-set-parameter -Wno-cast-function-type -Wno-unused-label -Wno-multichar -Wno-discarded-qualifiers
$(patsubst %.c,$(BUILD)/%.o,$(CORE_SRC) $(GLUE_SRC)): CPPFLAGS += -D__LIBVLC__
$(patsubst %.c,$(BUILD)/%.o,$(MAD_SRC)): CPPFLAGS += -DFPM_64BIT -DOPT_SPEED
$(patsubst %.c,$(BUILD)/%.o,$(MPEG2_SRC)): CPPFLAGS += -Ilibmpeg2/libmpeg2

# Each module is compiled as a builtin named after its MOD_ variable.
define BUILTIN
$(patsubst %.c,$(BUILD)/%.o,$(MOD_$(1))): CPPFLAGS += -D__BUILTIN__ -DMODULE_NAME=$(1) -DMODULE_NAME_IS_$(1)
endef
$(foreach m,$(MODULES),$(eval $(call BUILTIN,$(m))))

# The list the core registers at start-up (src/misc/modules_builtin.h).
$(BUILD)/gen/modules_builtin.h: Makefile
	@mkdir -p $(dir $@)
	{ echo '#define ALLOCATE_BUILTIN( NAME ) AllocateBuiltinModule( p_this, vlc_entry__ ## NAME );'; \
	  echo 'int vlc_entry__main( module_t* );'; \
	  $(foreach m,$(MODULES) $(GLUE_MODULES),echo 'int vlc_entry__$(m)( module_t* );';) \
	  echo '#define ALLOCATE_ALL_BUILTINS() do { \'; \
	  $(foreach m,$(MODULES) $(GLUE_MODULES),echo '    ALLOCATE_BUILTIN($(m)); \';) \
	  echo '} while( 0 )'; } > $@
$(BUILD)/vlc/src/misc/modules.o: $(BUILD)/gen/modules_builtin.h
$(BUILD)/vlc/src/misc/modules.o: CPPFLAGS += -I$(BUILD)/gen
