/* db.h -- content.db (read-only, rebuildable) + user.db (progress, yours).
 *
 * Card lifecycle:
 *   no srs row                  never seen; eligible as NEW
 *   state=0, due >  today       postponed; not offered until due
 *   state=0, due <= today       back in the new pool
 *   state>0, due <= today       due for review
 *   state>0, due >  today       learnt, not due
 *
 * New cards are only introduced from decks you have ticked. With nothing
 * ticked you get reviews only -- entering a deck explicitly overrides that.
 */
#ifndef DB_H
#define DB_H
#include <stdint.h>
#include <string>
#include <vector>
#include "srs.h"

#define MODE_WRITE 0
#define MODE_READ  1
#define MAXSTROKE  64
#define POSTPONE_DAYS 3
#define USER_LIST_BASE 100000

typedef struct {
    int id; char book[160]; char chapter[160];
    int n;
    int enabled[2];          /* per mode: MODE_WRITE, MODE_READ */
    int due, fresh;          /* filled by db_list_counts, not by db_lists */
} list_t;
/* enabled[]: 0 none of its chapters, 1 all of them, 2 some */
typedef struct { char name[160]; int n_lists; int enabled[2]; } book_t;
typedef struct {
    char key[96], headword[96], pinyin[160], defn[512];
    int  writable, is_new;
    card_t c;
} cardrow_t;
/* simp is as long as a card's key: a headword of your own is shown and
 * removed by it (db_user_entries, db_user_entry_delete) */
typedef struct { char simp[96], trad[64], pinyin[160], defn[512]; } dictrow_t;
typedef struct {
    int n;
    int16_t *out[MAXSTROKE]; int nout[MAXSTROKE];
    int16_t *med[MAXSTROKE]; int nmed[MAXSTROKE];
} strokes_t;
typedef struct { char pinyin[160], defn[512], radical[16], decomp[64]; int n_stroke; } charinfo_t;

int  db_open(const char *content_path, const char *user_path);
void db_close(void);
const char *db_err(void);
/* The content.db the app was opened on, as given (the desktop export runs
 * tools/export_book.py against it). */
const char *db_content_path(void);

int  db_books(book_t *out, int max);
int  db_lists(const char *book, list_t *out, int max);

/* due = reviews waiting; fresh = new cards available (respecting the ticks
 * when list_id is 0). Either pointer may be NULL. */
void db_counts(int list_id, const char *book, int mode, long today,
               int *due, int *fresh);
/* Fill due/fresh for a whole page of rows in one query. Drawing a chapter
 * list used to cost one query per visible row, every redraw. */
void db_list_counts(list_t *rows, int n, int mode, long today);

int  db_next_card(int list_id, const char *book, int mode, long today,
                  cardrow_t *out);

/* ---- tests -------------------------------------------------------------
 * A test is a fixed batch: 10 cards to write, 30 to read. Anything you get
 * wrong comes back at the end, and keeps coming back until you get it right.
 *
 * Every answer is written to disk as it happens -- both the card's schedule
 * and its place in the test -- so quitting mid-test loses nothing and the
 * test resumes exactly where it stopped.
 *
 * The card's schedule is updated on the FIRST answer only. Later repeats
 * within the same test are there to make you produce it correctly, not to
 * punish the card's stability a second time. */
#define TEST_WRITE 10
#define TEST_READ  30

typedef struct {
    int  mode, list_id;
    int  scored;     /* 0 = reviewing ahead: answers do not touch the schedule */
    char book[160], label[200];
    int  total;      /* cards in the test */
    int  correct;    /* answered right */
    int  pending;    /* never answered */
    int  repeat;     /* answered wrong, waiting to come round again */
    int  later;      /* put off: out of the test, neither right nor wrong */
    int  wrong;      /* answered wrong at least once, redone or not */
} session_t;

/* ---- daily new-card allowance ------------------------------------------
 * Reviews are never capped -- what is due is due. New cards are, because
 * introducing them without limit is how a backlog becomes unmanageable.
 * Counted per mode, per day, from the day a card was first scheduled. */
/* Pen only: on a Kobo with a stylus, a finger never writes -- it only
 * presses buttons. Toggled from the writing screens. 1 by default. */
int  db_pen_only(void);
void db_set_pen_only(int on);
/* Home's categories in the user's order: a category's place, by its name
 * (-1 none set: the app's own order), and setting it. */
int  db_home_cat_pos(const char *name);
void db_set_home_cat_pos(const char *name, int pos);

/* Guided writing: check each stroke as it is drawn. Off by default -- the
 * free hand is the older behaviour and the one not everybody wants replaced. */
int  db_guided(void);
void db_set_guided(int on);

/* Whether a text shows its punctuation. Off by default. */
int  db_show_punct(void);
void db_set_show_punct(int on);
/* Where a text's pinyin comes from: 1 the book's own (translator B, the pinyin
 * edition of JTTW) where it has it, 0 the dictionary's first reading. */
int  db_text_readings(void);
/* a comic volume's exact place, packed by the app; -1 when none is saved */
int  db_comic_place(int chapter_id);
void db_set_comic_place(int chapter_id, int v);
void db_set_text_readings(int on);

/* How large a text is set, as a percentage. 100 is the size the sizes in
 * the code are written at; the steps are TEXT_STEPS. */
#define TEXT_SCALE_MIN 85
#define TEXT_SCALE_MAX 145
int  db_text_scale(void);
void db_set_text_scale(int pct);

/* Reading light level to restore on waking. -1 means never set one. */
int  db_light(void);
void db_set_light(int level);

/* The desktop's window mode (surf.h's WIN_*), as chosen in settings; -1
 * when it never was. */
int  db_window_mode(void);
void db_set_window_mode(int m);

/* ---- PDFs ----------------------------------------------------------------
 * Pinned PDFs, by absolute path, the last read first. Unpinning forgets the
 * reading spot as well. `now` is any increasing count (seconds). */
std::vector<std::string> db_pdf_pins(void);
int  db_pdf_pinned(const char *path);
void db_pdf_pin(const char *path, int on, long now);
void db_pdf_used(const char *path, long now);       /* a pin read just now */
/* Where a pinned PDF was left: the page (0-based) and the view, as the
 * reader packs it (zoom mode, its part, the margin trim, the rotation in
 * degrees, the Kobo's flip). db_pdf_spot: 1 if one is saved.
 * db_pdf_set_spot keeps it for a pinned PDF only (0 otherwise), and writes
 * nothing when it has not changed. */
typedef struct { int page, zoom, part, trim, rot, flip; } pdf_spot_t;
int  db_pdf_spot(const char *path, pdf_spot_t *out);
int  db_pdf_set_spot(const char *path, const pdf_spot_t *s);

#define NEW_PER_DAY_WRITE_DEFAULT 5
#define NEW_PER_DAY_READ_DEFAULT  15


/* Forget review history. book NULL means everything; otherwise just that
 * book. mode -1 means both. Returns how many cards were reset. */
int  db_reset_progress(const char *book, int mode);
int  db_reset_count(const char *book, int mode);

int  db_new_limit(int mode);
void db_set_new_limit(int mode, int n);
int  db_new_today(int mode, long today);
static inline int db_new_allowance(int mode, long today) {
    int left = db_new_limit(mode) - db_new_today(mode, today);
    return left < 0 ? 0 : left;
}

/* 1 if an unfinished test exists. */
int  db_session_load(session_t *out);
/* scored = 1 for a normal test. scored = 0 builds a test from cards that are
 * learnt but not yet due, for when there is nothing left to do and you want
 * the practice anyway -- answers are not written to the schedule, so getting
 * one wrong early cannot damage an interval you have earned. */
/* `ignore_limit` builds the test from new cards without regard to the daily
 * allowance, for a day that is going well and has run out of them. */
void db_session_start(int mode, int list_id, const char *book,
                      const char *label, long today, int size, int scored,
                      int ignore_limit);
/* How many cards could be reviewed ahead of schedule right now. */
int  db_ahead_available(int list_id, const char *book, int mode, long today);
/* How many cards in scope have never been seen, allowance aside. */
int  db_new_available(int list_id, const char *book, int mode, long today);

/* Where one list stands, per mode. `writable` counts only the entries that
 * can be written at all, which is what the 写 column is measured against. */
typedef struct {
    int total, writable;
    int unseen[2], learning[2], known[2], due[2];
} liststats_t;
void db_list_stats(int list_id, long today, liststats_t *out);
/* The same, over every list of one book. */
void db_book_stats(const char *book, long today, liststats_t *out);
/* Read one card's stored state back, for verification. */
int  db_card_by_key(const char *key, int mode, cardrow_t *out);
/* Next card to show; 0 when the test is finished. */
int  db_session_next(cardrow_t *out);
/* correct = graded anything but "again". first_answer tells the caller
 * whether to apply the card's schedule. */
void db_session_mark(const char *key, int correct);
/* "later" in a test: out of it, counted as neither right nor wrong. It was
 * marked right, and a test put off whole said "all correct first time". */
void db_session_put_off(const char *key);
int  db_session_is_first(const char *key);
void db_session_clear(void);
void db_save_card(const cardrow_t *r, int mode);
void db_postpone(const char *key, int mode, long today, int days);

/* Deck ticks, keyed on book+chapter+mode so they survive a content rebuild.
 * Writing and reading are chosen separately: the lists worth learning to write
 * are usually not the lists worth learning to read. */
int  db_deck_enabled(const char *book, const char *chapter, int mode);
void db_deck_set(const char *book, const char *chapter, int mode, int on);
void db_apply_deck_renames(void);   /* ticks follow a retitled chapter (build_db deck_renames) */
void db_book_set(const char *book, int mode, int on);
void db_begin(void);       /* nestable: only the outermost commits */
void db_commit(void);
int  db_book_enabled(const char *book, int mode);   /* 0 none, 1 all, 2 some */

int  db_strokes(const char *ch_utf8, strokes_t *s);
void db_strokes_free(strokes_t *s);
int  db_charinfo(const char *ch_utf8, charinfo_t *ci);
int  db_dict(const char *q, dictrow_t *out, int max);

/* ---- handwriting search -------------------------------------------------
 * A character is reduced to 6 bytes per stroke: the start, middle and end of
 * each stroke, normalised into a 0..255 box with aspect preserved. Matching
 * sums absolute differences, filtered first by stroke count, so it expects
 * roughly correct stroke order -- which is what this app is teaching anyway. */
#define HW_MAX_STROKES 32
typedef struct { char ch[8]; int score; } hwcand_t;
/* sig is 6*n bytes. Returns candidates, best first. */
int  db_hw_search(const unsigned char *sig, int n, hwcand_t *out, int max);
/* The stored signature for a character, for testing and for "draw this". */
int  db_hw_signature_of(const char *ch, unsigned char *sig, int max_strokes);

/* your own lists */
int  db_user_lists(list_t *out, int max);
/* Just the totals. The menu wants "3 lists, 214 words" and was reading five
 * hundred rows into 174KB of stack to add them up. */
void db_user_list_totals(int *n_lists, int *n_words);
int  db_user_list_create(const char *name);
/* 0 added; -1 already there, or a headword of 96 bytes or more (32 hanzi):
 * longer than a card's key holds, so it could never be studied or removed */
int  db_user_add(int list_id, const char *hw, const char *py, const char *df);
int  db_word_lookup(const char *hw, char *py, int pyn, char *df, int dfn);

typedef struct { char book[160], text[256]; } example_t;
/* Sentences from the five source books that use this word. */
int  db_examples(const char *key, example_t *out, int max);

/* what you have actually studied */
typedef struct {
    char headword[96], pinyin[160];
    int  mode, reps, lapses, state;
    long due, last;
    double stability;
} revrow_t;
/* mode -1 = both. Ordered most recently reviewed first. */
int  db_reviewed(int mode, int offset, int limit, revrow_t *out);
int  db_reviewed_count(int mode);

/* Drop Pleco .txt exports in a folder and import them all. Same 3-column
 * format the build pipeline reads; //Book/Chapter headers become list names.
 * Returns entries imported, or -1 if the folder cannot be read. */
int  db_import_pleco_dir(const char *dir, int *n_files, int *n_lists);

/* Write every one of your lists back out as Pleco-format .txt, one file per
 * list, into `dir`. Returns entries written, or -1 if `dir` is not writable. */
int  db_export_pleco(const char *dir, int *n_files);

int  db_user_list_rename(int list_id, const char *name);
int  db_user_list_delete(int list_id);
/* 0 removed, -1 nothing was */
int  db_user_entry_delete(int list_id, const char *headword);
/* entries of one of your lists, for browsing and pruning */
int  db_user_entries(int list_id, int offset, int limit, dictrow_t *out);
/* Words in one content list, with their current state for each mode.
 * state: 0 unseen, 1 learning/known, 2 due. */
typedef struct { char headword[96], pinyin[160], defn[512]; int st[2]; long due[2]; } wordrow_t;
int  db_list_words(int list_id, int offset, int limit, long today, wordrow_t *out);
int  db_list_word_count(int list_id);
int  db_user_entry_count(int list_id);

/* ---- texts: books read and written line by line ------------------------ *
 * A text is a book whose chapters hold lines rather than words: classical
 * verse, read a clause at a time and written out the same way. */
typedef struct { int id; char title[120], author[80]; int n_chap, n_line; } textbook_t;
typedef struct { int id; char title[160]; int n_line; } textchap_t;

int  db_texts(textbook_t *out, int max);
int  db_text_chapters(int text_id, textchap_t *out, int max);
/* One chapter by id. The home screen wants the title of the chapter you
 * were on, and loading a hundred of them to find it is a hundred rows and
 * 21KB of stack on every redraw. 0 if there is no such chapter. */
int  db_text_chapter(int chapter_id, textchap_t *out);

/* A translator's notes on one sentence of a text, passage by passage --
 * English, the Chinese of the passage, pinyin, a word-by-word gloss and a
 * commentary, any of which may be empty. Sized for Legge, whose
 * passages and notes run long. */
typedef struct {
    char english[6144];
    char zh[1024];
    char pinyin[1024];
    char gloss[8192];      /* a word by word, a line per word */
    char notes[16384];
} textnote_t;
/* The passages for one sentence, in order; 0 when there are none. These
 * read the source set by db_set_note_source(), all its rows. */
int  db_text_notes(int chapter_id, int line, textnote_t *out, int max);
int  db_text_has_notes(int chapter_id, int line);
/* The same of a source named: which of its rows -- all of them, those with
 * English (a translation of the line), or those with notes (commentary,
 * word by word: a row's notes, gloss or app_only). The English and the
 * notes pages each read their own source, so a book can be read in one
 * translator's English with translator B's notes. */
enum { NOTES_ALL = 0, NOTES_EN = 1, NOTES_NOTED = 2 };
int  db_text_notes_of(int chapter_id, int line, const char *src, int what, textnote_t *out, int max);
int  db_text_has_of(int chapter_id, int line, const char *src, int what);
/* Every row of a chapter with notes in a source (as NOTES_NOTED), in line
 * order, in one query: its line, its word by word (gloss) and whether it
 * has more than that (notes, app_only). The reading page's "notes" marks
 * are made from these. Returns the rows given to fn. */
typedef void (*noted_row_fn)(int line, const char *gloss, int more, void *ctx);
int  db_chapter_noted(int chapter_id, const char *src, noted_row_fn fn, void *ctx);
/* A line a translator translates inside his sentence on another line (a
 * row of the source with nothing in it but src_ord, that line:
 * the build; the classics' own English, source '', has them too,
 * classics_split.py): that line, or -1 -- for a line he leaves out, a line
 * with English of its own, and any other row (translator B's, source '' too, whose
 * src_ord is his passage). */
int  db_text_en_cover(int chapter_id, int line, const char *src);
/* Whether a book's source has such rows on any line: a source "gives
 * English" where it has one row with English, whatever its pages are called
 * (translator C's 注's are "translations of the commentaries", and it has none). */
int  db_source_gives(int text_id, const char *src, int what);
/* Whether it has a page of any chapter (text_chapter_notes). */
int  db_source_has_chapter_pages(int text_id, const char *src);
/* The last chapter (its ord, from 0) with a row or a page of the source, or
 * -1: where a translation of part of a book ends (Joly's 红楼梦 at 56). */
int  db_source_last_chapter(int text_id, const char *src);
/* A 连环画 plate: its picture's path in the series' EPUB, and its caption's
 * lines in the chapter. A chapter with none is an ordinary text. */
typedef struct { int line_first, n_lines; char image[96]; } plate_t;
int  db_text_plates(int chapter_id, plate_t *out, int max);
/* The EPUB a text's pictures are in, by file name; "" if it has none. */
void db_text_epub(int text_id, char *out, int n);
/* Whether its pictures and their text go on pages of their own (the
 * Qing-painting 西游记, whose text runs to 300 characters a painting): the
 * picture alone, then its text read as a chapter page is. A plate there
 * may have no picture (image ""): a 前言, text alone. */
int  db_text_epub_apart(int text_id);
/* Whether a line's notes include English: the novels' carry only word notes. */
int  db_text_has_english(int chapter_id, int line);
/* A note on the whole chapter, where there is one, and what it is: the
 * chapter in English (translator B's 道德经; `label` empty), or a translator's
 * introduction to it ("Legge's introduction", "about this year"). Empty
 * strings when there is none. */
void db_text_chapter_note(int chapter_id, char *out, int n, char *label, int ln);
void db_text_chapter_note_of(int chapter_id, const char *src, char *out, int n, char *label, int ln);
/* How each hanzi of a line is read in that line, where the build knew
 *: one syllable per hanzi, space-separated, '-' where
 * unknown. Empty string when the line has none. */
void db_text_pinyin(int chapter_id, int line, char *out, int n);
/* Every dictionary entry for a word -- a character with two readings is two
 * entries, 徼 jiǎo and 徼 jiào -- in the dictionary's own order. */
int  db_word_entries(const char *w, dictrow_t *out, int max);
/* The word as the book's own decks give it: the list of `chapter` first
 * (with its 少见 / 生造 / 人名 lists), else the book's earliest list that has it.
 * 1 and *out filled, or 0. */
int  db_book_entry(const char *book, const char *chapter, const char *w, dictrow_t *out);
/* Between the two editions of a text, by the translator's passages: the
 * first passage placed on `line`, or -1; and the first line carrying passage
 * `src` or the nearest one before it, or -1. */
int  db_note_src(int chapter_id, int line);
/* A book's sources of notes, in the order the switch shows them (the first
 * is what it opens with); 0 when it has only its own. */
typedef struct { char key[16]; char label[24]; } notesrc_t;
int  db_note_sources(int text_id, notesrc_t *out, int max);
/* Which source the notes queries read: a key from db_note_sources, or "". */
void db_set_note_source(const char *src);
/* The reader's choice for a book, as an index into its sources `srcs` (0 if
 * none), remembered by the source's key (a source put in between does not
 * move it); and remembering it. The notes' and the English's apart
 * (note_src:<id>, en_src:<id>). */
int  db_note_source_choice(int text_id, const notesrc_t *srcs, int n);
int  db_en_source_choice(int text_id, const notesrc_t *srcs, int n);
void db_set_en_source_choice(int text_id, const char *key);
/* Whether any source of the text has a chapter note and sentence notes in
 * one chapter: the notes page's "chapter" toggle can appear in it. */
int  db_text_has_chapter_toggle(int text_id);
/* For the harness: a setting put (v < 0 deletes it) and read (-1 if none). */
void db_setting_put(const char *k, int v);
int  db_setting_peek(const char *k);
/* Your own handwritten page on a sentence, one per line, in user.db: packed
 * ink (app.cpp, lnote) and the size of the pad it was written on, so it can
 * be scaled to another screen. get returns the bytes, 0 when there is none;
 * put with len 0 deletes it. */
int  db_line_ink_get(int chapter_id, int line, void *buf, int cap, int *w, int *h);
/* `text` is the sentence it is on (the page is shown only while the chapter
 * still has it there); returns 0 if the write failed */
int  db_line_ink_put(int chapter_id, int line, const char *text, const void *buf, int len,
                     int w, int h);
/* which lines of a chapter have a page: has[line] = 1; returns how many */
int  db_line_ink_lines(int chapter_id, unsigned char *has, int n);
/* the reading page's marks for them, on or off */
/* the comic reader: picture size (0 large, 1 medium, 2 small) and whether a
 * caption may break across pages, the part that fits staying under the picture */
int  db_comic_size(void);
void db_set_comic_size(int v);
int  db_comic_split(void);
void db_set_comic_split(int on);
int  db_show_my_notes(void);
void db_set_show_my_notes(int on);
/* and the marks of the book's own notes on it ("notes"), on or off */
int  db_show_book_notes(void);
void db_set_show_book_notes(int on);
void db_set_note_source_choice(int text_id, const char *key);
int  db_note_line_for_src(int chapter_id, int src);
/* Lines of a chapter, in order. `out` is an array of `max` fixed buffers.
 * A sentence in these books reaches 822 bytes, so the buffer is not 256. */
#define TEXT_LINE_MAX 1024
int  db_text_lines(int chapter_id, char (*out)[TEXT_LINE_MAX], int max);
/* How far through a chapter you had written it out, and remembering it. */
int  db_text_pos(int chapter_id);
void db_set_text_pos(int chapter_id, int line);
/* Where you were reading, which is a different place: the line in a chapter
 * and, per book, the chapter to come back to. 0 means never opened. */
int  db_text_read(int chapter_id);
void db_set_text_read(int chapter_id, int line);
/* Both of them, but -1 where nothing is saved. 0 is line 0 as well as
 * never, and line 0 is all of a one-line chapter, which could never show
 * as read or written. */
int  db_text_read_saved(int chapter_id);
int  db_text_pos_saved(int chapter_id);
int  db_text_last_chapter(int text_id);
void db_set_text_last_chapter(int text_id, int chapter_id);
/* And which book was the last one open at all, for the home screen. */
int  db_last_text(void);
void db_set_last_text(int text_id);

/* Kobo's own saved words: .kobo/KoboReader.sqlite WordList + Bookmark.
 * Returns the number of entries imported, or -1 if the file is unreadable. */
int  db_import_kobo(const char *kobo_sqlite_path, int *n_words, int *n_highlights);

#endif
