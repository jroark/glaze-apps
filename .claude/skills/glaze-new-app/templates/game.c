#include "game.h"
#include <string.h>

/* Bump the last character when the Game layout changes, so old saves are refused. */
#define SAVE_MAGIC 0x31475a47u /* "GZG1" */

static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};

static uint32_t random32(Game *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static void place_star(Game *g)
{
    do {
        g->star_x = (uint8_t)(random32(g) % GRID_W);
        g->star_y = (uint8_t)(random32(g) % GRID_H);
    } while (g->star_x == g->x && g->star_y == g->y);
}

void game_new(Game *g, uint32_t seed, uint32_t best)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->best = best;
    g->x = GRID_W / 2;
    g->y = GRID_H / 2;
    g->ms_left = ROUND_MS;
    place_star(g);
}

bool game_move(Game *g, int dir)
{
    if (g->over || dir < 0 || dir > 3)
        return false;
    int x = g->x + dx[dir], y = g->y + dy[dir];
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
        return false;
    g->x = (uint8_t)x;
    g->y = (uint8_t)y;
    return true;
}

bool game_collect(Game *g)
{
    if (g->over || g->x != g->star_x || g->y != g->star_y)
        return false;
    g->score += 10;
    if (g->score > g->best)
        g->best = g->score;
    place_star(g);
    return true;
}

void game_tick(Game *g, uint32_t ms)
{
    if (g->over)
        return;
    if (ms >= g->ms_left) {
        g->ms_left = 0;
        g->over = 1;
    } else {
        g->ms_left -= ms;
    }
}

/* FNV-1a over the record. */
static uint32_t checksum(const uint8_t *p, unsigned n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

void game_encode(const Game *g, uint8_t *out)
{
    uint32_t header[3] = {SAVE_MAGIC, sizeof(Game), checksum((const uint8_t *)g, sizeof(*g))};
    memcpy(out, header, 12);
    memcpy(out + 12, g, sizeof(*g));
}

bool game_decode(Game *g, const uint8_t *in, unsigned size)
{
    uint32_t h[3];
    Game t;
    if (size != SAVE_BYTES)
        return false;
    memcpy(h, in, 12);
    memcpy(&t, in + 12, sizeof(t));
    if (h[0] != SAVE_MAGIC || h[1] != sizeof(Game) || h[2] != checksum(in + 12, sizeof(t)))
        return false;
    /* Validate every field a corrupt file could break. */
    if (t.x >= GRID_W || t.y >= GRID_H || t.star_x >= GRID_W || t.star_y >= GRID_H)
        return false;
    if (t.over > 1 || !t.rng || t.ms_left > ROUND_MS || t.best < t.score || t.score % 10)
        return false;
    *g = t;
    return true;
}
