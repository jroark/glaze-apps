# An SDK project. The top-level Makefile packs it with its icon.
GLAZE_SDK ?= $(abspath ../../../glaze-os/out/sdk/glaze-sdk)
SOURCES := $(wildcard *.c)
CPPFLAGS += -Wno-unused-parameter
# glaze/gps.h parsing and projection, linked in from the SDK.
LDLIBS += -lgps
include $(GLAZE_SDK)/share/app.mk
