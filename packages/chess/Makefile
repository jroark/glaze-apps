# An SDK project. The top-level Makefile packs it with its icon.
GLAZE_SDK ?= $(abspath ../../../glaze-os/out/sdk/glaze-sdk)
# engine/ links to ../../third_party/micromax, the pinned chess engine.
SOURCES := $(wildcard *.c) $(wildcard engine/*.c)
CPPFLAGS += -Wno-unused-parameter -Iengine
# micro-Max relies on signed char overflow in its eval arithmetic, and its
# dense one-liner style trips -Wall -Wextra idioms that are deliberate
# (char-array subscripts like b[x-2], bitwise-combined comparisons such as
# x==K&y==L, and comment-continuation layout); waived for that object only.
$(patsubst %.c,$(BUILD)/%.o,engine/umax4_8.c): CPPFLAGS += -fwrapv \
    -Wno-char-subscripts -Wno-parentheses -Wno-comment
include $(GLAZE_SDK)/share/app.mk
