#include "srs.h"
#include <math.h>

/* Published FSRS-5 default parameters. */
static const double W[19] = {
    0.40255, 1.18385, 3.173,   15.69105, 7.1949,  0.5345,  1.4604,
    0.0046,  1.54575, 0.1192,  1.01925,  1.9395,  0.11,    0.29605,
    2.2698,  0.2315,  2.9898,  0.51655,  0.6621
};

#define DECAY  (-0.5)
#define FACTOR (19.0 / 81.0)
#define RETENTION 0.90
#define S_MIN  0.01
#define S_MAX  36500.0

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static double init_difficulty(int g) {
    return clampd(W[4] - exp(W[5] * (g - 1)) + 1.0, 1.0, 10.0);
}

static double retrievability(double elapsed, double stability) {
    if (stability <= 0) return 0.0;
    return pow(1.0 + FACTOR * elapsed / stability, DECAY);
}

static double next_interval(double stability) {
    double iv = (stability / FACTOR) * (pow(RETENTION, 1.0 / DECAY) - 1.0);
    if (iv < 1.0) iv = 1.0;
    if (iv > S_MAX) iv = S_MAX;
    return iv;
}

/* FSRS-5:  delta = -w6*(G-3);  D' = D + delta*(10-D)/9;  then mean reversion.
 * The (10-D)/9 term damps the change as difficulty approaches its ceiling.
 * Without it a "hard" on a card at D=9 moves nine times as far as the fitted
 * weights assume, and intervals drift away from the 90% retention target. */
static double next_difficulty(double d, int g) {
    double delta = -W[6] * (double)(g - 3);
    double dd    = d + delta * (10.0 - d) / 9.0;
    dd = W[7] * init_difficulty(4) + (1.0 - W[7]) * dd;   /* mean reversion */
    return clampd(dd, 1.0, 10.0);
}

static double stability_recall(double d, double s, double r, int g) {
    double hard  = (g == 2) ? W[15] : 1.0;
    double easy  = (g == 4) ? W[16] : 1.0;
    double inc = exp(W[8]) * (11.0 - d) * pow(s, -W[9])
               * (exp(W[10] * (1.0 - r)) - 1.0) * hard * easy;
    return clampd(s * (1.0 + inc), S_MIN, S_MAX);
}

static double stability_forget(double d, double s, double r) {
    double sf = W[11] * pow(d, -W[12]) * (pow(s + 1.0, W[13]) - 1.0)
              * exp(W[14] * (1.0 - r));
    /* FSRS-5 caps it: forgetting never leaves a card more stable than it
     * was. Without the cap an "again" 200 days on took S from 3.2 to 4.6. */
    double cap = s / exp(W[17] * W[18]);
    if (sf > cap) sf = cap;
    return clampd(sf, S_MIN, S_MAX);
}

void srs_init_card(card_t *c) {
    c->stability = 0.0; c->difficulty = 0.0;
    c->reps = 0; c->lapses = 0; c->state = 0;
    c->due = 0; c->last = 0;
}

static void apply(card_t *c, int grade, long today, int commit) {
    double s, d;
    if (c->state == 0) {
        s = clampd(W[grade - 1], S_MIN, S_MAX);
        d = init_difficulty(grade);
    } else {
        double elapsed = (double)(today - c->last);
        if (elapsed < 0) elapsed = 0;
        double r = retrievability(elapsed, c->stability);
        d = next_difficulty(c->difficulty, grade);
        if (grade == 1) s = stability_forget(c->difficulty, c->stability, r);
        else            s = stability_recall(c->difficulty, c->stability, r, grade);
    }
    long iv = (long)(next_interval(s) + 0.5);
    if (grade == 1 && iv > 1) iv = 1;
    if (commit) {
        c->stability = s;
        c->difficulty = d;
        c->reps++;
        if (grade == 1 && c->state == 2) c->lapses++;
        c->state = 2;
        c->last = today;
        c->due = today + iv;
    } else {
        c->due = today + iv;    /* caller only reads due on a scratch copy */
    }
}

void srs_review(card_t *c, int grade, long today) { apply(c, grade, today, 1); }

int srs_preview(const card_t *c, int grade, long today) {
    card_t tmp = *c;
    apply(&tmp, grade, today, 0);
    int d = (int)(tmp.due - today);
    return d < 1 ? 1 : d;
}
