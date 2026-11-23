/* Replays real stroke medians as though they had been written, and checks
 * what the matcher accepts. Thresholds are meaningless in the abstract; the
 * only way to set them is against the data the app actually carries. */
#include "../src/core/match.h"
#include "../src/core/db.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static int bad = 0;

static unsigned seed = 12345;
static int rnd(int n) { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) % (unsigned)n); }

/* strokes_t medians are y-down ints in the 1024 box, packed as pairs. */
static int to_mstroke(const strokes_t *s, int i, mstroke_t *out, int jitter) {
    int n = s->nmed[i];
    if (n < 2) return 0;
    if (n > MATCH_MAXPT) n = MATCH_MAXPT;
    out->n = n;
    for (int j = 0; j < n; j++) {
        out->p[j].x = (float)s->med[i][j*2]     + (jitter ? (float)(rnd(jitter*2) - jitter) : 0);
        out->p[j].y = (float)s->med[i][j*2 + 1] + (jitter ? (float)(rnd(jitter*2) - jitter) : 0);
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *content = argc > 1 ? argv[1] : "content.db";
    if (db_open(content, ":memory:") != 0) {
        printf("    FAIL: cannot open %s (%s)\n", content, db_err());
        return 1;
    }

    /* A spread of characters rather than a hand-picked few: simple ones where
     * strokes are easy to confuse, and dense ones where they are not. */
    static const char *WORDS =
        "\344\270\200\344\272\214\344\270\211\345\215\201\344\272\272\345\244\247"
        "\345\260\217\346\260\264\347\201\253\346\234\250\345\245\275\344\275\240"
        "\351\201\223\345\276\267\347\273\217\346\224\253\351\234\262\350\272\201";

    int chars = 0, strokes = 0;
    int self_ok = 0, jitter_ok = 0, back_caught = 0, wrong_caught = 0;
    int back_total = 0, wrong_total = 0;
    int sloppy_ok = 0, sloppy_total = 0;

    int n = 0;
    for (const char *p = WORDS; *p; ) {
        int len = 1;
        unsigned char c = (unsigned char)*p;
        if (c >= 0xF0) len = 4; else if (c >= 0xE0) len = 3; else if (c >= 0xC0) len = 2;
        char ch[8] = {0};
        memcpy(ch, p, (size_t)len);
        p += len;
        n++;

        strokes_t st;
        if (db_strokes(ch, &st) <= 0) continue;
        chars++;

        mstroke_t model[64];
        int nm = 0;
        for (int i = 0; i < st.n && nm < 64; i++)
            if (to_mstroke(&st, i, &model[nm], 0)) nm++;

        for (int i = 0; i < nm; i++) {
            strokes++;

            /* its own median, written exactly */
            matchout_t r = match_stroke(model, nm, i, &model[i], 1.0f);
            if (r.res == MATCH_OK) self_ok++;
            else printf("    %s stroke %d: own median rejected (%d, score %.0f)\n",
                        ch, i + 1, (int)r.res, (double)r.score);

            /* the same, written by a human hand */
            mstroke_t j;
            if (to_mstroke(&st, i, &j, 40)) {
                r = match_stroke(model, nm, i, &j, 1.0f);
                if (r.res == MATCH_OK) jitter_ok++;
            }

            /* A real hand: drawn a little off-centre, a little short, and
             * captured as only a handful of points because the pen moved
             * fast. This is the case that decides whether guided writing is
             * usable or maddening. */
            {
                mstroke_t h;
                mstroke_t src;
                match_resample(&model[i], &src, 5);       /* a fast stroke */
                h.n = src.n;
                float ox = (float)(rnd(120) - 60), oy = (float)(rnd(120) - 60);
                float cx = 0, cy = 0;
                for (int k = 0; k < src.n; k++) { cx += src.p[k].x; cy += src.p[k].y; }
                cx /= (float)src.n; cy /= (float)src.n;
                for (int k = 0; k < src.n; k++) {         /* 85% as long */
                    h.p[k].x = cx + (src.p[k].x - cx) * 0.85f + ox;
                    h.p[k].y = cy + (src.p[k].y - cy) * 0.85f + oy;
                }
                sloppy_total++;
                matchout_t hr = match_stroke(model, nm, i, &h, 1.0f);
                if (hr.res == MATCH_OK) sloppy_ok++;
            }

            /* drawn backwards must not pass as correct */
            mstroke_t rev;
            rev.n = model[i].n;
            for (int k = 0; k < model[i].n; k++) rev.p[k] = model[i].p[model[i].n - 1 - k];
            back_total++;
            r = match_stroke(model, nm, i, &rev, 1.0f);
            if (r.res != MATCH_OK) back_caught++;

            /* a different stroke of the same character must not pass */
            for (int k = 0; k < nm; k++) {
                if (k == i) continue;
                wrong_total++;
                r = match_stroke(model, nm, i, &model[k], 1.0f);
                if (r.res != MATCH_OK) wrong_caught++;
            }
        }
        db_strokes_free(&st);
    }

    /* A dot: a tap, or a dab of the pen. A 点 is written that way as often
     * as not, and a tap -- one point, no length -- was refused every time.
     * Tapped at the middle of each stroke no longer than END_DIST (260, in
     * match.cpp) it must pass; at the middle of any longer stroke, or of
     * another stroke of the character, it must not. Separate characters from
     * the ones above, so their counts stay comparable. */
    static const char *DOTS =
        "\344\270\273\346\226\207\345\205\255\345\277\203\345\206\260"   /* 主文六心冰 */
        "\345\260\217\345\207\211\346\260\270\345\244\252\347\202\271";  /* 小凉永太点 */
    int dot_ok = 0, dot_total = 0, dab_ok = 0, dot_near = 0, long_taken = 0, long_total = 0;
    int dot_wrong = 0, dot_wrong_total = 0;
    for (const char *p = DOTS; *p; ) {
        char ch[8] = {0};
        memcpy(ch, p, 3);                         /* all three bytes */
        p += 3;
        strokes_t st;
        if (db_strokes(ch, &st) <= 0) continue;
        mstroke_t model[64];
        int nm = 0;
        for (int i = 0; i < st.n && nm < 64; i++)
            if (to_mstroke(&st, i, &model[nm], 0)) nm++;
        for (int i = 0; i < nm; i++) {
            mstroke_t mid3;
            match_resample(&model[i], &mid3, 3);
            float len = 0;
            for (int k = 1; k < model[i].n; k++)
                len += hypotf(model[i].p[k].x - model[i].p[k-1].x,
                              model[i].p[k].y - model[i].p[k-1].y);
            mstroke_t tap;
            tap.n = 1;
            tap.p[0] = mid3.p[1];
            matchout_t r = match_stroke(model, nm, i, &tap, 1.0f);
            if (len > 260.0f) {
                long_total++;
                if (r.res == MATCH_OK) {
                    long_taken++;
                    printf("    %s stroke %d (%.0f long) taken by a tap\n", ch, i + 1, (double)len);
                }
                continue;
            }
            dot_total++;
            if (r.res == MATCH_OK) dot_ok++;
            else printf("    %s stroke %d (%.0f long): a tap on it refused (%d)\n",
                        ch, i + 1, (double)len, (int)r.res);
            /* a hurried tap, off the middle by as much as the hand above */
            tap.p[0].x += (float)(rnd(120) - 60);
            tap.p[0].y += (float)(rnd(120) - 60);
            if (match_stroke(model, nm, i, &tap, 1.0f).res == MATCH_OK) dot_near++;
            /* a dab: two points, a few units apart, along the stroke */
            mstroke_t dab;
            dab.n = 2;
            dab.p[0] = mid3.p[1];
            dab.p[1].x = mid3.p[1].x + (mid3.p[2].x - mid3.p[0].x) * 0.1f;
            dab.p[1].y = mid3.p[1].y + (mid3.p[2].y - mid3.p[0].y) * 0.1f;
            if (match_stroke(model, nm, i, &dab, 1.0f).res == MATCH_OK) dab_ok++;
            /* and a tap on each other stroke, while this one is wanted */
            for (int k = 0; k < nm; k++) {
                if (k == i) continue;
                mstroke_t o3, t2;
                match_resample(&model[k], &o3, 3);
                t2.n = 1;
                t2.p[0] = o3.p[1];
                dot_wrong_total++;
                if (match_stroke(model, nm, i, &t2, 1.0f).res != MATCH_OK) dot_wrong++;
            }
        }
        db_strokes_free(&st);
    }

    printf("    %d characters, %d strokes\n", chars, strokes);
    printf("    own median accepted:      %d/%d\n", self_ok, strokes);
    printf("    with +-40 jitter:         %d/%d\n", jitter_ok, strokes);
    printf("    a real hand (short, off-centre, few points): %d/%d\n",
           sloppy_ok, sloppy_total);
    printf("    drawn backwards rejected: %d/%d\n", back_caught, back_total);
    printf("    wrong stroke rejected:    %d/%d\n", wrong_caught, wrong_total);
    printf("    a dot tapped on a dot-length stroke: %d/%d, dabbed: %d/%d,"
           " off-centre: %d/%d\n", dot_ok, dot_total, dab_ok, dot_total, dot_near, dot_total);
    printf("    a tap on another stroke rejected: %d/%d;"
           " a tap taking a longer stroke: %d/%d\n",
           dot_wrong, dot_wrong_total, long_taken, long_total);

    if (self_ok != strokes)                 { printf("    FAIL: a stroke does not match itself\n"); bad = 1; }
    if (jitter_ok * 100 < strokes * 90)     { printf("    FAIL: too fussy about a human hand\n"); bad = 1; }
    if (sloppy_ok * 100 < sloppy_total * 85){ printf("    FAIL: too fussy for a real hand\n"); bad = 1; }
    if (back_caught * 100 < back_total * 85){ printf("    FAIL: accepts strokes drawn backwards\n"); bad = 1; }
    if (wrong_caught * 100 < wrong_total * 95) { printf("    FAIL: accepts the wrong stroke\n"); bad = 1; }
    if (dot_total == 0 || dot_ok != dot_total || dab_ok != dot_total)
                                            { printf("    FAIL: a dot is refused where a dot goes\n"); bad = 1; }
    if (dot_near * 100 < dot_total * 85)    { printf("    FAIL: too fussy about where a dot lands\n"); bad = 1; }
    if (long_taken)                         { printf("    FAIL: a tap passes for a stroke with length\n"); bad = 1; }
    if (dot_wrong * 100 < dot_wrong_total * 95) { printf("    FAIL: a dot passes for another stroke\n"); bad = 1; }

    db_close();
    printf(bad ? "    -> FAILED\n" : "    -> the matcher behaves\n");
    return bad;
}
