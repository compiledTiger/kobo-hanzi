#include "ui.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

int ui_hit(rect_t r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void ui_btn(btn_t *b, int big) {
    /* One size for every button. `big` is emphasis, drawn as a heavier frame:
     * two buttons side by side in different type sizes just looks like a
     * mistake. */
    int px = 42;
    gfx_fill(b->r, b->on ? BLACK : WHITE);
    gfx_frame(b->r, big ? 5 : 3, BLACK);
    int w = gfx_text_w(px, b->label);
    /* smaller only where it would not fit: "写 try writing it" ran over
     * both borders of a third-width button on the Clara */
    while (w > b->r.w - 24 && px > 28) { px -= 2; w = gfx_text_w(px, b->label); }
    gfx_text(b->r.x + (b->r.w - w) / 2,
             b->r.y + b->r.h / 2 + px / 3,
             px, b->on ? WHITE : BLACK, b->label);
}

void ui_cell(rect_t r) {
    gfx_frame(r, 2, GREY);
    gfx_dash_h(r.x + 6, r.x + r.w - 6, r.y + r.h / 2, 9, FAINT);
    gfx_dash_v(r.x + r.w / 2, r.y + 6, r.y + r.h - 6, 9, FAINT);
}

void ui_char(rect_t r, const strokes_t *s, int upto, int hi, uint8_t v) {
    if (!s || s->n == 0) return;
    float sc = (float)r.w / 1024.0f;
    int n = (upto < 0 || upto > s->n) ? s->n : upto;
    for (int i = 0; i < n; i++)
        gfx_poly(s->out[i], s->nout[i], (float)r.x, (float)r.y, sc,
                 (i == hi) ? GREY : v);
}

void ui_char_numbered(rect_t r, const strokes_t *s) {
    if (!s || s->n == 0) return;
    float sc = (float)r.w / 1024.0f;
    for (int i = 0; i < s->n; i++)
        gfx_poly(s->out[i], s->nout[i], (float)r.x, (float)r.y, sc, BLACK);
    for (int i = 0; i < s->n; i++) {
        if (s->nmed[i] < 1) continue;
        int mx = (int)(r.x + s->med[i][0] * sc);
        int my = (int)(r.y + s->med[i][1] * sc);
        char num[12];
        snprintf(num, sizeof num, "%d", i + 1);
        int pxs = r.w / 14; if (pxs < 14) pxs = 14;
        int tw = gfx_text_w(pxs, num);
        /* white disc so the number stays legible on top of black ink */
        for (int yy = -pxs*3/4; yy <= pxs*3/4; yy++)
            for (int xx = -pxs*3/4; xx <= pxs*3/4; xx++)
                if (xx*xx + yy*yy <= (pxs*3/4)*(pxs*3/4)) {
                    int px_ = mx + xx, py_ = my + yy;
                    if (px_ >= 0 && py_ >= 0 && px_ < gfx_w && py_ < gfx_h)
                        gfx_fb[(size_t)py_ * gfx_w + px_] = WHITE;
                }
        gfx_text(mx - tw/2, my + pxs/3, pxs, BLACK, num);
    }
}

/* Returns the number of rows actually drawn, which is not always the number
 * needed: callers advance their layout by it, and over-reporting used to push
 * the example sentences off the card. */
int ui_frames(rect_t area, const strokes_t *s, int cell, int gap) {
    if (!s || s->n == 0) return 0;
    int per = (area.w + gap) / (cell + gap);
    if (per < 1) per = 1;
    int total = s->n + 1;              /* progressive frames + full trace */
    int drawn_rows = 0;
    for (int i = 0; i < total; i++) {
        int rr = i / per, cc = i % per;
        rect_t c = rc(area.x + cc * (cell + gap), area.y + rr * (cell + gap), cell, cell);
        if (c.y + cell > area.y + area.h) break;
        drawn_rows = rr + 1;
        ui_cell(c);
        if (i < s->n) {
            ui_char(c, s, i, -1, BLACK);        /* strokes so far, black */
            float sc = (float)c.w / 1024.0f;    /* current stroke, mid grey */
            gfx_poly(s->out[i], s->nout[i], (float)c.x, (float)c.y, sc, 0x78);
        } else {
            ui_char(c, s, -1, -1, FAINT);       /* trace box */
        }
    }
    return drawn_rows;
}

/* ---- ink --------------------------------------------------------------- */

int ui_ink_signature(const ink_t *k, unsigned char *sig, int max_strokes) {
    if (!k || k->n < 1) return 0;
    int n = k->n < max_strokes ? k->n : max_strokes;

    int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < k->s[i].n; j++) {
            int x = k->s[i].p[j].x, y = k->s[i].p[j].y;
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    if (maxx < minx || maxy < miny) return 0;

    int w = maxx - minx, h = maxy - miny;
    int big = w > h ? w : h;
    if (big < 1) big = 1;
    float scale = 255.0f / (float)big;
    float ox = (255.0f - w * scale) / 2.0f;
    float oy = (255.0f - h * scale) / 2.0f;

    for (int i = 0; i < n; i++) {
        const inkstroke_t *st = &k->s[i];
        if (st->n < 1) return 0;
        int idx[3] = { 0, st->n / 2, st->n - 1 };
        for (int q = 0; q < 3; q++) {
            float fx = (st->p[idx[q]].x - minx) * scale + ox;
            float fy = (st->p[idx[q]].y - miny) * scale + oy;
            int   cx = (int)(fx + 0.5f), cy = (int)(fy + 0.5f);
            sig[i * 6 + q * 2]     = (unsigned char)(cx < 0 ? 0 : (cx > 255 ? 255 : cx));
            sig[i * 6 + q * 2 + 1] = (unsigned char)(cy < 0 ? 0 : (cy > 255 ? 255 : cy));
        }
    }
    return n;
}

void ui_ink_clear(ink_t *k) { k->n = 0; }

int ui_ink_down(ink_t *k, int x, int y) {
    if (k->n >= MAXINK_STROKE) return 0;
    k->s[k->n].n = 0;
    k->s[k->n].p[0].x = (short)x;
    k->s[k->n].p[0].y = (short)y;
    k->s[k->n].n = 1;
    k->n++;
    return 1;
}

void ui_ink_move(ink_t *k, int x, int y) {
    if (k->n == 0) return;
    inkstroke_t *s = &k->s[k->n - 1];
    if (s->n >= MAXINK_PT) return;
    if (s->n > 0) {
        int dx = x - s->p[s->n-1].x, dy = y - s->p[s->n-1].y;
        if (dx*dx + dy*dy < 4) return;     /* drop jitter */
    }
    s->p[s->n].x = (short)x;
    s->p[s->n].y = (short)y;
    s->n++;
}

void ui_ink_thumb(inkthumb_t *out, const ink_t *k, rect_t box) {
    out->n = 0;
    if (box.w <= 0 || box.h <= 0) return;
    for (int i = 0; i < k->n && out->n < THUMB_STROKE; i++) {
        const inkstroke_t *s = &k->s[i];
        if (s->n < 1) continue;
        int keep = s->n < THUMB_PT ? s->n : THUMB_PT;
        int m = out->n;
        out->s[m].n = keep;
        for (int j = 0; j < keep; j++) {
            /* Even subsample: the shape survives, the point count does not. */
            int src = (keep == 1) ? 0 : j * (s->n - 1) / (keep - 1);
            long x = (long)(s->p[src].x - box.x) * 255 / box.w;
            long y = (long)(s->p[src].y - box.y) * 255 / box.h;
            out->s[m].p[j][0] = (unsigned char)(x < 0 ? 0 : (x > 255 ? 255 : x));
            out->s[m].p[j][1] = (unsigned char)(y < 0 ? 0 : (y > 255 ? 255 : y));
        }
        out->n++;
    }
}

void ui_ink_thumb_draw(rect_t to, const inkthumb_t *th, int t) {
    for (int i = 0; i < th->n; i++) {
        int n = th->s[i].n;
        for (int j = 1; j < n; j++) {
            int x0 = to.x + th->s[i].p[j-1][0] * to.w / 255;
            int y0 = to.y + th->s[i].p[j-1][1] * to.h / 255;
            int x1 = to.x + th->s[i].p[j][0]   * to.w / 255;
            int y1 = to.y + th->s[i].p[j][1]   * to.h / 255;
            gfx_line(x0, y0, x1, y1, t, BLACK);
        }
        if (n == 1) {
            int x0 = to.x + th->s[i].p[0][0] * to.w / 255;
            int y0 = to.y + th->s[i].p[0][1] * to.h / 255;
            gfx_line(x0, y0, x0, y0, t, BLACK);
        }
    }
}

void ui_ink_draw(const ink_t *k, int t) {
    for (int i = 0; i < k->n; i++) {
        const inkstroke_t *s = &k->s[i];
        /* A tap is a dot: one point, no segment. Drawing segments only, it
         * was kept, recognised and never shown. */
        if (s->n == 1) gfx_line(s->p[0].x, s->p[0].y, s->p[0].x, s->p[0].y, t, INK);
        for (int j = 1; j < s->n; j++)
            gfx_line(s->p[j-1].x, s->p[j-1].y, s->p[j].x, s->p[j].y, t, INK);
    }
}


void ui_ink_draw_last_seg(const ink_t *k, int t, rect_t *dirty) {
    if (k->n == 0) return;
    const inkstroke_t *s = &k->s[k->n - 1];
    if (s->n < 1) return;
    /* one point: the pen just down, drawn as the dot it is if it lifts here */
    int a = s->n >= 2 ? s->n - 2 : 0;
    int x0 = s->p[a].x, y0 = s->p[a].y;
    int x1 = s->p[s->n-1].x, y1 = s->p[s->n-1].y;
    gfx_line(x0, y0, x1, y1, t, INK);
    int lx = x0 < x1 ? x0 : x1, ly = y0 < y1 ? y0 : y1;
    int hx = x0 > x1 ? x0 : x1, hy = y0 > y1 ? y0 : y1;
    dirty->x = lx - t - 2; dirty->y = ly - t - 2;
    dirty->w = hx - lx + 2*t + 4; dirty->h = hy - ly + 2*t + 4;
}

/* ---- utf8 -------------------------------------------------------------- */

int u8_clen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* Length of the character at `s`, clamped to what is actually there. Guards
 * against a sequence cut short by truncation. */
static int u8_clen_at(const char *s) {
    int l = u8_clen((unsigned char)s[0]);
    for (int i = 1; i < l; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) return 1;
    return l;
}

int u8_len(const char *s) {
    int n = 0;
    while (*s) { s += u8_clen_at(s); n++; }
    return n;
}

int u8_at(const char *s, int i, char *out) {
    int n = 0;
    while (*s) {
        int l = u8_clen_at(s);
        if (n == i) { memcpy(out, s, (size_t)l); out[l] = 0; return l; }
        s += l; n++;
    }
    out[0] = 0;
    return 0;
}

/* ---- wrapped text with the headword picked out ------------------------- */

int ui_text_hl(int x, int y, int maxw, int px, int lineh,
               const char *utf8, const char *hl) {
    if (!utf8 || !*utf8) return y;
    size_t hlen = hl ? strlen(hl) : 0;
    size_t tlen = strlen(utf8);
    const char *end = utf8 + tlen;
    const char *p = utf8;
    int cx = x;
    while (*p) {
        int l = u8_clen_at(p);
        char ch[8];
        memcpy(ch, p, (size_t)l);
        ch[l] = 0;

        /* Is this character inside an occurrence of hl? Scan back far enough
         * that a match starting earlier still covers it. The q + hlen <= end
         * test matters: without it memcmp reads past the terminator. */
        int inhl = 0;
        if (hlen && hlen <= tlen) {
            for (const char *q = utf8; q <= p; q++) {
                if (q + hlen > end) break;
                if (q + hlen > p && !memcmp(q, hl, hlen)) { inhl = 1; break; }
            }
        }

        int w = gfx_text_w(px, ch);
        /* the marks that may not start a line (。，”) go down with the
         * character before them: that line, not theirs, is the one broken */
        int tail = 0;
        for (const char *q = p + l; *q && gfx_no_line_start(q); q += u8_clen_at(q)) {
            char m[8];
            int ml = u8_clen_at(q);
            memcpy(m, q, (size_t)ml);
            m[ml] = 0;
            tail += gfx_text_w(px, m);
        }
        if (cx > x && (cx + w > x + maxw || (tail && cx + w + tail > x + maxw)))
            { cx = x; y += lineh; }
        gfx_text(cx, y, px, inhl ? BLACK : 0x52, ch);
        cx += w;
        p += l;
    }
    return y + lineh;
}

/* ---- keyboard ---------------------------------------------------------- */

/* Digits matter here: tone-numbered pinyin is the most precise way to search,
 * and hao3 cannot be typed without them. */
static const char *KB[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };

typedef struct { rect_t r; int key; const char *label; } kbkey_t;

/* One layout, used for both drawing and hit testing. Rows are centred the way
 * a phone keyboard is; the bottom row is clear / space / delete. */
static int kb_layout(rect_t a, kbkey_t *out, int max) {
    const int gap = 7;
    int unit = a.w / 10;
    int kh   = a.h / 5;
    int n    = 0;
    static char labels[48][2];
    int li = 0;

    for (int row = 0; row < 4; row++) {
        int len = (int)strlen(KB[row]);
        float off = (row == 0 || row == 1) ? 0.0f : (row == 2 ? 0.5f : 1.5f);
        int x0 = a.x + (int)(off * unit);
        for (int i = 0; i < len && n < max; i++) {
            out[n].r   = rc(x0 + i * unit + gap/2, a.y + row * kh + gap/2,
                            unit - gap, kh - gap);
            out[n].key = KB[row][i];
            labels[li][0] = KB[row][i];
            labels[li][1] = 0;
            out[n].label = labels[li];
            li++;
            n++;
        }
    }

    int y = a.y + 4 * kh;
    int w25 = (int)(2.5f * unit), w5 = 5 * unit;
    if (n < max) { out[n].r = rc(a.x + gap/2, y + gap/2, w25 - gap, kh - gap);
                   out[n].key = KEY_CLEAR; out[n].label = "clear"; n++; }
    if (n < max) { out[n].r = rc(a.x + w25 + gap/2, y + gap/2, w5 - gap, kh - gap);
                   out[n].key = ' ';       out[n].label = "space"; n++; }
    if (n < max) { out[n].r = rc(a.x + w25 + w5 + gap/2, y + gap/2, w25 - gap, kh - gap);
                   out[n].key = 8;         out[n].label = "delete"; n++; }
    return n;
}

void ui_keyboard(rect_t a) {
    kbkey_t k[40];
    int n = kb_layout(a, k, 40);
    for (int i = 0; i < n; i++) {
        int special = k[i].key == ' ' || k[i].key == 8 || k[i].key == KEY_CLEAR;
        gfx_round_rect(k[i].r, 14, 2, special ? 0x98 : 0xB8,
                                      special ? 0xDC : 0xF6);
        int px = special ? 30 : 46;
        int w  = gfx_text_w(px, k[i].label);
        gfx_text(k[i].r.x + (k[i].r.w - w) / 2,
                 k[i].r.y + k[i].r.h / 2 + px / 3,
                 px, special ? 0x30 : BLACK, k[i].label);
    }
}

int ui_keyboard_hit(rect_t a, int x, int y) {
    kbkey_t k[40];
    int n = kb_layout(a, k, 40);
    for (int i = 0; i < n; i++)
        if (ui_hit(k[i].r, x, y)) return k[i].key;
    return 0;
}
