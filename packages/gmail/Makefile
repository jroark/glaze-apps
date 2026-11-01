# An SDK project. The top-level Makefile packs it with its icon.
GLAZE_SDK ?= $(abspath ../../../glaze-os/out/sdk/glaze-sdk)
SOURCES := $(wildcard *.c)
# The system wolfSSL (libwolfssl.so.1) from the SDK.
CPPFLAGS += -Wno-unused-parameter -DWOLFSSL_USER_SETTINGS
LDLIBS += -l:libwolfssl.so.1
include $(GLAZE_SDK)/share/app.mk
