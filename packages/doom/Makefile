# An SDK project. The top-level Makefile packs it with its icon.
GLAZE_SDK ?= $(abspath ../../../glaze-os/out/sdk/glaze-sdk)
# engine/ links to ../../third_party/doomgeneric, the pinned engine (no WAD).
SOURCES := $(wildcard *.c) $(wildcard engine/*.c)
CPPFLAGS += -Wno-unused-parameter -Iengine -include compat.h -fno-strict-aliasing -fwrapv
include $(GLAZE_SDK)/share/app.mk
# Keep upstream's warning policy separate from the Glaze glue.
$(patsubst %.c,$(BUILD)/%.o,$(wildcard engine/*.c)): CPPFLAGS += -Wno-unused-function \
    -Wno-unused-variable -Wno-sign-compare -Wno-type-limits -Wno-missing-field-initializers \
    -Wno-implicit-fallthrough -Wno-unused-but-set-parameter -Wno-enum-conversion
