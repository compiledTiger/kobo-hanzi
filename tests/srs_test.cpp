/* Scheduler invariants. Intervals must grow on repeated success, a lapse must
 * bring the card back within a day, and what the buttons preview must be what
 * the grade actually does. */
#include "../src/core/srs.h"
#include <stdio.h>

static int bad = 0;
static void chk(int cond, const char *what) {
    if (!cond) { printf("    FAIL: %s\n", what); bad = 1; }
}

int main(void) {
    long day = 20000;

    printf("    repeated 'good': ");
    card_t c; srs_init_card(&c);
    long prev = 0;
    for (int i = 0; i < 8; i++) {
        srs_review(&c, 3, day);
        long iv = c.due - day;
        printf("%ld ", iv);
        chk(iv >= 1, "interval is at least a day");
        chk(iv >= prev, "intervals never shrink on repeated success");
        chk(c.difficulty >= 1.0 && c.difficulty <= 10.0, "difficulty stays in 1..10");
        chk(c.stability > 0, "stability stays positive");
        prev = iv; day = c.due;
    }
    printf("\n");

    srs_review(&c, 1, day);
    chk(c.due - day <= 1, "'again' brings a mature card back within a day");
    chk(c.lapses == 1, "the lapse is counted");

    card_t d; srs_init_card(&d);
    for (int i = 0; i < 5; i++) {
        srs_review(&d, 1, day);
        chk(d.due - day <= 1, "repeated 'again' stays daily");
        chk(d.stability > 0 && d.stability < 1e6, "stability stays sane");
        day = d.due;
    }

    card_t f; srs_init_card(&f);
    srs_review(&f, 3, day);
    int pv = srs_preview(&f, 3, f.due);
    card_t g = f;
    srs_review(&g, 3, f.due);
    chk(pv == (int)(g.due - f.due), "preview equals what the grade does");

    /* Forgetting never makes a card more stable than it was (FSRS-5's cap):
     * a card learnt once and forgotten 200 days later went from 3.2 to 4.6. */
    for (int first = 1; first <= 4; first++) {
        card_t h; srs_init_card(&h);
        srs_review(&h, first, day);
        double before = h.stability;
        srs_review(&h, 1, day + 200);
        chk(h.stability <= before, "a lapse never raises stability");
    }

    printf(bad ? "    -> FAILED\n" : "    -> scheduler invariants hold\n");
    return bad;
}
