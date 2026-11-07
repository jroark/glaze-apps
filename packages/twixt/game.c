/*
 * game.c - TwixT model: rules, link logic, save/restore, Black's AI.
 *
 * Links are identified by their top-most hole plus one of the four
 * forward knight offsets, giving each real link exactly one id.
 * Whether a link lives depends on play order (crossing links compete),
 * so every peg is kept in plylog[] and positions are rebuilt exactly
 * by replay. Boards are tiny; replay is cheap and always right.
 */
#include "game.h"
#include <stdlib.h>
#include <string.h>

#define NLINK        (TW_NPOS * 4)
#define LINK_ID(p,k) ((int)(p) * 4 + (k))

static const signed char KDR[4] = {1, 2, 2, 1};
static const signed char KDC[4] = {2, 1, -1, -2};
/* all eight knight moves: 0..3 are the forward ones above, 4..7 back */
static const signed char DR8[8] = {1, 2, 2, 1, -1, -2, -2, -1};
static const signed char DC8[8] = {2, 1, -1, -2, -2, -1, 1, 2};

#define XC_MAX 10 /* widest crossing list measured on 12x12 is 9 */

static int nbr[NLINK][2];      /* both holes of a link; [-1] = off-grid */
static int xtab[NLINK][XC_MAX]; /* links that cross it, -1 terminated   */
static int tables_ready = 0;

/* strict segment crossing. Knight-move vectors are primitive, so a
 * live link can never have another link's endpoint exactly on it:
 * strict is exact here. */
static int seg_cross(int ax, int ay, int bx, int by,
                     int cx, int cy, int dx, int dy)
{
    long o1 = (long)(bx - ax) * (cy - ay) - (long)(by - ay) * (cx - ax);
    long o2 = (long)(bx - ax) * (dy - ay) - (long)(by - ay) * (dx - ax);
    long o3 = (long)(dx - cx) * (ay - cy) - (long)(dy - cy) * (ax - cx);
    long o4 = (long)(dx - cx) * (by - cy) - (long)(dy - cy) * (bx - cx);
    return ((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) &&
           ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0));
}

static void build_tables(void)
{
    int p, k, i, j, cnt;
    if (tables_ready)
        return;
    for (p = 0; p < TW_NPOS; p++) {
        int r = p / TW_DIM, c = p % TW_DIM;
        for (k = 0; k < 4; k++) {
            int r2 = r + KDR[k], c2 = c + KDC[k];
            nbr[LINK_ID(p, k)][0] = p;
            nbr[LINK_ID(p, k)][1] = (r2 < 0 || r2 >= TW_DIM ||
                                     c2 < 0 || c2 >= TW_DIM)
                                        ? -1
                                        : r2 * TW_DIM + c2;
        }
    }
    for (i = 0; i < NLINK; i++) {
        cnt = 0;
        if (nbr[i][1] >= 0) {
            int a = nbr[i][0], b = nbr[i][1];
            int ax = a % TW_DIM, ay = a / TW_DIM;
            int bx = b % TW_DIM, by = b / TW_DIM;
            for (j = 0; j < NLINK; j++) {
                int c2, d2;
                if (j == i || nbr[j][1] < 0)
                    continue;
                c2 = nbr[j][0];
                d2 = nbr[j][1];
                if (c2 == a || c2 == b || d2 == a || d2 == b)
                    continue; /* sharing a hole never crosses */
                if (seg_cross(ax, ay, bx, by,
                              c2 % TW_DIM, c2 / TW_DIM,
                              d2 % TW_DIM, d2 / TW_DIM)) {
                    if (cnt < XC_MAX)
                        xtab[i][cnt++] = j;
                }
            }
        }
        xtab[i][cnt] = -1;
    }
    tables_ready = 1;
}

/* ---------- state ---------- */

#define F_TOP   1
#define F_BOT   2
#define F_LEFT  4
#define F_RIGHT 8

static unsigned char who[TW_NPOS];
static unsigned char live[NLINK];
static unsigned char parent_[TW_NPOS]; /* union-find; ids < 256 fit u8 */
static unsigned char comp_[TW_NPOS];   /* border flags, roots only */

static int plylog[TW_MAX_PLIES];
static int nplies;
static int to_move;
static int result;
static int reason;
static uint32_t rng_state;

static int root(int i)
{
    while (parent_[i] != i) {
        parent_[i] = parent_[parent_[i]];
        i = parent_[i];
    }
    return i;
}

static void unite(int a, int b)
{
    int ra = root(a), rb = root(b);
    if (ra == rb)
        return;
    parent_[rb] = (unsigned char)ra;
    comp_[ra] |= comp_[rb];
}

static int home_mask(int color)
{
    return (color == TW_RED) ? (F_TOP | F_BOT) : (F_LEFT | F_RIGHT);
}

static int border_flags(int p, int color)
{
    int r = p / TW_DIM, c = p % TW_DIM, f = 0;
    if (color == TW_RED) {
        if (r == 0)
            f |= F_TOP;
        if (r == TW_DIM - 1)
            f |= F_BOT;
    } else {
        if (c == 0)
            f |= F_LEFT;
        if (c == TW_DIM - 1)
            f |= F_RIGHT;
    }
    return f;
}

/* canonical link id between knight-adjacent holes p,q, or -1 */
static int link_between(int p, int q)
{
    int pr = p / TW_DIM, pc = p % TW_DIM;
    int dr = q / TW_DIM - pr, dc = q % TW_DIM - pc;
    int k;
    for (k = 0; k < 4; k++)
        if (dr == KDR[k] && dc == KDC[k])
            return LINK_ID(p, k);
    for (k = 0; k < 4; k++)
        if (dr == -KDR[k] && dc == -KDC[k])
            return LINK_ID(q, k);
    return -1;
}

static int link_clear(int i)
{
    const int *xc;
    for (xc = xtab[i]; *xc >= 0; xc++)
        if (live[*xc])
            return 0;
    return 1;
}

/* links a peg at p (whose color must already be in who[p]) would add */
static int links_at(int p, int color)
{
    int r = p / TW_DIM, c = p % TW_DIM, m, cnt = 0;
    for (m = 0; m < 8; m++) {
        int nr = r + DR8[m], nc = c + DC8[m];
        int q, i;
        if (nr < 0 || nr >= TW_DIM || nc < 0 || nc >= TW_DIM)
            continue;
        q = nr * TW_DIM + nc;
        if (who[q] != (unsigned char)color)
            continue;
        i = link_between(p, q);
        if (i >= 0 && link_clear(i))
            cnt++;
    }
    return cnt;
}

/* border flags of the component a peg at p joins; read-only flood */
static int flags_at(int p, int color)
{
    static int vis[TW_NPOS];
    static int stamp = 0;
    int stack[TW_NPOS], sp = 0, flags;
    stamp++;
    vis[p] = stamp;
    stack[sp++] = p;
    flags = border_flags(p, color);
    while (sp) {
        int u = stack[--sp];
        int r = u / TW_DIM, c = u % TW_DIM, m;
        for (m = 0; m < 8; m++) {
            int nr = r + DR8[m], nc = c + DC8[m];
            int q, i;
            if (nr < 0 || nr >= TW_DIM || nc < 0 || nc >= TW_DIM)
                continue;
            q = nr * TW_DIM + nc;
            if (vis[q] == stamp || who[q] != (unsigned char)color)
                continue;
            i = link_between(u, q);
            if (i < 0 || !link_clear(i))
                continue;
            vis[q] = stamp;
            stack[sp++] = q;
            flags |= border_flags(q, color);
        }
    }
    return flags;
}

/* does a peg for color at p (temporarily placed) join its home edges? */
static int win_at(int p, int color)
{
    int was = who[p], f;
    who[p] = (unsigned char)color;
    f = flags_at(p, color);
    who[p] = (unsigned char)was;
    return (f & home_mask(color)) == home_mask(color);
}

/* place the peg and bring in every link it earns */
static void apply_peg(int p, int color)
{
    int r = p / TW_DIM, c = p % TW_DIM, m;
    who[p] = (unsigned char)color;
    parent_[p] = (unsigned char)p;
    comp_[p] = (unsigned char)border_flags(p, color);
    for (m = 0; m < 8; m++) {
        int nr = r + DR8[m], nc = c + DC8[m];
        int q, i;
        if (nr < 0 || nr >= TW_DIM || nc < 0 || nc >= TW_DIM)
            continue;
        q = nr * TW_DIM + nc;
        if (who[q] != (unsigned char)color)
            continue;
        i = link_between(p, q);
        if (i >= 0 && link_clear(i)) {
            live[i] = 1;
            unite(p, q);
        }
    }
}

static void clear_board(void)
{
    int p, i;
    for (p = 0; p < TW_NPOS; p++) {
        who[p] = TW_EMPTY;
        parent_[p] = (unsigned char)p;
        comp_[p] = 0;
    }
    for (i = 0; i < NLINK; i++)
        live[i] = 0;
    nplies = 0;
    to_move = TW_RED;
    result = TW_ONGOING;
    reason = TW_R_NONE;
}

/* forward */
int tw_play(int pos);

/* exact rewind: replay the first n plies of plylog */
static void rebuild(int n)
{
    int saved[TW_MAX_PLIES], i;
    uint32_t rng = rng_state;
    for (i = 0; i < n; i++)
        saved[i] = plylog[i];
    clear_board();
    for (i = 0; i < n; i++)
        tw_play(saved[i]);
    rng_state = rng;
}

/* ---------- rules ---------- */

void tw_new(int seed)
{
    build_tables();
    clear_board();
    rng_state = (uint32_t)seed * 2654435761u + 1013904223u;
    if (!rng_state)
        rng_state = 1;
}

/* geometry + occupancy only, for a given side */
static int hole_ok(int pos, int side)
{
    int r, c;
    if (pos < 0 || pos >= TW_NPOS || who[pos] != TW_EMPTY)
        return 0;
    r = pos / TW_DIM;
    c = pos % TW_DIM;
    if ((r == 0 || r == TW_DIM - 1) && (c == 0 || c == TW_DIM - 1))
        return 0; /* the four corners are never playable */
    if (side == TW_BLACK && (r == 0 || r == TW_DIM - 1))
        return 0; /* black's home-row end holes are out of bounds */
    if (side == TW_RED && (c == 0 || c == TW_DIM - 1))
        return 0; /* red's home-column end holes are out of bounds */
    return 1;
}

int tw_legal(int pos)
{
    return result == TW_ONGOING && hole_ok(pos, to_move);
}

int tw_link_ok(int pos1, int pos2)
{
    int i = link_between(pos1, pos2);
    if (i < 0 || nbr[i][1] < 0)
        return 0;
    if (who[pos1] == TW_EMPTY || who[pos1] != who[pos2])
        return 0;
    return link_clear(i);
}

int tw_play(int pos)
{
    int color, q, any = 0;
    if (!tw_legal(pos))
        return -1;
    if (nplies >= TW_MAX_PLIES) {
        result = TW_DRAW;
        reason = TW_R_BOARD_FULL;
        return -1;
    }
    color = to_move;
    plylog[nplies++] = pos;
    apply_peg(pos, color);

    if (win_at(pos, color)) {
        result = (color == TW_RED) ? TW_RED_WIN : TW_BLACK_WIN;
        reason = TW_R_CHAIN;
        return 1;
    }
    to_move = color ^ 3; /* RED(1) <-> BLACK(2) */
    for (q = 0; q < TW_NPOS; q++)
        if (hole_ok(q, to_move)) {
            any = 1;
            break;
        }
    if (!any) {
        result = TW_DRAW;
        reason = TW_R_BOARD_FULL;
    }
    return 0;
}

void tw_undo(void)
{
    if (nplies > 0)
        rebuild(nplies - 1);
}

/* the human is always Red, so resigning is always a Black win */
void tw_resign(void)
{
    if (result != TW_ONGOING)
        return;
    result = TW_BLACK_WIN;
    reason = TW_R_RESIGN;
}

/* state accessors */
int tw_state(void)    { return result; }
int tw_result(void)   { return result; }
int tw_reason(void)   { return reason; }
int tw_to_move(void)  { return to_move; }
int tw_nplies(void)   { return nplies; }
int tw_who_at(int pos)
{
    if (pos < 0 || pos >= TW_NPOS)
        return TW_EMPTY;
    return who[pos];
}

/* fills pa/pb with the two holes of each live link (its color is
 * who[pa]); returns how many were written */
int tw_get_links(int *pa, int *pb, int max)
{
    int i, n = 0;
    for (i = 0; i < NLINK && n < max; i++)
        if (live[i]) {
            pa[n] = nbr[i][0];
            pb[n] = nbr[i][1];
            n++;
        }
    return n;
}

/* ---------- save / restore ---------- */

static uint32_t fnv(const unsigned char *b, int n)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

int tw_save(unsigned char *buf, int cap)
{
    int i;
    uint32_t h;
    if (cap < TW_REC_CAP || nplies > TW_MAX_PLIES)
        return -1;
    buf[0] = 'T'; buf[1] = 'W'; buf[2] = 'S'; buf[3] = '1';
    buf[4] = 1;
    buf[5] = (unsigned char)result;
    buf[6] = (unsigned char)reason;
    buf[7] = (unsigned char)(nplies & 0xff);
    buf[8] = (unsigned char)((nplies >> 8) & 0xff);
    for (i = 0; i < nplies; i++)
        buf[9 + i] = (unsigned char)plylog[i];
    h = fnv(buf, 9 + nplies);
    buf[9 + nplies]      = (unsigned char)(h & 0xff);
    buf[10 + nplies]     = (unsigned char)((h >> 8) & 0xff);
    buf[11 + nplies]     = (unsigned char)((h >> 16) & 0xff);
    buf[12 + nplies]     = (unsigned char)((h >> 24) & 0xff);
    return 13 + nplies;
}

/* replays the log through tw_play, so a forged record cannot create an
 * impossible board; the replayed state is trusted over the header,
 * except for resign, the only ending replay cannot reach: it is
 * re-applied when the replay is still ongoing and the header agrees */
int tw_restore(const unsigned char *buf, int len)
{
    unsigned char keep_who[TW_NPOS], keep_live[NLINK];
    unsigned char keep_parent[TW_NPOS], keep_comp[TW_NPOS];
    int keep_plylog[TW_MAX_PLIES];
    int keep_nplies, keep_to_move, keep_result, keep_reason;
    int n, i;
    uint32_t h;

    if (len < 13 || buf[0] != 'T' || buf[1] != 'W' || buf[2] != 'S' ||
        buf[3] != '1' || buf[4] != 1)
        return -1;
    n = buf[7] | (buf[8] << 8);
    if (n > TW_MAX_PLIES || len != 13 + n)
        return -1;
    h = fnv(buf, 9 + n);
    if (buf[9 + n]  != (unsigned char)(h & 0xff) ||
        buf[10 + n] != (unsigned char)((h >> 8) & 0xff) ||
        buf[11 + n] != (unsigned char)((h >> 16) & 0xff) ||
        buf[12 + n] != (unsigned char)((h >> 24) & 0xff))
        return -1;
    for (i = 0; i < n; i++)
        if (buf[9 + i] >= TW_NPOS)
            return -1;

    memcpy(keep_who, who, sizeof who);
    memcpy(keep_live, live, sizeof live);
    memcpy(keep_parent, parent_, sizeof parent_);
    memcpy(keep_comp, comp_, sizeof comp_);
    memcpy(keep_plylog, plylog, (size_t)nplies * sizeof(int));
    keep_nplies = nplies;
    keep_to_move = to_move;
    keep_result = result;
    keep_reason = reason;

    build_tables();
    clear_board();
    for (i = 0; i < n; i++) {
        if (tw_play(buf[9 + i]) < 0) {
            memcpy(who, keep_who, sizeof who);
            memcpy(live, keep_live, sizeof live);
            memcpy(parent_, keep_parent, sizeof parent_);
            memcpy(comp_, keep_comp, sizeof comp_);
            memcpy(plylog, keep_plylog, (size_t)keep_nplies * sizeof(int));
            nplies = keep_nplies;
            to_move = keep_to_move;
            result = keep_result;
            reason = keep_reason;
            return -1;
        }
    }
    if (result == TW_ONGOING && buf[5] == TW_BLACK_WIN &&
        buf[6] == TW_R_RESIGN) {
        result = TW_BLACK_WIN;
        reason = TW_R_RESIGN;
    }
    /* a process can boot straight into a saved game without ever
     * calling tw_new(), which would leave the xorshift stuck at 0 and
     * freeze the AI jitter; reseed from this record's own checksum */
    rng_state = h;
    if (!rng_state)
        rng_state = 1;
    return 0;
}

/* ---------- random ---------- */

uint32_t tw_rand(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

void tw_srand(uint32_t seed)
{
    rng_state = seed ? seed : 1;
}

/* ---------- AI ----------
 * score(hole) = 120 * own new links
 *             - 100 * links the hole denies to the opponent
 *             + 8000 * sitting on an opponent winning hole
 *             + 90   * touching our home edge band
 *             - 2    * distance from center, plus level jitter
 * STRONG: candidates within 900 of the best static score get one
 * opponent reply checked: opponent wins right back => -50000. */

static const int TW_JITTER_[3] = {256, 32, 8};

int tw_ai(int level, int *out_pos)
{
    int myl[TW_NPOS], oppb[TW_NPOS], oppw[TW_NPOS];
    int myc, oppc, p, q, best = -1, bestv, bestv_stat;

    *out_pos = -1;
    if (result != TW_ONGOING)
        return -1;
    if (level < TW_WEAK)
        level = TW_WEAK;
    if (level > TW_STRONG)
        level = TW_STRONG;
    myc = to_move;
    oppc = myc ^ 3; /* RED(1) <-> BLACK(2) */

    for (p = 0; p < TW_NPOS; p++) {
        myl[p] = oppb[p] = oppw[p] = 0;
        if (who[p] != TW_EMPTY)
            continue;
        if (hole_ok(p, myc)) {
            who[p] = (unsigned char)myc;
            myl[p] = links_at(p, myc);
            who[p] = TW_EMPTY;
        }
        if (hole_ok(p, oppc)) {
            who[p] = (unsigned char)oppc;
            oppw[p] = (flags_at(p, oppc) & home_mask(oppc)) ==
                      home_mask(oppc);
            oppb[p] = links_at(p, oppc);
            who[p] = TW_EMPTY;
        }
    }

    /* 1. take our own win */
    for (p = 0; p < TW_NPOS; p++)
        if (hole_ok(p, myc) &&
            (flags_at(p, myc) & home_mask(myc)) == home_mask(myc)) {
            *out_pos = p;
            return p;
        }

    /* 2. block the only hole that loses for them right now */
    {
        int cnt = 0, hole = -1;
        for (p = 0; p < TW_NPOS; p++)
            if (oppw[p]) {
                cnt++;
                hole = p;
            }
        if (cnt == 1 && hole_ok(hole, myc)) {
            *out_pos = hole;
            return hole;
        }
    }

    /* 3. positional score */
    bestv = -0x7ffffff0;
    bestv_stat = -0x7ffffff0;
    for (p = 0; p < TW_NPOS; p++) {
        int v, vstat, r, c;
        if (!hole_ok(p, myc))
            continue;
        vstat = 120 * myl[p] - 100 * oppb[p] + (oppw[p] ? 8000 : 0);
        r = p / TW_DIM;
        c = p % TW_DIM;
        if (myc == TW_BLACK) {
            if (c == 1 || c == TW_DIM - 2)
                vstat += 90;
        } else {
            if (r == 1 || r == TW_DIM - 2)
                vstat += 90;
        }
        vstat -= 2 * (abs(r - 5) + abs(c - 5));
        if (vstat > bestv_stat)
            bestv_stat = vstat;
        v = vstat + (int)(tw_rand() %
                          (unsigned)(2 * TW_JITTER_[level] + 1)) -
            TW_JITTER_[level];
        if (v > bestv) {
            bestv = v;
            best = p;
        }
    }
    if (best < 0)
        return -1;

    /* 4. STRONG: one opponent reply deeper for near-tied candidates */
    if (level == TW_STRONG) {
        int cand[TW_NPOS], base[TW_NPOS], ncand = 0;
        for (p = 0; p < TW_NPOS; p++) {
            int v;
            if (!hole_ok(p, myc))
                continue;
            v = 120 * myl[p] - 100 * oppb[p] + (oppw[p] ? 8000 : 0);
            if (myc == TW_BLACK) {
                if (p % TW_DIM == 1 || p % TW_DIM == TW_DIM - 2)
                    v += 90;
            } else {
                if (p / TW_DIM == 1 || p / TW_DIM == TW_DIM - 2)
                    v += 90;
            }
            v -= 2 * (abs(p / TW_DIM - 5) + abs(p % TW_DIM - 5));
            if (v >= bestv_stat - 900 && ncand < TW_NPOS) {
                base[ncand] = v;
                cand[ncand++] = p;
            }
        }
        best = -1;
        bestv = -0x7ffffff0;
        for (p = 0; p < ncand; p++) {
            int worst = 0;
            if (tw_play(cand[p]) < 0)
                continue;
            if (result != TW_ONGOING) {
                if (result == (myc == TW_RED ? TW_RED_WIN : TW_BLACK_WIN))
                    worst = 1000000;
                else if (result == TW_DRAW)
                    worst = 0;
                else
                    worst = -1000000;
            } else {
                for (q = 0; q < TW_NPOS; q++)
                    if (hole_ok(q, oppc) &&
                        (flags_at(q, oppc) & home_mask(oppc)) ==
                            home_mask(oppc)) {
                        worst = -50000; /* they win right back */
                        break;
                    }
            }
            tw_undo();
            {
                int v = base[p] * 12 + worst;
                if (v > bestv) {
                    bestv = v;
                    best = cand[p];
                }
            }
        }
        if (best < 0)
            return -1;
    }

    *out_pos = best;
    return best;
}
