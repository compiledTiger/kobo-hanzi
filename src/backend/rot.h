/* rot.h -- software rotation between the app's buffer and the panel.
 *
 * The Kobo's panel stays in its native portrait orientation; nothing is
 * asked of the kernel. For a landscape ("flip") view the app draws into a
 * buffer of swapped size, and these map between the two: the buffer's
 * pixels onto the panel (blit), the buffer's dirty rectangles onto the
 * panel's (the refresh), and the panel's touch points back into the buffer.
 *
 * Pure functions, header only, as light.h: tests/rot_test.cpp replays them
 * on the desktop against a brute-force rotation, since only the device runs
 * surf_fb.cpp.
 *
 * Quarter turns are clockwise, as the buffer is laid on the panel:
 *   0  as is
 *   1  the buffer's top edge runs down the panel's right edge
 *      (read it with the device turned a quarter anticlockwise)
 *   2  upside down
 *   3  the buffer's top edge runs up the panel's left edge
 * `pw` x `ph` is always the panel's (portrait) size; the buffer is that,
 * swapped for odd turns (rot_buf_size). */
#ifndef ROT_H
#define ROT_H
#include "../core/gfx.h"

static inline int rot_norm(int q) { return ((q % 4) + 4) % 4; }

static inline void rot_buf_size(int q, int pw, int ph, int *bw, int *bh) {
    if (rot_norm(q) & 1) { *bw = ph; *bh = pw; }
    else                 { *bw = pw; *bh = ph; }
}

/* A buffer pixel's place on the panel. */
static inline void rot_to_panel(int q, int pw, int ph, int bx, int by, int *px, int *py) {
    switch (rot_norm(q)) {
    default: *px = bx;          *py = by;          break;
    case 1:  *px = pw - 1 - by; *py = bx;          break;
    case 2:  *px = pw - 1 - bx; *py = ph - 1 - by; break;
    case 3:  *px = by;          *py = ph - 1 - bx; break;
    }
}

/* A panel pixel's place in the buffer: the inverse, for touch. */
static inline void rot_to_buf(int q, int pw, int ph, int px, int py, int *bx, int *by) {
    switch (rot_norm(q)) {
    default: *bx = px;          *by = py;          break;
    case 1:  *bx = py;          *by = pw - 1 - px; break;
    case 2:  *bx = pw - 1 - px; *by = ph - 1 - py; break;
    case 3:  *bx = ph - 1 - py; *by = px;          break;
    }
}

/* r cut to 0..w x 0..h; an empty result has w or h <= 0. */
static inline rect_t rot_clip(rect_t r, int w, int h) {
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > w) r.w = w - r.x;
    if (r.y + r.h > h) r.h = h - r.y;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

/* A buffer rectangle's place on the panel: exactly the pixels rot_to_panel
 * sends its pixels to. */
static inline rect_t rot_rect_to_panel(int q, int pw, int ph, rect_t r) {
    rect_t o;
    switch (rot_norm(q)) {
    default: o = r; break;
    case 1:  o.x = pw - (r.y + r.h); o.y = r.x;             o.w = r.h; o.h = r.w; break;
    case 2:  o.x = pw - (r.x + r.w); o.y = ph - (r.y + r.h); o.w = r.w; o.h = r.h; break;
    case 3:  o.x = r.y;              o.y = ph - (r.x + r.w); o.w = r.h; o.h = r.w; break;
    }
    return o;
}

/* Walking a panel row rightwards from (px,py): the buffer index of its first
 * pixel, and how far each step right moves in the buffer. The buffer is
 * bw = rot_buf_size's width, row-major. */
static inline void rot_row_walk(int q, int pw, int ph, int bw, int px, int py,
                                long *start, long *step) {
    int bx, by;
    rot_to_buf(q, pw, ph, px, py, &bx, &by);
    *start = (long)by * bw + bx;
    switch (rot_norm(q)) {
    default: *step = 1;         break;
    case 1:  *step = -(long)bw; break;    /* px+1: by-1 */
    case 2:  *step = -1;        break;
    case 3:  *step = (long)bw;  break;    /* px+1: by+1 */
    }
}

/* n grey values of panel row py from px on, read out of the buffer `src`
 * (bw wide). The caller keeps px..px+n-1 inside the panel and the panel
 * rectangle inside rot_rect_to_panel of a buffer rectangle inside the
 * buffer, so every read is in bounds. */
static inline void rot_row(int q, int pw, int ph, const uint8_t *src, int bw,
                           int px, int py, int n, uint8_t *out) {
    long i, step;
    rot_row_walk(q, pw, ph, bw, px, py, &i, &step);
    for (int k = 0; k < n; k++, i += step) out[k] = src[i];
}

#endif
