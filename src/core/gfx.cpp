#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <math.h>
#include <stdio.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../vendor/stb_truetype.h"
#pragma GCC diagnostic pop

uint8_t *gfx_fb = NULL;
int gfx_w = 0, gfx_h = 0;

static void glyph_cache_clear(void);

/* Also the way to a new size (the Kobo's flip view swaps it): the old buffer
 * is freed once the new one exists, and a failed allocation leaves the old
 * one and its size in place. */
int gfx_init(int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    uint8_t *fb = (uint8_t *)malloc((size_t)w * h);
    if (!fb) return -1;
    free(gfx_fb);
    gfx_fb = fb;
    gfx_w = w; gfx_h = h;
    memset(gfx_fb, WHITE, (size_t)w * h);
    return 0;
}
void gfx_free(void) { glyph_cache_clear(); free(gfx_fb); gfx_fb = NULL; }

static inline void px(int x, int y, uint8_t v) {
    if (x < 0 || y < 0 || x >= gfx_w || y >= gfx_h) return;
    gfx_fb[(size_t)y * gfx_w + x] = v;
}
/* alpha blend v over dst, a in 0..255 */
static inline void px_a(int x, int y, uint8_t v, int a) {
    if (x < 0 || y < 0 || x >= gfx_w || y >= gfx_h || a <= 0) return;
    uint8_t *p = &gfx_fb[(size_t)y * gfx_w + x];
    if (a >= 255) { *p = v; return; }
    *p = (uint8_t)(((int)v * a + (int)*p * (255 - a)) / 255);
}

void gfx_clear(uint8_t v) { memset(gfx_fb, v, (size_t)gfx_w * gfx_h); }

void gfx_fill(rect_t r, uint8_t v) {
    int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w, y1 = r.y + r.h;
    if (x1 > gfx_w) x1 = gfx_w;
    if (y1 > gfx_h) y1 = gfx_h;
    if (x1 <= x0 || y1 <= y0) return;     /* off the panel, or no width: a
                                             negative size to memset */
    for (int y = y0; y < y1; y++) memset(&gfx_fb[(size_t)y*gfx_w + x0], v, (size_t)(x1-x0));
}

void gfx_frame(rect_t r, int t, uint8_t v) {
    gfx_fill(rc(r.x, r.y, r.w, t), v);
    gfx_fill(rc(r.x, r.y + r.h - t, r.w, t), v);
    gfx_fill(rc(r.x, r.y, t, r.h), v);
    gfx_fill(rc(r.x + r.w - t, r.y, t, r.h), v);
}

void gfx_line(int x0, int y0, int x1, int y1, int t, uint8_t v) {
    int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
    int dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1;
    int err = dx + dy;
    int h = t / 2;
    for (;;) {
        for (int a = -h; a <= h; a++)
            for (int b = -h; b <= h; b++)
                if (a*a + b*b <= h*h + 1) px(x0+a, y0+b, v);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_dash_h(int x0, int x1, int y, int dash, uint8_t v) {
    for (int x = x0; x < x1; x += dash*2)
        for (int i = 0; i < dash && x+i < x1; i++) px(x+i, y, v);
}
void gfx_dash_v(int x, int y0, int y1, int dash, uint8_t v) {
    for (int y = y0; y < y1; y += dash*2)
        for (int i = 0; i < dash && y+i < y1; i++) px(x, y+i, v);
}

/* Horizontal inset of a rounded corner at row `dy` into a corner of radius
 * `rad`; fractional so the edge can be antialiased. */
static float corner_inset(int dy, int rad) {
    if (dy >= rad) return 0.0f;
    float k = (float)(rad - dy) - 0.5f;
    float s = (float)rad * (float)rad - k * k;
    if (s < 0) s = 0;
    return (float)rad - sqrtf(s);
}

void gfx_round_fill(rect_t r, int rad, uint8_t v) {
    if (rad < 1) { gfx_fill(r, v); return; }
    if (rad > r.w / 2) rad = r.w / 2;
    if (rad > r.h / 2) rad = r.h / 2;
    for (int y = 0; y < r.h; y++) {
        int dy = y < rad ? y : (y >= r.h - rad ? r.h - 1 - y : rad);
        float ins = corner_inset(dy, rad);
        int   hard = (int)ceilf(ins);
        int   x0 = r.x + hard, x1 = r.x + r.w - hard;
        if (x1 > x0) gfx_fill(rc(x0, r.y + y, x1 - x0, 1), v);
        if (hard > 0) {                       /* soften the stepped corner */
            int a = (int)((1.0f - (ins - (float)(hard - 1))) * 255.0f);
            px_a(r.x + hard - 1,            r.y + y, v, a);
            px_a(r.x + r.w - hard,          r.y + y, v, a);
        }
    }
}

void gfx_round_rect(rect_t r, int rad, int t, uint8_t border, uint8_t fill) {
    gfx_round_fill(r, rad, border);
    rect_t in = rc(r.x + t, r.y + t, r.w - 2*t, r.h - 2*t);
    if (in.w > 0 && in.h > 0) gfx_round_fill(in, rad - t > 0 ? rad - t : 1, fill);
}

void gfx_round_frame(rect_t r, int rad, int t, uint8_t v) {
    gfx_round_rect(r, rad, t, v, WHITE);
}

/* ---- polygon fill (even-odd, 3x vertical supersampling for smooth edges) */
void gfx_poly(const int16_t *pts, int npts, float ox, float oy, float s, uint8_t v) {
    if (npts < 3) return;
    float *xs = (float *)malloc(sizeof(float) * npts);
    float *ys = (float *)malloc(sizeof(float) * npts);
    if (!xs || !ys) { free(xs); free(ys); return; }
    float miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < npts; i++) {
        xs[i] = ox + pts[i*2]   * s;
        ys[i] = oy + pts[i*2+1] * s;
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (y0 < 0) y0 = 0;
    if (y1 > gfx_h) y1 = gfx_h;

    const int SS = 2;   /* subsamples per scanline; 3 cost 50% more for no
                         * visible gain at e-ink cell sizes */
    float *xi = (float *)malloc(sizeof(float) * (npts + 1));
    int   *cov = (int *)malloc(sizeof(int) * (gfx_w + 2));
    if (!xi || !cov) { free(xs); free(ys); free(xi); free(cov); return; }

    for (int y = y0; y < y1; y++) {
        memset(cov, 0, sizeof(int) * (gfx_w + 2));
        for (int sub = 0; sub < SS; sub++) {
            float sy = y + (sub + 0.5f) / SS;
            int n = 0;
            for (int i = 0, j = npts - 1; i < npts; j = i++) {
                float yi = ys[i], yj = ys[j];
                if ((yi <= sy && yj > sy) || (yj <= sy && yi > sy)) {
                    float t = (sy - yi) / (yj - yi);
                    xi[n++] = xs[i] + t * (xs[j] - xs[i]);
                }
            }
            for (int a = 1; a < n; a++) {            /* insertion sort */
                float k = xi[a]; int b = a - 1;
                while (b >= 0 && xi[b] > k) { xi[b+1] = xi[b]; b--; }
                xi[b+1] = k;
            }
            for (int a = 0; a + 1 < n; a += 2) {
                int xa = (int)ceilf(xi[a] - 0.5f), xb = (int)ceilf(xi[a+1] - 0.5f);
                if (xa < 0) xa = 0;
                if (xb > gfx_w) xb = gfx_w;
                for (int x = xa; x < xb; x++) cov[x]++;
            }
        }
        for (int x = 0; x < gfx_w; x++)
            if (cov[x]) px_a(x, y, v, cov[x] * 255 / SS);
    }
    free(xs); free(ys); free(xi); free(cov);
}

/* ---- text -------------------------------------------------------------- */

static stbtt_fontinfo s_font;
static unsigned char  *s_fontbuf = NULL;
static int s_font_ok = 0;

struct FileCloser { void operator()(FILE *f) const { if (f) fclose(f); } };

int gfx_font_load(const char *path) {
    std::unique_ptr<FILE, FileCloser> f(fopen(path, "rb"));
    if (!f) { fprintf(stderr, "gfx: cannot open font %s\n", path); return -1; }
    fseek(f.get(), 0, SEEK_END); long n = ftell(f.get()); fseek(f.get(), 0, SEEK_SET);
    s_fontbuf = (unsigned char *)malloc((size_t)n);
    if (!s_fontbuf || fread(s_fontbuf, 1, (size_t)n, f.get()) != (size_t)n) {
        free(s_fontbuf); s_fontbuf = NULL; return -1;
    }
    if (!stbtt_InitFont(&s_font, s_fontbuf, stbtt_GetFontOffsetForIndex(s_fontbuf, 0))) {
        fprintf(stderr, "gfx: stbtt_InitFont failed for %s\n", path);
        return -1;
    }
    s_font_ok = 1;
    return 0;
}

/* Decode one codepoint. Text can arrive truncated mid-sequence -- a long
 * headword copied into a fixed buffer, for instance -- so every continuation
 * byte is checked before it is read. A partial sequence yields U+FFFD and
 * advances one byte rather than running off the end of the string. */
static const char *utf8_next(const char *s, unsigned *cp) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return s + 1; }

    int need = ((c & 0xE0) == 0xC0) ? 1
             : ((c & 0xF0) == 0xE0) ? 2
             : ((c & 0xF8) == 0xF0) ? 3 : 0;
    if (!need) { *cp = 0xFFFD; return s + 1; }

    for (int i = 1; i <= need; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return s + 1; }

    switch (need) {
    case 1: *cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu); break;
    case 2: *cp = ((c & 0x0Fu) << 12) | (((unsigned char)s[1] & 0x3Fu) << 6)
                | ((unsigned char)s[2] & 0x3Fu); break;
    default: *cp = ((c & 0x07u) << 18) | (((unsigned char)s[1] & 0x3Fu) << 12)
                 | (((unsigned char)s[2] & 0x3Fu) << 6) | ((unsigned char)s[3] & 0x3Fu); break;
    }
    return s + need + 1;
}

/* Glyph cache. CJK rasterisation is the slow part on device, but a single
 * screen of example sentences already needs ~370 distinct glyphs, and they are
 * cached per (codepoint, size). Undersized, this used to fill up and then
 * silently stop rendering text mid-string, so it is generous and it evicts. */
#define GC_N 4096
#define GC_FULL (GC_N - GC_N / 8)     /* keep the open-addressing table sparse */
/* Entry count is not the real limit: one 300px CJK glyph is ~88 KB, so 4096 of
 * them would be ~350 MB and the device would be killed long before the table
 * filled. Bound the bitmaps by bytes as well. */
#define GC_MAX_BYTES (4u * 1024u * 1024u)
/* A 300px CJK glyph is ~88 KB: cache 45 of those and every small UI glyph is
 * evicted with them, so all the body text re-rasterises. Big glyphs are drawn
 * once per card anyway, so render them straight through instead. */
#define GC_MAX_PX 110
typedef struct { unsigned cp; int px; int w,h,xoff,yoff,adv; unsigned char *bm; } glyph_t;

/* Kaiti is a Chinese font. Its plain Latin letters are half-width, but the
 * accented vowels pinyin is written with -- a e i o u with the four tone
 * marks, and u-umlaut -- are drawn as full-width CJK glyphs, twice as wide.
 * Left alone "ren" with a tone mark comes out looking like "r e n", three
 * words instead of one, on every single card.
 *
 * So for those codepoints only, give the glyph the font's ordinary Latin
 * advance and centre its ink in that narrower cell. The letter keeps the
 * shape the font drew; it just stops reserving a Chinese character's worth
 * of space. Anything the font already draws at Latin width is untouched. */
static int latin_ref_adv(int pxs) {
    static int c_px = -1, c_adv = 0;
    if (pxs == c_px) return c_adv;
    float sc = stbtt_ScaleForPixelHeight(&s_font, (float)pxs);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&s_font, 'n', &adv, &lsb);
    c_px = pxs; c_adv = (int)(adv * sc + 0.5f);
    return c_adv;
}

/* Returns the advance to use, and shifts *xoff to keep the ink centred. */
static int narrow_accent(unsigned cp, int pxs, int adv, int *xoff) {
    /* Latin-1 Supplement through Latin Extended-B, which covers every
     * accented vowel pinyin uses: macron, acute, caron, grave and umlaut. */
    if (cp < 0x00C0 || cp > 0x024F) return adv;
    int ref = latin_ref_adv(pxs);
    if (ref <= 0 || adv <= (ref * 7) / 5) return adv;
    if (xoff) *xoff -= (adv - ref) / 2;
    return ref;
}

static glyph_t s_gc[GC_N];
static int      s_gc_used = 0;
static size_t   s_gc_bytes = 0;

static void glyph_cache_clear(void) {
    for (int i = 0; i < GC_N; i++) {
        if (s_gc[i].bm) free(s_gc[i].bm);
        s_gc[i].bm = NULL;
    }
    s_gc_used = 0;
    s_gc_bytes = 0;
}

static glyph_t *glyph_get(unsigned cp, int pxs) {
    unsigned h = (cp * 131u + (unsigned)pxs * 7919u) % GC_N;
    for (unsigned i = 0; i < GC_N; i++) {
        glyph_t *g = &s_gc[(h + i) % GC_N];
        if (g->bm && g->cp == cp && g->px == pxs) return g;
        if (!g->bm) {
            if (s_gc_used >= GC_FULL || s_gc_bytes >= GC_MAX_BYTES) {
                glyph_cache_clear();      /* start over rather than give up */
                h = (cp * 131u + (unsigned)pxs * 7919u) % GC_N;
                g = &s_gc[h];
            }
            s_gc_used++;
            float sc = stbtt_ScaleForPixelHeight(&s_font, (float)pxs);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&s_font, (int)cp, &adv, &lsb);
            /* A codepoint the font has no glyph for draws as .notdef, the
             * hollow "tofu" box. On a Chinese font that catches anything
             * incidental -- arrows, the ideographic description operators --
             * and a box is worse than nothing, because it looks like data.
             * Render those as blank, keeping the advance so nothing shifts. */
            if (stbtt_FindGlyphIndex(&s_font, (int)cp) == 0) {
                g->bm = NULL; g->w = g->h = 0; g->xoff = g->yoff = 0;
            } else {
                g->bm = stbtt_GetCodepointBitmap(&s_font, 0, sc, (int)cp,
                                                 &g->w, &g->h, &g->xoff, &g->yoff);
            }
            g->cp = cp; g->px = pxs; g->adv = (int)(adv * sc + 0.5f);
            g->adv = narrow_accent(cp, pxs, g->adv, &g->xoff);
            if (!g->bm) {   /* blank glyph (space): keep a 1x1 so the slot is taken */
                g->bm = (unsigned char *)calloc(1, 1); g->w = g->h = 1;
                g->xoff = g->yoff = 0;
            }
            s_gc_bytes += (size_t)g->w * (size_t)g->h;
            return g;
        }
    }
    return NULL;
}


/* Rasterise a glyph too large to be worth caching, draw it, free it. */
static int draw_big_glyph(int x, int y, int pxs, uint8_t v, unsigned cp) {
    float sc = stbtt_ScaleForPixelHeight(&s_font, (float)pxs);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&s_font, (int)cp, &adv, &lsb);
    /* blank where the font has no glyph, as glyph_get does: past the cache's
     * size this drew the tofu box (鿍, which Kaiti lacks, shown large) */
    if (stbtt_FindGlyphIndex(&s_font, (int)cp) == 0) return (int)(adv * sc + 0.5f);
    int w, h, xo, yo;
    unsigned char *bm = stbtt_GetCodepointBitmap(&s_font, 0, sc, (int)cp, &w, &h, &xo, &yo);
    if (bm) {
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++) {
                int a = bm[yy * w + xx];
                if (a) px_a(x + xo + xx, y + yo + yy, v, a);
            }
        stbtt_FreeBitmap(bm, NULL);
    }
    return (int)(adv * sc + 0.5f);
}

static int big_glyph_adv(int pxs, unsigned cp) {
    float sc = stbtt_ScaleForPixelHeight(&s_font, (float)pxs);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&s_font, (int)cp, &adv, &lsb);
    return (int)(adv * sc + 0.5f);
}

/* y is the text baseline */
int gfx_text(int x, int y, int pxs, uint8_t v, const char *utf8) {
    if (!s_font_ok || !utf8) return 0;
    int x0 = x;
    unsigned cp;
    while (*utf8) {
        utf8 = utf8_next(utf8, &cp);
        if (cp == '\n') continue;
        if (pxs > GC_MAX_PX) { x += draw_big_glyph(x, y, pxs, v, cp); continue; }
        glyph_t *g = glyph_get(cp, pxs);
        if (!g) break;
        for (int yy = 0; yy < g->h; yy++)
            for (int xx = 0; xx < g->w; xx++) {
                int a = g->bm[yy * g->w + xx];
                if (a) px_a(x + g->xoff + xx, y + g->yoff + yy, v, a);
            }
        x += g->adv;
    }
    return x - x0;
}

int gfx_text_hscale(int x, int y, int pxs, float xs, uint8_t v, const char *utf8) {
    if (!s_font_ok || !utf8) return 0;
    if (xs <= 0.f || xs > 1.f) xs = 1.f;
    float sc = stbtt_ScaleForPixelHeight(&s_font, (float)pxs), fx = (float)x;
    unsigned cp;
    while (*utf8) {
        utf8 = utf8_next(utf8, &cp);
        if (cp == '\n') continue;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&s_font, (int)cp, &adv, &lsb);
        int xoff = 0, a = (int)(adv * sc + 0.5f);
        /* the accented vowels at the Latin advance, as glyph_get has them */
        int na = narrow_accent(cp, pxs, a, &xoff);
        if (stbtt_FindGlyphIndex(&s_font, (int)cp) != 0) {
            int w, h, xo, yo;
            unsigned char *bm = stbtt_GetCodepointBitmap(&s_font, sc * xs, sc, (int)cp,
                                                         &w, &h, &xo, &yo);
            if (bm) {
                int ox = (int)(fx + 0.5f) + xo + (int)(xoff * xs);
                for (int yy = 0; yy < h; yy++)
                    for (int xx = 0; xx < w; xx++) {
                        int al = bm[yy * w + xx];
                        if (al) px_a(ox + xx, y + yo + yy, v, al);
                    }
                stbtt_FreeBitmap(bm, NULL);
            }
        }
        fx += na * xs;
    }
    return (int)(fx - (float)x + 0.5f);
}

int gfx_text_w(int pxs, const char *utf8) {
    if (!s_font_ok || !utf8) return 0;
    int w = 0; unsigned cp;
    while (*utf8) {
        utf8 = utf8_next(utf8, &cp);
        if (pxs > GC_MAX_PX) { w += big_glyph_adv(pxs, cp); continue; }
        glyph_t *g = glyph_get(cp, pxs);
        if (!g) break;
        w += g->adv;
    }
    return w;
}

/* draw, stopping before maxw and appending an ellipsis if it does not fit */
int gfx_text_clip(int x, int y, int pxs, int maxw, uint8_t v, const char *utf8) {
    if (!s_font_ok || !utf8) return 0;
    if (gfx_text_w(pxs, utf8) <= maxw) return gfx_text(x, y, pxs, v, utf8);
    int ell = gfx_text_w(pxs, "...");
    int x0 = x; unsigned cp;
    while (*utf8) {
        const char *nx = utf8_next(utf8, &cp);
        glyph_t *g = glyph_get(cp, pxs);
        if (!g) break;
        if (x - x0 + g->adv + ell > maxw) break;
        for (int yy = 0; yy < g->h; yy++)
            for (int xx = 0; xx < g->w; xx++) {
                int a = g->bm[yy * g->w + xx];
                if (a) px_a(x + g->xoff + xx, y + g->yoff + yy, v, a);
            }
        x += g->adv;
        utf8 = nx;
    }
    x += gfx_text(x, y, pxs, v, "...");
    return x - x0;
}

int gfx_text_fit(int x, int y, int pxs, int minpx, int maxw, uint8_t v,
                 const char *utf8) {
    if (!s_font_ok || !utf8) return 0;
    while (pxs > minpx && gfx_text_w(pxs, utf8) > maxw) pxs--;
    return gfx_text_clip(x, y, pxs, maxw, v, utf8);
}

/* Marks that may not begin a line. Chinese typesetting calls this 避头:
 * breaking a line so that a full stop or a closing bracket starts the next
 * one looks like a mistake, and in a book of short sentences it happens
 * constantly. */
static int no_line_start(unsigned cp) {
    /* the small and vertical forms (﹔ ︰ ...), all but their openers */
    if (cp >= 0xFE30 && cp <= 0xFE6B)
        return !(cp == 0xFE35 || cp == 0xFE37 || cp == 0xFE39 || cp == 0xFE3B || cp == 0xFE3D ||
                 cp == 0xFE3F || cp == 0xFE41 || cp == 0xFE43 || cp == 0xFE47 ||
                 cp == 0xFE59 || cp == 0xFE5B || cp == 0xFE5D);
    switch (cp) {
    case 0x3002: case 0x3001:                       /* 。、            */
    case 0xFF0C: case 0xFF1B: case 0xFF1A:          /* ，；：          */
    case 0xFF01: case 0xFF1F:                       /* ！？            */
    case 0x2026: case 0x2014:                       /* … —             */
    case 0x300B: case 0x3009: case 0x300D: case 0x300F:  /* 》〉」』   */
    case 0xFF09: case 0x3011:                       /* ）】            */
    case 0x201D: case 0x2019:                       /* ” ’            */
    case 0x3015: case 0x3017: case 0xFF3D:          /* 〕〗］          */
    case 0x00B7: case 0x2027:                       /* · ‧ (间隔号)    */
    case 0x2015: case 0x2500: case 0xFF0D: case 0xFF0E:  /* ― ─ － ． dashes and stops */
    case 0x3000:                                    /* the ideographic space */
    case 0x3019: case 0x301B: case 0x301E: case 0x301F:  /* 〙〛〞〟      */
        return 1;
    default: return 0;
    }
}

/* And marks that may not end one (避尾): an opening quote or bracket left at
 * the end of a line, the words it opens on the next. */
static int no_line_end(unsigned cp) {
    switch (cp) {
    case 0x201C: case 0x2018:                       /* “ ‘            */
    case 0x300A: case 0x3008: case 0x300C: case 0x300E:  /* 《〈「『   */
    case 0xFF08: case 0x3010: case 0x3014: case 0x3016:  /* （【〔〖   */
    case 0xFF3B:                                    /* ［              */
    case 0x3018: case 0x301A: case 0x301D:          /* 〘〚〝           */
    case 0xFE35: case 0xFE37: case 0xFE39: case 0xFE3B: case 0xFE3D:  /* vertical */
    case 0xFE3F: case 0xFE41: case 0xFE43: case 0xFE47:
    case 0xFE59: case 0xFE5B: case 0xFE5D:          /* small ﹙﹛﹝      */
        return 1;
    default: return 0;
    }
}

int gfx_no_line_start(const char *utf8) {
    if (!utf8 || !*utf8) return 0;
    unsigned cp;
    utf8_next(utf8, &cp);
    return no_line_start(cp);
}

/* Where the line starting at `p` breaks: `*brk` is the end of its text and
 * the return value is where the next line starts. The one rule for both
 * drawing and measuring, so a caller that skips lines lands where drawing
 * would have. */
static const char *wrap_break(const char *p, int pxs, int maxw, const char **brk_out) {
    /* Walk forward accumulating width, remembering the last point this
     * line could legally break at. */
    const char *brk = NULL;       /* end of the text if we break here   */
    const char *nxt = NULL;       /* where the next line starts         */
    const char *q = p;
    int w = 0, at_space = 0;      /* whether the break so far is at a space */
    while (*q) {
        unsigned cp;
        const char *after = utf8_next(q, &cp);
        int adv;
        if (pxs > GC_MAX_PX) adv = big_glyph_adv(pxs, cp);
        else { glyph_t *g = glyph_get(cp, pxs); adv = g ? g->adv : 0; }
        if (w + adv > maxw) {
            /* the space after a word that just fits: break there, rather
             * than at the space before it */
            if (cp == ' ') { brk = q; nxt = after; at_space = 1; break; }
            if (brk) break;
            /* A single run longer than the line: split it here, but never
             * before the first character, or the loop would not advance. */
            brk = nxt = (q == p) ? after : q;
            break;
        }
        w += adv;
        if (cp == ' ')          { brk = q; nxt = after; at_space = 1; }
        else if (cp >= 0x2E80)  { brk = after; nxt = after; at_space = 0; }  /* CJK: break anywhere */
        q = after;
    }
    if (!*q) { brk = q; nxt = q; }

    /* If the next line would begin with a mark that may not start one,
     * hand that character back to this line's predecessor instead. */
    /* Chinese punctuation only: at a space in English the step back landed
     * inside the word before ("the Master sai" / "d — learning"), and an
     * English line may well begin with a dash. */
    /* At a space the line ends with the word before it -- unless that is a
     * mark that opens something (说：“ + space): it goes down too. */
    if (*nxt && brk > p && at_space) {
        const char *scan = p, *lastp = p;
        unsigned cp, last = 0;
        while (scan < brk) {
            const char *here = scan;
            scan = utf8_next(scan, &cp);
            if (cp != ' ') { lastp = here; last = cp; }
        }
        if (no_line_end(last) && lastp > p) { brk = lastp; nxt = lastp; at_space = 0; }
    }
    /* Nor end with a mark that opens something: the same step back takes
     * it down with the words it opens. Repeated, for 曰：“ before a
     * closing mark, but never so far that the line is left empty. */
    if (*nxt && brk > p && !at_space) {
        for (int guard = 0; guard < 4; guard++) {
            unsigned first, last = 0, cp;
            utf8_next(nxt, &first);
            const char *scan = p, *prev = p;
            while (scan < brk) { prev = scan; scan = utf8_next(scan, &cp); last = cp; }
            if (!no_line_start(first) && !no_line_end(last)) break;
            /* step the break back one character, if there is one to give */
            if (prev <= p) break;
            brk = prev; nxt = prev;
        }
    }
    *brk_out = brk;
    while (*nxt == ' ') nxt++;
    return nxt;
}

const char *gfx_wrap_skip(int pxs, int maxw, const char *utf8, int lines) {
    const char *p = utf8 ? utf8 : "";
    if (!s_font_ok) return p;
    for (int i = 0; i < lines && *p; i++) {
        const char *brk;
        p = wrap_break(p, pxs, maxw, &brk);
    }
    return p;
}

int gfx_text_wrap(int x, int y, int pxs, int lineh, int maxw, int max_lines,
                  uint8_t v, const char *utf8) {
    if (!s_font_ok || !utf8 || !*utf8 || max_lines < 1) return y;

    int line = 0;
    const char *p = utf8;
    while (*p && line < max_lines) {
        const char *brk;
        const char *nxt = wrap_break(p, pxs, maxw, &brk);

        int last = (line == max_lines - 1) && *nxt;
        /* On the final allowed line, hand the clipper everything that is
         * left rather than just this line's worth: given a segment that
         * already fits it has nothing to ellipsise, and the remainder would
         * disappear with no sign that anything had been cut. */
        char seg[512];
        size_t n = last ? strlen(p) : (size_t)(brk - p);
        if (n >= sizeof seg) n = sizeof seg - 1;
        memcpy(seg, p, n);
        seg[n] = 0;
        if (last) gfx_text_clip(x, y, pxs, maxw, v, seg);
        else      gfx_text(x, y, pxs, v, seg);

        y += lineh;
        line++;
        p = nxt;
    }
    return y;
}
