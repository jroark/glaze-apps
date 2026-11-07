# Drawing, fonts and widgets (`#include <glaze/ui.h>`)

Draw into `gfx_surface *s = &ui.surf;` (240 x 304, `UI_W` x `UI_H`), then show it:

```c
ui_invalidate_all();   /* or ui_invalidate(gfx_r(x, y, w, h)) for one changed area */
ui_present();          /* blocks until shown */
```

Redrawing the whole screen each frame is fine for small games.

- Colors are `0xRRGGBB` (stored as RGB565, so close shades merge).
- `alpha` is 0..255, where 255 = opaque.
- Rectangles are `gfx_r(x, y, w, h)`.

## Shapes

```c
void gfx_fill(gfx_surface *s, gfx_rect r, uint32_t rgb);
void gfx_fill_alpha(gfx_surface *s, gfx_rect r, uint32_t rgb, int alpha);      /* dim overlays */
void gfx_vgradient(gfx_surface *s, gfx_rect r, uint32_t top, uint32_t bottom, int alpha);
void gfx_round_rect(gfx_surface *s, gfx_rect r, int radius, uint32_t rgb, int alpha);
void gfx_round_rect_vgradient(gfx_surface *s, gfx_rect r, int radius, uint32_t top, uint32_t bottom, int alpha);
void gfx_circle(gfx_surface *s, int cx, int cy, int radius, uint32_t rgb, int alpha);  /* filled */
void gfx_line(gfx_surface *s, int x0, int y0, int x1, int y1, uint32_t rgb, int alpha);
/* Smooth thick line; coordinates and width in 1/16 px (multiply by 16). */
void gfx_line_aa(gfx_surface *s, int x0_16, int y0_16, int x1_16, int y1_16, int width_16, uint32_t rgb, int alpha);
static inline bool gfx_rect_contains(gfx_rect r, int x, int y);   /* hit test for touches */
void gfx_set_clip(gfx_surface *s, gfx_rect r);  void gfx_reset_clip(gfx_surface *s);
```

## Text

`y` is the top of the text line.

```c
const gfx_font *ui_font(int which);
int  gfx_text(gfx_surface *s, int x, int y, const gfx_font *f, const char *str, uint32_t rgb, int alpha);  /* returns width */
void gfx_text_center(gfx_surface *s, int x, int y, int w, const gfx_font *f, const char *str, uint32_t rgb, int alpha);
void gfx_text_fit(gfx_surface *s, int x, int y, int max_w, const gfx_font *f, const char *str, uint32_t rgb, int alpha); /* cuts with ... */
int  gfx_text_width(const gfx_font *f, const char *str);
int  ui_wrap(const gfx_font *f, const char *text, int width, int *starts, int max_lines); /* word wrap: line start offsets */
```

Example: `gfx_text_center(s, 0, 100, UI_W, ui_font(FONT_B20), "Game over", 0xFFFFFF, 255);`

Fonts (R regular, B bold, I italic, M mono), with measured sizes:

| Font | Line height | Avg px/char | Chars in 240 px |
| --- | --- | --- | --- |
| `FONT_R11` | 14 | 6.4 | ~37 |
| `FONT_R12` | 15 | 7.0 | ~34 |
| `FONT_B12` | 15 | 7.8 | ~30 |
| `FONT_B14` | 17 | 9.1 | ~26 |
| `FONT_B20` | 24 | 13 | ~18 |

All sizes: `FONT_R9 R10 R11 R12 R13 R14 R17 R24 R44`, `FONT_B8 B9 B10 B11 B12 B13 B14 B17 B20`,
`FONT_I10..I14`, `FONT_M10 M11 M12`. Symbols for strings: `GFX_CH_TIMES`,
`GFX_CH_BULLET`, `GFX_CH_LARROW`, `GFX_CH_RARROW`, `GFX_CH_CHECK`, `GFX_CH_DEGREE`.

## Standard widgets (iPhone-OS style)

```c
/* Top bar, 30 px (UI_NAV_H). style 0 = blue, 1 = black. back may be NULL. */
gfx_rect ui_draw_navbar(gfx_surface *s, const char *title, const char *back, gfx_rect *back_rect, int style);
/* Button on the right (right = true) of the nav bar; returns its rect for hit tests. */
gfx_rect ui_draw_bar_button(gfx_surface *s, const char *label, bool right, bool pressed, bool blue, int style);
void ui_draw_button(gfx_surface *s, gfx_rect r, const char *label, bool pressed);
void ui_draw_switch(gfx_surface *s, int x, int y, bool on);          /* UI_SWITCH_W x UI_SWITCH_H */
void ui_draw_slider(gfx_surface *s, gfx_rect r, int value, int max);
int  ui_slider_value(gfx_rect r, int max, int x);
void ui_draw_pinstripes(gfx_surface *s, gfx_rect r);                 /* settings-style background */
/* Modal box with 1 or 2 buttons (b2 may be NULL); msg may hold one '\n'. pressed = -1, 0 or 1. */
void ui_draw_alert(gfx_surface *s, const char *title, const char *msg, const char *b1, const char *b2,
                   gfx_rect *r1, gfx_rect *r2, int pressed);
```

Button pattern: keep `gfx_rect btn` from the last draw. On `TOUCH_DOWN`, set
`pressed = gfx_rect_contains(btn, e.x, e.y)`. On `TOUCH_UP`, act if `pressed` and still
inside. Then clear `pressed` and redraw. Also clear `pressed` on `GZ_EV_TOUCH_CANCEL`.

Scrolling list (for menus, high-score tables):

```c
struct ui_list list;
ui_list_init(&list, gfx_r(0, UI_NAV_H, UI_W, UI_H - UI_NAV_H), UI_ROW_H);
ui_list_reset(&list, count);
void draw_row(gfx_surface *s, int i, gfx_rect r, bool selected, void *ctx);   /* yours */
ui_list_draw(s, &list, draw_row, NULL);
bool dirty = false;
int tapped = ui_list_event(&list, &e, &dirty);   /* row index on a tap, else -1 */
```

## Images

```c
gfx_image img;
if (res_image(&img, "ball.gzi"))            /* packages/<id>/res/ball.gzi */
    gfx_draw_image(s, x, y, &img, 255);
```

To make a `.gzi`: `python3 ../glaze-os/out/sdk/glaze-sdk/tools/mkgzi.py in.png packages/<id>/res/ball.gzi`
(needs Pillow). Simple shapes drawn with `gfx_*` need no files and are cheaper.
