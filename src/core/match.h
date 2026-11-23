/* match -- does this stroke belong here?
 *
 * Guided writing: the app knows which character is wanted, so it can say
 * whether each stroke, as it is drawn, is the stroke that comes next.
 *
 * The approach is HanziWriter's (MIT, https://hanziwriter.org), reimplemented
 * here against the stroke medians this app already carries from Make Me a
 * Hanzi. A stroke is accepted when all of these hold against the model:
 *
 *   - it starts near where the model starts, and ends near where it ends
 *   - it runs in the same direction -- backwards is a different stroke in
 *     Chinese, and accepting it would teach the wrong thing
 *   - its path stays near the model's path along its whole length
 *   - it is not a markedly better fit for some other stroke of the character
 *
 * Everything is in the 0..1024 box the medians use, so the caller normalises
 * screen coordinates once and thresholds mean the same thing everywhere.
 */
#ifndef MATCH_H
#define MATCH_H

#define MATCH_MAXPT 256

typedef struct {
    int n;
    struct { float x, y; } p[MATCH_MAXPT];
} mstroke_t;

typedef enum {
    MATCH_OK,          /* this is the stroke that was wanted          */
    MATCH_BACKWARDS,   /* the right shape, drawn the wrong way round  */
    MATCH_ELSEWHERE,   /* a stroke of this character, but not this one */
    MATCH_NO           /* not a stroke of this character              */
} matchres_t;

typedef struct {
    matchres_t res;
    int   which;       /* MATCH_ELSEWHERE: the stroke it looked like */
    float score;       /* 0 is perfect; smaller is closer            */
} matchout_t;

/* `model` is the character's medians, `n_model` of them, in the 0..1024 box.
 * `want` is the stroke expected next. `user` is what was drawn, in the same
 * box. `leniency` scales every threshold: 1.0 is the default, higher is
 * more forgiving. */
matchout_t match_stroke(const mstroke_t *model, int n_model, int want,
                        const mstroke_t *user, float leniency);

/* Resample `in` to `n` evenly spaced points along its length. Exposed because
 * the tests need it and because a caller may want to thin a raw stroke. */
void match_resample(const mstroke_t *in, mstroke_t *out, int n);

#endif
