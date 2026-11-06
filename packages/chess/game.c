/* chess rules model: authoritative board, legal moves, game record.
 * The vendored engine (engine/) is kept in sync by replaying accepted moves;
 * nothing here knows the engine exists, so this file tests natively. */
#include "game.h"

#include <string.h>

#define WHITE_ON 1 /* white starts at the bottom rows (0x60-0x77), like micro-Max */

/* ---------- tiny helpers ---------- */

static int sq_ok(int s) { return !(s & 0x88); }

static int color_of(int p) { return p & (C_WHITE | C_BLACK); }

static int king_sq(const Cg *g, int side)
{
    int s;
    for (s = 0; s < 128; s++)
        if ((g->sq[s] & 7) == C_K && color_of(g->sq[s]) == side)
            return s;
    return -1;
}

/* ---------- attacks ---------- */

static const signed char kn_dirs[] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const signed char kg_dirs[] = { -17, -16, -15, -1, 1, 15, 16, 17 };
static const signed char rk_dirs[] = { -16, -1, 1, 16 };
static const signed char bq_dirs[] = { -17, -15, 15, 17 };

int c_attacked(const unsigned char b[], int sq, int by)
{
    int i, d, s, p;

    /* pawns: a white pawn sits one rank "below" the squares it attacks,
     * so probes point at sq+15 and sq+17 for white, mirrored for black */
    d = (by == C_WHITE) ? 15 : -15;
    for (i = 0; i < 2; i++, d += (by == C_WHITE) ? 2 : -2) {
        s = sq + d;
        if (sq_ok(s) && (b[s] & 7) == C_P && color_of(b[s]) == by)
            return 1;
    }
    for (i = 0; i < 8; i++) {
        s = sq + kn_dirs[i];
        if (sq_ok(s) && (b[s] & 7) == C_N && color_of(b[s]) == by)
            return 1;
    }
    for (i = 0; i < 8; i++) {
        s = sq + kg_dirs[i];
        if (sq_ok(s) && (b[s] & 7) == C_K && color_of(b[s]) == by)
            return 1;
    }
    for (i = 0; i < 4; i++) { /* rook and queen rays */
        for (s = sq + rk_dirs[i]; sq_ok(s); s += rk_dirs[i]) {
            p = b[s];
            if (p) {
                if (color_of(p) == by && ((p & 7) == C_R || (p & 7) == C_Q))
                    return 1;
                break;
            }
        }
    }
    for (i = 0; i < 4; i++) { /* bishop and queen diagonals */
        for (s = sq + bq_dirs[i]; sq_ok(s); s += bq_dirs[i]) {
            p = b[s];
            if (p) {
                if (color_of(p) == by && ((p & 7) == C_B || (p & 7) == C_Q))
                    return 1;
                break;
            }
        }
    }
    return 0;
}

int c_in_check(const Cg *g, int side)
{
    int k = king_sq(g, side);
    return k >= 0 && c_attacked(g->sq, k, side ^ (C_WHITE | C_BLACK) ^ 0);
}

/* ---------- move generation (pseudo-legal, then filtered for legality) ---------- */

typedef struct {
    int pawns, minors[3], heavy; /* heavy = R|Q count; minors per side by N/B */
} MatCount;

static void push(Cmove out[], int *n, int from, int to, int promo)
{
    out[*n].from = (unsigned char)from;
    out[*n].to = (unsigned char)to;
    out[*n].promo = (unsigned char)promo;
    (*n)++;
}

static int gen_pseudo(Cmove out[], const Cg *g)
{
    int n = 0, s, p, side = g->side, i, d, t, pr;
    const int pawn_dir = (side == C_WHITE) ? -16 : 16;
    const int promo_rank = (side == C_WHITE) ? 0 : 7;

    for (s = 0; s < 128; s++) {
        if (s & 0x88)
            continue;
        p = g->sq[s];
        if (!p || color_of(p) != side)
            continue;
        switch (p & 7) {
        case C_P:
            t = s + pawn_dir;
            if (sq_ok(t) && !g->sq[t]) {
                int home = (side == C_WHITE) ? 6 : 1;
                pr = (t >> 4) == promo_rank;
                push(out, &n, s, t, pr ? C_Q : 0);
                if ((s >> 4) == home && sq_ok(t + pawn_dir) && !g->sq[t + pawn_dir])
                    push(out, &n, s, t + pawn_dir, 0);
            }
            for (i = -1; i <= 1; i += 2) {
                t = s + pawn_dir + i;
                if (!sq_ok(t))
                    continue;
                if (g->sq[t] && color_of(g->sq[t]) != side) {
                    pr = (t >> 4) == promo_rank;
                    push(out, &n, s, t, pr ? C_Q : 0);
                } else if (g->ep >= 0 && t == g->ep) {
                    push(out, &n, s, t, 0);
                }
            }
            break;
        case C_N:
            for (i = 0; i < 8; i++) {
                t = s + kn_dirs[i];
                if (sq_ok(t) && color_of(g->sq[t]) != side)
                    push(out, &n, s, t, 0);
            }
            break;
        case C_B:
        case C_R:
        case C_Q:
            for (i = 0; i < 4; i++) {
                d = ((p & 7) == C_B) ? bq_dirs[i] : rk_dirs[i];
                if ((p & 7) == C_Q) { /* queen uses all eight rays */
                    for (t = s + bq_dirs[i]; sq_ok(t); t += bq_dirs[i]) {
                        if (!g->sq[t])
                            push(out, &n, s, t, 0);
                        else {
                            if (color_of(g->sq[t]) != side)
                                push(out, &n, s, t, 0);
                            break;
                        }
                    }
                    d = rk_dirs[i];
                }
                for (t = s + d; sq_ok(t); t += d) {
                    if (!g->sq[t])
                        push(out, &n, s, t, 0);
                    else {
                        if (color_of(g->sq[t]) != side)
                            push(out, &n, s, t, 0);
                        break;
                    }
                }
            }
            break;
        case C_K:
            for (i = 0; i < 8; i++) {
                t = s + kg_dirs[i];
                if (sq_ok(t) && color_of(g->sq[t]) != side)
                    push(out, &n, s, t, 0);
            }
            /* castling: king on start square, virgin rook, empty path, and
             * none of the squares the king crosses attacked (model rule;
             * micro-Max skips the attack test and we outveto it later) */
            if (side == C_WHITE && s == 0x74 && !c_attacked(g->sq, 0x74, C_BLACK)) {
                if ((g->castle & C_WK) && g->sq[0x75] == 0 && g->sq[0x76] == 0 &&
                    g->sq[0x77] == (C_WHITE | C_R | C_VIRGIN) &&
                    !c_attacked(g->sq, 0x75, C_BLACK) && !c_attacked(g->sq, 0x76, C_BLACK))
                    push(out, &n, 0x74, 0x76, 0);
                if ((g->castle & C_WQ) && g->sq[0x73] == 0 && g->sq[0x72] == 0 &&
                    g->sq[0x71] == 0 &&
                    g->sq[0x70] == (C_WHITE | C_R | C_VIRGIN) &&
                    !c_attacked(g->sq, 0x73, C_BLACK) && !c_attacked(g->sq, 0x72, C_BLACK))
                    push(out, &n, 0x74, 0x72, 0);
            } else if (side == C_BLACK && s == 0x04 &&
                       !c_attacked(g->sq, 0x04, C_WHITE)) {
                if ((g->castle & C_BK) && g->sq[0x05] == 0 && g->sq[0x06] == 0 &&
                    g->sq[0x07] == (C_BLACK | C_R | C_VIRGIN) &&
                    !c_attacked(g->sq, 0x05, C_WHITE) && !c_attacked(g->sq, 0x06, C_WHITE))
                    push(out, &n, 0x04, 0x06, 0);
                if ((g->castle & C_BQ) && g->sq[0x03] == 0 && g->sq[0x02] == 0 &&
                    g->sq[0x01] == 0 &&
                    g->sq[0x00] == (C_BLACK | C_R | C_VIRGIN) &&
                    !c_attacked(g->sq, 0x03, C_WHITE) && !c_attacked(g->sq, 0x02, C_WHITE))
                    push(out, &n, 0x04, 0x02, 0);
            }
            break;
        }
    }
    return n;
}

/* apply to a scratch board for legality testing only (undo is full) */
typedef struct {
    unsigned char from, to, moved, victim, vdest, ep_old, castle_old, side_old;
} Snap;

static void raw_apply(Cg *g, const Cmove *m)
{
    int from = m->from, to = m->to, p = g->sq[from];
    int side = g->side;
    int ep = g->ep;

    g->sq[to] = p;
    g->sq[from] = 0;
    if ((p & 7) == C_K && (to - from == 2 || from - to == 2)) { /* castling rook */
        if (to > from) {
            g->sq[to - 1] = (unsigned char)(g->sq[to + 1] & ~C_VIRGIN);
            g->sq[to + 1] = 0;
        } else {
            g->sq[to + 1] = (unsigned char)(g->sq[to - 2] & ~C_VIRGIN);
            g->sq[to - 2] = 0;
        }
    }
    if (m->promo)
        g->sq[to] = (unsigned char)((p & (C_WHITE | C_BLACK)) | m->promo);
    if ((p & 7) == C_P && to == ep) /* en passant: victim sits behind the target */
        g->sq[to + ((side == C_WHITE) ? 16 : -16)] = 0;
    if ((p & 7) == C_P && (to - from == 32 || from - to == 32))
        g->ep = (signed char)(from + ((side == C_WHITE) ? -16 : 16));
    else
        g->ep = -1;
    if ((p & 7) == C_K)
        g->castle &= (side == C_WHITE) ? (unsigned char)~(C_WK | C_WQ)
                                       : (unsigned char)~(C_BK | C_BQ);
    if (from == 0x77 || to == 0x77)
        g->castle = (unsigned char)(g->castle & ~C_WK);
    if (from == 0x70 || to == 0x70)
        g->castle = (unsigned char)(g->castle & ~C_WQ);
    if (from == 0x07 || to == 0x07)
        g->castle = (unsigned char)(g->castle & ~C_BK);
    if (from == 0x00 || to == 0x00)
        g->castle = (unsigned char)(g->castle & ~C_BQ);
    if (p & C_VIRGIN)
        g->sq[to] = (unsigned char)(g->sq[to] & ~C_VIRGIN);
    g->side = (unsigned char)((side == C_WHITE) ? C_BLACK : C_WHITE);
}

static int c_gen_legit(Cmove out[], const Cg *g)
{
    Cmove ps[256];
    Cg t;
    int n = gen_pseudo(ps, g), i, mine;

    for (i = 0, mine = 0; i < n; i++) {
        t = *g;
        raw_apply(&t, &ps[i]);
        if (!c_in_check(&t, g->side))
            out[mine++] = ps[i];
    }
    return mine;
}

int c_gen(Cmove out[], const Cg *g)
{
    return c_gen_legit(out, g);
}

/* ---------- do / undo ---------- */

void c_init(Cg *g)
{
    static const unsigned char back[] = { C_R, C_N, C_B, C_Q, C_K, C_B, C_N, C_R };
    int f;

    memset(g, 0, sizeof *g);
    for (f = 0; f < 8; f++) {
        g->sq[0x00 + f] = (unsigned char)(back[f] | C_BLACK);
        g->sq[0x10 + f] = (unsigned char)(C_P | C_BLACK);
        g->sq[0x60 + f] = (unsigned char)(C_P | C_WHITE);
        g->sq[0x70 + f] = (unsigned char)(back[f] | C_WHITE);
    }
    /* the virgin flag only marks rooks and kings (castling rights); the
     * double-push test uses the home rank instead, matching micro-Max */
    g->sq[0x00] |= C_VIRGIN; g->sq[0x04] |= C_VIRGIN; g->sq[0x07] |= C_VIRGIN;
    g->sq[0x70] |= C_VIRGIN; g->sq[0x74] |= C_VIRGIN; g->sq[0x77] |= C_VIRGIN;
    g->side = C_WHITE;
    g->castle = C_WK | C_WQ | C_BK | C_BQ;
    g->ep = -1;
    g->result = C_ONGOING;
    g->keys[0] = c_key(g);
}

int c_do(Cg *g, const Cmove *m)
{
    Cmove legal[256], norm;
    int n, i;

    if (g->result != C_ONGOING)
        return 0;
    /* the model promotes to queen only, so any non-zero promo spells it */
    norm = *m;
    norm.promo = (unsigned char)(m->promo ? C_Q : 0);
    n = c_gen_legit(legal, g);
    for (i = 0; i < n; i++)
        if (legal[i].from == norm.from && legal[i].to == norm.to &&
            legal[i].promo == norm.promo)
            break;
    if (i == n)
        return 0;

    /* history bookkeeping */
    {
        int from = m->from, to = m->to, p = g->sq[from];
        int victim = 0;
        if (to == g->ep && (p & 7) == C_P)
            victim = g->sq[to + ((g->side == C_WHITE) ? 16 : -16)];
        else
            victim = g->sq[to];
        if (g->nplies < C_MAX_PLIES)
            g->hist[g->nplies] = norm;
        raw_apply(g, &norm);
        g->nplies++;
        if ((p & 7) == C_P || victim)
            g->halfmove = 0;
        else if (g->halfmove < 60000)
            g->halfmove++;
        g->keys[g->nplies] = c_key(g);
    }

    /* game state detection */
    {
        int n2, k;
        n2 = c_gen_legit(legal, g);
        if (n2 == 0) {
            if (c_in_check(g, g->side))
                g->result = (g->side == C_WHITE) ? C_BLACK_WIN : C_WHITE_WIN;
            else
                g->result = C_DRAW;
            g->reason = c_in_check(g, g->side) ? C_R_MATE : C_R_STALE;
        } else if (g->halfmove >= 100) {
            g->result = C_DRAW;
            g->reason = C_R_FIFTY;
        } else if (g->nplies >= C_MAX_PLIES) {
            g->result = C_DRAW;
            g->reason = C_R_LENGTH;
        } else {
            int rep = 0;
            unsigned key = c_key(g);
            for (k = 0; k <= g->nplies; k++)
                if (g->keys[k] == key)
                    rep++;
            if (rep >= 3) {
                g->result = C_DRAW;
                g->reason = C_R_REP;
            } else if (c_material_draw(g)) {
                g->result = C_DRAW;
                g->reason = C_R_MATERIAL;
            }
        }
    }
    return 1;
}

/* ---------- material ---------- */

static int piece_val(int p)
{
    static const int v[] = { 0, 1, 3, 3, 5, 9, 0 };
    return v[p & 7];
}

int c_material_draw(const Cg *g)
{
    int s, p, pawn = 0, heavy = 0, minors[2] = { 0, 0 };
    for (s = 0; s < 128; s++) {
        if (s & 0x88)
            continue;
        p = g->sq[s];
        if (!p || (p & 7) == C_K)
            continue;
        if ((p & 7) == C_P)
            pawn++;
        else if ((p & 7) == C_R || (p & 7) == C_Q)
            heavy++;
        else
            minors[color_of(p) == C_WHITE ? 0 : 1]++;
    }
    return !pawn && !heavy && minors[0] <= 1 && minors[1] <= 1;
}

/* ---------- fallback move (used if the engine ever fails to rebuild) ---------- */

int c_fallback_move(Cg *g, Cmove *out, int no_castle)
{
    Cmove legal[256];
    int n = c_gen_legit(legal, g), i, best = -1, bestv = -1;
    static const int wv[] = { 0, 10, 30, 30, 50, 90, 0 };

    for (i = 0; i < n; i++) {
        int v = 0, p = g->sq[legal[i].from];
        if (no_castle && (p & 7) == C_K &&
            (legal[i].to - legal[i].from == 2 || legal[i].from - legal[i].to == 2))
            continue;
        if (legal[i].promo)
            v += 800;
        {
            int vic = (legal[i].to == g->ep && (p & 7) == C_P)
                          ? g->sq[legal[i].to + ((g->side == C_WHITE) ? 16 : -16)]
                          : g->sq[legal[i].to];
            v += 10 * piece_val(vic);
        }
        /* gentle preference: moved piece value keeps pieces active, and lower
         * from/to breaks ties deterministically (we scan in ascending order) */
        v += wv[p & 7] / 3;
        if (v > bestv) {
            bestv = v;
            best = i;
        }
    }
    if (best < 0)
        return 0;
    *out = legal[best];
    return 1;
}

/* ---------- position key (repetition) ---------- */

static unsigned zpiece[64][8][2], zturn, zcastle[16], zep[65];
static int zready;

static unsigned zrnd(unsigned *x)
{
    *x = *x * 1103515245u + 12345u;
    return *x ^ (*x >> 15);
}

static void zinit(void)
{
    unsigned x = 0x9E3779B9u;
    int i, j, c;
    for (i = 0; i < 64; i++)
        for (j = 0; j < 8; j++)
            for (c = 0; c < 2; c++)
                zpiece[i][j][c] = zrnd(&x);
    zturn = zrnd(&x);
    for (i = 0; i < 16; i++)
        zcastle[i] = zrnd(&x);
    for (i = 0; i < 65; i++)
        zep[i] = zrnd(&x);
    zready = 1;
}

unsigned c_key(const Cg *g)
{
    unsigned h;
    int s, idx;
    if (!zready)
        zinit();
    h = (g->side == C_WHITE ? 0 : zturn) ^ zcastle[g->castle & 15];
    h ^= zep[(g->ep < 0) ? 0 : ((g->ep & 15) + (g->ep >> 4) * 8 + 1)];
    for (s = 0; s < 128; s++) {
        if (s & 0x88 || !g->sq[s])
            continue;
        idx = (s & 15) + (s >> 4) * 8;
        h ^= zpiece[idx][g->sq[s] & 7][color_of(g->sq[s]) == C_BLACK ? 1 : 0];
    }
    return h;
}

/* ---------- save / restore (game record, checksummed, replayed on load) ---------- */

static unsigned fnv(const unsigned char *p, int n)
{
    unsigned h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

int c_save(const Cg *g, unsigned char *buf, int cap)
{
    int i, n = g->nplies, need = 13 + 3 * n;
    unsigned h;

    if (cap < need || n > C_MAX_PLIES)
        return 0;
    memcpy(buf, "CGS1", 4);
    buf[4] = 1;                 /* format version */
    buf[5] = (unsigned char)g->result;
    buf[6] = (unsigned char)g->reason;
    buf[7] = (unsigned char)n;
    buf[8] = (unsigned char)(n >> 8);
    for (i = 0; i < n; i++) {
        buf[9 + 3 * i] = g->hist[i].from;
        buf[10 + 3 * i] = g->hist[i].to;
        buf[11 + 3 * i] = g->hist[i].promo;
    }
    h = fnv(buf, 9 + 3 * n);
    buf[9 + 3 * n] = (unsigned char)h;
    buf[10 + 3 * n] = (unsigned char)(h >> 8);
    buf[11 + 3 * n] = (unsigned char)(h >> 16);
    buf[12 + 3 * n] = (unsigned char)(h >> 24);
    return 13 + 3 * n;
}

int c_restore(Cg *g, const unsigned char *buf, int len)
{
    Cmove m;
    Cg t;
    int i, n;
    unsigned h;

    if (len < 13 || memcmp(buf, "CGS1", 4) != 0 || buf[4] != 1)
        return 0;
    n = buf[7] | (buf[8] << 8);
    if (n > C_MAX_PLIES || len != 13 + 3 * n)
        return 0;
    h = fnv(buf, 9 + 3 * n);
    if (buf[9 + 3 * n] != (unsigned char)h || buf[10 + 3 * n] != (unsigned char)(h >> 8) ||
        buf[11 + 3 * n] != (unsigned char)(h >> 16) || buf[12 + 3 * n] != (unsigned char)(h >> 24))
        return 0;

    c_init(&t);
    for (i = 0; i < n; i++) {
        m.from = buf[9 + 3 * i];
        m.to = buf[10 + 3 * i];
        m.promo = buf[11 + 3 * i];
        if (!c_do(&t, &m))
            return 0; /* forged or stale record */
    }
    *g = t;
    return 1;
}
