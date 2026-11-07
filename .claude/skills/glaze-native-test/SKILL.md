---
name: glaze-native-test
description: Write and run fast host-side (native) tests for a Glaze app's pure logic (game.c) with ASan/UBSan. Use when writing or changing game rules, save/load code, or when a native test fails.
---

# Native tests for Glaze app logic

The rules in `packages/<id>/game.c` have no `glaze/` includes, so they compile
on the host with the normal C compiler. `tests/native/<id>.c` drives them with
`assert`. This runs in about a second. Use it for every rule change, before any
device build.

```sh
make -C tests <id>-test          # one app; prints "<id>-test: ok" on success
make test                        # every app's tests; run before you finish
```

`glaze-new-app` already created the test file and the `<id>-test` target. To add one by hand, copy
the `snake-test` block in `tests/Makefile` (lines are tab-indented):

```make
<id>-test: ../tests/native/<id>.c ../packages/<id>/game.c ../packages/<id>/game.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(filter %.c,$^) -o $(O)/<id>-test
	UBSAN_OPTIONS=halt_on_error=1 $(O)/<id>-test
.PHONY: <id>-test
```

Also append `<id>-test` to the `check:` line. If your logic spans more `.c` files,
add them to the dependency list, because `$(filter %.c,$^)` compiles every `.c` listed.

## What to test (in this order)

1. **New game**: the starting state is exactly what the rules say.
2. **Determinism**: the same seed gives the same game (`game_new(&a, 7, 0)` twice, compare fields).
3. **Each rule**, with one small hand-built position per rule:
   - set up the struct directly (`g.x = 0; g.len = 4; …`);
   - call one function;
   - assert the result and the changed fields.
   Test the edges: walls, the last cell, the score limit, illegal moves returning `false`.
4. **Time**: `game_tick(&g, ms)` just before and just past a boundary. If game_tick clamps `ms`,
   test that a huge value (e.g. 60000) advances no more than the clamp. To reach a later
   state, tick in a loop (`for (i = 0; i < 50; i++) game_tick(&g, 20);`).
5. **Game over / win**: reaches the state, and further moves are refused.
6. **Save**:
   - `game_encode` → `game_decode` gives back an identical struct (`memcmp`);
   - decoding a short size, a flipped byte (`buf[12] ^= 1`) or an impossible value is refused.

Rules of thumb:
- Restate geometry in the test (e.g. direction tables) instead of copying game.c's
  helpers, so the test checks game.c rather than itself.
- One `assert` per fact; a failing assert prints its line number.
- No `glaze/` includes, no timing, no files: tests must be deterministic.
- End `main` with `puts("<id>-test: ok"); return 0;`.

## Reading failures

| Output | Meaning |
| --- | --- |
| `<id>.c:57: main: Assertion '...' failed` | Line 57 of the test is false. Print the values (`printf("%d\n", g.x);`), then decide whether the test or game.c is wrong. |
| `AddressSanitizer: stack-buffer-overflow` / `global-buffer-overflow` | Out-of-bounds array access. The first stack frame in `packages/<id>/` is the bad line. |
| `runtime error: signed integer overflow` / `shift exponent` / `index out of bounds` | UBSan: undefined behavior at the printed `file:line`. Use unsigned math or check the range. |
| `undefined reference` | A function declared in game.h but not defined in game.c, or a .c file missing from the target. |
| `glaze/...h: No such file` | game.c included a platform header. Move that code to main.c. |
