# An SDK project. The top-level Makefile packs it with its icon.
#
# VLC 0.8.6i's core and a fixed set of modules, linked statically as builtins
# (no plugins), with libmad and libmpeg2. The vendored sources are reached
# through the vlc, libmad and libmpeg2 links to ../../third_party; see
# third_party/vlc/UPSTREAM.md for the pins and local changes. The source
# lists are in sources.mk, which tests/Makefile's native build shares.
GLAZE_SDK ?= $(abspath ../../../glaze-os/out/sdk/glaze-sdk)

include sources.mk

GLUE_SRC := $(wildcard glaze/*.c)
# Glaze's own modules (glaze/modules/<name>.c): builtins under the glue's
# warning policy.
GLUE_MODULES := aout_glaze vout_glaze codec_mjpeg intf_glaze
GMOD_SRC := $(addprefix glaze/modules/,$(addsuffix .c,$(GLUE_MODULES)))

SOURCES := $(GLUE_SRC) $(GMOD_SRC) $(VLC_CORE_SRC) $(VLC_MODULES_SRC) $(VLC_MAD_SRC) $(VLC_MPEG2_SRC)

include $(GLAZE_SDK)/share/app.mk

# Every object sees the Glaze configuration and compat layer first.
CPPFLAGS += -Iglaze -include glaze/compat.h $(VLC_CPPFLAGS) -Iglaze/sysinc \
    -fno-strict-aliasing -fwrapv -Wno-unused-parameter
# Soft-float builtins for the core's few float paths (exported by the SDK).
LDLIBS += -l:libsoftfp.a
# The standard JPEG Huffman tables for Motion-JPEG frames (codec_mjpeg).
LDLIBS += -l:libmedia.so.1

VENDOR_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(VLC_CORE_SRC) $(VLC_MODULES_SRC) $(VLC_MAD_SRC) $(VLC_MPEG2_SRC))
# Upstream's warning policy is kept apart from the Glaze glue.
$(VENDOR_OBJ): CFLAGS += $(VLC_VENDOR_WARNINGS)
$(patsubst %.c,$(BUILD)/%.o,$(VLC_CORE_SRC) $(GLUE_SRC)): CPPFLAGS += -D__LIBVLC__
$(patsubst %.c,$(BUILD)/%.o,$(VLC_MAD_SRC)): CPPFLAGS += $(VLC_MAD_CPPFLAGS)
$(patsubst %.c,$(BUILD)/%.o,$(VLC_MPEG2_SRC)): CPPFLAGS += -Ilibmpeg2/libmpeg2

# Each module is compiled as a builtin named after its MOD_ variable.
define BUILTIN
$(patsubst %.c,$(BUILD)/%.o,$(MOD_$(1))): CPPFLAGS += -D__BUILTIN__ -DMODULE_NAME=$(1) -DMODULE_NAME_IS_$(1)
endef
$(foreach m,$(VLC_MODULES),$(eval $(call BUILTIN,$(m))))
$(foreach m,$(GLUE_MODULES),$(eval MOD_$(m) := glaze/modules/$(m).c)$(eval $(call BUILTIN,$(m))))

$(BUILD)/gen/modules_builtin.h: Makefile sources.mk
	@mkdir -p $(dir $@)
	$(call VLC_BUILTIN_H,$(VLC_MODULES) $(GLUE_MODULES)) > $@
$(BUILD)/vlc/src/misc/modules.o: $(BUILD)/gen/modules_builtin.h
$(BUILD)/vlc/src/misc/modules.o: CPPFLAGS += -I$(BUILD)/gen
