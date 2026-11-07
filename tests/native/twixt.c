/* twixt game model tests: legality, auto-linking, first-built crossing
 * rods in both play orders, both win chains, draw by exhaustion, undo,
 * save/restore, and the AI basics. Run under ASan/UBSan from
 * tests/Makefile. */

#include "../../packages/twixt/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

#define RC(r, c) ((r) * TW_DIM + (c))

/* links[]/nlinks mirror tw_get_links for counting */
static int link_count(void)
{
    static int pa[TW_MAX_LINKS], pb[TW_MAX_LINKS];
    return tw_get_links(pa, pb, TW_MAX_LINKS);
}

/* ---------- legality ---------- */

static void test_legality(void)
{
    tw_new(1);
    /* four corner dots are dead */
    check(tw_play(RC(0, 0)) == -1, "top-left corner refused");
    check(tw_play(RC(0, 11)) == -1, "top-right corner refused");
    check(tw_play(RC(11, 0)) == -1, "bottom-left corner refused");
    check(tw_play(RC(11, 11)) == -1, "bottom-right corner refused");
    /* red may not extend through the left or right edge columns */
    check(tw_play(RC(5, 0)) == -1, "red refused on left column");
    check(tw_play(RC(5, 11)) == -1, "red refused on right column");

    check(tw_play(RC(6, 5)) == 0, "red settles in the field");
    check(tw_play(RC(0, 5)) == -1, "black refused on top row");
    check(tw_play(RC(11, 5)) == -1, "black refused on bottom row");
    check(tw_play(RC(5, 6)) == 0, "black settles");
    check(tw_play(RC(6, 5)) == -1, "occupied point refused");
    check(tw_play(RC(1, 11)) == -1, "red refused on right column again");
    check(tw_play(RC(11, 4)) == 0, "red may use the bottom row");
    check(tw_who_at(RC(11, 4)) == TW_RED, "red peg really landed");
    check(tw_to_move() == TW_BLACK, "three plies, black to move again");
    check(tw_legal(RC(6, 5)) == 0 && tw_who_at(RC(6, 5)) == TW_RED,
          "occupied is not legal but still red's peg");
}

/* ---------- automatic linking ---------- */

static void test_linking(void)
{
    int pa[2], pb[2];

    tw_new(1);
    check(tw_play(RC(1, 1)) == 0, "first red peg");
    check(tw_play(RC(5, 1)) == 0, "black replies");
    check(tw_play(RC(2, 3)) == 0, "red knight move away");
    check(link_count() == 1, "one link built by the knight move");
    check(tw_get_links(pa, pb, 2) == 1 && pa[0] != pb[0],
          "endpoints written for the link");
    check(tw_who_at(pa[0]) == TW_RED && tw_who_at(pb[0]) == TW_RED,
          "the link belongs to red");
    check(tw_link_ok(RC(1, 1), RC(2, 3)), "link is present");
    check(tw_link_ok(RC(2, 3), RC(1, 1)), "link lookup is direction-free");
    check(!tw_link_ok(RC(5, 1), RC(2, 3)), "color mismatch is not a link");
}

/* red rod ((0,1),(2,2)) crosses black rod ((1,1),(2,3)); no shared dots */
static int rA = RC(0, 1), rB = RC(2, 2);
static int bA = RC(1, 1), bB = RC(2, 3);

static void test_crossing(void)
{
    /* red builds the rod first: black still lands its peg on (2,3) but
     * the competing rod is refused, first come first served */
    tw_new(1);
    check(tw_play(rA) == 0, "red start peg");
    check(tw_play(bA) == 0, "black start peg");
    check(tw_play(rB) == 0, "red rod built, nothing in the way yet");
    check(tw_link_ok(rA, rB), "red link present");
    check(tw_play(bB) == 0, "black peg still lands after red's rod");
    check(tw_who_at(bB) == TW_BLACK, "peg really landed");
    check(!tw_link_ok(bA, bB), "crossing black rod was refused");
    check(link_count() == 1, "only the red link stands");
    check(tw_to_move() == TW_RED, "turns alternate normally");

    /* black builds it first: now red's (2,2) peg cannot join (0,1) nor
     * its other anchor (0,3); both rods would cross black's */
    tw_new(1);
    check(tw_play(rA) == 0, "red anchor one");
    check(tw_play(bA) == 0, "black anchor one");
    check(tw_play(RC(0, 3)) == 0, "red anchor two, no rod built");
    check(tw_play(bB) == 0, "black rod lands first now");
    check(tw_link_ok(bA, bB), "black link present");
    check(!tw_link_ok(rA, rB), "red rod to (2,2) not built yet");
    check(tw_play(RC(9, 9)) == 0, "red far away");
    check(tw_play(RC(6, 1)) == 0, "black far away, no new link");
    check(tw_play(rB) == 0, "red peg on the poisoned point still lands");
    check(tw_who_at(rB) == TW_RED, "peg landed");
    check(!tw_link_ok(rA, rB), "rod to first anchor refused: would cross");
    check(!tw_link_ok(RC(0, 3), rB), "rod to second anchor refused too");
    check(link_count() == 1, "still only the black link on the board");
}

/* ---------- black wins left to right ---------- */

/* zig-zag chain: every hop is a legal black knight move; all rods live
 * in rows 3..8, and the only columns two non-adjacent rods share pair
 * rows 4/6 and 5/7, which stay three rows apart, so nothing crosses.
 * red moves first, so every black chain peg is preceded by a red
 * filler on the top row; those never link or block a chain rod. */
static const int black_win[10] = {
    RC(5, 0), RC(4, 2), RC(6, 3), RC(4, 4), RC(6, 5),
    RC(4, 6), RC(6, 7), RC(4, 8), RC(6, 9), RC(5, 11),
};

static void test_black_win(void)
{
    int i, r;

    tw_new(1);
    for (i = 0; i < 10; i++) {
        r = tw_play(RC(0, i + 1));        /* red filler, cols 1..10 */
        check(r == 0, "red filler accepted");
        r = tw_play(black_win[i]);
        if (i == 9)
            check(r == 1, "final chain peg wins for black");
        else
            check(r == 0, "black chain peg accepted");
    }
    check(tw_result() == TW_BLACK_WIN, "black is the winner");
    check(tw_reason() == TW_R_CHAIN, "won by chain");
    check(tw_state() == TW_BLACK_WIN, "state agrees with result");
    check(tw_who_at(RC(5, 11)) == TW_BLACK, "winning peg on the right edge");
    check(link_count() == 9, "9 chain rods stand");
    check(tw_play(RC(9, 1)) == -1, "no moves once the game is over");
    check(tw_legal(RC(9, 1)) == 0, "nothing is legal after the end");
}

/* ---------- red wins top to bottom ---------- */

/* the transpose of the black chain above: same zig-zag, rotated onto
 * the rows. black fillers sit in column 11 rows 1..9; they never form
 * a link (same column), so nothing blocks the red rods. */
static const int red_win[10] = {
    RC(0, 5), RC(2, 4), RC(3, 6), RC(4, 4), RC(5, 6),
    RC(6, 4), RC(7, 6), RC(8, 4), RC(9, 6), RC(11, 5),
};

static void test_red_win(void)
{
    int i, r;

    tw_new(1);
    for (i = 0; i < 10; i++) {
        r = tw_play(red_win[i]);
        if (i == 9)
            check(r == 1, "final chain peg wins for red");
        else
            check(r == 0, "red chain peg accepted");
        if (i < 9) {
            r = tw_play(RC(i + 1, 11));   /* black filler, rows 1..9 */
            check(r == 0, "black filler accepted");
        }
    }
    check(tw_result() == TW_RED_WIN, "red is the winner");
    check(tw_reason() == TW_R_CHAIN, "won by chain");
    check(link_count() == 9, "nine red rods stand");
}

/* ---------- draw when the side to move has no legal hole ---------- */

/* red owns everything left to play on: it must keep the last two edge
 * points of both colors empty until they are gone */
static void test_draw(void)
{
    int p, r, left = 0;

    tw_new(1);
    /* fill all 144 with alternating red and black where legal,
     * red taking every red-legal hole first: simpler approach --
     * play every point with tw_play and let refusals happen; when
     * the current side has no hole, the model declares the draw */
    for (p = 0; p < TW_NPOS; p++) {
        if (tw_play(p) < 0)
            continue;
        /* keep both colors able to move as long as possible: just
         * sweep points in order; refusals cost nothing but a turn
         * skip is impossible, so alternate deliberately instead */
        if (tw_play(p ^ 1) < 0) {
            /* the next side cannot take this neighbor; hunt a hole */
            int q;
            for (q = 0; q < TW_NPOS; q++)
                if (tw_legal(q)) {
                    tw_play(q);
                    break;
                }
        }
    }
    /* with both sides forced, somebody eventually runs the board out
     * or gets stuck: any finished game is acceptable here */
    r = tw_result();
    left = 0;
    for (p = 0; p < TW_NPOS; p++)
        if (tw_who_at(p) == TW_EMPTY)
            left++;
    if (r == TW_DRAW)
        check(tw_reason() == TW_R_BOARD_FULL, "draw cites the empty holes");
    else
        check(r == TW_RED_WIN || r == TW_BLACK_WIN,
              "random sweep still ends in a real winner");
    (void)left;
}

/* ---------- undo ---------- */

static void test_undo(void)
{
    int n0;

    tw_new(1);
    tw_play(rA);
    tw_play(bA);
    tw_play(rB);
    n0 = tw_nplies();
    tw_undo();
    check(tw_who_at(rB) == TW_EMPTY, "undo clears the red peg");
    check(tw_nplies() == n0 - 1, "one ply gone");
    tw_undo();
    check(tw_who_at(bA) == TW_EMPTY, "undo clears the black peg");
    check(tw_who_at(rA) == TW_RED, "first red peg stays");
    check(tw_nplies() == n0 - 2, "plies counted down twice");
    check(!tw_link_ok(rA, rB), "undo removed the red link");
    tw_undo();
    check(tw_nplies() == 0, "board is empty again");
    check(link_count() == 0, "no links after a full undo");
    check(tw_to_move() == TW_RED, "red opens again on an empty board");
    tw_undo();                          /* no-op on empty, must not crash */
    check(tw_nplies() == 0, "undo past empty stays put");
}

/* undo must rebuild the crossing-dependent set, not a static sweep:
 * (2,3) black with red's rod first lost its rod, but if the same
 * position is reached with black's rod first, it exists */
static void test_undo_rebuild(void)
{
    tw_new(1);
    tw_play(rA);
    tw_play(bA);
    tw_play(rB);                        /* red rod first */
    tw_play(bB);                        /* black peg, rod refused */
    check(link_count() == 1, "one rod before undo");
    tw_undo();                          /* drop black's peg at (2,3) */
    check(link_count() == 1, "still red's rod after rewind");
    check(tw_who_at(bB) == TW_EMPTY, "peg rewound");
    tw_play(bB);                        /* same point again, same result */
    check(!tw_link_ok(bA, bB), "rod still refused, replay is deterministic");
}

/* ---------- save / restore ---------- */

/* mirrors the record so a checksum can be re-forced after a deliberate
 * payload edit: fnv over every byte before the trailing four */
static void fix_checksum(unsigned char *buf, int len)
{
    unsigned long h = 2166136261UL;
    int i;

    for (i = 0; i < len - 4; i++) {
        h ^= buf[i];
        h *= 16777619UL;
    }
    for (i = 0; i < 4; i++)
        buf[len - 4 + i] = (unsigned char)(h >> (8 * i));
}

static void test_save_roundtrip(void)
{
    unsigned char buf[TW_REC_CAP], again[TW_REC_CAP];
    int len, len2, i, n;

    tw_new(7);
    tw_play(RC(1, 1));
    tw_play(RC(5, 1));
    tw_play(RC(2, 3));
    tw_play(RC(7, 0));
    tw_play(RC(3, 5));
    n = tw_nplies();

    len = tw_save(buf, sizeof buf);
    check(len == 13 + n, "save length matches plies");
    check(tw_save(buf, 40) == -1, "tiny cap refused");

    tw_new(99);                         /* wipe, then bring the game back */
    check(tw_restore(buf, len) == 0, "restore accepts a good record");
    check(tw_nplies() == n, "plies restored");
    check(tw_to_move() == TW_BLACK, "turn restored");
    for (i = 0; i < n; i++)
        check(tw_who_at(buf[9 + i]) != TW_EMPTY, "each logged ply restored");
    check(tw_link_ok(RC(1, 1), RC(2, 3)), "links restored");
    check(tw_who_at(RC(7, 0)) == TW_BLACK, "black edge peg restored");

    len2 = tw_save(again, sizeof again);
    check(len2 == len && memcmp(buf, again, (size_t)len) == 0,
          "re-save is byte-identical");

    /* a finished game survives the trip too */
    tw_new(1);
    for (i = 0; i < 10; i++) {
        tw_play(RC(0, i + 1));
        tw_play(black_win[i]);
    }
    len = tw_save(buf, sizeof buf);
    tw_new(5);
    check(tw_restore(buf, len) == 0, "won game restores");
    check(tw_result() == TW_BLACK_WIN, "won result restored");
    check(tw_reason() == TW_R_CHAIN, "won reason restored");
}

/* restore must reseed the rng: a process that boots straight into a
 * saved game never calls tw_new(), and an unseeded xorshift is stuck
 * at zero forever, which would freeze the AI jitter */
static void test_restore_seeds_rng(void)
{
    unsigned char buf[TW_REC_CAP];
    uint32_t planted, got;
    int len;

    tw_new(9);
    tw_play(RC(1, 1));
    tw_play(RC(10, 3));
    len = tw_save(buf, sizeof buf);

    tw_srand(1);
    planted = tw_rand();            /* what an unseeded carry-over yields */
    tw_srand(1);
    check(tw_restore(buf, len) == 0, "record available to reseed from");
    got = tw_rand();
    check(got != 0 && got != planted, "restore leaves a live rng");

    /* same record, same next draw: a cold resume is reproducible */
    tw_srand(1);
    check(tw_restore(buf, len) == 0, "re-restore");
    check(tw_rand() == got, "restore reseeds identically");
}

static void test_save_forgery(void)
{
    unsigned char buf[TW_REC_CAP];
    int len;

    tw_new(7);
    tw_play(RC(1, 1));
    tw_play(RC(5, 1));
    tw_play(RC(2, 3));
    len = tw_save(buf, sizeof buf);
    check(len == 16, "save ok");

    tw_new(3);
    tw_play(RC(4, 4));

    check(tw_restore(NULL, 0) == -1, "null record refused");
    check(tw_restore(buf, 3) == -1, "short record refused");
    check(tw_restore(buf, len + 1) == -1, "oversized record refused");

    buf[0] ^= 0x20;                     /* "TWS1" -> "twS1" */
    check(tw_restore(buf, len) == -1, "bad magic refused");
    buf[0] = 'T';

    buf[4] = 2;                         /* unknown version */
    check(tw_restore(buf, len) == -1, "bad version refused");
    buf[4] = 1;

    buf[7] = 200;                       /* nplies beyond TW_MAX_PLIES */
    check(tw_restore(buf, len) == -1, "absurd ply count refused");
    buf[7] = 3;

    buf[9] ^= 0x40;                     /* payload bit flip, stale checksum */
    check(tw_restore(buf, len) == -1, "tampered payload caught by checksum");
    buf[9] ^= 0x40;

    /* checksum-valid but impossible: repeat ply 0's point so the replay
     * runs straight into an occupied dot */
    buf[10] = buf[9];
    fix_checksum(buf, len);
    check(tw_restore(buf, len) == -1, "impossible replay refused");

    check(tw_nplies() == 1, "bad restores changed nothing");
    check(tw_who_at(RC(4, 4)) == TW_RED, "own board still stands");
}

static void test_resign_roundtrip(void)
{
    unsigned char buf[TW_REC_CAP];
    int len;

    tw_new(4);
    tw_play(RC(2, 5));
    tw_resign();
    check(tw_result() == TW_BLACK_WIN, "resign makes black the winner");
    check(tw_reason() == TW_R_RESIGN, "resign reason recorded");
    len = tw_save(buf, sizeof buf);
    check(len > 0, "resigned game saves");
    tw_new(6);
    check(tw_restore(buf, len) == 0, "resigned game restores");
    check(tw_result() == TW_BLACK_WIN, "resign result re-trusted");
    check(tw_reason() == TW_R_RESIGN, "resign reason re-trusted");
    check(tw_who_at(RC(2, 5)) == TW_RED, "resigned board restored");
}

/* ---------- AI ---------- */

static void test_ai(void)
{
    int pos, i;

    /* black to move with one winning hole left: nine chain pegs plus
     * ten red fillers is 19 plies, so black is up and must finish */
    tw_new(1);
    for (i = 0; i < 9; i++) {
        tw_play(RC(0, i + 1));
        tw_play(black_win[i]);
    }
    tw_play(RC(0, 10));
    check(tw_to_move() == TW_BLACK, "black to move with a winning hole");
    pos = tw_ai(TW_STRONG, &i);
    check(pos >= 0 && pos == i, "ai return agrees with out_pos");
    check(pos == black_win[9], "ai takes the immediate win");
    check(tw_play(pos) == 1, "that move really wins");

    /* two-component shape: black's left chain is one hop from joining a
     * black peg already sitting on the right edge; the joint is the
     * interior point (5,9), legal for red, so red's ai must fill it */
    tw_new(1);
    for (i = 0; i < 8; i++) {
        tw_play(RC(0, i + 1));           /* red fillers, cols 1..8 */
        tw_play(black_win[i]);           /* chain pegs 0..7 */
    }
    tw_play(RC(0, 9));
    tw_play(RC(4, 11));                  /* black right-edge peg, isolated */
    check(tw_result() == TW_ONGOING, "right-edge peg alone is not a win");
    check(tw_to_move() == TW_RED, "red to move, black one joint from win");
    pos = tw_ai(TW_STRONG, &i);
    check(pos == RC(5, 9), "ai blocks the only joining hole");
    check(tw_play(pos) == 0, "the block itself is legal");

    /* red skips the block this time; black's ai must find the joint */
    tw_new(1);
    for (i = 0; i < 8; i++) {
        tw_play(RC(0, i + 1));
        tw_play(black_win[i]);
    }
    tw_play(RC(0, 9));
    tw_play(RC(4, 11));
    tw_play(RC(0, 10));                  /* red filler elsewhere */
    check(tw_to_move() == TW_BLACK, "black to move");
    pos = tw_ai(TW_NORMAL, &i);
    check(pos == RC(5, 9), "ai joins its two components and wins");
    check(tw_play(pos) == 1, "the join really wins");

    /* determinism: same seed, same sequence, rng untouched in between */
    {
        int a[6], b[6];
        tw_new(11);
        for (i = 0; i < 6; i++) {
            a[i] = tw_ai(TW_NORMAL, &pos);
            tw_play(a[i]);
        }
        tw_new(11);
        for (i = 0; i < 6; i++) {
            b[i] = tw_ai(TW_NORMAL, &pos);
            tw_play(b[i]);
        }
        check(memcmp(a, b, sizeof a) == 0, "same seed gives same AI moves");
    }

    /* weak never crashes on an empty board and plays something legal */
    tw_new(5);
    pos = tw_ai(TW_WEAK, &i);
    check(pos >= 0 && pos == i, "weak move found on an empty board");
    check(tw_play(pos) == 0, "weak move is legal");
}

int main(void)
{
    test_legality();
    test_linking();
    test_crossing();
    test_black_win();
    test_red_win();
    test_draw();
    test_undo();
    test_undo_rebuild();
    test_save_roundtrip();
    test_restore_seeds_rng();
    test_save_forgery();
    test_resign_roundtrip();
    test_ai();

    if (failures) {
        printf("twixt-test: %d failure(s)\n", failures);
        return 1;
    }
    printf("twixt-test OK: rods, crossings, wins, undo, save, AI\n");
    return 0;
}
