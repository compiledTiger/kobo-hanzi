#include "match.h"

#include <math.h>

namespace {

const int   SAMPLES   = 24;    /* points each stroke is reduced to */
/* Thresholds, in the 0..1024 median box. They come from fitting against the
 * medians themselves: see tests/match_test.cpp, which replays every stroke of
 * a few thousand characters and checks what is accepted and what is not. */
const float END_DIST  = 260.0f;   /* how far the ends may sit from the model */
const float PATH_DIST = 180.0f;   /* mean distance between the two paths     */
const float MIN_COS   = 0.30f;    /* direction agreement                     */
const float LEN_RATIO = 3.0f;     /* longer or shorter than the model by     */

float dist(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

float stroke_len(const mstroke_t *s) {
    float t = 0;
    for (int i = 1; i < s->n; i++)
        t += dist(s->p[i-1].x, s->p[i-1].y, s->p[i].x, s->p[i].y);
    return t;
}

/* Mean distance from each point of a to the nearest point of b, and back.
 * Symmetric, so a short stroke lying along part of a long one is not a match
 * for it -- which matters for the horizontal strokes of 三. */
float path_distance(const mstroke_t *a, const mstroke_t *b) {
    float total = 0;
    for (int i = 0; i < a->n; i++) {
        float best = 1e9f;
        for (int j = 0; j < b->n; j++) {
            float d = dist(a->p[i].x, a->p[i].y, b->p[j].x, b->p[j].y);
            if (d < best) best = d;
        }
        total += best;
    }
    for (int j = 0; j < b->n; j++) {
        float best = 1e9f;
        for (int i = 0; i < a->n; i++) {
            float d = dist(a->p[i].x, a->p[i].y, b->p[j].x, b->p[j].y);
            if (d < best) best = d;
        }
        total += best;
    }
    int n = a->n + b->n;
    return n ? total / (float)n : 1e9f;
}

/* How far `a` lands from the path of `b`: the mean, over a's points, of the
 * distance to b's nearest point. For a dot, which has no ends or length to
 * compare, this is the whole question -- which stroke did it land on. */
float reach(const mstroke_t *a, const mstroke_t *b) {
    float total = 0;
    for (int i = 0; i < a->n; i++) {
        float best = 1e9f;
        for (int j = 0; j < b->n; j++) {
            float d = dist(a->p[i].x, a->p[i].y, b->p[j].x, b->p[j].y);
            if (d < best) best = d;
        }
        total += best;
    }
    return a->n ? total / (float)a->n : 1e9f;
}

/* How well two strokes agree, ignoring direction. Lower is better; a large
 * value means "not this stroke". */
float shape_score(const mstroke_t *u, const mstroke_t *m) {
    float ends = dist(u->p[0].x, u->p[0].y, m->p[0].x, m->p[0].y)
               + dist(u->p[u->n-1].x, u->p[u->n-1].y,
                      m->p[m->n-1].x, m->p[m->n-1].y);
    return path_distance(u, m) + ends * 0.5f;
}

float direction_cos(const mstroke_t *u, const mstroke_t *m) {
    float ux = u->p[u->n-1].x - u->p[0].x, uy = u->p[u->n-1].y - u->p[0].y;
    float mx = m->p[m->n-1].x - m->p[0].x, my = m->p[m->n-1].y - m->p[0].y;
    float un = sqrtf(ux*ux + uy*uy), mn = sqrtf(mx*mx + my*my);
    /* A dot with no length has no direction to disagree about. */
    if (un < 1e-3f || mn < 1e-3f) return 1.0f;
    return (ux*mx + uy*my) / (un * mn);
}

void reverse(const mstroke_t *in, mstroke_t *out) {
    out->n = in->n;
    for (int i = 0; i < in->n; i++) out->p[i] = in->p[in->n - 1 - i];
}

}  // namespace

void match_resample(const mstroke_t *in, mstroke_t *out, int n) {
    if (n < 2) n = 2;
    if (n > MATCH_MAXPT) n = MATCH_MAXPT;
    out->n = n;
    if (in->n <= 0) { out->n = 0; return; }
    if (in->n == 1) {
        for (int i = 0; i < n; i++) out->p[i] = in->p[0];
        return;
    }
    float total = stroke_len(in);
    if (total <= 0) {
        for (int i = 0; i < n; i++) out->p[i] = in->p[0];
        return;
    }
    float step = total / (float)(n - 1);
    out->p[0] = in->p[0];
    int seg = 1;
    float walked = 0, seglen = dist(in->p[0].x, in->p[0].y, in->p[1].x, in->p[1].y);
    for (int i = 1; i < n - 1; i++) {
        float want = step * (float)i;
        while (seg < in->n - 1 && walked + seglen < want) {
            walked += seglen;
            seg++;
            seglen = dist(in->p[seg-1].x, in->p[seg-1].y, in->p[seg].x, in->p[seg].y);
        }
        float t = seglen > 0 ? (want - walked) / seglen : 0;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        out->p[i].x = in->p[seg-1].x + (in->p[seg].x - in->p[seg-1].x) * t;
        out->p[i].y = in->p[seg-1].y + (in->p[seg].y - in->p[seg-1].y) * t;
    }
    out->p[n-1] = in->p[in->n-1];
}

matchout_t match_stroke(const mstroke_t *model, int n_model, int want,
                        const mstroke_t *user, float leniency) {
    matchout_t out = { MATCH_NO, -1, 1e9f };
    if (!model || !user || n_model <= 0 || want < 0 || want >= n_model) return out;
    if (user->n < 1) return out;        /* one point is a tap: a dot, below */
    if (leniency <= 0) leniency = 1.0f;

    mstroke_t u, m;
    match_resample(user, &u, SAMPLES);
    match_resample(&model[want], &m, SAMPLES);

    float score = shape_score(&u, &m);
    out.score = score;

    float mlen = stroke_len(&m), ulen = stroke_len(&u);
    int len_ok = mlen > 0 && ulen > 0
              && ulen < mlen * LEN_RATIO * leniency
              && ulen > mlen / (LEN_RATIO * leniency);

    float ends = dist(u.p[0].x, u.p[0].y, m.p[0].x, m.p[0].y)
               + dist(u.p[u.n-1].x, u.p[u.n-1].y, m.p[m.n-1].x, m.p[m.n-1].y);

    int shape_ok = path_distance(&u, &m) < PATH_DIST * leniency
                && ends < END_DIST * 2.0f * leniency;

    /* A dot: a tap, or a dab too short for its length to be judged. A 点 is
     * written that way as often as not, and was refused every time -- for
     * having no length, and a tap for being one point. Where the stroke
     * wanted is itself no longer than its ends may be off by (END_DIST),
     * a point lies within the matcher's tolerance of all of it, so a dot is
     * judged by where it is: the shape test, and no better fit elsewhere.
     * It has no direction to be backwards in. */
    int dot = ulen < mlen / (LEN_RATIO * leniency) && mlen <= END_DIST * leniency;

    if (shape_ok && (len_ok || dot)) {
        if (dot || direction_cos(&u, &m) >= MIN_COS) {
            /* Is some other stroke a clearly better fit? Accepting a stroke
             * that belongs elsewhere teaches the wrong order. A dot is
             * measured by the path it landed on, not by its ends: a tap on
             * the middle of 文's 横 is nearer the 点 above it, end for end,
             * than either end of the 横. */
            float mine = dot ? reach(&u, &m) : score;
            for (int i = 0; i < n_model; i++) {
                if (i == want) continue;
                mstroke_t o;
                match_resample(&model[i], &o, SAMPLES);
                if ((dot ? reach(&u, &o) : shape_score(&u, &o)) < mine * 0.7f) {
                    out.res = MATCH_ELSEWHERE;
                    out.which = i;
                    return out;
                }
            }
            out.res = MATCH_OK;
            return out;
        }
        /* Right shape, wrong way round. Worth saying so: it is a common
         * mistake and a different one from drawing the wrong stroke. */
        mstroke_t r;
        reverse(&u, &r);
        if (direction_cos(&r, &m) >= MIN_COS) {
            out.res = MATCH_BACKWARDS;
            return out;
        }
    }

    /* Not this stroke. Name the one it does look like, if any, so the caller
     * can say "that is stroke 5" rather than only "no". */
    float best = score * 0.7f;
    for (int i = 0; i < n_model; i++) {
        if (i == want) continue;
        mstroke_t o;
        match_resample(&model[i], &o, SAMPLES);
        float sc = shape_score(&u, &o);
        if (sc < best) { best = sc; out.which = i; }
    }
    out.res = out.which >= 0 ? MATCH_ELSEWHERE : MATCH_NO;
    return out;
}
