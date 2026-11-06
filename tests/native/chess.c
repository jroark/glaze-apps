/* Native tests for the chess model (packages/chess/game.c) and the vendored
 * micro-Max glue (third_party/micromax/umax4_8.c).  The model is checked
 * against published perft results -- that covers its whole move generator:
 * castling, en passant, promotion, pins, checks -- and against known terminal
 * positions.  The engine is checked the way the app drives it (mm_pending +
 * mm_search, plus mm_apply for save-replay): every move it plays must be
 * legal in the model, and a glue call that reports failure when both sides
 * are in sync is engine desync, which is what these asserts catch. */
#include "../../packages/chess/game.h"
#include "../../packages/chess/engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Cg g, g3;

/* FEN for tests; rank 8 first, halfmove clock honoured.  Castling rights also
 * mark the matching king and rook virgin (the |32 bit game.c relies on). */
static void fen(Cg *x, const char *f)
{
    int i = 0, s = 0;
    memset(x, 0, sizeof *x);
    while (f[i] != ' ') {
        if (f[i] == '/') {
            s = (((s >> 4) + 1) << 4); /* next 0x88 rank */
        } else if (f[i] >= '1' && f[i] <= '8') {
            s += f[i] - '0';
        } else {
            int p;
            switch (f[i]) {
            case 'p': p = C_P | C_BLACK; break;
            case 'n': p = C_N | C_BLACK; break;
            case 'b': p = C_B | C_BLACK; break;
            case 'r': p = C_R | C_BLACK; break;
            case 'q': p = C_Q | C_BLACK; break;
            case 'k': p = C_K | C_BLACK; break;
            case 'P': p = C_P | C_WHITE; break;
            case 'N': p = C_N | C_WHITE; break;
            case 'B': p = C_B | C_WHITE; break;
            case 'R': p = C_R | C_WHITE; break;
            case 'Q': p = C_Q | C_WHITE; break;
            case 'K': p = C_K | C_WHITE; break;
            default: assert(0);
            }
            assert(!(s & 0x88));
            x->sq[s] = (unsigned char)p;
            s++;
        }
        i++;
    }
    while (f[i] == ' ')
        i++;
    x->side = f[i] == 'w' ? C_WHITE : C_BLACK;
    while (f[i] != ' ')
        i++;
    while (f[i] == ' ') /* step onto the castling token */
        i++;
    while (f[i] != ' ') {
        if (f[i] == 'K') {
            x->castle |= C_WK;
            x->sq[0x74] |= C_VIRGIN;
            x->sq[0x77] |= C_VIRGIN;
        } else if (f[i] == 'Q') {
            x->castle |= C_WQ;
            x->sq[0x74] |= C_VIRGIN;
            x->sq[0x70] |= C_VIRGIN;
        } else if (f[i] == 'k') {
            x->castle |= C_BK;
            x->sq[0x04] |= C_VIRGIN;
            x->sq[0x07] |= C_VIRGIN;
        } else if (f[i] == 'q') {
            x->castle |= C_BQ;
            x->sq[0x04] |= C_VIRGIN;
            x->sq[0x00] |= C_VIRGIN;
        }
        i++;
    }
    while (f[i] == ' ')
        i++;
    if (f[i] == '-') {
        x->ep = -1;
        i++;
    } else {
        x->ep = (signed char)(((8 - (f[i + 1] - '1')) << 4) + (f[i] - 'a'));
        i += 2;
    }
    while (f[i] == ' ')
        i++;
    x->halfmove = (unsigned short)atoi(f + i);
    x->result = C_ONGOING;
    x->keys[0] = c_key(x);
}

static void mv(Cg *x, int from, int to)
{
    Cmove m;
    m.from = (unsigned char)from;
    m.to = (unsigned char)to;
    m.promo = 0;
    assert(c_do(x, &m));
}

/* perft: legal move counts, the standard generator regression.  The model has
 * no undo, so the mutable fields are saved and restored around every try;
 * hist/keys get trashed but perft never reads them. */
static long perft(Cg *x, int depth)
{
    Cmove legal[256];
    int n, i;
    unsigned char sq[128], side, castle;
    signed char ep;
    unsigned short half;
    int nplies;
    Cgres result;
    Cgreason reason;
    long total = 0;

    if (depth == 0)
        return 1;
    memcpy(sq, x->sq, sizeof sq);
    side = x->side;
    castle = x->castle;
    ep = x->ep;
    half = x->halfmove;
    nplies = x->nplies;
    result = x->result;
    reason = x->reason;
    n = c_gen(legal, x);
    for (i = 0; i < n; i++) {
        assert(c_do(x, &legal[i]));
        total += perft(x, depth - 1);
        memcpy(x->sq, sq, sizeof sq);
        x->side = side;
        x->castle = castle;
        x->ep = ep;
        x->halfmove = half;
        x->nplies = nplies;
        x->result = result;
        x->reason = reason;
    }
    return total;
}

/* engine self-play: every ply the engine bids must be legal in the model.
 * mm_search plays on the engine board itself (it only returns 1 when its
 * board actually changed), the model follows after validating. */
static void selfplay(int plies, int budget)
{
    int i, from, to;
    c_init(&g);
    mm_new_game();
    mm_set_budget(budget);
    for (i = 0; i < plies && g.result == C_ONGOING; i++) {
        Cmove m, ms[96];
        int n, k, found = 0;
        assert(g.nplies < C_MAX_PLIES);
        assert(mm_search(&from, &to, g.side));
        assert(mm_last_nodes() >= 1);
        /* same path the app takes: engine bids bare squares, pick the full
         * legal move (with its promo tag) out of the model's own list */
        n = c_gen(ms, &g);
        for (k = 0; k < n; k++)
            if (ms[k].from == (unsigned char)from && ms[k].to == (unsigned char)to) {
                m = ms[k];
                found = 1;
                break;
            }
        if (!found) { /* fails only if the engine bid an illegal move */
            fprintf(stderr, "selfplay ply %d: %02x-%02x not in %d legal\n",
                    i, from, to, n);
        }
        assert(found);
        assert(c_do(&g, &m));
        /* no mm_apply here: mm_search already played the move on the engine
         * board (upstream semantics); the return of 1 above is proof the
         * engine's board changed.  A drift would surface next ply when the
         * engine bids something illegal in the model. */
    }
}

/* app play flow: the human's move is queued with mm_pending, and one
 * mm_search call applies it on the engine board, thinks for black, and
 * plays the reply.  Human moves here use the model's deterministic picker
 * as a stand-in for touch input; both boards must stay in lockstep. */
static void pending_flow(int plies, int budget)
{
    int i, from, to;
    c_init(&g);
    mm_new_game();
    mm_set_budget(budget);
    for (i = 0; i < plies && g.result == C_ONGOING; i++) {
        Cmove m, ms[96];
        int n, k, found = 0;
        assert(g.nplies < C_MAX_PLIES);
        /* human (white) move: model-side only, queued for the engine */
        assert(g.side == C_WHITE);
        assert(c_fallback_move(&g, &m, 0));
        assert(c_do(&g, &m));
        assert(mm_pending(m.from, m.to) == 1);
        /* engine (black): applies the queued move, then thinks and plays */
        assert(mm_search(&from, &to, g.side));
        n = c_gen(ms, &g);
        for (k = 0; k < n; k++)
            if (ms[k].from == (unsigned char)from && ms[k].to == (unsigned char)to) {
                m = ms[k];
                found = 1;
                break;
            }
        if (!found) {
            fprintf(stderr, "pending_flow ply %d: %02x-%02x not in %d legal\n",
                    i, from, to, n);
        }
        assert(found);
        assert(c_do(&g, &m));
    }
}

int main(void)
{
    unsigned char buf[C_REC_CAP];
    int len;
    Cg g2;

    /* ---------- perft (published move-generator results) ---------- */
    fen(&g, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    assert(perft(&g, 1) == 20);
    assert(perft(&g, 2) == 400);
    assert(perft(&g, 3) == 8902);
    assert(perft(&g, 4) == 197281);

    fen(&g, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    assert(perft(&g, 1) == 48);
    assert(perft(&g, 2) == 2039);
    assert(perft(&g, 3) == 97862);

    /* en passant, pins, blocked pawns (wiki position 3) */
    fen(&g, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    assert(perft(&g, 1) == 14);
    assert(perft(&g, 2) == 191);
    assert(perft(&g, 3) == 2812);
    assert(perft(&g, 4) == 43238);

    /* open middlegame, no castling rights, no promotions to depth 4
       (wiki position 6; depth 4 = 3894594 is too slow here) */
    fen(&g, "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
    assert(perft(&g, 1) == 46);
    assert(perft(&g, 2) == 2079);
    assert(perft(&g, 3) == 89890);

    /* ---------- terminal positions ---------- */
    /* fool's mate: 1. f3 e5 2. g4 Qh4# */
    fen(&g, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    mv(&g, 0x65, 0x55); /* f3   */
    mv(&g, 0x14, 0x34); /* e5   */
    mv(&g, 0x66, 0x46); /* g4   */
    mv(&g, 0x03, 0x47); /* Qh4# */
    assert(g.result == C_BLACK_WIN && g.reason == C_R_MATE);

    /* stalemate: Qa2-f7 covers g7/g8/h7 without touching h8, and the white
       king on b1 is too far away to interfere with the queen's rays */
    fen(&g, "7k/8/8/8/8/8/Q7/1K6 w - - 0 1");
    mv(&g, 0x60, 0x15);
    assert(g.result == C_DRAW && g.reason == C_R_STALE);

    /* fifty-move rule (the white pawn keeps c_material_draw quiet) */
    fen(&g, "7k/5K2/8/8/8/8/6P1/R7 w - - 98 130");
    mv(&g, 0x70, 0x71); /* Rb1 -> halfmove 99  */
    assert(g.result == C_ONGOING);
    mv(&g, 0x07, 0x17); /* Kh7 -> halfmove 100 */
    assert(g.result == C_DRAW && g.reason == C_R_FIFTY);

    /* threefold repetition: four knight rounds */
    fen(&g, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    mv(&g, 0x76, 0x55);
    mv(&g, 0x06, 0x25);
    mv(&g, 0x55, 0x76);
    mv(&g, 0x25, 0x06);
    mv(&g, 0x76, 0x55);
    mv(&g, 0x06, 0x25);
    mv(&g, 0x55, 0x76);
    mv(&g, 0x25, 0x06);
    assert(g.result == C_DRAW && g.reason == C_R_REP);

    /* insufficient material */
    fen(&g, "4k3/8/8/8/8/8/4n3/4K3 w - - 0 1");
    mv(&g, 0x74, 0x64); /* Kxe2, leaving bare kings */
    assert(g.result == C_DRAW && g.reason == C_R_MATERIAL);
    assert(c_material_draw(&g));

    /* ---------- save / restore ---------- */
    fen(&g, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    mv(&g, 0x64, 0x44); /* e4 (clears e2 for the f1-bishop) */
    mv(&g, 0x14, 0x34); /* e5 */
    mv(&g, 0x76, 0x55); /* Nf3 (clears g1) */
    mv(&g, 0x01, 0x22); /* Nc6 */
    mv(&g, 0x75, 0x42); /* Bc4 (clears f1) */
    mv(&g, 0x16, 0x26); /* d6 */
    mv(&g, 0x74, 0x76); /* O-O (castling is encoded as the king move) */
    assert(g.sq[0x76] == (C_K | C_WHITE) && g.sq[0x75] == (C_R | C_WHITE));
    len = c_save(&g, buf, (int)sizeof buf);
    assert(len == 13 + 3 * g.nplies);

    c_init(&g2);
    assert(c_restore(&g2, buf, len));
    assert(!memcmp(&g, &g2, sizeof g)); /* replay lands on the same state */

    /* broken records are rejected and must leave the target untouched */
    buf[len - 1] ^= 0xFF; /* bad checksum   */
    c_init(&g2);
    g3 = g2; /* snapshot: c_restore must not touch a byte of this */
    assert(!c_restore(&g2, buf, len));
    assert(!memcmp(&g2, &g3, sizeof g3));

    buf[len - 1] ^= 0xFF; /* restore byte   */
    assert(c_restore(&g2, buf, len)); /* sanity: good record again */
    buf[0] = 'X'; /* bad magic    */
    assert(!c_restore(&g2, buf, len));
    buf[0] = 'C';
    buf[4] = 2; /* bad version  */
    assert(!c_restore(&g2, buf, len));
    buf[4] = 1;
    assert(!c_restore(&g2, buf, len - 3)); /* truncation   */

    /* a forgery with a recomputed checksum still fails: its replay is illegal
       (first "move" e7-e5 on White's turn) */
    buf[9] = 0x14;
    buf[10] = 0x34;
    {
        unsigned h = 2166136261u;
        int i;
        for (i = 0; i < 9 + 3 * g.nplies; i++)
            h = (h ^ buf[i]) * 16777619u;
        buf[9 + 3 * g.nplies] = (unsigned char)h;
        buf[10 + 3 * g.nplies] = (unsigned char)(h >> 8);
        buf[11 + 3 * g.nplies] = (unsigned char)(h >> 16);
        buf[12 + 3 * g.nplies] = (unsigned char)(h >> 24);
    }
    assert(!c_restore(&g2, buf, len));

    /* ---------- engine glue ---------- */
    mm_init_once();
    mm_new_game();
    assert(mm_apply(0x01, 0x25) == 0); /* Nb1-f3: not a knight move  */
    assert(mm_apply(0x70, 0x07) == 0); /* Ra1-h8: own pieces in the way */
    {
        int f, t;
        c_init(&g);
        assert(mm_search(&f, &t, g.side)); /* engine state survived the rejects */
        {
            Cmove m;
            m.from = (unsigned char)f;
            m.to = (unsigned char)t;
            m.promo = 0;
            assert(c_do(&g, &m));
        }
    }

    /* a queued move the engine rejects makes mm_search fail, and the engine
     * state survives that the same way (D() restores what it touched) */
    mm_new_game();
    assert(mm_pending(0x01, 0x25) == 1); /* Nb1-f3: not a knight move */
    {
        int f, t;
        assert(mm_search(&f, &t, C_WHITE) == 0);
        assert(mm_search(&f, &t, C_WHITE) == 1); /* queue consumed, engine alive */
    }

    /* en passant capture through the wall */
    mm_new_game();
    assert(mm_apply(0x64, 0x44) == 1); /* e4        */
    assert(mm_apply(0x13, 0x33) == 1); /* d5        */
    assert(mm_apply(0x44, 0x33) == 1); /* exd5 e.p. */

    /* castling through the wall */
    mm_new_game();
    assert(mm_apply(0x64, 0x44) == 1);
    assert(mm_apply(0x14, 0x34) == 1);
    assert(mm_apply(0x76, 0x55) == 1);
    assert(mm_apply(0x06, 0x25) == 1); /* Nf6 clears g8 for black castling */
    assert(mm_apply(0x75, 0x42) == 1); /* Bc4: f1 is 0x75 */
    assert(mm_apply(0x05, 0x32) == 1); /* Bc5 now open: e7 moved */
    assert(mm_apply(0x74, 0x76) == 1); /* white castles */
    assert(mm_apply(0x04, 0x06) == 1); /* black castles */

    /* promotion with capture: the black knight shuttles f6/g8 to keep the
       tempo, the white a-pawn takes b7 and then scoops the rook on a8 */
    mm_new_game();
    c_init(&g);
    {
        static const int line[] = {
            0x60, 0x40,  0x06, 0x25,  0x40, 0x30,  0x25, 0x06,
            0x30, 0x20,  0x06, 0x25,  0x20, 0x11,  0x25, 0x06,
            0x11, 0x00
        };
        int i;
        for (i = 0; i < (int)(sizeof line / sizeof line[0]); i += 2) {
            Cmove m;
            m.from = (unsigned char)line[i];
            m.to = (unsigned char)line[i + 1];
            /* the model keeps promotions explicit: a pawn reaching the last
               rank must carry the promo flag, plain pushes there are illegal */
            m.promo = ((g.side == C_WHITE && (line[i] >> 4) == 1 &&
                        (line[i + 1] >> 4) == 0) ||
                       (g.side == C_BLACK && (line[i] >> 4) == 6 &&
                        (line[i + 1] >> 4) == 7)) ? C_Q : 0;
            assert(c_do(&g, &m));
            assert(mm_apply(line[i], line[i + 1]) == 1);
        }
        assert(g.sq[0x00] == (C_Q | C_WHITE)); /* promoted queen landed on a8 */
    }

    /* engine self-play: the model must never reject an engine move */
    selfplay(60, 1500);
    selfplay(50, 12000);

    /* app flow end-to-end: pending human moves + engine replies, in lockstep */
    pending_flow(40, 4000);

    /* fallback move: legal and deterministic */
    c_init(&g);
    {
        Cmove m1, m2;
        assert(c_fallback_move(&g, &m1, 0));
        assert(c_fallback_move(&g, &m2, 0));
        assert(!memcmp(&m1, &m2, sizeof m1));
        assert(c_do(&g, &m1));
    }

    printf("chess: all tests passed\n");
    return 0;
}
