# Native tests of the apps, run from this directory (app paths are ../-relative).
# Host cc with ASan/UBSan. The apps' tests also compile glaze-os sources (libgfx,
# libui, Browser's transport, test stubs) from a sibling checkout, GLAZE.
CC ?= cc
GLAZE ?= ../../glaze-os
G := $(GLAZE)
O := ../out/host
$(shell mkdir -p $(O))
# glaze-os's host resource loader (res_map) reads its generated assets.
export GLAZE_ASSETS := $(abspath $(G)/assets/gen)

CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -I$(G)/lib/include -I$(G)/host
GFX := $(G)/lib/gfx/gfx.c $(G)/lib/gfx/blit.c $(G)/lib/gfx/text.c $(G)/lib/gfx/res.c
EDIT := $(G)/lib/ui/edit.c $(G)/host/clipboard_stub.c
KEYBOARD := $(G)/lib/ui/keyboard.c $(G)/lib/ui/handwriting.c $(G)/lib/ui/recognize.c $(EDIT)
WEB_CFLAGS := $(CFLAGS) -I$(G)/third_party/stb -I$(G)/third_party/wolfssl -I$(G)/apps/browser/tls \
    -DWOLFSSL_USER_SETTINGS -I$(G)/third_party/jerryscript -I$(G)/apps/browser/js_platform -I$(G)/out/include \
    -include $(G)/apps/browser/js_config.h -fsanitize=address,undefined
# glaze-os builds the native wolfSSL its own host tests use.
WOLFSSL_LIB := $(G)/out/host/libwolfssl.a
$(WOLFSSL_LIB): FORCE
	$(MAKE) -C $(G)/host ../out/host/libwolfssl.a

.DEFAULT_GOAL := check
# doom-test also runs the full engine when DOOM_WAD=/absolute/path/doom1.wad.
check: aim-test gmail-test chatgpt-test somafm-ui-test maps-ui-test tetris-test snake-test flappy-test twixt-test chess-test doom-test breakout-test sky1942-test
.PHONY: check FORCE
FORCE:

aim-test: ../tests/native/aim.c ../packages/aim/oscar.c ../packages/aim/md5.c ../packages/aim/text.c ../packages/aim/oscar.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $(O)/aim-test ../tests/native/aim.c \
	    ../packages/aim/oscar.c ../packages/aim/md5.c ../packages/aim/text.c
	$(O)/aim-test
.PHONY: aim-test

gmail-test: ../tests/native/gmail.c ../packages/gmail/main.c ../packages/gmail/mime.c ../packages/gmail/imap.c ../packages/gmail/smtp.c ../packages/gmail/mail.h ../packages/gmail/gmail.h $(WOLFSSL_LIB)
	$(CC) $(WEB_CFLAGS) -include $(G)/tests/native/settings_sys.h -o $(O)/gmail-test ../tests/native/gmail.c \
	    ../packages/gmail/mime.c ../packages/gmail/imap.c ../packages/gmail/smtp.c \
	    $(G)/host/hostres.c $(GFX) $(G)/lib/ui/widgets.c $(G)/lib/ui/list.c $(KEYBOARD) \
	    $(G)/lib/ui/text.c $(G)/lib/ui/input.c $(G)/lib/ui/transition.c $(G)/lib/libc/tz.c $(WOLFSSL_LIB)
	$(O)/gmail-test
.PHONY: gmail-test

# Chat's protocol engine, then its HTTP/TLS client against local fixtures.
CHAT := ../packages/chatgpt
HTTP := $(CHAT)/http
$(O)/chatgpt-protocol: ../tests/native/chatgpt.c $(CHAT)/protocol.c $(HTTP)/url.c $(CHAT)/json.c $(CHAT)/tokens.c $(wildcard $(CHAT)/*.h)
	$(CC) $(CFLAGS) -I$(CHAT) -I$(HTTP) -fsanitize=address,undefined $(filter %.c,$^) -o $@
$(O)/chatgpt-client: host/chatgpt.c $(CHAT)/protocol.c $(HTTP)/url.c $(addprefix $(CHAT)/,json.c tokens.c oauth.c responses.c http_io.c) \
    $(HTTP)/http.c $(HTTP)/conn.c $(HTTP)/tls.c $(HTTP)/inflate.c $(WOLFSSL_LIB) $(wildcard $(CHAT)/*.h)
	$(CC) $(CFLAGS) -I$(CHAT) -I$(HTTP) -I$(G)/third_party/wolfssl -I$(G)/apps/browser/tls -DWOLFSSL_USER_SETTINGS \
	    -fsanitize=address,undefined $(filter %.c,$^) $(WOLFSSL_LIB) -o $@
chatgpt-test: $(O)/chatgpt-protocol $(O)/chatgpt-client
	$(O)/chatgpt-protocol
	python3 chatgpt_oauth.py --client $(O)/chatgpt-client
.PHONY: chatgpt-test

somafm-ui-test: ../tests/native/somafm_ui.c ../tests/native/somafm_sys.h ../packages/somafm/main.c ../packages/somafm/stations.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined -include ../tests/native/somafm_sys.h \
	    -o $(O)/somafm-ui-test ../tests/native/somafm_ui.c $(G)/host/hostres.c $(GFX) \
	    $(G)/lib/ui/widgets.c $(G)/lib/ui/list.c $(G)/lib/ui/text.c $(EDIT)
	$(O)/somafm-ui-test
.PHONY: somafm-ui-test

# Maps against a host directory holding glaze-os's synthetic tile pack.
MAPS := ../packages/maps
$(O)/maps-fs/Maps/demo.gzmap: $(G)/tools/mkmaptiles.py $(G)/tests/maptiles_test.py
	python3 $(G)/tests/maptiles_test.py --keep $(O)/maps-fs/Maps
maps-ui-test: ../tests/native/maps_ui.c ../tests/native/maps_sys.h $(MAPS)/main.c $(MAPS)/gzmap.c $(MAPS)/gzmap.h \
    $(MAPS)/track.c $(MAPS)/track.h $(MAPS)/puff.c $(G)/lib/gps/gps.c $(O)/maps-fs/Maps/demo.gzmap
	$(CC) $(CFLAGS) -fsanitize=address,undefined -include ../tests/native/maps_sys.h \
	    -o $(O)/maps-ui-test ../tests/native/maps_ui.c $(MAPS)/gzmap.c $(MAPS)/track.c $(MAPS)/puff.c \
	    $(G)/lib/gps/gps.c $(G)/host/hostres.c $(GFX) $(G)/lib/ui/widgets.c $(G)/lib/ui/list.c $(G)/lib/ui/text.c $(EDIT)
	rm -f $(O)/maps-fs/Maps/demo.mbtiles
	MAPS_FS=$(abspath $(O)/maps-fs) $(O)/maps-ui-test
.PHONY: maps-ui-test

# Portable falling-block rules and versioned save-state validation.
tetris-test: ../tests/native/tetris.c ../packages/tetris/game.c ../packages/tetris/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/tetris-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/tetris-test
.PHONY: tetris-test

# Portable snake rules and versioned save-state validation.
snake-test: ../tests/native/snake.c ../packages/snake/game.c ../packages/snake/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/snake-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/snake-test
.PHONY: snake-test

# Portable flappy rules and versioned save-state validation.
flappy-test: ../tests/native/flappy.c ../packages/flappy/game.c ../packages/flappy/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/flappy-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/flappy-test
.PHONY: flappy-test

# Portable TwixT rules: pegs, auto rods, crossing blocks, win floods,
# undo, save/restore and the three AI levels.
twixt-test: ../tests/native/twixt.c ../packages/twixt/game.c ../packages/twixt/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/twixt-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/twixt-test
.PHONY: twixt-test

# Chess rules model, then the micro-Max engine and its glue exercising it.
# The vendored engine object keeps upstream's terse idioms, so it builds with
# the warning waivers its style needs; the model and tests use stock flags.
CHESS_UBSAN := -fsanitize=address,undefined -fno-sanitize=shift,signed-integer-overflow \
    -fno-sanitize-recover=all
$(O)/chess/umax4_8.o: ../third_party/micromax/umax4_8.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fwrapv -Wno-char-subscripts -Wno-parentheses -Wno-comment \
	    $(CHESS_UBSAN) -c $< -o $@
$(O)/chess-test: ../tests/native/chess.c ../packages/chess/game.c \
    ../packages/chess/game.h ../packages/chess/engine.h $(O)/chess/umax4_8.o
	$(CC) $(CFLAGS) $(CHESS_UBSAN) $(filter %.c,$^) $(O)/chess/umax4_8.o -o $@
chess-test: $(O)/chess-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/chess-test
.PHONY: chess-test

# Doom's real engine and Glaze I/O/audio/video glue; no WAD in the repository.
DOOM_VENDOR := $(wildcard ../third_party/doomgeneric/*.c)
DOOM_GLUE := $(addprefix ../packages/doom/,files.c input.c keys.c sound.c system.c video.c wad.c)
DOOM_NATIVE_FLAGS := $(CFLAGS) -I../third_party/doomgeneric -fno-strict-aliasing -fwrapv -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-type-limits -Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-unused-but-set-parameter -Wno-enum-conversion -Wno-format-truncation -Wno-pointer-to-int-cast -Wno-absolute-value -fsanitize=address,undefined -fno-sanitize=shift,signed-integer-overflow
DOOM_NATIVE_OBJS := $(patsubst ../%.c,$(O)/doom/%.o,$(DOOM_VENDOR) $(DOOM_GLUE))
$(O)/doom/%.o: ../%.c $(wildcard ../packages/doom/*.h ../third_party/doomgeneric/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_NATIVE_FLAGS) -include ../packages/doom/compat.h -c $< -o $@
$(O)/doom/host.o: host/doom.c
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_NATIVE_FLAGS) -c $< -o $@
$(O)/doom-test: $(DOOM_NATIVE_OBJS) $(O)/doom/host.o
	$(CC) $(DOOM_NATIVE_FLAGS) $^ -o $@
doom-test: $(O)/doom-test
	$(O)/doom-test
	$(if $(DOOM_WAD),ASAN_OPTIONS=detect_leaks=0 $(O)/doom-test "$(DOOM_WAD)",@echo "Optional full engine test: make doom-test DOOM_WAD=/absolute/path/doom1.wad")
.PHONY: doom-test

# Breakout rules and save-state validation.
breakout-test: ../tests/native/breakout.c ../packages/breakout/game.c ../packages/breakout/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/breakout-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/breakout-test
.PHONY: breakout-test

# 1942 rules and save-state validation.
sky1942-test: ../tests/native/sky1942.c ../packages/sky1942/game.c ../packages/sky1942/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/sky1942-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/sky1942-test
.PHONY: sky1942-test
