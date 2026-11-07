/* .gzmap reading; format in glaze-os tools/mkmaptiles.py. */
#include "gzmap.h"
#include "puff.h"
#include <glaze/fs.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int read_at(int fd, uint32_t offset, void *buf, uint32_t len)
{
    if (fs_seek(fd, (int32_t)offset, FS_SEEK_SET) < 0) return GZ_EIO;
    uint32_t got = 0;
    while (got < len) {
        int n = fs_read(fd, (uint8_t *)buf + got, len - got);
        if (n <= 0) return n < 0 ? n : GZ_EIO;
        got += (uint32_t)n;
    }
    return 0;
}

static void string_at(int fd, uint32_t offset, uint32_t len, char *out, size_t cap)
{
    out[0] = 0;
    if (!len) return;
    if (len >= cap) len = (uint32_t)cap - 1;
    if (read_at(fd, offset, out, len) == 0) out[len] = 0;
    else out[0] = 0;
}

int gzmap_open(struct gzmap *m, const char *path)
{
    memset(m, 0, sizeof(*m));
    m->fd = -1;
    struct fs_stat st;
    if (fs_stat(path, &st) < 0) return GZ_ENOENT;
    m->fd = fs_open(path, FS_O_READ);
    if (m->fd < 0) return m->fd;
    m->file_size = st.size;
    uint8_t h[64];
    int rv = read_at(m->fd, 0, h, sizeof(h));
    if (rv) goto fail;
    rv = GZ_ECORRUPT;
    if (memcmp(h, "GZMP", 4) || le32(h + 4) != 1 || le16(h + 8) != GZMAP_TILE) goto fail;
    m->min_zoom = h[10]; m->max_zoom = h[11];
    m->count = le32(h + 12);
    uint32_t index_off = le32(h + 16), attr_off = le32(h + 20), attr_len = le32(h + 24);
    m->south = (int32_t)le32(h + 28); m->west = (int32_t)le32(h + 32);
    m->north = (int32_t)le32(h + 36); m->east = (int32_t)le32(h + 40);
    uint32_t name_off = le32(h + 44), name_len = le32(h + 48);
    if (m->min_zoom > m->max_zoom || m->max_zoom > 20 || !m->count || m->count > 1000000 ||
        index_off < 64 || (uint64_t)index_off + 16ull * m->count > m->file_size ||
        (uint64_t)attr_off + attr_len > m->file_size || (uint64_t)name_off + name_len > m->file_size)
        goto fail;
    m->index = malloc(16u * m->count);
    if (!m->index) { rv = GZ_ENOMEM; goto fail; }
    uint8_t *raw = (uint8_t *)m->index;
    if ((rv = read_at(m->fd, index_off, raw, 16u * m->count))) goto fail;
    for (uint32_t i = 0; i < m->count; i++) {     /* little-endian on the VR4131 too; checked */
        struct gzmap_entry e = { le32(raw + 16 * i), le32(raw + 16 * i + 4), le32(raw + 16 * i + 8),
                                 le32(raw + 16 * i + 12) };
        if ((uint64_t)e.offset + e.length > m->file_size || e.length < 4) { rv = GZ_ECORRUPT; goto fail; }
        m->index[i] = e;
    }
    string_at(m->fd, attr_off, attr_len, m->attribution, sizeof(m->attribution));
    string_at(m->fd, name_off, name_len, m->name, sizeof(m->name));
    return 0;
fail:
    gzmap_close(m);
    return rv;
}

void gzmap_close(struct gzmap *m)
{
    if (m->fd >= 0) fs_close(m->fd);
    free(m->index);
    memset(m, 0, sizeof(*m));
    m->fd = -1;
}

const struct gzmap_entry *gzmap_find(const struct gzmap *m, unsigned z, uint32_t x, uint32_t y)
{
    if (!m->index || z < m->min_zoom || z > m->max_zoom) return NULL;
    uint32_t zx = (uint32_t)z << 24 | x, lo = 0, hi = m->count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const struct gzmap_entry *e = &m->index[mid];
        if (e->zx < zx || (e->zx == zx && e->y < y)) lo = mid + 1;
        else hi = mid;
    }
    if (lo < m->count && m->index[lo].zx == zx && m->index[lo].y == y) return &m->index[lo];
    return NULL;
}

static bool decode_with(const uint8_t *blob, uint32_t length, uint16_t *pixels, uint8_t *indices)
{
    if (length < 4) return false;
    unsigned colours = le16(blob);
    if (!colours || colours > 256 || 4 + 2u * colours > length) return false;
    uint16_t palette[256];
    for (unsigned i = 0; i < colours; i++) palette[i] = le16(blob + 4 + 2 * i);
    unsigned long out = GZMAP_TILE * GZMAP_TILE, in = length - 4 - 2 * colours;
    if (puff(indices, &out, blob + 4 + 2 * colours, &in) || out != GZMAP_TILE * GZMAP_TILE) return false;
    for (unsigned i = 0; i < GZMAP_TILE * GZMAP_TILE; i++) {
        uint8_t k = indices[i];
        pixels[i] = k < colours ? palette[k] : 0;
    }
    return true;
}

bool gzmap_decode(const uint8_t *blob, uint32_t length, uint16_t *pixels)
{
    uint8_t *indices = malloc(GZMAP_TILE * GZMAP_TILE);
    bool ok = indices && decode_with(blob, length, pixels, indices);
    free(indices);
    return ok;
}

bool gzmap_cache_init(struct gzmap_cache *c)
{
    memset(c, 0, sizeof(*c));
    c->indices = malloc(GZMAP_TILE * GZMAP_TILE);
    return c->indices != NULL;
}

void gzmap_cache_flush(struct gzmap_cache *c)
{
    for (unsigned i = 0; i < GZMAP_CACHE; i++) c->slot[i].valid = false;
}

const gfx_surface *gzmap_tile_cached(struct gzmap_cache *c, const struct gzmap *m, unsigned z, uint32_t x, uint32_t y)
{
    for (unsigned i = 0; i < GZMAP_CACHE; i++)
        if (c->slot[i].valid && c->slot[i].map == m && c->slot[i].z == z && c->slot[i].x == x && c->slot[i].y == y) {
            c->slot[i].used = ++c->clock;
            return &c->slot[i].s;
        }
    return NULL;
}

const gfx_surface *gzmap_tile_load(struct gzmap_cache *c, const struct gzmap *m, unsigned z, uint32_t x, uint32_t y)
{
    const gfx_surface *hit = gzmap_tile_cached(c, m, z, x, y);
    if (hit) return hit;
    const struct gzmap_entry *e = gzmap_find(m, z, x, y);
    if (!e || e->length > 512u * 1024) return NULL;
    if (e->length > c->blob_size) {
        uint8_t *b = realloc(c->blob, e->length);
        if (!b) return NULL;
        c->blob = b; c->blob_size = e->length;
    }
    if (read_at(m->fd, e->offset, c->blob, e->length)) return NULL;
    unsigned victim = 0;
    for (unsigned i = 0; i < GZMAP_CACHE; i++) {
        if (!c->slot[i].valid) { victim = i; break; }
        if (c->slot[i].used < c->slot[victim].used) victim = i;
    }
    if (!c->slot[victim].px) {
        c->slot[victim].px = malloc(GZMAP_TILE * GZMAP_TILE * 2);
        if (!c->slot[victim].px) return NULL;
        gfx_surface_init(&c->slot[victim].s, c->slot[victim].px, GZMAP_TILE, GZMAP_TILE, GZMAP_TILE);
    }
    c->slot[victim].valid = false;
    if (!decode_with(c->blob, e->length, c->slot[victim].px, c->indices)) return NULL;
    c->slot[victim].map = m; c->slot[victim].z = (uint8_t)z; c->slot[victim].x = x; c->slot[victim].y = y;
    c->slot[victim].valid = true;
    c->slot[victim].used = ++c->clock;
    return &c->slot[victim].s;
}
