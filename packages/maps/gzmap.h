/* Glaze map tile packs (.gzmap, glaze-os tools/mkmaptiles.py): the index,
 * tile lookup with fallback to a coarser zoom, and a small decoded-tile
 * cache of 256x256 RGB565 surfaces. */
#ifndef MAPS_GZMAP_H
#define MAPS_GZMAP_H
#include <glaze/gfx.h>
#include <glaze/types.h>

#define GZMAP_TILE 256
#define GZMAP_CACHE 12          /* decoded tiles kept: 12 x 128 KiB */

struct gzmap_entry { uint32_t zx, y, offset, length; };

struct gzmap {
    int fd;
    uint8_t min_zoom, max_zoom;
    uint32_t count;
    struct gzmap_entry *index;
    int32_t south, west, north, east;   /* 1e-7 degrees */
    char name[64], attribution[256];
    uint32_t file_size;
};

/* Opens a pack, reading its header and index. 0, or a negative GZ_ error. */
int gzmap_open(struct gzmap *m, const char *path);
void gzmap_close(struct gzmap *m);
/* Index entry for a tile, or NULL. */
const struct gzmap_entry *gzmap_find(const struct gzmap *m, unsigned z, uint32_t x, uint32_t y);
/* Decodes a tile blob into 256x256 RGB565 pixels. */
bool gzmap_decode(const uint8_t *blob, uint32_t length, uint16_t *pixels);

/* Cache: a tile's surface if decoded already; gzmap_tile_load decodes it
 * (reading the pack), evicting the least recently used. */
struct gzmap_cache {
    struct { const struct gzmap *map; uint8_t z; uint32_t x, y, used; bool valid; gfx_surface s; uint16_t *px; } slot[GZMAP_CACHE];
    uint32_t clock;
    uint8_t *blob;              /* scratch for one compressed tile */
    uint32_t blob_size;
    uint8_t *indices;           /* scratch: 65536 palette indices */
};
bool gzmap_cache_init(struct gzmap_cache *c);
void gzmap_cache_flush(struct gzmap_cache *c);
const gfx_surface *gzmap_tile_cached(struct gzmap_cache *c, const struct gzmap *m, unsigned z, uint32_t x, uint32_t y);
const gfx_surface *gzmap_tile_load(struct gzmap_cache *c, const struct gzmap *m, unsigned z, uint32_t x, uint32_t y);

#endif
