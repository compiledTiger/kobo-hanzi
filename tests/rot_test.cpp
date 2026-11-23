/* The Kobo's flip view, without the Kobo: src/backend/rot.h against a
 * rotation done the slow, obvious way.
 *
 * surf_fb.cpp blits a rotated buffer onto the portrait panel, refreshes the
 * rotated rectangle and maps touch back. Only the device runs that file, so
 * the arithmetic it uses lives in rot.h and is held here to: every pixel of
 * a rotated blit where a brute-force rotation puts it, a refresh rectangle
 * that is exactly the pixels a blit of the buffer rectangle wrote, touch
 * that comes back to the buffer pixel drawn under the finger, and no read
 * outside the buffer for rectangles that run off any edge. */
#include "../src/backend/rot.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int bad = 0;
static void chk(bool cond, const char *what, int q) {
    if (!cond) { printf("    FAIL: q=%d: %s\n", q, what); bad = 1; }
}

/* The panel of a small odd-sized device: 7 x 11, so a swapped axis or an
 * off-by-one cannot hide behind a square or an even size. */
static const int PW = 7, PH = 11;

/* What the panel shows of buffer `b` (bw x bh), by the definition: buffer
 * row 0 runs down the right edge for q=1, and so on. */
static std::vector<int> brute(int q, const std::vector<int> &b, int bw, int bh) {
    std::vector<int> p((size_t)PW * PH, -1);
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int px, py;
            switch (q) {
            default: px = x;          py = y;          break;
            case 1:  px = PW - 1 - y; py = x;          break;   /* turned clockwise */
            case 2:  px = PW - 1 - x; py = PH - 1 - y; break;
            case 3:  px = y;          py = PH - 1 - x; break;
            }
            p[(size_t)py * PW + px] = b[(size_t)y * bw + x];
        }
    return p;
}

int main(void) {
    for (int q = 0; q < 4; q++) {
        int bw, bh;
        rot_buf_size(q, PW, PH, &bw, &bh);
        chk((q & 1) ? (bw == PH && bh == PW) : (bw == PW && bh == PH), "buffer size", q);

        /* every buffer pixel distinct: its own index */
        std::vector<int> buf((size_t)bw * bh);
        for (size_t i = 0; i < buf.size(); i++) buf[i] = (int)i;
        std::vector<int> want = brute(q, buf, bw, bh);

        /* 1. points both ways, against the brute force, and round trips */
        int okpt = 1, okinv = 1;
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                int px, py, bx, by;
                rot_to_panel(q, PW, PH, x, y, &px, &py);
                if (px < 0 || py < 0 || px >= PW || py >= PH ||
                    want[(size_t)py * PW + px] != y * bw + x) okpt = 0;
                rot_to_buf(q, PW, PH, px, py, &bx, &by);
                if (bx != x || by != y) okinv = 0;
            }
        chk(okpt, "rot_to_panel puts a pixel where the rotation does", q);
        chk(okinv, "rot_to_buf undoes rot_to_panel (touch lands on what was drawn)", q);

        /* 2. a whole-buffer blit, row by row as surf_fb.cpp does it */
        {
            std::vector<uint8_t> b8(buf.size());
            for (size_t i = 0; i < b8.size(); i++) b8[i] = (uint8_t)(buf[i] * 3 + 1);
            std::vector<uint8_t> row(PW);
            int ok = 1;
            for (int py = 0; py < PH; py++) {
                rot_row(q, PW, PH, b8.data(), bw, 0, py, PW, row.data());
                for (int px = 0; px < PW; px++)
                    if (row[px] != (uint8_t)(want[(size_t)py * PW + px] * 3 + 1)) ok = 0;
            }
            chk(ok, "rot_row reads each panel row out of the right buffer pixels", q);
        }

        /* 3. every rectangle, including ones that run off each edge: the
         * clipped buffer rectangle's panel rectangle is exactly the pixels
         * those buffer pixels go to, and a partial blit of it, walking only
         * inside it, never reads outside the buffer */
        int okrect = 1, okread = 1, okpart = 1;
        for (int y0 = -3; y0 <= bh + 1; y0++)
            for (int x0 = -3; x0 <= bw + 1; x0++)
                for (int h = 0; h <= bh + 4; h += 2)
                    for (int w = 0; w <= bw + 4; w += 3) {
                        rect_t r = rot_clip(rc(x0, y0, w, h), bw, bh);
                        if (r.w <= 0 || r.h <= 0) continue;
                        rect_t pr = rot_rect_to_panel(q, PW, PH, r);
                        /* the panel rect covers exactly r's pixels */
                        std::vector<char> hit((size_t)PW * PH, 0);
                        for (int y = r.y; y < r.y + r.h; y++)
                            for (int x = r.x; x < r.x + r.w; x++) {
                                int px, py;
                                rot_to_panel(q, PW, PH, x, y, &px, &py);
                                hit[(size_t)py * PW + px] = 1;
                            }
                        int n = 0;
                        for (int py = 0; py < PH; py++)
                            for (int px = 0; px < PW; px++) {
                                bool in = px >= pr.x && px < pr.x + pr.w &&
                                          py >= pr.y && py < pr.y + pr.h;
                                if (in != (bool)hit[(size_t)py * PW + px]) okrect = 0;
                                n += in;
                            }
                        if (n != r.w * r.h) okrect = 0;
                        /* the walk inside pr stays in the buffer and agrees */
                        for (int py = pr.y; py < pr.y + pr.h; py++) {
                            long i, step;
                            rot_row_walk(q, PW, PH, bw, pr.x, py, &i, &step);
                            for (int k = 0; k < pr.w; k++, i += step) {
                                if (i < 0 || i >= (long)buf.size()) { okread = 0; break; }
                                if (buf[(size_t)i] != want[(size_t)py * PW + pr.x + k]) okpart = 0;
                            }
                        }
                    }
        chk(okrect, "a refresh rectangle is exactly the pixels its blit wrote", q);
        chk(okread, "a partial blit reads only inside the buffer", q);
        chk(okpart, "a partial blit writes what the whole one would", q);
    }

    /* 4. the Libra's own sizes, the corners by name: the flip buffer is
     * 1680 x 1264 and its top-left lands at the panel's top-right */
    {
        int bw, bh, px, py, bx, by;
        rot_buf_size(1, 1264, 1680, &bw, &bh);
        chk(bw == 1680 && bh == 1264, "Libra flip buffer is 1680x1264", 1);
        rot_to_panel(1, 1264, 1680, 0, 0, &px, &py);
        chk(px == 1263 && py == 0, "buffer top-left at the panel's top-right", 1);
        rot_to_panel(1, 1264, 1680, 1679, 1263, &px, &py);
        chk(px == 0 && py == 1679, "buffer bottom-right at the panel's bottom-left", 1);
        rot_to_buf(1, 1264, 1680, 1263, 1679, &bx, &by);
        chk(bx == 1679 && by == 0, "a touch at the panel's bottom-right is the buffer's top-right", 1);
        rect_t full = rot_rect_to_panel(1, 1264, 1680, rc(0, 0, 1680, 1264));
        chk(full.x == 0 && full.y == 0 && full.w == 1264 && full.h == 1680,
            "the whole buffer refreshes the whole panel", 1);
        /* a rectangle running past the buffer's right edge is clipped
         * before it is turned: it never becomes a read past gfx_fb */
        rect_t c = rot_clip(rc(1600, 1200, 200, 200), 1680, 1264);
        chk(c.w == 80 && c.h == 64, "clipped to the buffer, not the panel", 1);
    }
    /* quarter turns outside 0..3 mean the same turn */
    {
        int a, b, c, d;
        rot_to_panel(5, PW, PH, 2, 3, &a, &b);
        rot_to_panel(1, PW, PH, 2, 3, &c, &d);
        chk(a == c && b == d && rot_norm(-1) == 3, "turns taken modulo 4", 5);
    }

    if (!bad) printf("    -> rotation: points, rows, rectangles and touch agree (4 turns)\n");
    return bad;
}
