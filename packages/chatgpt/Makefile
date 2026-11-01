# An SDK project. The top-level Makefile packs it with its icon.
GLAZE_SDK ?= $(abspath ../../../glaze-os/out/sdk/glaze-sdk)
# http/ is the Browser's HTTP/TLS transport, copied from glaze-os (http/README.md).
SOURCES := $(wildcard *.c) $(wildcard http/*.c)
CPPFLAGS += -Wno-unused-parameter -Ihttp -DWOLFSSL_USER_SETTINGS
LDLIBS += -l:libwolfssl.so.1
# Diagnostic fixtures only: CHATGPT_TEST_ORIGIN redirects the client to a test
# server. http_io.o is always rebuilt, so a normal build drops the override.
ifneq ($(CHATGPT_TEST_ORIGIN),)
CPPFLAGS += -DCHATGPT_TEST_ORIGIN='"$(CHATGPT_TEST_ORIGIN)"'
endif
include $(GLAZE_SDK)/share/app.mk
$(BUILD)/http_io.o: FORCE
.PHONY: FORCE
FORCE:
# Compiled as glaze-os compiles them for Browser: small, and hidden except
# tls.c's explicitly exported glaze_tls_seed/glaze_tls_time callbacks.
$(patsubst %.c,$(BUILD)/%.o,$(wildcard http/*.c)): CPPFLAGS += -Os -fvisibility=hidden
