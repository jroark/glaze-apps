
# __NAME__ rules and save-state validation.
__ID__-test: ../tests/native/__ID__.c ../packages/__ID__/game.c ../packages/__ID__/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/__ID__-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/__ID__-test
.PHONY: __ID__-test
