# micro-Max

Source: http://home.hccnet.nl/h.g.muller/umax4_8.c (H.G. Muller's Micro-Max
download page: http://home.hccnet.nl/h.g.muller/dwnldpage.html, retrieved
2026-11-05)

Pinned version: micro-Max 4.8 standalone ("1953 characters"; 1929 non-comment,
non-whitespace characters in this exact file)

Pristine SHA-256: `447cd24da6275221a1e9564d9282cb69f019b6c521dde1f063b9eb7de3fd34eb`

License: the upstream page carries no license text; the author presents
micro-Max as open source (the Chess Programming Wiki lists it under
Category:Open_Source) and its source has been widely copied and ported
(Atmel ATmega88, UCI ports, Android). No explicit grant of redistribution
rights appears in the file itself. Treat this vendored copy as author-copyrighted
code used under the upstream's public-source practice; do not relicense it.

Only `umax4_8.c` is vendored: the standalone C source, no Windows executables,
no WinBoard front-end (the app supplies its own UI), no game data.

All deviations from the pristine file are marked `[GLAZE PATCH n]` inline:

1. Hash table resized from 1<<24 (192MB BSS) to 1<<17 (1.5MB) to fit the
   BE-300's 16MB of RAM.
2. `K()` reworked from an unaligned `*(int*)` load through the `T[]` char
   array into a byte-by-byte little-endian load; MIPS II faults on the
   unaligned word access.
3. The `N<1e6` time/depth heuristic replaced by an explicit node budget
   (`NODE_BUDGET`, set with `mm_set_budget`); `float` literal removed.
4. `b[129]` and `T[1035]` wrapped in padded objects (`bb`, `tt`) with `b`/`T`
   as pointers into them, so the engine's deliberate out-of-range probes
   (uninitialized-index `J()` hashing, step offsets) stay inside allocations.
5. Upstream's stdio `main()` play-loop replaced by the `mm_*` entry points
   the app links against (`mm_init_once`, `mm_new_game`, `mm_set_budget`,
   `mm_pending`, `mm_apply`, `mm_search`, `mm_last_nodes`, declared in
   `packages/chess/engine.h`): no printf/getchar/rand, deterministic hash
   seeding from a xorshift PRNG, center-point table computed with integer
   only (identical values to upstream's float expression).  The entry points
   keep upstream's pairing: `mm_pending()` queues the opponent's move the way
   upstream's `main()` scanned it into `K,L`, and one `mm_search()` call then
   does what upstream's one `D()` call did — applies that queued move
   through a shallow root scan and afterwards searches and lets the
   budget-exhausted root scan apply the engine's own reply through the
   pending-move exit.  `mm_search()` only returns success when the engine's
   board proves it agrees with the caller's authoritative game state;
   `mm_apply()` applies a move immediately and exists solely for replaying a
   saved game's whole history onto a fresh engine board.
6. Upstream declared `D()` in pre-C90 style (implicit-int return, identifier
   parameter list); the definition now carries a prototype.  The parameters
   and return type are exactly the ones the old declaration implied.

The Glaze app keeps its own rules model (`packages/chess/game.c`) as the
authority and replays only model-accepted moves into the engine, so the
engine's shortcuts (castling that skips attacked-square checks, its gradual
promotion possibly picking a minor piece where the model tags the move
queen) never hand an illegal move to the model; when such a shortcut leaves
the two boards disagreeing, `mm_search()`'s agreement check fails and the
app retires the engine, finishing the game with the model's own deterministic
`c_fallback_move()`.
