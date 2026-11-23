/* srs.h -- FSRS-5 scheduler. Grades: 1 Again, 2 Hard, 3 Good, 4 Easy. */
#ifndef SRS_H
#define SRS_H

typedef struct {
    double stability;
    double difficulty;
    int    reps;
    int    lapses;
    int    state;        /* 0 new, 1 learning, 2 review */
    long   due;          /* unix day number */
    long   last;         /* unix day number of previous review */
} card_t;

void srs_init_card(card_t *c);
/* Apply a grade on day `today`; updates the card in place. */
void srs_review(card_t *c, int grade, long today);
/* Days until the card would next be due, for preview on the buttons. */
int  srs_preview(const card_t *c, int grade, long today);

#endif
