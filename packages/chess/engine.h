#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

/*
 * C glue around vendored micro-Max 4.8 (engine/ -> ../../third_party/micromax).
 * Definitions live inside third_party/micromax/umax4_8.c next to the search
 * itself (see [GLAZE PATCH 5] there); this header only exposes them.
 * All squares use the 0x88 layout, identical to game.h's board model.
 *
 * Upstream main() plays one ply as: read the opponent's move into K,L, then
 * call D() once; D() applies that move while looking for it, and the next
 * scan (after the node budget burns out) applies the engine's own reply the
 * same way.  mm_pending() + mm_search() reproduce that pairing; mm_apply()
 * is only for replaying a saved game's whole history onto a fresh board.
 */
void mm_init_once(void);     /* one-time table setup; call before anything else */
void mm_new_game(void);      /* reset board + tables, white to move */
void mm_set_budget(int n);   /* nodes per search, clamped by the engine */
int mm_pending(int from, int to); /* queue one move the engine did NOT search
                                     itself (the human's reply); it is applied
                                     by the next mm_search(), which then thinks
                                     for the side to move, like upstream */
int mm_apply(int from, int to); /* apply one move immediately (history replay
                                    only); 1 if the engine's board changed
                                    (move applied), 0 if rejected (state
                                    untouched) */
int mm_search(int *from, int *to, int side); /* think for `side` (C_WHITE or
                                    C_BLACK, the caller's truth) and play the
                                    move on the engine board, after first
                                    applying any mm_pending() move; 1 only if
                                    the move came back AND the engine board
                                    proves it agrees with `side`; on 0 the
                                    engine board may have advanced anyway:
                                    stop trusting the engine for this game */
int mm_last_nodes(void);     /* nodes of the previous search (debug/tests) */

#endif
