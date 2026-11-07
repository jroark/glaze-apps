/*
 * game.h - TwixT game model (12x12 dot grid).
 *
 * Red joins the top and bottom edges and moves first. Black (the
 * computer) joins the left and right edges, and may not use the end
 * holes of its home rows (row 0 / row 11). The four corner holes are
 * never playable.
 *
 * Placing a peg builds every knight-move link to own pegs that does
 * not cross a link already on the board; players never pick links.
 * Competing crossing links go to whoever builds them first, so the
 * live link set depends on play order - the model keeps the full ply
 * log and can rebuild any position by replaying it.
 */
#ifndef TWIXT_GAME_H
#define TWIXT_GAME_H

#include <stdint.h>

#define TW_DIM      12
#define TW_NPOS     (TW_DIM * TW_DIM)

/* sides */
#define TW_EMPTY    0
#define TW_RED      1   /* human, top<->bottom, moves first   */
#define TW_BLACK    2   /* computer, left<->right             */

/* states */
#define TW_ONGOING  0
#define TW_RED_WIN  1
#define TW_BLACK_WIN 2
#define TW_DRAW     3

/* end reasons */
#define TW_R_NONE       0
#define TW_R_CHAIN      1 /* home edges connected             */
#define TW_R_BOARD_FULL 2 /* no legal hole for the side to move */
#define TW_R_RESIGN     3

/* AI levels */
#define TW_WEAK   0
#define TW_NORMAL 1
#define TW_STRONG 2

/* every link the board can hold (12x12 grid, 4 knight offsets per hole) */
#define TW_MAX_LINKS (TW_NPOS * 4)

/* hard cap on stored plies; a real game ends far sooner */
#define TW_MAX_PLIES 128
/* worst-case save record: 9B header + plies + 4B checksum */
#define TW_REC_CAP  (13 + TW_MAX_PLIES)

void tw_new(int seed);

int tw_state(void);
int tw_result(void);
int tw_reason(void);
int tw_to_move(void);
int tw_nplies(void);
int tw_who_at(int pos);

/* fills pa/pb with the two holes of each live link (the link's color
 * is tw_who_at(pa)); returns how many were written, never more than
 * max. Handy for drawing the rod network */
int tw_get_links(int *pa, int *pb, int max);

/* hole is playable by the side on turn (geometry + occupancy only;
 * returns 0 when the game is over) */
int tw_legal(int pos);

/* could a link between two adjacent-by-knight-move holes p,q be laid
 * right now for the side on turn (both ends are its pegs, and no
 * crossing live link)? handy for tests and the win banner debug */
int tw_link_ok(int pos1, int pos2);

/* place a peg for the side on turn; returns 0 played, 1 played and
 * won, -1 illegal */
int tw_play(int pos);

void tw_undo(void);
void tw_resign(void); /* the human always plays Red, so this is always
                       * a Black win - the one ending save/restore
                       * replay cannot reproduce on its own */

/* save/restore: 9-byte header, one byte per ply, 4-byte FNV-1a checksum.
 * tw_save returns the record length or -1 when cap is too small;
 * tw_restore accepts exactly that many bytes and replays the log, so
 * forged records are rejected instead of producing impossible boards.
 * a successful tw_restore also reseeds the rng from the record's own
 * checksum, so a process that boots straight into a saved game gets
 * live AI jitter instead of a stuck-at-zero one.
 * it returns 0 on success, -1 on rejection */
int tw_save(unsigned char *buf, int cap);
int tw_restore(const unsigned char *buf, int len);

/* the rng is deliberately uint32_t: the app runs on MIPS32 and is
 * debugged with host replays, and an unsigned long would be 64-bit
 * off-target and change every xorshift sequence */
uint32_t tw_rand(void);
void tw_srand(uint32_t seed);

/* picks a move for the side on turn (Black): takes a win, blocks its
 * only immediate losing hole, otherwise scores all holes by own link
 * growth, opponent denial and edge affinity; STRONG checks near-tied
 * candidates one opponent reply deeper. returns the chosen pos */
int tw_ai(int level, int *out_pos);

#endif
