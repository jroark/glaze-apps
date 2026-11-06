#ifndef CHESS_GAME_H
#define CHESS_GAME_H

#include <stddef.h>

/*
 * Chess rules model, the single source of truth for the board. The engine
 * (micro-Max, engine/) only ever syncs through the moves this model accepts.
 * Boards use the 0x88 layout: sq = rank*16 + file with rank 0 = rank 8, so
 * e2 = 100 (0x64) and the a1-h8 diagonal keeps its 0x11 stride. Out-of-board
 * probes are caught by (sq & 0x88) without a branch on bounds.
 */

#define C_MAX_PLIES 512

/* piece codes: low nibble kind, color bit, |32 = castling rook / king not yet
 * moved (same encoding micro-Max uses) */
enum { C_EMPTY = 0, C_P = 1, C_N = 2, C_B = 3, C_R = 4, C_Q = 5, C_K = 6 };
enum { C_WHITE = 8, C_BLACK = 16 };
enum { C_VIRGIN = 32 };

/* castling availability bits */
enum { C_WK = 1, C_WQ = 2, C_BK = 4, C_BQ = 8 };

typedef enum { C_ONGOING = 0, C_WHITE_WIN, C_BLACK_WIN, C_DRAW } Cgres;
typedef enum {
    C_R_NONE = 0, C_R_MATE, C_R_STALE, C_R_FIFTY, C_R_REP,
    C_R_MATERIAL, C_R_RESIGN, C_R_LENGTH
} Cgreason;

typedef struct {
    unsigned char from, to, promo;
} Cmove;

typedef struct {
    unsigned char sq[128];   /* 0x88 board; outer border always 0 */
    unsigned char side;      /* C_WHITE or C_BLACK to move */
    unsigned char castle;    /* availability mask */
    signed char ep;          /* square a double-pushed pawn skipped, or -1 */
    unsigned short halfmove; /* plies since last pawn move or capture */
    int nplies;
    Cmove hist[C_MAX_PLIES];
    unsigned keys[C_MAX_PLIES + 1];
    Cgres result;
    Cgreason reason;
} Cg;

void c_init(Cg *g);
int c_gen(Cmove out[], const Cg *g);       /* legal moves, queen promo only; count */
int c_do(Cg *g, const Cmove *m);           /* validate + apply; 0 rejected */
int c_in_check(const Cg *g, int side);
int c_attacked(const unsigned char b[], int sq, int by); /* raw 0x88 board */
int c_fallback_move(Cg *g, Cmove *out, int no_castle);   /* deterministic best move */
int c_material_draw(const Cg *g);

unsigned c_key(const Cg *g);               /* position key for repetition */

/* Portable game records so game.c stays free of SDK calls (the app does the
 * file IO under its bundle's data directory, native tests poke memory).
 * c_save writes a checksummed record into buf and returns its length (0 if
 * cap too small).
 * c_restore rejects bad magic/version/checksum, then replays every move
 * through c_do, so a corrupt or hand-forged file can never put a nonsensical
 * position on screen. Returns 1 on success; g is untouched on failure. */
#define C_REC_CAP (13 + 3 * C_MAX_PLIES)
int c_save(const Cg *g, unsigned char *buf, int cap);
int c_restore(Cg *g, const unsigned char *buf, int len);

#endif
