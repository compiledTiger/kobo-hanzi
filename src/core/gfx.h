/* gfx.h -- software rasteriser into an 8-bit greyscale buffer.
 *
 * Everything the app draws goes here. The platform backends (SDL on the
 * desktop, framebuffer on the Kobo) only present this buffer and read input,
 * which keeps them tiny and keeps the two builds pixel-identical. */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>

#define WHITE 0xFF
#define BLACK 0x00
#define GREY  0xB4
#define FAINT 0xDC
#define INK   0x10

typedef struct { int x, y, w, h; } rect_t;

extern uint8_t *gfx_fb;
extern int gfx_w, gfx_h;

int  gfx_init(int w, int h);
void gfx_free(void);

void gfx_clear(uint8_t v);
void gfx_fill(rect_t r, uint8_t v);
void gfx_frame(rect_t r, int t, uint8_t v);
void gfx_line(int x0, int y0, int x1, int y1, int t, uint8_t v);
/* Rounded rectangles, antialiased on the corners. Keys and fields look far
 * less like a spreadsheet with these. */
void gfx_round_fill(rect_t r, int rad, uint8_t v);
void gfx_round_frame(rect_t r, int rad, int t, uint8_t v);
/* border ring plus an explicit interior, so a fill is not overwritten */
void gfx_round_rect(rect_t r, int rad, int t, uint8_t border, uint8_t fill);
void gfx_dash_h(int x0, int x1, int y, int dash, uint8_t v);
void gfx_dash_v(int x, int y0, int y1, int dash, uint8_t v);

/* Filled polygon from packed int16 x,y pairs in the 1024 em box.
 * Mapped as  X = ox + x*s,  Y = oy + y*s.  Even-odd scanline fill. */
void gfx_poly(const int16_t *pts, int npts, float ox, float oy, float s, uint8_t v);

int  gfx_font_load(const char *path);
int  gfx_text(int x, int y, int px, uint8_t v, const char *utf8);
int  gfx_text_w(int px, const char *utf8);
/* gfx_text drawn narrower, at `xs` (0..1] of its width, the height as at px:
 * a long pinyin syllable condensed into its character's cell. Not cached. */
int  gfx_text_hscale(int x, int y, int px, float xs, uint8_t v, const char *utf8);
int  gfx_text_clip(int x, int y, int px, int maxw, uint8_t v, const char *utf8);
/* Draw at `px`, stepping the size down (never below `minpx`) until the string
 * fits `maxw`; ellipsise only if it still does not. For secondary lines whose
 * tail carries meaning, where clipping would be worse than a smaller size. */
int  gfx_text_fit(int x, int y, int px, int minpx, int maxw, uint8_t v,
                  const char *utf8);
/* Wrap `utf8` into `maxw`, at most `max_lines` of them, and return the y below
 * the last line drawn. Breaks at spaces for Latin and between characters for
 * CJK, which is how Chinese wraps. The final line is ellipsised if the text
 * runs past the budget. A NULL/empty string draws nothing and returns y. */
int  gfx_text_wrap(int x, int y, int px, int lineh, int maxw, int max_lines,
                   uint8_t v, const char *utf8);
/* Where the text continues after its first `lines` wrapped lines, wrapped as
 * gfx_text_wrap would: lets a long paragraph carry on onto the next page
 * from the line it stopped at. */
const char *gfx_wrap_skip(int px, int maxw, const char *utf8, int lines);
/* Whether the first character of `utf8` may not begin a line (。，” ...). */
int gfx_no_line_start(const char *utf8);

static inline rect_t rc(int x, int y, int w, int h) { rect_t r = {x,y,w,h}; return r; }

#endif
