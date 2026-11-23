#ifndef UI_H
#define UI_H
#include "../core/gfx.h"
#include "../core/db.h"

#define MAXINK_STROKE 64
#define MAXINK_PT     600

typedef struct { int n; struct { short x, y; } p[MAXINK_PT]; } inkstroke_t;
typedef struct { int n; inkstroke_t s[MAXINK_STROKE]; } ink_t;

typedef struct { rect_t r; const char *label; int on; } btn_t;

void ui_btn(btn_t *b, int big);
int  ui_hit(rect_t r, int x, int y);

/* One 田字格 cell: frame plus dashed centre cross. */
void ui_cell(rect_t r);
/* Render strokes [0,upto) of a character filled into cell r.
 * upto < 0 means all. `hi` highlights stroke (hi) in mid grey. */
void ui_char(rect_t r, const strokes_t *s, int upto, int hi, uint8_t v);
/* Numbered stroke-order diagram: whole char plus a number at each median head. */
void ui_char_numbered(rect_t r, const strokes_t *s);
/* Progressive frames strip, wrapping inside `area`. Returns rows used. */
int  ui_frames(rect_t area, const strokes_t *s, int cell, int gap);

/* Reduce captured ink to a handwriting-search signature: start, middle and
 * end of each stroke, normalised into a 0..255 box with aspect preserved --
 * the same shape the build pipeline derives from the stroke medians.
 * Returns the stroke count, or 0 if there is nothing usable. */
int  ui_ink_signature(const ink_t *k, unsigned char *sig, int max_strokes);

void ui_ink_clear(ink_t *k);
/* 0 when the ink already holds MAXINK_STROKE strokes: no stroke was begun,
 * and moves must not follow, or they are drawn onto the end of the last. */
int  ui_ink_down(ink_t *k, int x, int y);
void ui_ink_move(ink_t *k, int x, int y);
void ui_ink_draw(const ink_t *k, int t);
/* A character's worth of ink, kept small.
 *
 * These are only ever redrawn as thumbnails -- a row showing the line in your
 * own hand -- so full resolution is wasted: one ink_t is 150KB, and a line of
 * them would be five megabytes of static memory on a device where every one
 * counts. Strokes are subsampled and normalised into a 0..255 box. */
#define THUMB_STROKE 24
#define THUMB_PT     80
typedef struct {
    int n;
    struct { int n; unsigned char p[THUMB_PT][2]; } s[THUMB_STROKE];
} inkthumb_t;

/* Capture `k`, drawn against `box`, into a thumbnail. */
void ui_ink_thumb(inkthumb_t *out, const ink_t *k, rect_t box);
/* Draw one back, filling `to`. */
void ui_ink_thumb_draw(rect_t to, const inkthumb_t *th, int t);
/* The segment the last move added -- or, on a stroke of one point, the dot
 * -- and the rectangle it touched. */
void ui_ink_draw_last_seg(const ink_t *k, int t, rect_t *dirty);

/* utf8 helpers */
int  u8_clen(unsigned char c);
int  u8_len(const char *s);                 /* number of codepoints */
int  u8_at(const char *s, int i, char *out);/* copy codepoint i into out (5 bytes) */

/* Wrapped CJK text with every occurrence of `hl` drawn darker than the rest.
 * Breaks between characters, which is how Chinese wraps anyway.
 * Returns the y just below the last line drawn. */
int  ui_text_hl(int x, int y, int maxw, int px, int lineh,
                const char *utf8, const char *hl);

/* On-screen latin keyboard. Drawing and hit-testing share one layout function
 * so they cannot drift apart. Returns the key pressed, or 0.
 * Special keys: 8 backspace, 24 clear, ' ' space. */
void ui_keyboard(rect_t area);
int  ui_keyboard_hit(rect_t area, int x, int y);
#define KEY_CLEAR 24
/* Height the keyboard needs; callers must reserve exactly this. */
#define KB_HEIGHT 470

#endif
