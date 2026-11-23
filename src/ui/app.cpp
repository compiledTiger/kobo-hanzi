/* app.c -- screens and the review loop. */
#include "ui.h"
#include "../backend/surf.h"
#include "../core/db.h"
#include "../core/match.h"
#include "../core/srs.h"
#include "../core/pic.h"
#include "../core/light.h"
#include "../core/pdf.h"
#include "../core/ocr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

enum { SCR_HOME, SCR_LISTS, SCR_REVIEW, SCR_DICT, SCR_CHAR, SCR_PICK, SCR_NEWLIST,
       SCR_MENU, SCR_LEARNED, SCR_MYLISTS, SCR_LISTVIEW, SCR_DONE, SCR_SETTINGS, SCR_RESET, SCR_WORDS, SCR_DRAW,
       SCR_STUDY, SCR_STATS, SCR_STROKES,
       SCR_TEXTCH, SCR_TEXTREAD, SCR_TEXTLINE, SCR_TEXTWRITE, SCR_BOOK,
       SCR_TEXTDONE, SCR_TEXTNOTES, SCR_MYNOTE, SCR_EXPORT,
       SCR_PDFS, SCR_PDFVIEW, SCR_CATORDER };

enum {
    H_NONE = 0,
    H_WRITE, H_READ, H_DICT, H_BACK, H_QUIT,
    H_CHECK, H_CLEAR, H_AGAIN, H_HARD, H_GOOD,
    H_PRACTICE, H_POSTPONE, H_ADD, H_NEWLIST, H_KOBO,
    H_MORE, H_PREV,
    H_MENU, H_LEARNED, H_PLECO, H_FILTER,
    H_MYLISTS, H_EXPORT, H_RENAME, H_DELETE, H_RESUME, H_AGAIN_TEST,
    H_SETTINGS, H_WMINUS, H_WPLUS, H_RMINUS, H_RPLUS, H_AHEAD, H_MORENEW, H_PENONLY,
    H_DRAW, H_UNDO, H_STUDY, H_STUDY_NEXT, H_STUDY_PREV, H_SCRATCH, H_STATS,
    H_TREAD, H_TCONTINUE, H_TWRITE, H_TNEXT, H_TPREV, H_TREVEAL, H_TPY, H_DECKS,
    H_TAGAIN,
    H_TNEXTLINE, H_TDONE, H_PUNCT, H_GUIDED, H_TNOTES, H_TEN, H_VERDICTCH, H_READINGS, H_CSIZE0, H_CSIZE1, H_CSIZE2, H_CSPLIT,
    H_TEDITION, H_EDSWITCH, H_MOREREAD, H_TCHAPEN, H_TPAGES, H_TPIC, H_TPDF, H_TPGPREV, H_TPGNEXT,
    H_TCHAP,                        /* the notes page: the chapter's, or the sentence's */
    H_ENMORE,                       /* the English under a sentence: the rest of it, on its page */
    H_RESET, H_RESET_ALL, H_LMINUS, H_LPLUS, H_TXMINUS, H_TXPLUS,
    H_MYNOTES, H_MYNOTESHOW,
    H_TSENT,                        /* the chapter page: the next line pressed opens as a sentence */
    H_BNOTESHOW,                    /* the chapter page: marks where the book has notes */
    H_WNOTE, H_WDICT,               /* the word page: its book note, or the dictionary */
    H_NOTESLINE,                    /* the notes page opened from the chapter: its sentence */
    H_HDRSET,                      /* the header's settings, left of exit */
    H_EXPORTBOOK,                  /* the chapter list: export the book (desktop) */
    H_EX_EPUB, H_EX_PDF, H_EX_M0, H_EX_M1, H_EX_M2, H_EX_BOOK, H_EX_CHAP, H_EX_GO,   /* the export screen */
    H_PDFS,                         /* the menu: the PDF screen */
    H_PDFFIELD, H_PDFDRAW, H_PDFRESCAN,   /* the PDF screen: the search, hanzi drawn, scan again */
    /* the PDF page: its zoom, rotation, the Kobo's flip, and what a press
     * on the page does (H_PV_PAGE) */
    H_PV_FIT, H_PV_QUAD, H_PV_HALF, H_PV_FLIP, H_PV_TRIMIN, H_PV_TRIMOUT, H_PV_ROT,
    H_PV_OCR, H_PV_DRAWON, H_PV_PAGE,
    H_PV_RCLOSE, H_PV_LOOKUP,       /* the PDF page: put the character read away; look up the word drawn */
    H_WIN0, H_WIN1, H_WIN2, H_WIN3,       /* settings: the desktop's window, WIN_* in order */
    H_CATORDER,                     /* the menu: the order of home's categories */
    /* Ranges, spaced wider than MAXLISTS: at 500 apart a row index of 500 or
     * more aliased onto the other mode's range. */
    H_ITEM = 10000, H_TICK = 20000, H_TICK_R = 22000,
    H_GO_W = 30000, H_GO_R = 32000, H_KEY = 40000,
    /* A base for a range, so it needs a range of its own: as a singleton in
     * the list above, H_CAND + 3 collided with H_LOCK and tapping the fourth
     * handwriting candidate locked the screen. */
    H_CAND = 50000,
    /* One per character of the word on screen, so a character of a compound
     * can be looked up on its own -- the second half of 声音, say. */
    H_CHARSEL = 60000,
    H_TLINE = 66000,                /* one per line of a chapter */
    /* One per character on a checked card, opening its strokes on their own */
    H_STROKEVIEW = 70000,
    /* One per character cell of a word's page: that character's own page */
    H_CHARCELL = 70100,
    /* One per source of notes, on the notes page's switch */
    H_NOTESRC = 80000,
    /* One per source of English, on the English's switch */
    H_ENSRC = 85000,
    /* One per line of the reading page: the mark of your own page on it */
    H_MYNOTE = 100000,
    /* and the mark of the book's notes on it, in the margin (MAXTLINES) */
    H_BNOTE = 110000,
    /* the PDF screen: one pin per row on the page (PDF_MAXROWS) */
    H_PDFPIN = 120000,
    /* the PDF page split in four, to choose the quarter to zoom into */
    H_PV_QPICK = 121000,
    /* the PDF page: the line a character was read from, one per character
     * (PV_CTX_MAX), and the characters of a word drawn over it (PV_MAXCL) */
    H_PV_CTX = 122000,
    H_PV_WCH = 123000,
    /* writing a sentence: one per box of the strip (MAXTCH), opening its
     * character in the dictionary */
    H_TWCELL = 124000
};

#define MAXBOOKS 128
#define MAXLISTS 512
#define MAXRES   40
/* One ink refresh per this many ms. A2 is ~120 ms, so anything below that is
 * asking for more than the panel can deliver. */
#define INK_MS 130
/* Pen width. The boxes are large, and a hairline in a 700px cell looks
 * nothing like a brush stroke -- it also reads badly on e-ink at a glance. */
#define INK_W  10

#define EX_PX    52      /* CJK needs the size to stay legible on e-ink */
#define EX_LINE   70
/* ui_text_hl returns one line-height *below* the last baseline it drew, so
 * that value is exactly where the next line would sit: the book label goes
 * there, and EX_TAIL is what to advance afterwards for the next sentence.
 * This used to be a bare 30px gap with the label crammed into it, which the
 * following sentence printed straight through once the type grew. */
#define EX_TAIL   66
#define EX_MAX     2

#define ROW_H_HOME 120
#define ROW_H_LIST 116

static int   W, H;
static int   scr = SCR_HOME;
static int   no_pen = 0;
static int   want_quit = 0;
static int   sleeping = 0;      /* panel blanked, waiting on the power button */
static session_t sess;
static int   in_test = 0;

static book_t books[MAXBOOKS];
static int    nbooks;
static char   cur_book[160];
static list_t lists[MAXLISTS];
static int    nlists;
static int    scroll = 0;
static int    page_step = 10;   /* rows that actually fit, set when a list draws */
static int    tick_mode = MODE_WRITE;  /* which tick column a tap landed in */
/* Pen only: on a Kobo with a stylus a finger never writes, only presses.
 * Toggled from the writing screens, where it matters, not from settings. */
static int    pen_only  = 1;
static int    has_pen   = 1;

static int   list_id = 0;
static char  list_label[200] = "all decks";
static char  scope_book[160] = "";   /* non-empty = whole-book scope */
static int   mode = MODE_WRITE;

static cardrow_t card;
static int   have_card = 0, revealed = 0;
static ink_t ink;
static strokes_t strk[8];
static int   nstrk = 0;
static example_t exs[3];
static int   nexs = 0;

/* Automatic assessment of what was written, per character of the headword.
 * Advisory only: it recognises what you drew and says whether it matches, but
 * you still grade the card. The recogniser is the same one the dictionary's
 * draw-search uses. */
static int  hw_rank[8];            /* -1 not found, else rank among candidates */
static char hw_top[8][8];          /* what it thought you drew */
static int  hw_checked = 0;

static char  q[64] = "";
static dictrow_t res[MAXRES];
static int   nres = 0;

static char  detail_ch[8] = "";
static char  detail_word[64] = "";
/* The word as looked up: detail_word without the marks inside a phrase the
 * book glosses whole (天时、地利、人和 is 天时地利人和 in the dictionary). And
 * what "add" puts in a list: the word, or that where the dictionary has it;
 * empty, and no add, for a phrase with marks that is no word of its own. */
static char  detail_key[64] = "", detail_add[64] = "";
static strokes_t detail_ws[8];
static int   detail_n = 0;
static int   detail_from_book = 0;  /* detail_ents[0] is the book's own deck entry */
static char  detail_py[160] = "", detail_df[512] = "";
/* Every entry for the word on the character screen. A character with two
 * readings is two entries, and the one it is read with where it was pressed
 * comes first -- 徼 in 常有欲以观其徼 is jiào, and jiǎo is the one the
 * dictionary happens to list first. */
#define MAXENTS 8
static dictrow_t detail_ents[MAXENTS];
static int  detail_nents = 0;
static int  detail_ctx_first = 0;   /* the first entry is the one read here */
static int  detail_all = 0;         /* every reading, before the strokes */
/* The book's own note on the word where it was pressed (a reader's glossary,
 * a novel's 注释), as stored; empty when it has none. With one, the page has
 * a switch, "book note | dictionary": on the note when the word was pressed
 * where it is marked, on the dictionary otherwise. Not remembered. */
static char detail_bnote[8192] = "";
static int  detail_bnote_on = 0;
static strokes_t detail_s;
static charinfo_t detail_ci;

static list_t ulists[MAXLISTS];
static int    nulists = 0;
static char   newname[200] = "";
static char   toast[160] = "";
/* What the last page-key press did. A physical button has no label under
 * your thumb, and grading moves straight to the next card, so the only
 * place to say it is on the card that follows. Not a flash: on e-ink that
 * is a full refresh, slow and finished before the eye gets there. */
static char   keynote[64] = "";

/* ---- watchdog ----------------------------------------------------------- *
 * The app has twice been left alive with a blank panel and no way out but
 * holding the power button. The log stopped mid-stroke both times, which
 * says the main loop never came back round -- and says nothing at all about
 * where it was.
 *
 * So: a breadcrumb before anything that could block, and an alarm that goes
 * off if the loop has not come round in twenty seconds. The handler writes
 * the breadcrumb and quits. run.sh's trap then reboots, which gets the Kobo
 * back on its own -- a reboot nobody asked for is bad, a device you have to
 * hold the power button on is worse, and either way the next log says where
 * it stopped. */
static const char *volatile g_where = "start";

static void watchdog_fired(int sig) {
    (void)sig;
    static const char pre[] = "\nWATCHDOG: stuck in: ";
    static const char post[] = "\nWATCHDOG: quitting so the device can recover\n";
    const char *w = g_where ? g_where : "(unset)";
    size_t n = 0;
    while (w[n] && n < 120) n++;
    ssize_t ignored;
    ignored = write(2, pre, sizeof pre - 1);
    ignored = write(2, w, n);
    ignored = write(2, post, sizeof post - 1);
    (void)ignored;
    _exit(3);
}

#define WHERE(x) (g_where = (x))

/* A deliberate operation that may legitimately run long on this CPU and its
 * eMMC: importing every Pleco deck at once is 67,000 words, a second and a
 * half on a desktop and plausibly twenty on the Kobo. The watchdog must not
 * kill that halfway. The loop re-arms it when the operation returns. */
static void long_task(const char *what) {
    alarm(0);
    g_where = what;
}
static int    toast_y = 0;     /* baseline; 0 means the default near the bottom */
static int    delete_armed = 0; /* "delete list" pressed once, waiting for a second */
static int    pick_scroll = 0, pick_step = 1;   /* the "add to" list's own page */
static int    reset_scroll = 0, reset_step = 1; /* and the reset screen's */

#define MAXREV 64
static revrow_t revs[MAXREV];
static int      nrevs = 0, rev_total = 0, rev_filter = -1;  /* -1 both */
static char     pleco_dir[512] = "";
static char     export_dir[512] = "";
static int      cur_ulist = -1;
static char     cur_uname[200] = "";
static dictrow_t uents[64];
static int      nuents = 0, uent_total = 0;
static int      naming_rename = 0;   /* SCR_NEWLIST doubles as the rename screen */

static long today(void) { return (long)(time(NULL) / 86400); }

/* The log is appended to for as long as the app runs, and with sleeping on
 * its own a session can last days. Past a megabyte, start it again: the
 * launcher keeps the previous session's as hanzi-prev.log, so this only
 * ever drops the older part of one very long session. The launcher opens
 * it for appending, so writing carries on at the new end. */
static void log_cap(void) {
    struct stat st;
    if (fstat(STDERR_FILENO, &st) != 0 || !S_ISREG(st.st_mode)) return;
    if (st.st_size < 1024L * 1024L) return;
    if (ftruncate(STDERR_FILENO, 0) == 0)
        fprintf(stderr, "log: past 1 MB within one session; started again\n");
}


static int draw_examples(int y, int ybot, const char *word);
static void start_test(void);
static int ex_min_height(void);

/* ---- hot rects --------------------------------------------------------- */
#define MAXHOT 128
static struct { rect_t r; int id; } hot[MAXHOT];
static int nhot = 0;
static int hot_full = 0;
static void hotreg(rect_t r, int id) {
    if (nhot < MAXHOT) { hot[nhot].r = r; hot[nhot].id = id; nhot++; return; }
    /* Silently dropping these means a button that draws but cannot be
     * pressed, which looks like the app ignoring you. */
    hot_full = 1;
}
static int hottest(int x, int y) {
    for (int i = nhot - 1; i >= 0; i--) if (ui_hit(hot[i].r, x, y)) return hot[i].id;
    return H_NONE;
}

/* ---- chrome ------------------------------------------------------------ */

/* Every screen carries exit and lock, so leaving or putting the device down
 * never means navigating back first. And settings, beside exit: the reading
 * light and the text size are wanted in the middle of whatever you are
 * doing, and back from settings returns to exactly that (settings_open). */
static int edition_label(char *out, size_t n);   /* with the texts, below */
/* Where the header's right-hand buttons begin, for a screen that puts one
 * of its own beside them (home's menu), which tells header() how wide it
 * is (`reserve`) so the title stops short of it too. */
static int hdr_right_x = 0;

static void header(const char *title, int back, int reserve = 0) {
    gfx_fill(rc(0, 0, W, 96), WHITE);
    int right = W - 128;                  /* exit's left edge */
    /* Not on settings itself, nor on reset, which is a page of it: back
     * already goes there, and a settings opened from reset would make
     * settings' back lead to reset. */
    if (scr != SCR_SETTINGS && scr != SCR_RESET) {
        int w = gfx_text_w(39, "settings");
        rect_t s = rc(right - 16 - (w + 24), 18, w + 24, 60);
        gfx_frame(s, 3, BLACK);
        gfx_text(s.x + 12, s.y + 41, 39, BLACK, "settings");
        hotreg(s, H_HDRSET);
        right = s.x;
    }
    /* On a text with a second edition, the edition you are in, by name --
     * the author for the book's own, translator B for his -- and pressing it
     * moves to the other edition at the same place. */
    char ed[64] = "";
    int edw = 0;
    int eds = edition_label(ed, sizeof ed);
    if (eds) {
        edw = gfx_text_w(36, ed) + 40;
        if (edw < 120) edw = 120;
        rect_t e = rc(right - 16 - edw, 18, edw, 60);
        int w = gfx_text_w(36, ed);
        if (eds == 1) {
            gfx_round_rect(e, 12, 3, BLACK, WHITE);
            gfx_text(e.x + (e.w - w) / 2, e.y + 41, 36, BLACK, ed);
            hotreg(e, H_EDSWITCH);
        } else {
            /* named, not a button: switching mid-phrase would drop the ink */
            gfx_text(e.x + (e.w - w) / 2, e.y + 41, 36, 0x70, ed);
        }
        right = e.x;
    }
    hdr_right_x = right;
    /* The title stops 32px short of the first button on its right, and a
     * long one is set a little smaller before it is cut: on the Clara, with
     * an edition and settings beside it, "1/8 · 道经·第一章" lost its
     * chapter to the ellipsis at 44px. */
    int tx = back ? 150 : 40, tw = right - reserve - 32 - tx;
    /* Still too wide at that: a chapter's section before a numbered title
     * goes, as on home's continue -- 德经·第三十八章 is 第三十八章, unique in
     * its book -- and only before 第, since 诗经's poem names repeat across
     * its 什. The Clara's writing screen, with translator B beside it, needs it. */
    char shorter[256];
    const char *sec = gfx_text_w(36, title) > tw ? strstr(title, "\302\267\347\254\254") : NULL;
    if (sec) {
        const char *from = sec;
        while (from > title && from[-1] != ' ') from--;
        snprintf(shorter, sizeof shorter, "%.*s%s", (int)(from - title), title, sec + 2);
        title = shorter;
    }
    gfx_text_fit(tx, 62, 44, 36, tw, BLACK, title);
    if (back) {
        rect_t b = rc(24, 18, 104, 60);
        gfx_frame(b, 3, BLACK);
        int w = gfx_text_w(39, "back");
        gfx_text(b.x + (b.w - w)/2, b.y + 41, 39, BLACK, "back");
        hotreg(b, H_BACK);
    }
    rect_t q = rc(W - 128, 18, 104, 60);
    gfx_frame(q, 3, BLACK);
    { int w = gfx_text_w(39, "exit");
      gfx_text(q.x + (q.w - w)/2, q.y + 41, 39, BLACK, "exit"); }
    hotreg(q, H_QUIT);

    gfx_fill(rc(0, 96, W, 2), GREY);
}

static void pdfv_orient(void);
/* What the panel holds while the device sleeps. E-ink keeps showing it with
 * the power off, so it may as well say something. */
static void draw_sleeping(void) {
    pdfv_orient();               /* upright, from the flipped PDF page too */
    nhot = 0;
    gfx_clear(WHITE);
    const char *a = "\347\235\241";                 /* 睡 */
    const char *b = "press power to wake";
    int wa = gfx_text_w(200, a), wb = gfx_text_w(38, b);
    gfx_text((W - wa) / 2, H / 2 - 20, 200, 0x30, a);
    gfx_text((W - wb) / 2, H / 2 + 90, 38, 0x80, b);
}

#define TICK_W   72
#define TICK_GAP  14
#define TICK_X0   26
#define TICK_X1   (TICK_X0 + TICK_W + TICK_GAP)
#define TICK_END  (TICK_X1 + TICK_W + 14)

/* A toggle that says what it is: the box holds 写 or 读 rather than a tick,
 * so a row halfway down a list needs no column header to be readable.
 *   off      outlined box, faint character
 *   on       filled box, character knocked out white
 *   partial  outlined box, solid character, bar underneath */
static void mode_toggle(rect_t r, const char *label, int state) {
    gfx_round_rect(r, 12, 3, state == 1 ? BLACK : 0x90, state == 1 ? BLACK : WHITE);
    int px = 36;
    int w  = gfx_text_w(px, label);
    uint8_t ink = state == 0 ? 0xA8 : (state == 1 ? WHITE : BLACK);
    gfx_text(r.x + (r.w - w)/2, r.y + r.h/2 + px/3 - (state == 2 ? 3 : 0), px, ink, label);
    if (state == 2) gfx_fill(rc(r.x + 12, r.y + r.h - 14, r.w - 24, 4), BLACK);
}

/* A settings stepper: [-] value [+]. Every row that has one draws it here,
 * because the three that used to place it by hand each put the value in a
 * slightly different spot, and a four-character value ("145%") ran straight
 * through the minus button. The value is centred in the gap and shrinks
 * until it fits with a margin, so no value can reach the buttons. */
#define STEP_W   90
#define STEP_L   (W - 366)
#define STEP_R   (W - 130)

static void stepper(int cy, const char *val, int minus_id, int plus_id) {
    rect_t minus = rc(STEP_L, cy - STEP_W/2, STEP_W, STEP_W);
    rect_t plus  = rc(STEP_R, cy - STEP_W/2, STEP_W, STEP_W);
    gfx_round_rect(minus, 14, 3, BLACK, WHITE);
    gfx_round_rect(plus,  14, 3, BLACK, WHITE);
    int mw = gfx_text_w(56, "-"), pw = gfx_text_w(56, "+");
    gfx_text(minus.x + (minus.w - mw)/2, minus.y + STEP_W/2 + 17, 56, BLACK, "-");
    gfx_text(plus.x  + (plus.w  - pw)/2, plus.y  + STEP_W/2 + 17, 56, BLACK, "+");

    int gx = STEP_L + STEP_W, gw = STEP_R - gx;
    int px = 64;
    while (px > 30 && gfx_text_w(px, val) > gw - 24) px -= 4;
    int vw = gfx_text_w(px, val);
    gfx_text(gx + (gw - vw)/2, cy + px/3, px, BLACK, val);

    hotreg(minus, minus_id);
    hotreg(plus,  plus_id);
}

#define ACT_W  104
#define ACT_GAP  10
#define ACT_X1   (W - 24 - ACT_W)
#define ACT_X0   (ACT_X1 - ACT_W - ACT_GAP)

/* The two start buttons on the right of a row. Outlined with a play triangle,
 * so they cannot be mistaken for the filled selection boxes on the left. */
static void start_pair(int y, int h, int idx) {
    const char *lbl[2] = { "\345\206\231", "\350\257\273" };
    int xs[2] = { ACT_X0, ACT_X1 };
    int ids[2] = { H_GO_W + idx, H_GO_R + idx };
    for (int m = 0; m < 2; m++) {
        rect_t b = rc(xs[m], y + (h - 80) / 2, ACT_W, 80);
        gfx_round_rect(b, 12, 3, BLACK, WHITE);
        int tw = gfx_text_w(42, lbl[m]);
        gfx_text(b.x + 14, b.y + 50, 42, BLACK, lbl[m]);
        int tx = b.x + 22 + tw, ty = b.y + b.h / 2;      /* play triangle */
        for (int k = 0; k < 15; k++)
            gfx_fill(rc(tx + k, ty - (14 - k) / 2, 1, (14 - k) > 0 ? (14 - k) : 1), BLACK);
        hotreg(b, ids[m]);
    }
}

static void tick_pair(int y, int h, const int *enabled, int base_id, int idx) {
    rect_t w = rc(TICK_X0, y + (h - TICK_W)/2, TICK_W, TICK_W);
    rect_t r = rc(TICK_X1, y + (h - TICK_W)/2, TICK_W, TICK_W);
    mode_toggle(w, "\345\206\231", enabled[MODE_WRITE]);
    mode_toggle(r, "\350\257\273", enabled[MODE_READ]);
    hotreg(w, base_id + idx);
    hotreg(r, H_TICK_R + idx);
}

/* A tick, drawn. Kaiti has no U+2713 and gfx renders a glyph the font lacks
 * as blank, so every "\342\234\223" in this file was showing nothing -- including the
 * one that told you a character was finished. Two lines cannot go missing. */
static void tick_mark(int x, int y, int sz, uint8_t ink) {
    int w = sz > 3 ? sz / 4 : 1;
    gfx_line(x, y + sz / 2, x + sz * 2 / 5, y + sz - w, w, ink);
    gfx_line(x + sz * 2 / 5, y + sz - w, x + sz, y, w, ink);
}

/* tri-state tick: 0 empty, 1 full, 2 partial */
static void tickbox(rect_t r, int state) {
    gfx_frame(r, 3, BLACK);
    if (state == 1) {
        gfx_line(r.x + 9, r.y + r.h/2, r.x + r.w/2 - 2, r.y + r.h - 12, 5, BLACK);
        gfx_line(r.x + r.w/2 - 2, r.y + r.h - 12, r.x + r.w - 9, r.y + 10, 5, BLACK);
    } else if (state == 2) {
        gfx_fill(rc(r.x + 12, r.y + r.h/2 - 3, r.w - 24, 6), BLACK);
    }
}

void tickbox_fwd(rect_t r, int state) { tickbox(r, state); }

/* Pager sits on its own row, clear of the action buttons, and always shows
 * where you are so a 100-chapter book is navigable. */
void tickbox_fwd(rect_t r, int state);   /* defined just below tickbox */

static void pager(int y, int count, int from, int shown) {
    if (from > 0) {
        rect_t p = rc(24, y, 160, 78);
        btn_t b = { p, "prev", 0 };
        ui_btn(&b, 0);
        hotreg(p, H_PREV);
    }
    if (from + shown < count) {
        rect_t p = rc(W - 184, y, 160, 78);
        btn_t b = { p, "next", 0 };
        ui_btn(&b, 0);
        hotreg(p, H_MORE);
    }
    if (count > shown) {
        char buf[64];
        snprintf(buf, sizeof buf, "%d\u2013%d of %d", from + 1, from + shown, count);
        int w = gfx_text_w(38, buf);
        gfx_text((W - w) / 2, y + 50, 38, 0x70, buf);
    }
}


/* the ">" that says a row opens something */
static void chevron(rect_t r) {
    int cx = r.x + r.w - 34, cy = r.y + r.h / 2;
    gfx_line(cx - 9, cy - 13, cx + 3, cy, 4, 0x60);
    gfx_line(cx + 3, cy, cx - 9, cy + 13, 4, 0x60);
}

/* "12 words, 9 of them writable"; "1 word, writable" */
static void words_writable(char *buf, size_t n, int total, int writable) {
    if (total == 1)
        snprintf(buf, n, "1 word, %s", writable ? "writable" : "not writable");
    else
        snprintf(buf, n, "%d words, %d of them writable", total, writable);
}

/* A label centred in a button, made smaller until it fits inside with a
 * margin: "写 write  nothing due today" ran out of both ends of the buttons
 * on the Clara's book page. Returns the size used. */
static int label_in(rect_t b, int px, int dy, uint8_t ink, const char *s) {
    int w = gfx_text_w(px, s);
    while (w > b.w - 28 && px > 26) { px -= 2; w = gfx_text_w(px, s); }
    gfx_text(b.x + (b.w - w) / 2, b.y + dy, px, ink, s);
    return px;
}

/* At the height the screen asked for (toast_y), else near the bottom; the
 * height is for this draw only. Home set one and it was never read, so its
 * toast fell inside the button row and was painted over. */
static void toastline(void) {
    int y = toast_y > 0 ? toast_y : H - 152;
    toast_y = 0;
    if (!toast[0]) return;
    gfx_text_clip(40, y, 38, W - 80, 0x50, toast);
}

/* A toast belongs to the screen it first showed on, and leaving that screen
 * takes it away. It stayed until something cleared it: "added 道 to mine"
 * turned up on a book's page three screens on, and an import's count on
 * home. A new one -- different words -- starts again wherever it shows, so
 * "deleted" set on the way to the lists shows there. Called by render(). */
static void toast_follow(void) {
    static int  shown_on = -1;           /* the screen it first showed on */
    static char shown[sizeof toast];     /* and what it said there */
    if (!toast[0]) { shown_on = -1; return; }
    if (shown_on < 0 || strcmp(shown, toast)) {
        shown_on = scr;
        snprintf(shown, sizeof shown, "%s", toast);
    } else if (shown_on != scr) {
        toast[0] = 0;
        shown_on = -1;
    }
}

/* ---- home -------------------------------------------------------------- */

/* Sized against the books that are actually loaded, not against a guess:
 * a chapter reaches 933 lines (水浒传 24, below), a book 305 chapters (诗经,
 * a poem each), a sentence 822 bytes and 219 hanzi, and a phrase to write is
 * cut at MAXTCH (64) characters -- the 法华经 has one of 165. At 64 lines two
 * thirds of every chapter was simply unreachable. 53 texts with the editions
 * and the canon. */
#define MAXTEXTS  128         /* 53 with the canon: room to grow */
#define MAXTCHAPS 512
/* Lines in one chapter. 512 held every book until the novels' original
 * text: 水浒传's chapter 24 is 933 lines. 2048 is 2MB of tlines, and the
 * H_TLINE range below has room for 4000. */
#define MAXTLINES 2048
static_assert(H_TLINE + MAXTLINES <= H_STROKEVIEW, "H_TLINE range overlaps H_STROKEVIEW");
#define MAXNSRC 8
static_assert(H_NOTESRC + MAXNSRC <= H_ENSRC, "H_NOTESRC range");
static_assert(H_ENSRC + MAXNSRC <= H_MYNOTE, "H_ENSRC range");
#define MAXTCH    64          /* writable characters in one phrase */

/* A book on the home screen: its text, its word decks, or both. They arrive
 * as two unrelated tables -- texts by id, decks by book name -- and are the
 * same book to the reader, so one row carries both and the book's own page
 * offers the two ways in. Matched by title, which is why build_db.py names a
 * text's deck after the text. */
typedef struct { char name[160]; int text_i, book_i; } source_t;
static source_t srcs[MAXTEXTS + MAXBOOKS];
static int nsrcs = 0;

static textbook_t texts[MAXTEXTS];
static int  ntexts = 0;

/* Home's categories, each the exact titles it holds, in the order it shows
 * them. Matched by the whole title. Every text must be in exactly one: the
 * selftest says which is not, since a title no list names would be a loose
 * row at the top of home. CAT_COMICS is for picture books (text_plates). */
enum { CAT_RUJIA, CAT_DAOJIA, CAT_ZHUZI, CAT_MENGXUE, CAT_COMICS };
static const char *CATEGORY[] = { "儒家经典", "道家", "诸子", "蒙学", "连环画" };
#define NCATEGORY ((int)(sizeof CATEGORY / sizeof *CATEGORY))
#define CAT_MAX 24                   /* titles in one category, and a NULL */
static const char *const CAT_BOOKS[][CAT_MAX] = {
    { "论语", "孟子", "大学", "中庸" },
    { "道德经" },
    { "孙子兵法" },
    { "三字经", "千字文" },
    { NULL },
};
static_assert(sizeof CAT_BOOKS / sizeof *CAT_BOOKS == sizeof CATEGORY / sizeof *CATEGORY,
              "a title list for every category");
static_assert(NCATEGORY == CAT_COMICS + 1, "CATEGORY[] and the CAT_ enum disagree");
static int is_edition(const char *title);
static void edition_base(const char *title, char *out, size_t n);
/* Which category a book is in, and where in it (*at); -1 none (HSK, my
 * lists, an imported deck). */
static int book_category_at(const char *name, int *at) {
    char base[200];
    edition_base(name, base, sizeof base);      /* "道德经 · edition 2" is 道德经's */
    for (int c = 0; c < NCATEGORY; c++)
        for (int k = 0; k < CAT_MAX && CAT_BOOKS[c][k]; k++)
            if (!strcmp(base, CAT_BOOKS[c][k])) { if (at) *at = k; return c; }
    return -1;
}
static int book_category(const char *name) { return book_category_at(name, NULL); }

/* The order the home screen lists the books in: your own lists, HSK, then
 * each category's books in the category's own order. This is a reading
 * order and the database has no notion of one -- it sorts by title -- so it
 * is named here. A book not named falls to the end, in the order the
 * database gave it, which is what a newly imported deck does. */
static int home_rank(const char *name) {
    if (!strcmp(name, "My lists")) return -1;   /* al's name for them */
    if (strstr(name, "HSK")) return strstr(name, "handwriting") ? 1 : 0;   /* the words, then the 书写字 */
    int at = 0, c = book_category_at(name, &at);
    if (c >= 0) return 10 + c * CAT_MAX + at;   /* a category's own order */
    return 999;
}

/* A second edition of a book -- a simplified edition's of 道德经 and 孙子兵法 -- is a text
 * of its own titled "<book> · edition 2". It is not a row on the home screen:
 * the book's page offers it, beside the edition the book was built from. */
#define EDITION_SUFFIX " \302\267 edition 2"
static int is_edition(const char *title) {
    size_t n = strlen(title), m = strlen(EDITION_SUFFIX);
    return n > m && !strcmp(title + n - m, EDITION_SUFFIX);
}
/* The book an edition belongs to: its title without the suffix. */
static void edition_base(const char *title, char *out, size_t n) {
    snprintf(out, n, "%s", title);
    if (is_edition(out)) out[strlen(out) - strlen(EDITION_SUFFIX)] = 0;
}
/* texts[] index of the other edition of `base`, or -1. */
static int edition_of(const char *base) {
    char want[200];
    snprintf(want, sizeof want, "%s%s", base, EDITION_SUFFIX);
    for (int i = 0; i < ntexts; i++) if (!strcmp(texts[i].title, want)) return i;
    return -1;
}

/* The home screen's shape: at the top the books reached most (shortcut_rank),
 * then my lists, then decks with no text (HSK), then the categories, each a
 * row that opens its books. A shortcut is in its category too. */
static_assert(NCATEGORY <= 16, "hrows[] has room for 16 category rows");
static int cur_cat = -1;             /* the category open on home; -1 the top */
static int mylists_back = SCR_MENU;   /* where my lists' back goes: the menu, or home's row */
static int pdfs_back = SCR_MENU;      /* and the PDFs': the menu, or home's header */
static int top_scroll = 0;           /* the page of the top, kept while in a category */
static int cat_scroll = 0;           /* the page of the category, kept while in a book */

/* The shortcuts at the top of home, in the order they are listed. */
static int shortcut_rank(const char *name) {
    if (!strcmp(name, "道德经")) return 0;
    if (!strcmp(name, "论语")) return 1;
    if (!strcmp(name, "孙子兵法")) return 2;
    if (!strcmp(name, "My lists")) return 3;
    return -1;
}
#define NSHORTCUT 4

/* The rows home shows: a book (srcs[idx]), a category (CATEGORY[idx]), or
 * my lists before there is a word in any (no "My lists" book then: al has
 * no row for an empty list), which opens the lists themselves -- my lists
 * are on home whether or not they hold anything yet. */
enum { HR_BOOK, HR_CAT, HR_MYLISTS };

typedef struct { int kind, idx; } hrow_t;
static hrow_t hrows[MAXTEXTS + MAXBOOKS + 16 + 1];     /* + my lists' own row */
static_assert(MAXTEXTS + MAXBOOKS + 16 + 1 <= H_TICK_R - H_TICK, "home rows overrun the H_TICK range");
static int nhrows = 0;

/* Derived state, rebuilt whenever the home screen draws and whenever
 * books[] is reloaded (reload_books). Texts keep their order and a deck
 * with no text of its own (HSK 3.1) follows them. */
static void build_sources(void) {
    nsrcs = 0;
    int used[MAXBOOKS] = {};
    for (int i = 0; i < ntexts && nsrcs < (int)(sizeof srcs / sizeof *srcs); i++) {
        if (is_edition(texts[i].title)) continue;      /* on its book's page */
        source_t *sv = &srcs[nsrcs++];
        snprintf(sv->name, sizeof sv->name, "%s", texts[i].title);
        sv->text_i = i;
        sv->book_i = -1;
        for (int b = 0; b < nbooks; b++)
            if (!used[b] && !strcmp(books[b].name, texts[i].title)) {
                sv->book_i = b; used[b] = 1; break;
            }
    }
    for (int b = 0; b < nbooks && nsrcs < (int)(sizeof srcs / sizeof *srcs); b++) {
        if (used[b]) continue;
        source_t *sv = &srcs[nsrcs++];
        snprintf(sv->name, sizeof sv->name, "%s", books[b].name);
        sv->text_i = -1;
        sv->book_i = b;
    }

    /* Into reading order. An insertion sort: the list is some eighty long,
     * sorted once per home draw, and stable order matters for everything
     * home_rank does not name. */
    for (int i = 1; i < nsrcs; i++) {
        source_t v = srcs[i];
        int rv = home_rank(v.name), j = i - 1;
        while (j >= 0 && home_rank(srcs[j].name) > rv) {
            srcs[j + 1] = srcs[j];
            j--;
        }
        srcs[j + 1] = v;
    }
}

/* books[] and srcs[] together, always: srcs[] holds indices into books[].
 * Reloading books[] alone -- a dozen places do -- left a book page in the
 * back stack pointing at another book: creating your first own list puts
 * "My lists" among the books, 道德经's page came back with 谷梁传's decks,
 * and its 写 started a test on 谷梁传. */
static void reload_books(void) {
    nbooks = db_books(books, MAXBOOKS);
    build_sources();
}

/* The order home lists its categories in: the user's, set on the menu's
 * "home order" page and kept in user.db by name, else CATEGORY[]'s own. A
 * category the user's order does not name (one added since) goes by its
 * own place, after those that have one there. cat_order[i] is the i-th. */
static int cat_order[NCATEGORY];
static void cat_order_load(void) {
    int pos[NCATEGORY];
    for (int c = 0; c < NCATEGORY; c++) {
        int v = db_home_cat_pos(CATEGORY[c]);
        pos[c] = v >= 0 ? v : NCATEGORY + c;
        cat_order[c] = c;
    }
    std::stable_sort(cat_order, cat_order + NCATEGORY,
                     [&](int a, int b) { return pos[a] < pos[b]; });
}
/* Move the category at place i of the order to place j, and keep it. */
static void cat_order_move(int i, int j) {
    if (i < 0 || j < 0 || i >= NCATEGORY || j >= NCATEGORY || i == j) return;
    std::swap(cat_order[i], cat_order[j]);
    db_begin();
    for (int k = 0; k < NCATEGORY; k++) db_set_home_cat_pos(CATEGORY[cat_order[k]], k);
    db_commit();
}

/* Home's rows for what is open: the top (shortcuts, any book in no
 * category, then the categories), or one category's books. */
static void build_hrows(void) {
    nhrows = 0;
    if (cur_cat < 0) {
        int mine = 0;
        for (int r = 0; r < NSHORTCUT; r++)
            for (int i = 0; i < nsrcs; i++)
                if (shortcut_rank(srcs[i].name) == r) {
                    hrows[nhrows++] = (hrow_t){ HR_BOOK, i };
                    mine |= r == shortcut_rank("My lists");
                }
        if (!mine) hrows[nhrows++] = (hrow_t){ HR_MYLISTS, -1 };
        for (int i = 0; i < nsrcs; i++)
            if (shortcut_rank(srcs[i].name) < 0 && book_category(srcs[i].name) < 0)
                hrows[nhrows++] = (hrow_t){ HR_BOOK, i };
        cat_order_load();
        for (int o = 0; o < NCATEGORY; o++) {
            int c = cat_order[o], any = 0;
            for (int i = 0; i < nsrcs; i++) any |= book_category(srcs[i].name) == c;
            if (any) hrows[nhrows++] = (hrow_t){ HR_CAT, c };
        }
    } else {
        for (int i = 0; i < nsrcs; i++)
            if (book_category(srcs[i].name) == cur_cat) hrows[nhrows++] = (hrow_t){ HR_BOOK, i };
    }
}

/* A category's ticks: 1 every book with decks in it ticked, 0 none, 2 some. */
static void category_ticks(int c, int *en) {
    for (int m = 0; m < 2; m++) {
        int on = 0, all = 0, part = 0;
        for (int i = 0; i < nsrcs; i++) {
            if (book_category(srcs[i].name) != c || srcs[i].book_i < 0) continue;
            all++;
            on   += books[srcs[i].book_i].enabled[m] == 1;
            part |= books[srcs[i].book_i].enabled[m] == 2;   /* some of its decks */
        }
        en[m] = (on == 0 && !part) ? 0 : on == all ? 1 : 2;
    }
}

/* The book SCR_BOOK is showing, held by name rather than by row: the rows
 * are rebuilt every time the home screen draws, and deleting a list or
 * importing a deck renumbers them. Coming back from the word decks would
 * then land on a different book, quietly. */
static char cur_src_name[160] = "";

static const source_t *cur_source(void) {
    if (!cur_src_name[0]) return NULL;
    for (int i = 0; i < nsrcs; i++)
        if (!strcmp(srcs[i].name, cur_src_name)) return &srcs[i];
    return NULL;
}

static textchap_t tchaps[MAXTCHAPS];
static int  ntchaps = 0;
static int  text_id = 0;
static char text_title[120] = "";

static char tlines[MAXTLINES][TEXT_LINE_MAX];
static int  ntlines = 0, tchap_id = 0, tchap_idx = 0;
static char tchap_title[160] = "";

/* Every character drawn in the reading view, so a tap can be resolved to a
 * position in the text without a hot rect per glyph -- a chapter has far more
 * characters than the hot rect table holds. */
static struct { rect_t r; short line, idx; } tbox[512];
static int ntbox = 0;

/* Writing through a chapter. */
static int   tw_line = 0;      /* which line of the chapter */
static int   tw_i    = 0;      /* which character of that line */
/* How much help is on screen, stepped up as it is wanted:
 *   0  the reading only -- write it from that
 *   1  and the character
 *   2  and its stroke order, a stroke at a time
 * Always starts at 0, because being shown the answer first is not a test. */
static int   tw_reveal = 0;
static int   tread_py  = 0;    /* pinyin under the lines while reading */
/* The chapter page's "sentence": held in, the next line pressed opens as a
 * sentence; otherwise a press opens the word under it. */
static int   tread_pick = 0;
/* 80, up from 70 ("the chinese text can be a bit
 * bigger"): at 100% a row holds about 14 on the Libra and 12 on the Clara,
 * a few fewer where the syllables are long -- more than the 13 and 10 the
 * even px + pypx/2 cells of 70 held. */
#define TREAD_PX   80          /* the chapter page's hanzi, before text_scale */
#define TREAD_LX   88          /* its left margin, the notes marks' target */
#define TREAD_PYPX 40          /* and the syllables under them */
static int   tread_en  = 0;    /* the translation under the sentence */
/* A book's sources, two ways. English is a translation of the sentence,
 * shown under it; notes are commentary on it -- word by word, a
 * commentator's, a translator's notes -- on a page of their own. A source
 * can give both (translator A, translator B, translator D), or only notes (translator C's 注, 张湛注,
 * 王弼注), so each has its own switch listing the sources that give it
 * (known from the rows, not from what a page is called: db_source_gives),
 * and its own choice, remembered per book: translator A's English can be read
 * with translator B's notes. A book with only its own has none listed, and no
 * switch. */
static notesrc_t notesrcs[MAXNSRC];     /* the notes' switch */
static int   n_nsrc = 0, cur_nsrc = 0;
static notesrc_t ensrcs[MAXNSRC];       /* the English's */
static int   n_esrc = 0, cur_esrc = 0;
static const char *note_key(void) { return n_nsrc ? notesrcs[cur_nsrc].key : ""; }
static const char *en_key(void)   { return n_esrc ? ensrcs[cur_esrc].key : ""; }
static const char *en_label(void) { return n_esrc ? ensrcs[cur_esrc].label : ""; }
static void note_source_use(int i) {
    if (i < 0 || i >= n_nsrc) i = 0;
    cur_nsrc = i;
    db_set_note_source(note_key());
}
static void en_source_use(int i) {
    if (i < 0 || i >= n_esrc) i = 0;
    cur_esrc = i;
}
static int   tread_start[MAXTLINES]; /* first line of each page read so far: a page holds a line at least */
/* A 连环画 volume: its plates, each a picture with its caption's lines under
 * it. A chapter with plates is read a plate at a time instead of a page of
 * lines; everything else about it -- a line pressed, its place kept -- is
 * the same as any text's. */
#define MAXPLATES 320
static plate_t tplates[MAXPLATES];
static int   ntplates = 0;
static int   cplate = 0;           /* the plate shown */
static int   csub = 0;             /* its page: 0, or 1 for a caption that did
                                      not fit under it; -1 the caption's page */
static int   cpages = 1;           /* how many pages the plate shown takes */
static char  comic_path[512] = ""; /* the series' EPUB, found on this machine */
static int   comic_full = 0;       /* the picture alone, filling the screen */
/* The series puts a picture and its text on pages of their own (the
 * Qing-painting 西游记, db_text_epub_apart): the picture as large as the
 * page holds, then its text over as many pages as it needs, laid out and
 * read as a chapter page is -- a character opens its word, sentence,
 * pinyin, notes and my notes as there. Paging runs picture, its text,
 * the next picture; back the same way in reverse. A plate with no
 * picture (a 前言) is its text alone. */
static int   comic_apart = 0;
static void  comic_layout(void);                /* the plate shown, laid out */
static void  draw_comic_apart(void);
static int   cseek_line = -1;      /* a line to show: its plate, and its caption's page */
static void  comic_page_lines(int *a, int *b);  /* its lines on the page shown */
/* The very plate a line is in -- not "the start of the volume" that line 0
 * means on opening one -- at the caption (or text) page it is on. */
static void comic_seek_line(int line) {
    cplate = 0;
    for (int k = 0; k < ntplates; k++)
        if (tplates[k].n_lines > 0 && tplates[k].line_first <= line) cplate = k;
    cseek_line = line;
}
static int   tread_np   = 0;   /* how deep into that chapter we have paged */
/* What the stack of page starts was laid out with: the chapter and what
 * tread_walk's layout reads (the size, the marks, the readings -- a
 * cell's room is its syllable's). Pinyin and the margins' marks are not in
 * it: the rows are always broken by the cells with the readings shown, so
 * neither moves a character to another row or page. */
struct tread_layout { int chap = -1, scale = 0, punct = 0, readings = 0; };
static tread_layout tread_key;
static int   tread_shown = 0;  /* lines on the page now drawn */
static int   tline_page = 0;   /* which page of a sentence too long for one */
static int   tline_pages = 1;
static int   tline_top = 0;    /* the first character on the page last drawn */
static int   tline_end = 0;    /* and the first one after it */
static int   tline_seek = -1;  /* a character to open the sentence's page at */
/* Guided ("magic") writing, drawn and toggled by every screen you write
 * on. Declared here because the first of them is a long way above where
 * the guided code itself lives. */
static void guided_button(rect_t r);
static void pen_button(rect_t r);
static void writing_toggles(rect_t r);
static void guided_overlay(rect_t box, const char *ch);
static void guided_note_at(int x, int y, int w);
static void toggle_btn(rect_t r, const char *label, int on, int id);

static int   card_outline = 1; /* the model under a checked card's ink */
static int   show_punct = 1;   /* editorial marks; settings can hide them */
static int   text_readings = 1;/* pinyin from the book where it has it */
static int   text_scale = 100; /* how large a text is set, as a percentage */
static int   comic_size = 0;   /* a comic's picture: 0 large, 1 medium (80%), 2 small (65%) */
static int   comic_split = 0;  /* a caption may break across pages, the part that fits under the picture */

/* A size in the code is the size at 100%. Everything a text is *read* at
 * goes through this; the interface around it does not, because those
 * layouts are built from fixed offsets and would come apart. */
static int tpx(int px) {
    int v = px * text_scale / 100;
    return v < 12 ? 12 : v;
}
static int   chapter_has_translation(void);  /* the English source's page is its translation */
static int   line_has_notes(int line);      /* its own notes, in the notes source */
static int   line_has_english(int line);    /* its English, in the English source */
static int   src_lines(const char *key, int what);   /* a source gives lines that */
static void  draw_src_row(const notesrc_t *list, int n, int cur, int base, const char *lead,
                          int tog, int held, int slot);
static int   en_slot_book(void);            /* an English source of the book has chapter pages */
static int   chapter_note_kind_of(int chapter_id, const char *key);
static void  en_none_text(char *out, size_t n);   /* what a line without the English's English says */
static void  en_end_text(char *out, size_t n, const char *lab, int last);   /* where his English ends */
static int   line_offers_notes(int line);   /* its own, the chapter's, or another source's */
static int   notes_step(int dir, int *oc, int *ol);   /* the next/previous line with notes */
static int   tnotes_laid_out = 0; /* the notes page's layout is for what is loaded */
static int   practice_active = 0;   /* the practice box is over the screen */
static int   guided_on  = 0;   /* check each stroke as it is drawn */
static void  guided_reset(void);
/* What was written, character by character, for the whole sentence -- not
 * only the phrase in hand -- so that finishing a sentence can show all of it
 * back. Up to 219 hanzi in the longest line here; 320 leaves room for the
 * current phrase's 64 on top of the furthest offset. */
#define MAXLINECH 320
static inkthumb_t tw_all_ink[MAXLINECH];
/* How each character was judged when it was checked: -1 not checked yet,
 * 0 the handwriting search put the right character first, 1..n further down
 * its list, -2 not in the list at all. Writing a sentence out with no way to
 * ask "was that right?" is transcription, not practice. */
#define TW_UNCHECKED (-1)
#define TW_WRONG     (-2)
static int  tw_all_rank[MAXLINECH];
static char tw_all_guess[MAXLINECH][8];   /* what it looked most like, when wrong */
/* Written stroke by stroke with the matcher on. There is no ink to show for
 * it -- each accepted stroke is drawn from the character's own outline, not
 * from the pen -- so the box along the top shows the character itself. */
static char tw_all_magic[MAXLINECH];

/* The phrase in hand, as views into the sentence: index 0 is the phrase's
 * first character. Moving to another phrase moves the views, so everything
 * written in the sentence stays where it was. */
static inkthumb_t *tw_ink   = tw_all_ink;
static int        *tw_rank  = tw_all_rank;
static char      (*tw_guess)[8] = tw_all_guess;
static char       *tw_magic = tw_all_magic;
static int         tw_base  = 0;     /* the phrase's first character, in the sentence */
static int         tw_said  = -1;    /* the character judged last, in the sentence */
/* A wrong character brings the help up by itself; this is what it was set
 * to before, to put back once you move on. */
static int         tw_auto_help = 0, tw_help_was = 0;
/* The sentence's last character, judged right, stays on screen with its
 * verdict -- yours beside the font's -- until next is pressed again: going
 * straight on to the sentence written, it was never seen. tw_end_n is the ink it was judged on; more ink is judged. */
static int         tw_end_shown = 0, tw_end_n = 0;

static void tw_views(int base) {
    if (base < 0) base = 0;
    if (base > MAXLINECH - MAXTCH) base = MAXLINECH - MAXTCH;
    tw_base  = base;
    tw_ink   = tw_all_ink + base;
    tw_rank  = tw_all_rank + base;
    tw_guess = tw_all_guess + base;
    tw_magic = tw_all_magic + base;
}

static void tw_forget_sentence(void) {
    tw_said = -1;
    for (int i = 0; i < MAXLINECH; i++) {
        tw_all_ink[i].n = 0;
        tw_all_rank[i] = TW_UNCHECKED;
        tw_all_guess[i][0] = 0;
        tw_all_magic[i] = 0;
    }
}

/* "book · chapter" on the continue-reading button, within maxw at 31px.
 * The chapter says where you are, so the title gives way first: a 连环画's
 * publisher, then "连环画" -- the volume's number and name say it is the
 * comic -- and another book's "(...)" where no other text shares the rest
 * of its title: 老子道德经注 (王弼) hid the chapter of 72 of its 82 on the
 * Clara. A novel keeps (原著) or (青少年版), which say which. */
static void continue_label(const char *title, const char *chap, int maxw,
                           char *out, size_t n) {
    snprintf(out, n, "%s \302\267 %s", title, chap);
    if (gfx_text_w(31, out) <= maxw) return;
    char t[160];
    snprintf(t, sizeof t, "%s", title);
    char *br = strstr(t, " (");
    if (strstr(t, "\350\277\236\347\216\257\347\224\273")) {     /* 连环画 */
        if (br) {
            *br = 0;
            snprintf(out, n, "%s \302\267 %s", t, chap);
        }
        char *sp = strchr(t, ' ');
        if (sp && gfx_text_w(31, out) > maxw) {
            *sp = 0;
            snprintf(out, n, "%s \302\267 %s", t, chap);
        }
        return;
    }
    const char *head = title;
    if (br) {
        *br = 0;
        size_t l = strlen(t);
        int shared = 0;                          /* the short title says which */
        for (int i = 0; i < ntexts; i++)
            if (strcmp(texts[i].title, title) && !strncmp(texts[i].title, t, l) &&
                (texts[i].title[l] == 0 || texts[i].title[l] == ' '))
                shared = 1;
        if (!shared) {
            head = t;
            snprintf(out, n, "%s \302\267 %s", head, chap);
        }
    }
    /* Still too wide: a chapter's section before a numbered title goes
     * (道德经 · edition 2's 道经·第二十一章 is 第二十一章, unique in the book);
     * only before 第, since 诗经's poem names repeat across its 什. */
    const char *dot = strstr(chap, "\302\267");
    if (dot && gfx_text_w(31, out) > maxw && !strncmp(dot + 2, "\347\254\254", 3))
        snprintf(out, n, "%s \302\267 %s", head, dot + 2);
}

static void draw_home(void) {
    nhot = 0;
    gfx_clear(WHITE);
    if (cur_cat >= 0) header(CATEGORY[cur_cat], 1, 136);
    else header("hanzi", 0, 272);

    /* left of settings, which is left of exit */
    rect_t mb = rc(hdr_right_x - 16 - 120, 18, 120, 60);
    gfx_frame(mb, 3, BLACK);
    { int w = gfx_text_w(39, "menu");
      gfx_text(mb.x + (mb.w - w)/2, mb.y + 41, 39, BLACK, "menu"); }
    hotreg(mb, H_MENU);
    /* and at the top of home the PDFs, left of menu: inside the menu
     * alone they went unfound. Not in a category,
     * where the category's name needs the room (四大名著 青少年版 on the
     * Clara). */
    if (cur_cat < 0) {
        rect_t pb = rc(mb.x - 16 - 120, 18, 120, 60);
        gfx_frame(pb, 3, BLACK);
        int w = gfx_text_w(39, "PDFs");
        gfx_text(pb.x + (pb.w - w)/2, pb.y + 41, 39, BLACK, "PDFs");
        hotreg(pb, H_PDFS);
    }

    int dw, fw, dr, fr;
    db_counts(0, NULL, MODE_WRITE, today(), &dw, &fw);
    db_counts(0, NULL, MODE_READ,  today(), &dr, &fr);
    char buf[160];
    snprintf(buf, sizeof buf, "\345\206\231 %d due, %d new   \350\257\273 %d due, %d new",
             dw, fw, dr, fr);
    gfx_text(40, 156, 39, BLACK, buf);

    int by      = H - 120;             /* write | read      */
    int dby     = by - 104;            /* dictionary        */
    int pgy     = dby - 96;            /* pager             */
    int rowsbot = pgy - 14;

    int have_sess = db_session_load(&sess);
    int unfinished = have_sess && (sess.pending + sess.repeat) > 0;


    int y = 224;
    if (unfinished) {
        rect_t r = rc(24, y, W - 48, 86);
        gfx_round_rect(r, 16, 3, BLACK, WHITE);
        snprintf(buf, sizeof buf, "resume test  \302\267  %s  \302\267  %d left",
                 sess.label[0] ? sess.label : "all decks",
                 sess.pending + sess.repeat);
        gfx_text_clip(44, y + 52, 39, W - 140, BLACK, buf);
        chevron(r);
        hotreg(r, H_RESUME);
        y += 98;
    }
    /* One row per book, whether it arrived as a text, as word decks, or as
     * both -- 道德经 was two rows saying 道德经, which is one book. The row
     * opens the book's own page; the ticks on it stay here, because picking
     * what the daily test draws from is what this screen is for. */
    build_sources();
    build_hrows();
    int shown = 0;
    const int ROWH = 108;
    /* a page is as many rows as fit, whatever the last page holds: prev
     * from a short last page went back too few and the pages drifted */
    { int fit = (rowsbot - y) / ROWH; page_step = fit > 0 ? fit : 1; }
    for (int k = scroll; k < nhrows; k++) {
        if (y + ROWH > rowsbot) break;
        rect_t r = rc(24, y, W - 48, 96);
        if (hrows[k].kind == HR_CAT) {
            /* a category: its books' ticks together, and how many it holds */
            int c = hrows[k].idx, en[2], n = 0;
            category_ticks(c, en);
            for (int i = 0; i < nsrcs; i++) n += book_category(srcs[i].name) == c;
            gfx_round_rect(r, 12, 3, BLACK, WHITE);
            hotreg(r, H_ITEM + k);
            tick_pair(y, 96, en, H_TICK, k);
            gfx_fill(rc(TICK_END, y + 10, 1, 76), FAINT);
            int tx = TICK_END + 20;
            gfx_text_clip(tx, y + 48, 44, W - tx - 70, BLACK, CATEGORY[c]);
            snprintf(buf, sizeof buf, n == 1 ? "%d book" : "%d books", n);
            gfx_text_clip(tx, y + 84, 36, W - tx - 70, 0x80, buf);
            chevron(r);
            shown++;
            y += ROWH;
            continue;
        }
        if (hrows[k].kind == HR_MYLISTS) {
            /* my lists with no word in them yet: no decks to tick, and the
             * row opens the lists, as the menu's does */
            int nul = 0, nue = 0;
            db_user_list_totals(&nul, &nue);
            gfx_frame(r, 2, GREY);
            hotreg(r, H_ITEM + k);
            gfx_text_clip(46, y + 48, 44, W - 46 - 70, BLACK, "My lists");
            if (nul) snprintf(buf, sizeof buf, "%d list%s, no words yet", nul, nul == 1 ? "" : "s");
            else snprintf(buf, sizeof buf, "none yet \342\200\224 add words from the dictionary");
            gfx_text_clip(46, y + 84, 36, W - 46 - 70, 0x80, buf);
            chevron(r);
            shown++;
            y += ROWH;
            continue;
        }
        int i = hrows[k].idx;
        const source_t *sv = &srcs[i];
        gfx_frame(r, 2, GREY);
        hotreg(r, H_ITEM + k);          /* row first: the ticks must shadow it */

        int tx = 46;
        if (sv->book_i >= 0) {
            tick_pair(y, 96, books[sv->book_i].enabled, H_TICK, k);
            gfx_fill(rc(TICK_END, y + 10, 1, 76), FAINT);
            tx = TICK_END + 20;
        }
        gfx_text_clip(tx, y + 48, 44, W - tx - 70, BLACK, sv->name);

        /* Say which number is which: a bare pair read as neither. */
        buf[0] = 0;
        if (sv->text_i >= 0)
            snprintf(buf, sizeof buf, texts[sv->text_i].n_chap == 1 ? "%d chapter" : "%d chapters",
                     texts[sv->text_i].n_chap);
        if (sv->book_i >= 0) {
            char lb[64];
            snprintf(lb, sizeof lb, books[sv->book_i].n_lists == 1
                     ? "%d list" : "%d lists", books[sv->book_i].n_lists);
            if (buf[0]) {
                size_t o = strlen(buf);
                snprintf(buf + o, sizeof buf - o, "  \302\267  %s", lb);
            } else {
                snprintf(buf, sizeof buf, "%s", lb);
            }
        }
        gfx_text_clip(tx, y + 84, 36, W - tx - 70, 0x80, buf);
        chevron(r);

        shown++;
        y += ROWH;
    }
    pager(pgy, nhrows, scroll, shown);
    toast_y = dby - 18;
    toastline();

    /* Picking up the book you were reading is the commonest thing to want
     * from this screen, so it is on it, not two presses inside a book. It
     * shares the row with the dictionary rather than taking one of its own:
     * a row here is a row fewer for the books, and on the Clara that is the
     * difference between all eight fitting and not. */
    char cont[200] = "";
    int last_text = db_last_text();
    if (last_text) {
        for (int i = 0; i < ntexts; i++)
            if (texts[i].id == last_text) {
                textchap_t c;
                int ch = db_text_last_chapter(last_text);
                if (ch && db_text_chapter(ch, &c))
                    continue_label(texts[i].title, c.title, (W - 96) / 2 - 28,
                                   cont, sizeof cont);
                break;
            }
    }

    if (cont[0]) {
        /* Wide enough for the book and the chapter both: the chapter alone
         * does not say which book, and a clipped title says neither. */
        int cw  = (W - 96) / 2;
        int dbw = (W - 96) * 3 / 10;
        rect_t cr  = rc(24, dby, cw, 88);
        rect_t db  = rc(24 + cw + 24, dby, dbw, 88);
        rect_t drw = rc(24 + cw + 24 + dbw + 24, dby,
                        W - 24 - (24 + cw + 24 + dbw + 24), 88);
        gfx_round_rect(cr, 14, 3, BLACK, WHITE);
        { int w = gfx_text_w(38, "continue reading");
          gfx_text(cr.x + (cr.w - w)/2, cr.y + 42, 38, BLACK, "continue reading"); }
        { /* a size down before a clip: 道德经 · edition 2 · 第四十四章 is a few
           * pixels too wide for the Clara at 31 */
          int px = 31;
          while (px > 28 && gfx_text_w(px, cont) > cr.w - 28) px--;
          int sw = gfx_text_w(px, cont);
          int sx = sw < cr.w - 28 ? cr.x + (cr.w - sw)/2 : cr.x + 14;
          gfx_text_clip(sx, cr.y + 74, px, cr.w - 28, 0x70, cont); }
        hotreg(cr, H_TCONTINUE);
        btn_t bdr = { drw, "draw", 0 };
        ui_btn(&bdr, 0);
        hotreg(drw, H_SCRATCH);
        btn_t bd = { db, "dictionary", 0 };
        ui_btn(&bd, 0);
        hotreg(db, H_DICT);
    } else {
        int dbw = (W - 72) * 2 / 3;
        rect_t db = rc(24, dby, dbw, 88);
        rect_t drw = rc(24 + dbw + 24, dby, W - 48 - dbw - 24, 88);
        btn_t bdr = { drw, "draw", 0 };
        ui_btn(&bdr, 0);
        hotreg(drw, H_SCRATCH);
        btn_t bd = { db, "dictionary", 0 };
        ui_btn(&bd, 1);
        hotreg(db, H_DICT);
    }

    /* Start a test from whatever is ticked on the left. */
    {
        int bw2 = (W - 72) / 2;
        for (int m = 0; m < 2; m++) {
            int size  = m == MODE_WRITE ? TEST_WRITE : TEST_READ;
            int avail = (m == MODE_WRITE ? dw + fw : dr + fr);
            int nn    = size < avail ? size : avail;
            rect_t b  = rc(24 + m * (bw2 + 24), by, bw2, 104);
            gfx_round_rect(b, 16, 3, BLACK, WHITE);
            if (nn > 0) snprintf(buf, sizeof buf, "%s  %d card%s",
                                 m ? "\350\257\273 read" : "\345\206\231 write", nn, nn == 1 ? "" : "s");
            else        snprintf(buf, sizeof buf, "%s  nothing due today",
                                 m ? "\350\257\273 read" : "\345\206\231 write");
            label_in(b, 41, 64, nn > 0 ? BLACK : 0xA0, buf);
            hotreg(b, m ? H_READ : H_WRITE);
        }
    }
}

/* ---- chapter list ------------------------------------------------------ */

static void draw_lists(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header(cur_book, 1);


    int by      = H - 130;
    int pgy     = by - 92;
    int rowsbot = pgy - 14;

    db_list_counts(lists, nlists, mode, today());   /* one query for the page */

    int y = 176;
    int shown = 0;
    char buf[96];
    for (int i = scroll; i < nlists && y + ROW_H_LIST <= rowsbot; i++, shown++) {
        rect_t r = rc(24, y, W - 48, 100);
        gfx_frame(r, 2, GREY);
        hotreg(r, H_ITEM + i);      /* row first: the ticks must shadow it */
        tick_pair(y, 100, lists[i].enabled, H_TICK, i);
        gfx_fill(rc(TICK_END, y + 12, 1, 74), FAINT);
        /* Title, and under it the counts -- which are for one mode, the one
         * last studied, so they say which. Side by side they ran into each
         * other on the Clara (方便品第二 over "0 due"), and clipping the
         * title to make room left "Le...". */
        int textw = ACT_X0 - 22 - (TICK_END + 20);
        gfx_text_clip(TICK_END + 20, y + 50, 42, textw, BLACK,
                      lists[i].chapter[0] ? lists[i].chapter : "(no chapter)");
        snprintf(buf, sizeof buf, "%s %d due  \302\267  %d new  \302\267  %d words",
                 mode == MODE_READ ? "\350\257\273" : "\345\206\231",
                 lists[i].due, lists[i].fresh, lists[i].n);
        gfx_text_clip(TICK_END + 20, y + 88, 30, textw, 0x60, buf);
        start_pair(y, 100, i);
        y += ROW_H_LIST;
    }
    page_step = shown > 0 ? shown : 1;
    pager(pgy, nlists, scroll, shown);

    {   /* start from the chapters ticked in this book */
        int bw3 = (W - 72) / 2;
        char wb[160];
        for (int m = 0; m < 2; m++) {
            int dd = 0, ff = 0;
            db_counts(0, cur_book, m, today(), &dd, &ff);
            int size = m == MODE_WRITE ? TEST_WRITE : TEST_READ;
            int nn   = dd + ff;
            if (nn > size) nn = size;
            rect_t b = rc(24 + m * (bw3 + 24), by, bw3, 100);
            gfx_round_rect(b, 16, 3, BLACK, WHITE);
            if (nn > 0) snprintf(wb, sizeof wb, "%s  %d card%s",
                                 m ? "\350\257\273 read" : "\345\206\231 write", nn, nn == 1 ? "" : "s");
            else        snprintf(wb, sizeof wb, "%s  nothing due today",
                                 m ? "\350\257\273 read" : "\345\206\231 write");
            label_in(b, 41, 62, nn > 0 ? BLACK : 0xA0, wb);
            hotreg(b, m ? H_READ : H_WRITE);
        }
    }
}

/* ---- review ------------------------------------------------------------ */

static rect_t cellrect(int i, int n, int y, int size) {
    int gap = 20;
    int total = n * size + (n - 1) * gap;
    int x0 = (W - total) / 2;
    return rc(x0 + i * (size + gap), y, size, size);
}

/* A small page-key glyph: the Libra's two buttons, one of them filled to
 * say which. Drawn rather than written, because the answer to "which one
 * does this?" should be visible without reading. */
static void pagekey_mark(int x, int y, int fwd, uint8_t ink) {
    const int w = 16, h = 13, gap = 4;
    rect_t top = rc(x, y, w, h), bot = rc(x, y + h + gap, w, h);
    gfx_frame(top, 2, ink);
    gfx_frame(bot, 2, ink);
    gfx_fill(fwd ? rc(bot.x + 3, bot.y + 3, w - 6, h - 6)
                 : rc(top.x + 3, top.y + 3, w - 6, h - 6), ink);
}

static void grade_buttons(int y) {
    int bw = (W - 96) / 3;
    const char *lb[3] = { "again", "hard", "good" };
    int id[3] = { H_AGAIN, H_HARD, H_GOOD };
    for (int i = 0; i < 3; i++) {
        char buf[64];
        if (in_test && !sess.scored) {
            snprintf(buf, sizeof buf, "%s", lb[i]);    /* no interval is applied */
        } else {
            int d = srs_preview(&card.c, i + 1, today());
            if (d >= 365)     snprintf(buf, sizeof buf, "%s %dy",  lb[i], d / 365);
            else if (d >= 30) snprintf(buf, sizeof buf, "%s %dmo", lb[i], d / 30);
            else              snprintf(buf, sizeof buf, "%s %dd",  lb[i], d);
        }
        btn_t b = { rc(24 + i * (bw + 24), y, bw, 100), buf, 0 };
        ui_btn(&b, 0);
        hotreg(b.r, id[i]);
        /* Which key presses which. Forward is "good", back is "again" --
         * the two answers you give once you have looked. */
        if (surf_has_pagekeys() && (id[i] == H_GOOD || id[i] == H_AGAIN))
            pagekey_mark(b.r.x + b.r.w - 28, b.r.y + 32, id[i] == H_GOOD, 0x70);
    }
}

static void newbadge(void) {
    if (!card.is_new) return;
    const char *s = "new";
    int w = gfx_text_w(38, s);
    rect_t b = rc(W - 44 - w - 22, 116, w + 22, 42);
    gfx_fill(b, BLACK);
    gfx_text(b.x + 11, b.y + 31, 38, WHITE, s);
}

/* The write card's geometry, in one place.
 *
 * in_writearea() once hard-coded the y this returns while draw_write_card()
 * computed it, so as soon as the position depended on anything -- here, how
 * many lines the definition wrapped to -- the box you saw and the box that
 * accepted ink were different boxes. Both callers go through this. */
static void write_geom(int *ny, int *ncy, int *nsize) {
    int n = nstrk ? nstrk : 1;
    int maxcell = (n == 1 ? 640 : (n == 2 ? 480 : 420));
    int size = (W - 48 - (n - 1) * 20) / n;
    if (size > maxcell) size = maxcell;
    /* Two lines of definition are allowed; the box starts below whatever the
     * card actually needed, never above the old fixed position. */
    int dy = 226 + 46 * (gfx_text_w(41, card.defn) > W - 80 ? 2 : 1);
    int cy = dy + 28 > 300 ? dy + 28 : 300;
    if (ny)    *ny    = n;
    if (ncy)   *ncy   = cy;
    if (nsize) *nsize = size;
}

static int is_lookup(const char *ch);
/* How many H_CHARSEL a word can have: one per character of detail_word
 * (64 bytes), with room to spare below H_TLINE. */
#define CHARSEL_MAX 64
/* Word `word` cut into rows at `px` no wider than maxw: the character each
 * row starts at into start[] (start[rows] = n), the row count returned. A
 * row ends after a mark in its second half rather than mid-word where it
 * can, and never puts a closing mark (，；、。) at the start of the next. */
static int big_word_rows(const char *word, int px, int maxw, int *start, int maxrows) {
    int n = u8_len(word), rows = 0, i = 0;
    while (i < n && rows < maxrows) {
        start[rows++] = i;
        int w = 0, j = i, brk = -1;
        for (; j < n; j++) {
            char ch[8];
            u8_at(word, j, ch);
            int cw = gfx_text_w(px, ch);
            if (j > i && w + cw > maxw) break;
            w += cw;
            if (!is_lookup(ch)) brk = j + 1;
        }
        if (j < n) {
            /* the mark nearest the end, if it is past half the row */
            if (brk > i && (brk - i) * 2 >= j - i) j = brk;
            char c[8];
            while (j - 1 > i && u8_at(word, j, c) && gfx_no_line_start(c)) j--;
        }
        i = j > i ? j : i + 1;
    }
    start[rows] = i;                 /* past maxrows: cut there */
    return i < n ? maxrows + 1 : rows;
}

/* Draw a word large and centred below `top`, shrinking it until it fits, and
 * make each character of it pressable so it can be looked up on its own.
 * A word too long for one row at a readable size -- a phrase the book glosses
 * whole (你吃白面馒头，我吃菜团；你吃面条，我喝汤) -- wraps onto more, smaller,
 * never past the margins: drawn on one row at 90px it began 264px off the
 * Clara's left edge. Returns the last row's baseline, which is where the
 * next thing goes from. */
static int big_word(const char *word, int top, int px_max, int feet = 0) {
    const int maxw = W - 80, MAXR = 8;
    int n = u8_len(word);
    int start[MAXR + 2];
    /* one row, shrinking as it always has while it stays 100px or more */
    int px = px_max, rows = 0;
    auto line_h = [](int p) { return p + (p / 5 > 20 ? p / 5 : 20); };
    while (gfx_text_w(px, word) > maxw && px - 20 >= 100) px -= 20;
    if (gfx_text_w(px, word) <= maxw) {
        rows = 1; start[0] = 0; start[1] = n;
    } else {
        /* more rows, at the largest size whose rows fit about the height
         * the word would have had: two rows of 90, or the one row at its
         * largest; never below 60, which is still a target to press. From
         * 120 down, not from the top: each size tried is a size of glyphs
         * measured (and cached). */
        int budget = line_h(px_max) > 2 * line_h(90) ? line_h(px_max) : 2 * line_h(90);
        for (px = px < 120 ? px : 120; ; px -= 10) {
            rows = big_word_rows(word, px, maxw, start, MAXR);
            if (rows <= MAXR && rows * line_h(px) <= budget) break;
            if (px <= 60) {
                if (rows > MAXR) rows = MAXR;      /* cut; each row still fits */
                break;
            }
        }
    }
    int lh = line_h(px);
    /* Only worth offering on a compound: on a single character the whole
     * headword is already what you would be looking up. And only on a
     * character: a mark inside a phrase (、 of 天时、地利、人和) looks nothing
     * up -- pressed, it opened an empty page for "、". */
    int nlook = 0;
    for (int i = 0; i < n; i++) { char ch[8]; u8_at(word, i, ch); nlook += is_lookup(ch); }
    int base = top + px, lastx = W;
    for (int r = 0; r < rows; r++) {
        int rw = 0;
        for (int i = start[r]; i < start[r + 1]; i++) {
            char ch[8];
            u8_at(word, i, ch);
            rw += gfx_text_w(px, ch);
        }
        int x = (W - rw) / 2, rtop = top + r * lh;
        base = rtop + px;
        lastx = x;
        for (int i = start[r]; i < start[r + 1]; i++) {
            char ch[8];
            u8_at(word, i, ch);
            int cw = gfx_text_w(px, ch);
            gfx_text(x, base, px, BLACK, ch);
            if (nlook > 1 && i < CHARSEL_MAX && is_lookup(ch))
                hotreg(rc(x, rtop, cw, rows > 1 ? lh : px + 20), H_CHARSEL + i);
            x += cw;
        }
    }
    /* Room under the characters' feet where the last row reaches over what
     * goes under it at +34, from the left margin to x `feet` (the word
     * page's reading): it touched them -- 天时、地利、人和 on the Clara --
     * and a wrapped phrase's last row is as wide as the page more often
     * than not. */
    return rows > 1 || lastx < feet ? base + px / 5 + 8 : base;
}

/* Mark a frames row that has been cut short: the last cell shown becomes an
 * ellipsis, so it reads as "there is more here" rather than as the end of the
 * character. Pressing it is what opens the rest. */
static void frames_more(rect_t area, int cell, int gap, int rows, int per) {
    if (rows < 1 || per < 1) return;
    rect_t c = rc(area.x + (per - 1) * (cell + gap),
                  area.y + (rows - 1) * (cell + gap), cell, cell);
    gfx_fill(c, WHITE);
    gfx_frame(c, 2, 0xA8);
    int w = gfx_text_w(cell / 2, "\342\200\246");
    gfx_text(c.x + (c.w - w) / 2, c.y + c.h / 2 + cell / 6, cell / 2, 0x70,
             "\342\200\246");
}

/* One character's answer: the character itself, plainly and in black, with
 * its stroke order laid out beside it. Pressing either opens that character
 * on its own, which is the way out when a long word or a many-stroked one
 * leaves no room to show every frame here.
 *
 * Returns the height used, or 0 if it would not fit above `ybot`. */
#define CHAR_ROW_H 220   /* the character cell beside a stroke row */

static int stroke_row(int y, int ybot, const strokes_t *st, int idx) {
    const int CH = CHAR_ROW_H, cell = 130, gap = 10;
    if (y + CH > ybot) return 0;

    rect_t c = rc(24, y, CH, CH);
    ui_cell(c);
    ui_char(c, st, -1, -1, BLACK);
    /* On a word's page the character itself opens that character's page,
     * its readings and meanings (it opened only the
     * strokes); its stroke frames still open the stroke view. */
    hotreg(c, scr == SCR_CHAR && detail_n > 1 ? H_CHARCELL + idx : H_STROKEVIEW + idx);

    int fx = 24 + CH + 20;
    int fw = W - 24 - fx;
    int per = (fw + gap) / (cell + gap);
    if (per < 1) per = 1;

    int want = st->n + 1;                       /* blank cell, then each stroke */
    int rows = (want + per - 1) / per;
    int fit  = (ybot - y + gap) / (cell + gap);
    if (fit < 1) fit = 1;
    if (rows > fit) rows = fit;                 /* the rest is in the focused view */

    int used = ui_frames(rc(fx, y, fw, rows * (cell + gap)), st, cell, gap);
    rect_t fr = rc(fx, y, fw, used * (cell + gap));
    if (used * per < want) frames_more(fr, cell, gap, used, per);
    hotreg(fr, H_STROKEVIEW + idx);

    int h = used * (cell + gap);
    return h > CH ? h : CH;
}

static void draw_write_card(void) {
    /* One size and one position, revealed or not, and shared with the hit
     * test. It used to shrink on reveal, which left the ink -- captured
     * against the larger box -- sitting outside it. */
    int n, cy, size;
    write_geom(&n, &cy, &size);

    newbadge();
    if (keynote[0]) {
        int kw = gfx_text_w(32, keynote);
        gfx_text(W - 40 - kw, 132, 32, 0xA0, keynote);
    }
    gfx_text(40, 168, 56, BLACK, card.pinyin);
    gfx_text_wrap(40, 226, 41, 46, W - 80, 2, 0x40, card.defn);
    if (!no_pen) {
        for (int i = 0; i < n; i++) {
            rect_t c = cellrect(i, n, cy, size);
            ui_cell(c);
            /* Once the answer is out, the model sits faintly under your ink
             * so the two can be compared stroke for stroke.
             *
             * Not while the matcher is on: then the box holds what you
             * actually got right and nothing else. An outline under the pen
             * is a tracing guide, and tracing is not writing. */
            if (revealed && card_outline && !guided_on && i < nstrk)
                ui_char(c, &strk[i], -1, -1, FAINT);
            /* The stroke that was wanted, after three wrong tries. This
             * screen used to draw nothing at all while the matcher refused
             * strokes, which is what "it does not work" looked like. */
            char cch[8];
            if (u8_at(card.headword, i, cch)) guided_overlay(c, cch);
        }
        ui_ink_draw(&ink, INK_W);
    } else if (!revealed) {
        gfx_text(40, cy + 60, 42, GREY, "picture the character, then check");
    }

    if (!revealed) {
        /* Everything on the left. Writing right-handed, the palm covers the
         * lower right of the screen, so nothing tappable goes there. */
        int by = H - 130;
        int bw = (W - 96) / 3;
        btn_t c = { rc(24, by, bw + 40, 100), "check", 0 };
        btn_t p = { rc(24 + bw + 64, by, bw - 40, 100), "later", 0 };
        ui_btn(&c, 1); ui_btn(&p, 0);
        hotreg(c.r, H_CHECK); hotreg(p.r, H_POSTPONE);
        /* Which key does which: forward checks, back puts the card off. */
        if (surf_has_pagekeys()) {
            pagekey_mark(c.r.x + c.r.w - 28, c.r.y + 32, 1, 0x70);
            pagekey_mark(p.r.x + p.r.w - 28, p.r.y + 32, 0, 0x70);
        }
        if (!no_pen) {
            /* Left-hand side: a right-handed writer's hand sits over the right
             * of the page, so a button there is both hidden and easy to hit
             * by accident. */
            int ty = cy + size + 20;
            btn_t cl = { rc(24, ty, 290, 64), "clear", 0 };
            ui_btn(&cl, 0);
            hotreg(cl.r, H_CLEAR);

            /* Below clear, not beside it: same column, same hand. Guided
             * writing can be turned on before the answer is out -- it only
             * holds back the stroke it wants until then. */
            guided_button(rc(24, ty + 74, 290, 64));
            pen_button(rc(24, ty + 148, 290, 64));
            guided_note_at(340, ty + 44, W - 364);
        }
        return;
    }

    /* The verdict gets a band of its own between the box and everything
     * below, or its ascenders climb back inside the box and sit on top of
     * what was just written. Reserved whether or not there is a verdict, so
     * clearing and rewriting does not shift the page. */
    int vtop = no_pen ? cy - 70 : cy + size + 6;
    if (hw_checked) {
        char v[96];
        for (int i = 0; i < n; i++) {
            rect_t r = cellrect(i, n, cy, size);
            if (hw_rank[i] == 0)      snprintf(v, sizeof v, "matches");
            else if (hw_rank[i] > 0)  snprintf(v, sizeof v, "close \302\267 %s?", hw_top[i]);
            else if (hw_top[i][0])    snprintf(v, sizeof v, "reads as %s", hw_top[i]);
            else                      continue;
            int w = gfx_text_w(36, v);
            gfx_text(r.x + (r.w - w) / 2, vtop + 42, 36,
                     hw_rank[i] == 0 ? BLACK : 0x70, v);
        }
    }

    int fy   = vtop + 70;
    int ybot = H - 150;



    /* The box above is still taking ink, so it still needs a way to empty it.
     * Bottom left, clear of where a right hand rests while writing, and in
     * the same order as on the unanswered card. */
    if (!no_pen) {
        int cy2 = H - 130 - 92;
        rect_t cl = rc(24, cy2, 270, 72);
        gfx_round_rect(cl, 12, 3, BLACK, WHITE);
        int lw = gfx_text_w(36, "clear");
        gfx_text(cl.x + (cl.w - lw)/2, cl.y + 48, 36, BLACK, "clear");
        hotreg(cl, H_CLEAR);

        /* One row: clear, guided writing, and the model faintly under your
         * ink -- comparing the two is the point of a checked card, and
         * sometimes taking the model out of the way is the point of having
         * checked it. Called "outline", as it is everywhere else. */
        /* The outline is not offered while guided writing is on: nothing is
         * traced under the pen then, because tracing is not writing. */
        int gx = 24 + 270 + 16;
        int want_ol = !guided_on;
        int nt = (has_pen ? 2 : 1) + want_ol;
        int gw = (W - 24 - gx - 16 * (nt - 1)) / nt;
        int tw_w = want_ol ? (has_pen ? 2 * gw + 16 : gw) : W - 24 - gx;
        writing_toggles(rc(gx, cy2, tw_w, 72));
        if (want_ol) {
            int ox = gx + tw_w + 16;
            toggle_btn(rc(ox, cy2, W - 24 - ox, 72), "outline", card_outline, H_TREVEAL);
        }
        guided_note_at(40, cy2 - 18, W - 80);

        ybot = cy2 - 16;
    }

    /* Stroke order, one row of frames per character. The frames are the thing
     * to read -- they show the order a stroke at a time, for every character
     * of the word, not just the first -- so they get the room. The whole
     * character with numbered strokes goes underneath as the fallback, for
     * when the frames do not fit or one glance is enough. */
    for (int i = 0; i < n; i++) {
        if (strk[i].n <= 0) continue;
        int h = stroke_row(fy, ybot, &strk[i], i);
        if (h == 0) break;
        fy += h + 14;
    }


    draw_examples(fy, ybot, card.headword);
    grade_buttons(H - 130);
}

static void draw_read_card(void) {
    newbadge();
    if (keynote[0]) {
        int kw = gfx_text_w(32, keynote);
        gfx_text(W - 40 - kw, 132, 32, 0xA0, keynote);
    }
    big_word(card.headword, 170, 300);

    if (!revealed) {
        /* A button that says it, rather than "tap to show" printed on the
         * card. The card itself still shows it when pressed. */
        hotreg(rc(0, 160, W, H - 400), H_CHECK);
        int by = H - 130;
        int bw3 = (W - 24 * 4) / 3;
        btn_t l = { rc(24, by, bw3, 100), "later", 0 };
        btn_t p = { rc(24 + bw3 + 24, by, bw3, 100), "\345\206\231 try writing it", 0 };
        btn_t sh = { rc(24 + 2 * (bw3 + 24), by, bw3, 100), "show", 0 };
        ui_btn(&l, 0); ui_btn(&p, 0); ui_btn(&sh, 1);
        hotreg(l.r, H_POSTPONE); hotreg(p.r, H_PRACTICE); hotreg(sh.r, H_CHECK);
        /* back puts it off, forward shows it */
        if (surf_has_pagekeys()) {
            pagekey_mark(l.r.x + l.r.w - 28, l.r.y + 32, 0, 0x70);
            pagekey_mark(sh.r.x + sh.r.w - 28, sh.r.y + 32, 1, 0xC0);
        }
        return;
    }
    gfx_text(40, 700, 52, BLACK, card.pinyin);
    /* Definitions run long -- "to know; to recognize; to be familiar with;
     * to get acquainted with" was being cut off mid-word with half the panel
     * still empty. Give it up to three lines and start the sentences below
     * wherever it actually ended. */
    int fy   = gfx_text_wrap(40, 764, 41, 46, W - 80, 3, 0x40, card.defn);
    int ybot = H - 258;

    /* On a read card the sentences are the point -- stroke frames repeat what
     * the write card already shows, so they only get the space left over, and
     * only if the whole sequence fits rather than a truncated row. */
    fy = draw_examples(fy, ybot, card.headword);
    if (nstrk == 1) {
        int cell = 132, gap = 12;
        int per  = (W - 48 + gap) / (cell + gap);
        int rows = (strk[0].n + 1 + per - 1) / per;
        int need = rows * (cell + gap);
        if (fy + 14 + need <= ybot)
            ui_frames(rc(24, fy + 14, W - 48, need), &strk[0], cell, gap);
    }
    btn_t p = { rc(24, H - 250, W - 48, 96), "\345\206\231 try writing it", 0 };
    ui_btn(&p, 0);
    hotreg(p.r, H_PRACTICE);
    grade_buttons(H - 130);
}

static void draw_review(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char t[300];
    /* Session state first, the list's name last: the title is clipped to
     * fit, and on the Clara a long list name cut "1 to redo" off. */
    const char *md = mode == MODE_WRITE ? "write" : "read";
    if (in_test && sess.total) {
        int done = sess.correct + sess.later;          /* put off is done with */
        int at = done + 1 > sess.total ? sess.total : done + 1;
        size_t l = (size_t)snprintf(t, sizeof t, "%s%s  \302\267  %d/%d", md,
                                    sess.scored ? "" : " ahead", at, sess.total);
        if (sess.repeat > 0 && l < sizeof t)
            l += (size_t)snprintf(t + l, sizeof t - l, "  \302\267  %d to redo", sess.repeat);
        if (l < sizeof t)
            snprintf(t + l, sizeof t - l, "  \302\267  %s", list_label);
    } else
        snprintf(t, sizeof t, "%s  \302\267  %s", md, list_label);
    header(t, 1);
    if (!have_card) {
        const char *m = "nothing due here";
        int w = gfx_text_w(48, m);
        gfx_text((W - w) / 2, H / 2 - 40, 48, GREY, m);
        const char *m2 = "tick a deck on the home screen to draw new cards";
        w = gfx_text_w(38, m2);
        gfx_text((W - w) / 2, H / 2 + 10, 38, 0x80, m2);
        return;
    }
    if (mode == MODE_WRITE) draw_write_card();
    else                    draw_read_card();
}

/* ---- dict, char, pickers ----------------------------------------------- */

static void draw_dict(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("dictionary", 1);

    rect_t f = rc(24, 112, W - 48 - 150, 82);
    gfx_round_frame(f, 14, 3, BLACK);
    gfx_text_clip(46, 112 + 56, 46, f.w - 40, q[0] ? BLACK : GREY,
                  q[0] ? q : "pinyin (hao3), hanzi or english");
    rect_t dr = rc(W - 24 - 134, 112, 134, 82);
    gfx_round_rect(dr, 14, 3, BLACK, WHITE);
    { int w = gfx_text_w(38, "draw");
      gfx_text(dr.x + (dr.w - w)/2, dr.y + 52, 38, BLACK, "draw"); }
    hotreg(dr, H_DRAW);

    int kbh = KB_HEIGHT;
    int y = 210;
    int listbot = H - kbh - 16;
    for (int i = 0; i < nres && y < listbot - 84; i++) {
        rect_t r = rc(24, y, W - 48, 84);
        gfx_text(44, y + 56, 48, BLACK, res[i].simp);
        int hw = gfx_text_w(48, res[i].simp);
        gfx_text(44 + hw + 20, y + 54, 38, 0x50, res[i].pinyin);
        int pw = gfx_text_w(38, res[i].pinyin);
        gfx_text_clip(44 + hw + 32 + pw, y + 54, 38, W - (hw + pw + 140), 0x60, res[i].defn);
        gfx_fill(rc(24, y + 82, W - 48, 1), FAINT);
        hotreg(r, H_ITEM + i);
        y += 84;
    }

    rect_t kb = rc(0, H - kbh, W, kbh);
    ui_keyboard(kb);
    hotreg(kb, H_KEY);
}

static void char_buttons(void);
static void switch_btn(rect_t r, const char *label, int on, int px);
static int  wrap_rows(int px, int maxw, const char *t);
static void draw_char(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header(detail_word[0] ? detail_word : detail_ch, 1);

    /* Where the book has a note on the word, pressed in it: a switch between
     * the note and the dictionary, as the notes page's between sources. */
    int wtop = 118;
    if (detail_bnote[0]) {
        int sw = (W - 48 - 12) / 2;
        rect_t a = rc(24, 128, sw, 60), b = rc(24 + sw + 12, 128, sw, 60);
        switch_btn(a, "book note", detail_bnote_on, 40);
        switch_btn(b, "dictionary", !detail_bnote_on, 40);
        hotreg(a, H_WNOTE);
        hotreg(b, H_WDICT);
        wtop = 208;
    }
    /* The word itself, with each character pressable to open it alone. */
    int feet = 0;                  /* how far right the reading under it goes */
    if (!(detail_bnote_on && detail_bnote[0])) {
        const char *py0 = detail_nents > 0 ? detail_ents[0].pinyin : detail_ci.pinyin;
        feet = py0[0] ? 40 + gfx_text_w(52, py0)
                        + (detail_nents > 0 && (detail_ctx_first || detail_from_book)
                           ? 24 + gfx_text_w(32, detail_from_book ? "this book's list" : "as read here") : 0)
                      : 0;
    }
    int y = big_word(detail_word[0] ? detail_word : detail_ch, wtop,
                     detail_bnote_on && detail_bnote[0] ? 160 : 220, feet) + 34;
    if (detail_bnote_on && detail_bnote[0]) {
        /* the note as the book has it, a paragraph at a time, at the
         * largest size that shows all of it (and cut with "..." past 34) */
        int room = H - 150 - 16 - y;
        if (getenv("HANZI_NO_EXAMPLES")) {
            /* the book's words are not ours to publish */
            gfx_text(40, y + 40, 34, 0x70, "(the book's note is omitted from published screenshots)");
        } else {
            int px = 44;
            auto need = [&](int p) {
                int lh = p * 13 / 10, h = 0;
                for (const char *s = detail_bnote; *s; ) {
                    const char *nl = strchr(s, '\n');
                    std::string para(s, nl ? (size_t)(nl - s) : strlen(s));
                    if (!para.empty()) h += wrap_rows(p, W - 80, para.c_str()) * lh + lh / 3;
                    s = nl ? nl + 1 : s + strlen(s);
                }
                return h;
            };
            while (px > 34 && need(px) > room) px -= 2;
            int lh = px * 13 / 10, yy = y + px;
            for (const char *s = detail_bnote; *s; ) {
                const char *nl = strchr(s, '\n');
                std::string para(s, nl ? (size_t)(nl - s) : strlen(s));
                s = nl ? nl + 1 : s + strlen(s);
                if (para.empty()) continue;
                int left = (H - 150 - 16 - (yy - px)) / lh;
                if (left < 1) break;
                int rows = wrap_rows(px, W - 80, para.c_str());
                /* the last room there is: the rest of the note in it, "..." */
                if (rows > left || (rows == left && *s)) {
                    std::string rest = para;
                    if (*s) { rest += " "; rest += s; }
                    gfx_text_wrap(40, yy, px, lh, W - 80, left, 0x20, rest.c_str());
                    break;
                }
                yy = gfx_text_wrap(40, yy, px, lh, W - 80, rows, 0x20, para.c_str()) + lh / 3;
            }
        }
        toastline();
        char_buttons();
        return;
    }

    example_t dex[2];
    int nd = getenv("HANZI_NO_EXAMPLES") ? 0 : db_examples(detail_key, dex, 2);
    /* Room kept below for the stroke rows and the radical -- not for the
     * example sentences, which only ever get what is left: they were
     * reserved for first, and pushed every reading of 声音 off the page.
     * Asked for every reading, the strokes give way too. */
    int keep = detail_all ? 0
             : detail_n * (CHAR_ROW_H + 14)
               + (detail_n <= 1 && detail_ci.radical[0] ? 48 : 0);

    if (detail_nents > 0) {
        /* Every reading, each with its meaning. The first -- the one it is
         * read with where it was pressed, when that is known -- gets the
         * lines the page can spare; the others one or two each, and every
         * one of them is on the page. */
        for (int i = 0; i < detail_nents; i++) {
            const dictrow_t *e = &detail_ents[i];
            int left = (H - 150) - keep - y;
            int others = detail_nents - 1 - i;
            /* The first reading is always shown. Past it, a reading that
             * will not fit becomes a button that gives them all the page. */
            if (i > 0 && left < 54 + 50 + 70) {
                char more[64];
                snprintf(more, sizeof more, "+ %d more reading%s", others + 1,
                         others ? "s" : "");
                rect_t mb = rc(24, y - 8, gfx_text_w(34, more) + 40, 60);
                gfx_round_rect(mb, 12, 2, 0x90, WHITE);
                gfx_text(mb.x + 20, mb.y + 41, 34, 0x40, more);
                hotreg(mb, H_MOREREAD);
                y += 70;
                break;
            }
            int px = i == 0 ? 52 : 44;
            gfx_text(40, y, px, i == 0 ? BLACK : 0x30, e->pinyin);
            if (i == 0 && (detail_ctx_first || detail_from_book)) {
                int pw = gfx_text_w(px, e->pinyin);
                gfx_text(40 + pw + 24, y - 4, 32, 0x80, detail_from_book ? "this book's list" : "as read here");
            }
            y += i == 0 ? 62 : 54;
            int lines = (left - (i == 0 ? 62 : 54) - others * (54 + 50)) / 50;
            if (lines > (i == 0 ? 9 : 2)) lines = i == 0 ? 9 : 2;
            if (lines < (i == 0 ? 2 : 1)) lines = i == 0 ? 2 : 1;
            y = gfx_text_wrap(40, y, i == 0 ? 42 : 38, 50, W - 80, lines,
                              i == 0 ? 0x40 : 0x60, e->defn) + (others ? 14 : 6);
        }
    } else {
        /* not a dictionary word: what the character table knows */
        const char *py = detail_ci.pinyin, *df = detail_ci.defn;
        if (py[0]) { gfx_text(40, y, 52, BLACK, py); y += 62; }
        if (df[0]) {
            int lines = 3 + ((H - 150) - y - keep) / 50;
            if (lines < 3) lines = 3;
            if (lines > 9) lines = 9;
            y = gfx_text_wrap(40, y, 42, 50, W - 80, lines, 0x40, df) + 6;
        }
    }

    /* On a single character there is room to say more about it. */
    if (detail_n <= 1 && detail_ci.radical[0]) {
        char buf[128];
        snprintf(buf, sizeof buf, "radical %s   \302\267   %d strokes",
                 detail_ci.radical, detail_ci.n_stroke);
        gfx_text(40, y, 39, 0x60, buf); y += 48;
    }

    int ybot = H - 150;
    /* One row per character: the character in black, its stroke order beside
     * it, both pressable for the full view. */
    for (int i = 0; i < detail_n; i++) {
        if (detail_ws[i].n <= 0) continue;
        int h = stroke_row(y, ybot, &detail_ws[i], i);
        if (h == 0) break;
        y += h + 14;
    }

    if (nd && y + 90 < ybot) {
        gfx_fill(rc(24, y, W - 48, 1), FAINT);
        y += 44;
        for (int i = 0; i < nd && y + 90 < ybot; i++) {
            y = ui_text_hl(44, y, W - 88, EX_PX, EX_LINE, dex[i].text, detail_key);
            int bw = gfx_text_w(37, dex[i].book);
            gfx_text(W - 44 - bw, y, 37, 0x90, dex[i].book);
            y += EX_TAIL;
        }
    }
    toastline();
    char_buttons();
}

/* The word page's practise and add, under the dictionary or the book's note.
 * Add names what it adds, cut with "…" where that is longer than the button
 * holds at 34px (ui_btn shrinks no further than 28, and a glossed phrase of
 * twenty characters ran out of both sides); a phrase with marks that is no
 * word has no add (detail_add). */
static void char_buttons(void) {
    int by = H - 130;
    int bw = (W - 72) / 2;
    btn_t p = { rc(24, by, bw, 100), "\345\206\231 practise", 0 };
    ui_btn(&p, 1);
    hotreg(p.r, H_PRACTICE);
    if (!detail_add[0]) return;
    char ab[96];
    snprintf(ab, sizeof ab, "add %s", detail_add);
    int n = u8_len(detail_add);
    while (n > 1 && gfx_text_w(34, ab) > bw - 24) {
        n--;
        size_t len = 0;
        for (int i = 0; i < n; i++) { char c[8]; len += (size_t)u8_at(detail_add, i, c); }
        snprintf(ab, sizeof ab, "add %.*s\342\200\246", (int)len, detail_add);
    }
    btn_t a = { rc(24 + bw + 24, by, bw, 100), ab, 0 };
    ui_btn(&a, 1);
    hotreg(a.r, H_ADD);
}

static void draw_pick(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char t[200];
    snprintf(t, sizeof t, "add %s to", detail_add[0] ? detail_add : detail_ch);
    header(t, 1);

    rect_t nl = rc(24, 112, W - 48, 82);
    gfx_frame(nl, 3, BLACK);
    gfx_text(44, 112 + 56, 41, BLACK, "+ new list");
    hotreg(nl, H_NEWLIST);

    /* Paged, like every other list: without it only the first fifteen of
     * your lists could be chosen, and a Pleco import makes a hundred. The
     * page is its own, not the screen underneath's. */
    int pgy = H - 110, rowsbot = pgy - 14;
    int y = 210, shown = 0;
    if (pick_scroll >= nulists) pick_scroll = 0;
    for (int i = pick_scroll; i < nulists && y + 86 <= rowsbot; i++, shown++) {
        rect_t r = rc(24, y, W - 48, 86);
        gfx_frame(r, 2, GREY);
        gfx_text_clip(44, y + 52, 39, W - 200, BLACK, ulists[i].chapter);
        char buf[32];
        snprintf(buf, sizeof buf, "%d", ulists[i].n);
        int tw = gfx_text_w(38, buf);
        gfx_text(W - 44 - tw, y + 50, 38, 0x70, buf);
        hotreg(r, H_ITEM + i);
        y += 94;
    }
    pick_step = shown > 0 ? shown : 1;
    pager(pgy, nulists, pick_scroll, shown);
    if (nulists == 0)
        gfx_text_fit(44, 260, 39, 32, W - 88, GREY,
                     "no lists yet \342\200\224 make one above");
}

static void draw_newlist(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header(naming_rename ? "rename the list" : "name the list", 1);
    rect_t f = rc(24, 112, W - 48, 82);
    gfx_round_frame(f, 14, 3, BLACK);
    gfx_text(46, 112 + 56, 46, newname[0] ? BLACK : GREY,
             newname[0] ? newname : "type a name");
    rect_t ok = rc(24, 210, W - 48, 84);
    btn_t b = { ok, naming_rename ? "rename" : "create", 0 };
    ui_btn(&b, 1);
    hotreg(ok, H_CHECK);
    /* "a list called ... already exists" is said here, where the name is
     * typed: it was only ever seen on the next screen */
    toast_y = 350;
    toastline();
    rect_t kb = rc(0, H - KB_HEIGHT, W, KB_HEIGHT);
    ui_keyboard(kb);
    hotreg(kb, H_KEY);
}

#define MENU_ROW 112
#define MENU_GAP  14

static void menurow(int y, const char *label, const char *sub, int id) {
    rect_t r = rc(24, y, W - 48, MENU_ROW);
    gfx_round_rect(r, 16, 2, 0xB4, WHITE);
    gfx_text_clip(46, y + (sub && *sub ? 46 : 64), 41, W - 140, BLACK, label);
    if (sub && *sub) gfx_text_clip(46, y + 82, 38, W - 140, 0x70, sub);
    chevron(r);
    hotreg(r, id);
}

static std::string pdf_roots_said(void);   /* with the PDF screens, below */
static void draw_menu(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("menu", 1);

    char sub[160];
    /* Top-aligned, like every other list in the app. This was centred for a
     * while so five rows would not leave the screen empty, but that put the
     * empty half at the top, under the header, where it reads as a screen
     * that failed to draw. Empty space below a short list is ordinary. */
    int y = 138;

    int nw = db_reviewed_count(MODE_WRITE), nr = db_reviewed_count(MODE_READ);
    snprintf(sub, sizeof sub, "%d written, %d read", nw, nr);
    menurow(y, "words you have studied", sub, H_LEARNED); y += MENU_ROW + MENU_GAP;

    int nul = 0, nue = 0;
    db_user_list_totals(&nul, &nue);
    snprintf(sub, sizeof sub, "%d list%s, %d word%s \302\267 rename, prune, export",
             nul, nul == 1 ? "" : "s", nue, nue == 1 ? "" : "s");
    menurow(y, "my lists", sub, H_MYLISTS); y += MENU_ROW + MENU_GAP;

    {   /* the PDFs on the device: where they are looked for, and the pins */
        size_t np = db_pdf_pins().size();
        snprintf(sub, sizeof sub, "in %s \302\267 %zu pinned", pdf_roots_said().c_str(), np);
        menurow(y, "PDFs", sub, H_PDFS); y += MENU_ROW + MENU_GAP;
    }

    menurow(y, "import from Kobo",
            "your saved lookups and highlights", H_KOBO); y += MENU_ROW + MENU_GAP;

    snprintf(sub, sizeof sub, "drop .txt exports in %s/", pleco_dir);
    menurow(y, "import Pleco lists", sub, H_PLECO); y += MENU_ROW + MENU_GAP;

    snprintf(sub, sizeof sub,
             "new cards \345\206\231 %d \302\267 \350\257\273 %d per day  \302\267  texts, light",
             db_new_limit(MODE_WRITE), db_new_limit(MODE_READ));
    menurow(y, "settings", sub, H_SETTINGS); y += MENU_ROW + MENU_GAP;

    {   /* seldom wanted, so small and last: a button, not a row */
        const char *lb = "home order";
        int bw = gfx_text_w(36, lb) + 64;
        rect_t r = rc(W - 24 - bw, y + 12, bw, 72);
        gfx_round_rect(r, 12, 2, 0xB4, WHITE);
        gfx_text(r.x + 32, r.y + 48, 36, 0x50, lb);
        hotreg(r, H_CATORDER);
    }

    toastline();
}

/* The order of home's categories: each row moves up or down a place, and
 * the order is kept at once. Every category is a row here, whether or not
 * home has a book in it to show. */
static void draw_catorder(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("home order", 1);
    cat_order_load();
    int y = 138, rh = 96, bw = 130;
    for (int i = 0; i < NCATEGORY; i++, y += rh + 14) {
        rect_t r = rc(24, y, W - 48, rh);
        gfx_frame(r, 2, GREY);
        gfx_text_clip(46, y + 62, 44, W - 46 - 2 * (bw + 16) - 40, BLACK, CATEGORY[cat_order[i]]);
        /* none where it would do nothing: the first up, the last down */
        if (i > 0) {
            btn_t u = { rc(W - 24 - 2 * (bw + 12), y + 12, bw, rh - 24), "up", 0 };
            ui_btn(&u, 0);
            hotreg(u.r, H_GO_W + i);
        }
        if (i + 1 < NCATEGORY) {
            btn_t d = { rc(W - 24 - (bw + 12), y + 12, bw, rh - 24), "down", 0 };
            ui_btn(&d, 0);
            hotreg(d.r, H_GO_R + i);
        }
    }
    toastline();
}

static void draw_learned(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("words you have studied", 1);

    const char *fl = rev_filter == MODE_WRITE ? "\345\206\231 write only"
                   : rev_filter == MODE_READ  ? "\350\257\273 read only" : "both";
    char buf[128];
    snprintf(buf, sizeof buf, "%d total  \302\267  %s", rev_total, fl);
    gfx_text_fit(40, 140, 38, 32, W - 294, 0x60, buf);

    rect_t fb = rc(W - 254, 106, 230, 62);
    btn_t bf = { fb, "filter", 0 };
    ui_btn(&bf, 0);
    hotreg(fb, H_FILTER);

    int by      = H - 130;
    int pgy     = by - 92;
    int rowsbot = pgy - 14;

    int y = 186;
    int shown = 0;
    long t = today();
    for (int i = 0; i < nrevs && y + 74 <= rowsbot; i++, shown++) {
        gfx_text(40, y + 48, 46, BLACK, revs[i].headword);
        int hw = gfx_text_w(46, revs[i].headword);
        gfx_text(40 + hw + 18, y + 46, 38, 0x50, revs[i].pinyin);

        long d = revs[i].due - t;
        if (d < 0) d = 0;
        if (d >= 365)     snprintf(buf, sizeof buf, "%s  %ldy  \303\227%d",
                                   revs[i].mode == MODE_WRITE ? "\345\206\231" : "\350\257\273",
                                   d / 365, revs[i].reps);
        else if (d >= 30) snprintf(buf, sizeof buf, "%s  %ldmo  \303\227%d",
                                   revs[i].mode == MODE_WRITE ? "\345\206\231" : "\350\257\273",
                                   d / 30, revs[i].reps);
        else              snprintf(buf, sizeof buf, "%s  %ldd  \303\227%d",
                                   revs[i].mode == MODE_WRITE ? "\345\206\231" : "\350\257\273",
                                   d, revs[i].reps);
        int tw = gfx_text_w(38, buf);
        gfx_text(W - 44 - tw, y + 52, 38, 0x60, buf);
        gfx_fill(rc(24, y + 80, W - 48, 1), FAINT);
        y += 82;
    }
    if (nrevs == 0)
        gfx_text(44, 250, 39, GREY, "nothing studied yet");

    page_step = shown > 0 ? shown : 1;
    pager(pgy, rev_total, scroll, shown);
}

/* Past the end -- the last rows of the last page removed -- back to the
 * last page that has any, rather than an empty page saying "31-30 of 30". */
static void last_page_with_rows(int total) {
    int step = page_step > 0 ? page_step : 1;
    while (scroll >= total && scroll > 0) scroll -= step;
    if (scroll < 0) scroll = 0;
}

static void load_learned(void) {
    rev_total = db_reviewed_count(rev_filter);
    last_page_with_rows(rev_total);   /* the last word gone from the last page */
    nrevs = db_reviewed(rev_filter, scroll, MAXREV, revs);
}

static void draw_mylists(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("my lists", 1);

    nulists = db_user_lists(ulists, MAXLISTS);

    int by      = H - 130;
    int pgy     = by - 92;
    /* a toast gets a line of its own above the pager, not on top of it */
    int rowsbot = pgy - 14 - (toast[0] ? 56 : 0);
    if (toast[0]) toast_y = pgy - 20;

    int y = 166;
    int shown = 0;
    char buf[96];
    for (int i = scroll; i < nulists && y + ROW_H_LIST <= rowsbot; i++, shown++) {
        rect_t r = rc(24, y, W - 48, 86);
        gfx_frame(r, 2, GREY);
        hotreg(r, H_ITEM + i);      /* row first: the ticks must shadow it */
        tick_pair(y, 86, ulists[i].enabled, H_TICK, i);
        gfx_fill(rc(TICK_END, y + 8, 1, 62), FAINT);
        gfx_text_clip(TICK_END + 20, y + 52, 39, W - TICK_END - 190, BLACK, ulists[i].chapter);
        snprintf(buf, sizeof buf, "%d", ulists[i].n);
        int tw = gfx_text_w(38, buf);
        gfx_text(W - 96 - tw, y + 50, 38, 0x70, buf);
        chevron(r);
        y += ROW_H_LIST;
    }
    if (nulists == 0)
        gfx_text_fit(44, 220, 39, 32, W - 88, GREY,
                 "no lists yet \342\200\224 add words from the dictionary or import some");

    page_step = shown > 0 ? shown : 1;
    pager(pgy, nulists, scroll, shown);

    snprintf(buf, sizeof buf, "export all to %s/", export_dir);
    rect_t eb = rc(24, by, W - 48, 100);
    btn_t be = { eb, buf, 0 };
    ui_btn(&be, 0);
    hotreg(eb, H_EXPORT);
    toastline();
}

static void load_uents(void) {
    uent_total = db_user_entry_count(cur_ulist);
    last_page_with_rows(uent_total);   /* the last word gone from the last page */
    nuents = db_user_entries(cur_ulist, scroll, 64, uents);
}

static void draw_listview(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header(cur_uname, 1);
    char buf[128];
    snprintf(buf, sizeof buf, "%d words  \302\267  tap one to remove it", uent_total);
    gfx_text_fit(40, 140, 41, 32, W - 80, 0x70, buf);

    int by      = H - 130;
    int pgy     = by - 92;
    int rowsbot = pgy - 14 - (toast[0] ? 56 : 0);     /* room for a toast */
    if (toast[0]) toast_y = pgy - 20;

    int y = 176;
    int shown = 0;
    for (int i = 0; i < nuents && y + 82 <= rowsbot; i++, shown++) {
        rect_t r = rc(24, y, W - 48, 80);
        gfx_text(44, y + 54, 46, BLACK, uents[i].simp);
        int hw = gfx_text_w(46, uents[i].simp);
        gfx_text(44 + hw + 18, y + 52, 38, 0x50, uents[i].pinyin);
        int pw = gfx_text_w(38, uents[i].pinyin);
        gfx_text_clip(44 + hw + 30 + pw, y + 52, 38, W - (hw + pw + 150), 0x60,
                      uents[i].defn);
        gfx_fill(rc(24, y + 80, W - 48, 1), FAINT);
        hotreg(r, H_ITEM + i);
        y += 82;
    }
    if (nuents == 0) gfx_text(44, 240, 39, GREY, "empty");

    page_step = shown > 0 ? shown : 1;
    pager(pgy, uent_total, scroll, shown);

    int bw = (W - 72) / 2;
    btn_t br = { rc(24, by, bw, 100), "rename", 0 };
    btn_t bd = { rc(24 + bw + 24, by, bw, 100),
                 delete_armed ? "press again to delete" : "delete list", delete_armed };
    ui_btn(&br, 0); ui_btn(&bd, 0);
    hotreg(br.r, H_RENAME); hotreg(bd.r, H_DELETE);
    toastline();
}

static void draw_settings(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("settings", 1);

    int y = 130;     /* close under the header: the Clara needs the room (comic pictures row) */
    char buf[96];
    for (int m = 0; m < 2; m++) {
        int lim  = db_new_limit(m);
        int used = db_new_today(m, today());
        rect_t box = rc(24, y, W - 48, 170);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);

        gfx_text(46, y + 56, 46, BLACK,
                 m == MODE_WRITE ? "\345\206\231 new words a day" : "\350\257\273 new words a day");
        snprintf(buf, sizeof buf, "%d introduced today", used);
        gfx_text(46, y + 104, 37, 0x70, buf);
        snprintf(buf, sizeof buf, "%d", lim);
        stepper(y + 85, buf, m == MODE_WRITE ? H_WMINUS : H_RMINUS,
                             m == MODE_WRITE ? H_WPLUS  : H_RPLUS);
        y += 190;
    }

    {   /* editorial punctuation in texts */
        rect_t box = rc(24, y, W - 48, 130);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);
        gfx_text(46, y + 54, 41, BLACK, "punctuation in texts");
        gfx_text_clip(46, y + 96, 37, W - 260, 0x70,
                      show_punct ? "commas and stops are shown"
                                 : "hidden, as the text was written");
        rect_t t = rc(W - 190, y + 26, 150, 78);
        gfx_round_rect(t, 14, 3, BLACK, show_punct ? BLACK : WHITE);
        const char *lb = show_punct ? "on" : "off";
        int lw = gfx_text_w(39, lb);
        gfx_text(t.x + (t.w - lw)/2, t.y + 52, 39, show_punct ? WHITE : BLACK, lb);
        hotreg(t, H_PUNCT);
        y += 150;
    }

    {   /* where a text's pinyin comes from */
        rect_t box = rc(24, y, W - 48, 130);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);
        gfx_text(46, y + 54, 41, BLACK, "pinyin in texts");
        gfx_text_clip(46, y + 96, 37, W - 330, 0x70,
                      text_readings ? "the book's own, else a guess"
                                    : "the dictionary's first reading");
        rect_t t = rc(W - 260, y + 26, 220, 78);
        gfx_round_rect(t, 14, 3, BLACK, text_readings ? BLACK : WHITE);
        const char *lb = text_readings ? "book" : "dictionary";
        int lw = gfx_text_w(39, lb);
        gfx_text(t.x + (t.w - lw)/2, t.y + 52, 39, text_readings ? WHITE : BLACK, lb);
        hotreg(t, H_READINGS);
        y += 150;
    }

    {   /* a comic's picture size, and whether its caption may split across pages */
        rect_t box = rc(24, y, W - 48, 130);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);
        gfx_text(46, y + 54, 41, BLACK, "comic pictures");
        static const char *const lab[3] = { "large", "medium", "small" };
        int bw = 118, gap = 8, nb = 4;
        int bx = W - 24 - 16 - nb * bw - (nb - 1) * gap;
        gfx_text_clip(46, y + 96, 37, bx - 46 - 12, 0x70, "size, and caption split");
        for (int m = 0; m < 3; m++)
            toggle_btn(rc(bx + m * (bw + gap), y + 26, bw, 78), lab[m], m == comic_size, H_CSIZE0 + m);
        toggle_btn(rc(bx + 3 * (bw + gap), y + 26, bw, 78), "split", comic_split, H_CSPLIT);
        y += 150;
    }

    {   /* how large a text is set */
        rect_t box = rc(24, y, W - 48, 130);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);
        gfx_text(46, y + 54, 41, BLACK, "text size");
        char lb[64];
        snprintf(lb, sizeof lb, "%d%%", text_scale);
        gfx_text_clip(46, y + 96, 37, W - 400, 0x70,
                      "how large a book is set to read");
        stepper(y + 65, lb, H_TXMINUS, H_TXPLUS);
        y += 150;
    }

#ifdef HANZI_DESKTOP
    /* The desktop has no reading light, and the row is the window's: an
     * ordinary window, full screen, or the left or right half of it, kept
     * in user.db and put back at the next start (a --window flag wins for
     * that run). Placing a half needs X11 or XWayland; under native
     * Wayland the window is sized and the compositor places it. The row
     * holds in the mode kept, which is what the next start gets: F11 or
     * a --window flag changes only this run's window. */
    if (surf_light_get() < 0) {
        rect_t box = rc(24, y, W - 48, 130);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);
        gfx_text(46, y + 54, 41, BLACK, "window");
        static const char *const lab[WIN_NMODES] = { "window", "full", "left", "right" };
        int cur = db_window_mode();
        int bw = (W - 48 - 260 - 3 * 10 - 20) / WIN_NMODES;
        if (bw > 150) bw = 150;
        int bx = W - 24 - 20 - WIN_NMODES * bw - (WIN_NMODES - 1) * 10;
        gfx_text_clip(46, y + 96, 37, bx - 46 - 16, 0x70, "the screen it takes");
        for (int m = 0; m < WIN_NMODES; m++)
            toggle_btn(rc(bx + m * (bw + 10), y + 26, bw, 78), lab[m], m == cur, H_WIN0 + m);
        y += 150;
    } else
#endif
    {   /* the reading light, so there is always a way back from a dark
         * screen without leaving the app */
        int lv = surf_light_get();
        rect_t box = rc(24, y, W - 48, 130);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);
        gfx_text(46, y + 54, 41, BLACK, "reading light");
        char lb[64];
        if (lv < 0) snprintf(lb, sizeof lb, "not available on this device");
        else        snprintf(lb, sizeof lb, "%d", lv);
        gfx_text_clip(46, y + 96, 37, W - 400, 0x70,
                      lv < 0 ? lb : "brightness");
        if (lv >= 0) {
            stepper(y + 65, lb, H_LMINUS, H_LPLUS);
        }
        y += 150;
    }

    {   /* forget progress */
        rect_t r = rc(24, y, W - 48, 92);
        gfx_round_rect(r, 16, 2, 0xB4, WHITE);
        gfx_text(46, y + 44, 41, BLACK, "reset progress");
        gfx_text_clip(46, y + 80, 36, W - 140, 0x70,
                      "forget review history, all of it or one book");
        chevron(r);
        hotreg(r, H_RESET);
    }
    toastline();
}

#define MAXWORDS 64
static wordrow_t words[MAXWORDS];
static int  nwords = 0, word_total = 0, words_list = 0;
static char words_label[200] = "";

static void load_words(void) {
    word_total = db_list_word_count(words_list);
    last_page_with_rows(word_total);   /* the last word gone from the last page */
    nwords = db_list_words(words_list, scroll, MAXWORDS, today(), words);
}

static void draw_words(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header(words_label, 1);
    char buf[160];
    snprintf(buf, sizeof buf, "%d words", word_total);
    gfx_text_fit(40, 140, 37, 32, W - 260, 0x70, buf);
    {   /* where this chapter stands */
        rect_t sb = rc(W - 220, 104, 196, 64);
        gfx_round_rect(sb, 12, 3, BLACK, WHITE);
        int sw = gfx_text_w(37, "progress");
        gfx_text(sb.x + (sb.w - sw)/2, sb.y + 44, 37, BLACK, "progress");
        hotreg(sb, H_STATS);
    }

    int by      = H - 130;
    int pgy     = by - 92;
    int rowsbot = pgy - 14;

    int y = 178;
    int shown = 0;
    /* Two lines per word. On one line the definition had to be clipped down
     * to the smallest type on the screen to fit beside the headword; given a
     * line of its own it can be read. */
    const int ROW_W = 120;
    for (int i = 0; i < nwords && y + ROW_W <= rowsbot; i++, shown++) {
        /* two state pips: hollow unseen, outlined learning, filled due */
        for (int m = 0; m < 2; m++) {
            rect_t p = rc(28 + m * 54, y + 16, 48, 48);
            int st = words[i].st[m];
            gfx_round_rect(p, 10, 2, st ? BLACK : 0xC8, st == 2 ? BLACK : WHITE);
            int w = gfx_text_w(38, m ? "\350\257\273" : "\345\206\231");
            gfx_text(p.x + (p.w - w)/2, p.y + 36, 38,
                     st == 2 ? WHITE : (st ? BLACK : 0xC0), m ? "\350\257\273" : "\345\206\231");
        }
        gfx_text(152, y + 56, 56, BLACK, words[i].headword);
        int hw = gfx_text_w(56, words[i].headword);
        gfx_text_clip(152 + hw + 22, y + 52, 42, W - (hw + 200), 0x50,
                      words[i].pinyin);
        gfx_text_clip(46, y + 104, 40, W - 92, 0x60, words[i].defn);
        gfx_fill(rc(24, y + ROW_W - 2, W - 48, 1), FAINT);
        hotreg(rc(24, y, W - 48, ROW_W - 2), H_ITEM + i);
        y += ROW_W;
    }
    if (nwords == 0) gfx_text(44, 240, 38, GREY, "empty");

    page_step = shown > 0 ? shown : 1;
    pager(pgy, word_total, scroll, shown);

    /* Three ways into a chapter now, so they share the width. Study first:
     * it is what you reach for before the list is familiar enough to test. */
    int bw = (W - 96) / 3;
    btn_t st = { rc(24, by, bw, 100), "study", 0 };
    btn_t a  = { rc(24 + bw + 24, by, bw, 100), "\345\206\231 start", 0 };
    btn_t b  = { rc(24 + 2 * (bw + 24), by, bw, 100), "\350\257\273 start", 0 };
    ui_btn(&st, 1); ui_btn(&a, 0); ui_btn(&b, 0);
    hotreg(st.r, H_STUDY); hotreg(a.r, H_GO_W); hotreg(b.r, H_GO_R);
}

/* ---- handwriting search ------------------------------------------------ */

#define HW_CANDS 12
static ink_t     hw_ink;
static hwcand_t  hw_cand[HW_CANDS];
static int       hw_n = 0;

static char hw_built[64] = "";   /* characters accepted so far */

/* Where `back` leaves the draw screen: the dictionary when it was opened to
 * search, the home screen when it was opened from there. */
static int draw_back = SCR_DICT;

static rect_t hw_pad(void) {
    int size = W - 48;
    if (size > 760) size = 760;
    /* Clear of the line above it: at 150 the box sat almost on the text. */
    return rc((W - size) / 2, 210, size, size);
}

static void hw_recognise(void) {
    unsigned char sig[HW_MAX_STROKES * 6];
    int n = ui_ink_signature(&hw_ink, sig, HW_MAX_STROKES);
    hw_n = n > 0 ? db_hw_search(sig, n, hw_cand, HW_CANDS) : 0;
}

/* The candidates for one character, as the draw pad shows them and as the
 * PDF page shows a character read or drawn there: boxes of CAND_BOX px, as
 * many to a row as `w` from `x` holds, the best first and framed black, each
 * H_CAND + i. At most `maxrows` rows (0: as many as it takes). Returns the
 * y under the last row; one place, so the two cannot drift apart. */
#define CAND_BOX 128
#define CAND_GAP 12
static int cand_rows(int w, int n) {
    int per = (w + CAND_GAP) / (CAND_BOX + CAND_GAP);
    if (per < 1) per = 1;
    return (n + per - 1) / per;
}
static int cand_grid(int x, int y, int w, const hwcand_t *c, int n, int maxrows = 0) {
    int per = (w + CAND_GAP) / (CAND_BOX + CAND_GAP);
    if (per < 1) per = 1;
    if (n > HW_CANDS) n = HW_CANDS;
    if (maxrows > 0 && n > per * maxrows) n = per * maxrows;
    for (int i = 0; i < n; i++) {
        int r = i / per, col = i % per;
        rect_t b = rc(x + col * (CAND_BOX + CAND_GAP), y + r * (CAND_BOX + CAND_GAP), CAND_BOX, CAND_BOX);
        gfx_round_rect(b, 12, 2, i == 0 ? BLACK : 0xA8, WHITE);
        int tw = gfx_text_w(80, c[i].ch);
        gfx_text(b.x + (b.w - tw) / 2, b.y + b.h / 2 + 30, 80, BLACK, c[i].ch);
        hotreg(b, H_CAND + i);
    }
    return n > 0 ? y + cand_rows(w, n) * (CAND_BOX + CAND_GAP) - CAND_GAP : y;
}

static void draw_draw(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("draw", 1);

    /* What has been accepted so far. A word is drawn a character at a time,
     * so this is the part being built up and it wants to be readable. */
    if (hw_built[0]) gfx_text(40, 168, 64, BLACK, hw_built);

    rect_t pad = hw_pad();
    ui_cell(pad);
    ui_ink_draw(&hw_ink, INK_W);

    int y = pad.y + pad.h + 26;
    if (hw_n > 0) {
        cand_grid(24, y, W - 48, hw_cand, hw_n);
    } else if (hw_ink.n > 0) {
        gfx_text(44, y + 46, 40, GREY, "no match \342\200\224 try again");
    } else {
        gfx_text(44, y + 46, 40, GREY, "draw in the box above");
    }

    /* Four controls, one row: the same set every writing surface has, plus
     * the search that this box exists to feed. */
    int by  = H - 130;
    int nb  = has_pen ? 4 : 3;
    int bw3 = (W - 24 * (nb + 1)) / nb;
    int x   = 24;

    btn_t u = { rc(x, by, bw3, 100), "undo", 0 };
    ui_btn(&u, 0); hotreg(u.r, H_UNDO);
    x += bw3 + 24;

    btn_t c = { rc(x, by, bw3, 100), hw_built[0] ? "start over" : "clear", 0 };
    ui_btn(&c, 0); hotreg(c.r, H_CLEAR);
    x += bw3 + 24;

    /* The same toggle, and the same words, as every other writing screen. */
    if (has_pen) {
        pen_button(rc(x, by, bw3, 100));
        x += bw3 + 24;
    }

    char sb[64];
    /* With nothing accepted this is the way back, not a dead search button. */
    snprintf(sb, sizeof sb, hw_built[0] ? "search %s" : "done", hw_built);
    btn_t d = { rc(x, by, bw3, 100), sb, 0 };
    ui_btn(&d, hw_built[0] ? 1 : 0);
    hotreg(d.r, H_CHECK);
}

static int reset_armed = -2;    /* -2 none, -1 everything, >=0 book index */

static void draw_reset(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("reset progress", 1);
    gfx_text_clip(40, 140, 37, W - 80, 0x70,
                  "tap once to see the count, again to confirm");

    int y = 196;
    char buf[200];

    rect_t all = rc(24, y, W - 48, 92);
    int armed_all = reset_armed == -1;
    gfx_round_rect(all, 16, armed_all ? 4 : 2, armed_all ? BLACK : 0xB4, WHITE);
    if (armed_all) {
        snprintf(buf, sizeof buf, "tap again to forget %d cards",
                 db_reset_count(NULL, -1));
        gfx_text(46, y + 40, 41, BLACK, "everything");
        gfx_text_clip(46, y + 76, 36, W - 120, BLACK, buf);
    } else {
        gfx_text(46, y + 58, 41, BLACK, "everything");
    }
    hotreg(all, H_RESET_ALL);
    y += 108;

    /* Paged: seventeen books do not fit, and the ones past the edge --
     * 论语, 道德经, "My lists" -- could not be reset on their own. */
    int pgy = H - 110, shown = 0;
    if (reset_scroll >= nbooks) reset_scroll = 0;
    for (int i = reset_scroll; i < nbooks && y + 92 <= pgy - 14; i++, shown++) {
        rect_t r = rc(24, y, W - 48, 92);
        int armed = reset_armed == i;
        gfx_round_rect(r, 16, armed ? 4 : 2, armed ? BLACK : 0xB4, WHITE);
        gfx_text_clip(46, armed ? y + 40 : y + 58, 39, W - 120, BLACK, books[i].name);
        if (armed) {
            snprintf(buf, sizeof buf, "tap again to forget %d cards",
                     db_reset_count(books[i].name, -1));
            gfx_text_clip(46, y + 76, 36, W - 120, BLACK, buf);
        }
        hotreg(r, H_ITEM + i);
        y += 100;
    }
    reset_step = shown > 0 ? shown : 1;
    pager(pgy, nbooks, reset_scroll, shown);
    toastline();
}

static void draw_done(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("test finished", 1);

    char buf[160];
    int y = H / 2 - 190;
    const char *title = sess.total ? "\345\256\214 done" : "nothing to test";
    int w = gfx_text_w(64, title);
    gfx_text((W - w) / 2, y, 64, BLACK, title);
    y += 90;

    if (sess.total) {
        /* Put off is neither: it counted as right, and total - correct was
         * 0 at the end of every test, redone or not -- so this said "all
         * correct first time" whatever happened. */
        snprintf(buf, sizeof buf, "%d %s", sess.total - sess.later,
                 mode == MODE_WRITE ? "written" : "read");
        w = gfx_text_w(42, buf);
        gfx_text((W - w) / 2, y, 42, 0x40, buf);
        y += 60;
        size_t l = 0;
        buf[0] = 0;
        if (sess.wrong > 0) l = (size_t)snprintf(buf, sizeof buf, "%d needed a second go", sess.wrong);
        else if (sess.total > sess.later) l = (size_t)snprintf(buf, sizeof buf, "all correct first time");
        if (sess.later > 0 && l < sizeof buf)
            snprintf(buf + l, sizeof buf - l, "%s%d for later", l ? "  \302\267  " : "", sess.later);
        w = gfx_text_w(38, buf);
        gfx_text((W - w) / 2, y, 38, 0x70, buf);
    } else {
        const char *m = "nothing due, and today's new cards are done";
        w = gfx_text_w(38, m);
        gfx_text((W - w) / 2, y, 38, 0x80, m);
    }

    int size  = mode == MODE_WRITE ? TEST_WRITE : TEST_READ;
    int ahead = db_ahead_available(list_id, scope_book[0] ? scope_book : NULL,
                                   mode, today());
    int fresh = db_new_available(list_id, scope_book[0] ? scope_book : NULL,
                                 mode, today());
    int by = H - 130;
    int ry = by - 116;

    /* Out of cards on a day that is going well: take more new material rather
     * than re-reading what is already known. Offered first, because it is the
     * one that actually makes progress -- and it is scored, unlike reviewing
     * ahead, because these are cards you have genuinely not seen. */
    if (fresh > 0) {
        snprintf(buf, sizeof buf, "learn %d new  \302\267  past today's limit",
                 fresh < size ? fresh : size);
        rect_t r = rc(24, ry, W - 48, 100);
        btn_t e = { r, buf, 0 };
        ui_btn(&e, 1);
        hotreg(r, H_MORENEW);
        ry -= 116;
    }
    if (ahead > 0) {
        snprintf(buf, sizeof buf, "review %d ahead  \302\267  not scored",
                 ahead < size ? ahead : size);
        rect_t r = rc(24, ry, W - 48, 100);
        btn_t e = { r, buf, 0 };
        ui_btn(&e, fresh > 0 ? 0 : 1);
        hotreg(r, H_AHEAD);
    }
    int bw = (W - 72) / 2;
    /* "another test" said nothing about what it would test. It deals the
     * same deck again, which is worth stating on the button. */
    char ab[120];
    snprintf(ab, sizeof ab, "another %s", list_label[0] ? list_label : "test");
    btn_t a = { rc(24, by, bw, 100), ab, 0 };
    btn_t b = { rc(24 + bw + 24, by, bw, 100), "done", 0 };
    ui_btn(&a, 1); ui_btn(&b, 0);
    hotreg(a.r, H_AGAIN_TEST); hotreg(b.r, H_BACK);
}


/* ---- study ------------------------------------------------------------- *
 * The third way through a list, and the one that comes first in practice:
 * no question, no grading, just the word laid out whole -- the character
 * large, its stroke order under it, then the reading and the meaning. It is
 * the read card with the test taken out, for getting used to words before
 * being asked to recall them.
 *
 * It walks a chapter in order, by the buttons or by the Libra's page keys. */
static int  study_abs = 0;              /* index within the whole list */
static strokes_t study_s[8];
static int  study_ns = 0;
static example_t study_ex[2];
static int  study_nex = 0;

static void study_load(void) {
    for (int i = 0; i < study_ns; i++) db_strokes_free(&study_s[i]);
    study_ns = 0;
    study_nex = 0;
    if (study_abs < scroll || study_abs >= scroll + nwords) return;
    const wordrow_t *w = &words[study_abs - scroll];
    int n = u8_len(w->headword);
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        char ch[8];
        u8_at(w->headword, i, ch);
        db_strokes(ch, &study_s[study_ns]);
        study_ns++;
    }
    study_nex = db_examples(w->headword, study_ex, 2);
}

/* Show the word at `abs`, paging the loaded window if it falls outside. */
static void study_open(int abs) {
    if (abs < 0) abs = 0;
    if (word_total > 0 && abs >= word_total) abs = word_total - 1;
    if (abs < scroll || abs >= scroll + nwords) {
        scroll = (abs / MAXWORDS) * MAXWORDS;
        load_words();
    }
    study_abs = abs;
    study_load();
    scr = SCR_STUDY;
}

static void draw_study(void) {
    nhot = 0;
    gfx_clear(WHITE);

    if (study_abs < scroll || study_abs >= scroll + nwords) {
        header(words_label, 1);
        gfx_text(44, 240, 38, GREY, "no word here");
        return;
    }
    const wordrow_t *w = &words[study_abs - scroll];

    char h[240];
    snprintf(h, sizeof h, "%s  \302\267  %d of %d",
             words_label, study_abs + 1, word_total);
    header(h, 1);

    /* The character, as large as it will go. On a compound, each character
     * is pressable on its own. */
    int y = big_word(w->headword, 150, 260) + 40;

    /* Stroke order underneath it, one row per character while they fit. */
    int cell = 152, gap = 12;
    for (int i = 0; i < study_ns; i++) {
        if (study_s[i].n <= 0) continue;
        int per  = (W - 48 + gap) / (cell + gap);
        int rows = (study_s[i].n + 1 + per - 1) / per;
        int need = rows * (cell + gap);
        if (y + need > H - 430) break;
        y += ui_frames(rc(24, y, W - 48, need), &study_s[i], cell, gap)
             * (cell + gap) + 10;
    }

    /* The reading and the meaning are half of what studying a word is, so
     * they get read-card sizes rather than list-row ones, and clear air
     * between them and the stroke frames above. */
    y += 74;
    gfx_text(40, y, 64, BLACK, w->pinyin);
    y = gfx_text_wrap(40, y + 68, 46, 56, W - 80, 3, 0x40, w->defn);

    /* Sentences, if anything is left of the page. */
    int ybot = H - 250;
    if (study_nex && !getenv("HANZI_NO_EXAMPLES")) {
        y += 10;
        gfx_fill(rc(24, y, W - 48, 1), FAINT);
        y += 48;
        for (int i = 0; i < study_nex && y + EX_LINE + EX_TAIL <= ybot; i++) {
            y = ui_text_hl(44, y, W - 88, EX_PX, EX_LINE, study_ex[i].text,
                           w->headword);
            int bw = gfx_text_w(37, study_ex[i].book);
            gfx_text(W - 44 - bw, y, 37, 0x90, study_ex[i].book);
            y += EX_TAIL;
        }
    } else if (study_nex) {
        y += 10;
        gfx_fill(rc(24, y, W - 48, 1), FAINT);
        gfx_text(44, y + 54, 36, 0xA0,
                 "(example sentences omitted from published screenshots)");
    }

    /* Writing it is the point of studying it, so that button gets the width. */
    btn_t p = { rc(24, H - 240, W - 48, 100), "\345\206\231 try writing it", 0 };
    ui_btn(&p, 0);
    hotreg(p.r, H_PRACTICE);

    int by = H - 124;
    int bw = (W - 72) / 2;
    btn_t pv = { rc(24, by, bw, 100), "prev", 0 };
    btn_t nx = { rc(24 + bw + 24, by, bw, 100), "next", 0 };
    ui_btn(&pv, 0); ui_btn(&nx, 1);
    hotreg(pv.r, H_STUDY_PREV);
    hotreg(nx.r, H_STUDY_NEXT);
}



/* ---- one character's strokes, on their own ------------------------------ *
 * The way out when a word is long or a character has too many strokes to lay
 * out beside it: press the character or its frames on a checked card and it
 * gets the whole page. */

static strokes_t sv_s;
static char sv_ch[8] = "";
static int  sv_back = SCR_REVIEW;

static void open_strokeview(const char *ch) {
    snprintf(sv_ch, sizeof sv_ch, "%s", ch);
    db_strokes_free(&sv_s);
    db_strokes(ch, &sv_s);
    scr = SCR_STROKES;
}

static void draw_strokeview(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char h[120];
    snprintf(h, sizeof h, "%s  \302\267  %d strokes", sv_ch, sv_s.n);
    header(h, 1);

    int top = 130;
    rect_t c = rc((W - 420) / 2, top, 420, 420);
    ui_cell(c);
    ui_char(c, &sv_s, -1, -1, BLACK);

    int y = top + 460;
    int cell = 150, gap = 12;
    int avail = (H - 150) - y;
    if (avail >= cell + gap)
        ui_frames(rc(24, y, W - 48, avail), &sv_s, cell, gap);

    btn_t p = { rc(24, H - 130, W - 48, 100), "\345\206\231 try writing it", 0 };
    ui_btn(&p, 0);
    hotreg(p.r, H_PRACTICE);
}


/* ---- texts -------------------------------------------------------------- *
 * A text is a book whose chapters hold lines rather than words. It is read a
 * clause at a time, and written out the same way -- which is how classical
 * Chinese is actually learnt, and what a word deck cannot express.
 *
 * Three ways through a chapter, sharing one position: read it, practise
 * writing it with the stroke order in front of you, or be given the reading
 * and write it from memory. */

static void text_open(int id, const char *title) {
    /* Another book: the chapter in hand belonged to the last one. Left set,
     * "write this chapter" wrote that book's chapter, and mark_reading saved
     * it as this book's place -- continue reading then opened a chapter of
     * the wrong book. */
    if (id != text_id) {
        tchap_id = 0;
        tchap_idx = 0;
        ntlines = 0;
        tchap_title[0] = 0;
    }
    text_id = id;
    snprintf(text_title, sizeof text_title, "%s", title);
    ntchaps = db_text_chapters(text_id, tchaps, MAXTCHAPS);
    {
        /* each source into the switch of what it gives: English where it
         * has a row with English, notes where it has one with notes; a
         * source with pages and no rows (Richard's 西游记, a chapter at a
         * time) by what its pages are -- English where they translate the
         * chapter (chapter_note_kind_of 2: his translation or his outline),
         * else notes */
        notesrc_t all[MAXNSRC];
        int na = db_note_sources(text_id, all, MAXNSRC);
        n_nsrc = n_esrc = 0;
        for (int i = 0; i < na; i++) {
            int en = db_source_gives(text_id, all[i].key, NOTES_EN);
            int no = db_source_gives(text_id, all[i].key, NOTES_NOTED);
            if (!en && !no && !db_source_gives(text_id, all[i].key, NOTES_ALL)
                && db_source_has_chapter_pages(text_id, all[i].key)) {
                int kind = 0;
                for (int c = 0; c < ntchaps && !kind; c++) kind = chapter_note_kind_of(tchaps[c].id, all[i].key);
                if (kind == 2) en = 1; else no = 1;
            }
            if (en) ensrcs[n_esrc++] = all[i];
            if (no) notesrcs[n_nsrc++] = all[i];
        }
    }
    note_source_use(n_nsrc ? db_note_source_choice(text_id, notesrcs, n_nsrc) : 0);
    en_source_use(n_esrc ? db_en_source_choice(text_id, ensrcs, n_esrc) : 0);
    /* A 连环画's pictures are in its EPUB: on the Kobo, the one in its
     * library; on the desktop, the repository's data/comics (joined from
     * its parts by tools/data.sh join), run from out/ or from the top.
     * The library may hold it as a KEPUB instead ("X.kepub.epub",
     * for Nickel's faster reader): kepubify keeps
     * every picture's name, so either file serves; the .epub is tried first. */
    comic_path[0] = 0;
    comic_apart = 0;
    char file[256];
    db_text_epub(text_id, file, sizeof file);
    if (file[0]) {
        comic_apart = db_text_epub_apart(text_id);
        char kfile[272] = "";
        size_t fl = strlen(file);
        if (fl > 5 && !strcmp(file + fl - 5, ".epub") &&
            !(fl > 11 && !strcmp(file + fl - 11, ".kepub.epub")))
            snprintf(kfile, sizeof kfile, "%.*s.kepub.epub", (int)(fl - 5), file);
        const char *dirs[] = { getenv("HANZI_COMICS_DIR"), "/mnt/onboard/\350\277\236\347\216\257\347\224\273",   /* 连环画/ */
                               "/mnt/onboard", "../data/comics", "data/comics" };
        for (const char *d : dirs) {
            if (!d || !*d) continue;
            char path[600];
            snprintf(path, sizeof path, "%s/%s", d, file);
            if (access(path, R_OK) == 0) { snprintf(comic_path, sizeof comic_path, "%s", path); break; }
            if (kfile[0]) {
                snprintf(path, sizeof path, "%s/%s", d, kfile);
                if (access(path, R_OK) == 0) { snprintf(comic_path, sizeof comic_path, "%s", path); break; }
            }
        }
        /* or one folder down: each series in its own (连环画/红楼梦 上美 1981 16/) */
        const char *top = "/mnt/onboard/\350\277\236\347\216\257\347\224\273";
        if (!comic_path[0]) if (DIR *dp = opendir(top)) {
            while (struct dirent *e = readdir(dp)) {
                if (e->d_name[0] == '.') continue;
                for (const char *f : { (const char *)file, (const char *)kfile }) {
                    char path[600];
                    if (!*f) continue;
                    snprintf(path, sizeof path, "%s/%s/%s", top, e->d_name, f);
                    if (access(path, R_OK) == 0) { snprintf(comic_path, sizeof comic_path, "%s", path); break; }
                }
                if (comic_path[0]) break;
            }
            closedir(dp);
        }
        if (!comic_path[0]) fprintf(stderr, "comics: %s not found\n", file);
    }
    scroll = 0;
    scr = SCR_TEXTCH;
}

static void tchap_open(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= ntchaps) idx = ntchaps - 1;
    if (ntchaps <= 0) return;
    tchap_idx = idx;
    tchap_id  = tchaps[idx].id;
    /* Which edition this is, the header's own button says. */
    snprintf(tchap_title, sizeof tchap_title, "%s", tchaps[idx].title);
    ntlines = db_text_lines(tchap_id, tlines, MAXTLINES);
    tread_np = 0;                       /* a new chapter starts at its top */
    tread_start[0] = 0;
    tread_key = { tchap_id, text_scale, show_punct, text_readings };
    ntplates = db_text_plates(tchap_id, tplates, MAXPLATES);
    cplate = 0;
    csub = 0;
}

/* The longest run of characters starting at `idx` that the dictionary knows,
 * up to four. Pressing 无名天地之始 should open 天地, not 天. */
static void longest_word(const char *line, int idx, char *out, size_t outn) {
    out[0] = 0;
    int n = u8_len(line);
    for (int len = 4; len >= 1; len--) {
        if (idx + len > n) continue;
        char w[32] = "";
        for (int k = 0; k < len; k++) {
            char ch[8];
            u8_at(line, idx + k, ch);
            strncat(w, ch, sizeof w - strlen(w) - 1);
        }
        if (len == 1 || db_word_lookup(w, NULL, 0, NULL, 0)) {
            snprintf(out, outn, "%s", w);
            return;
        }
    }
}


static int is_lookup(const char *ch);
/* The word the character at `idx` is in: its run of characters (to the
 * marks either side) cut from the front into the longest words the
 * dictionary knows, as longest_word takes them, and the one holding idx.
 * Pressing the 地 of 无名天地之始 opens 天地; longest_word from 地 alone
 * would open 地之. Returns where the word starts. */
static int word_at(const char *line, int idx, char *out, size_t outn) {
    out[0] = 0;
    int s = idx;
    while (s > 0) {
        char c[8];
        u8_at(line, s - 1, c);
        if (!is_lookup(c)) break;
        s--;
    }
    while (s <= idx) {
        char w[32];
        longest_word(line, s, w, sizeof w);
        int len = u8_len(w);
        if (len < 1) len = 1;
        if (s + len > idx) { snprintf(out, outn, "%s", w); return s; }
        s += len;
    }
    longest_word(line, idx, out, outn);
    return idx;
}


/* Chapters of a text. */
/* How far a chapter is read and written. They are separate places in it,
 * so say both when both have happened, and neither when neither has. */
static void chapter_label(const textchap_t *c, char *buf, size_t n) {
    /* -1 is never; a place saved at line 0 is the whole of a one-line
     * chapter, which by "> 0" never showed as read or written */
    int rd = db_text_read_saved(c->id);
    int wr = db_text_pos_saved(c->id);
    int nl = c->n_line;
    const char *ls = nl == 1 ? "" : "s";
    int last   = nl - 1;
    int done_r = rd >= 0 && rd >= last;
    int done_w = wr >= 0 && wr >= last;
    if (done_r && done_w)
        snprintf(buf, n, "read and written");
    else if (rd > 0 && wr > 0)
        snprintf(buf, n, "read %d \302\267 written %d of %d", rd + 1, wr + 1, nl);
    else if (done_r)
        snprintf(buf, n, "%d line%s \302\267 read", nl, ls);
    else if (rd > 0)
        snprintf(buf, n, "read %d of %d", rd + 1, nl);
    else if (done_w)
        snprintf(buf, n, "%d line%s \302\267 written", nl, ls);
    else if (wr > 0)
        snprintf(buf, n, "written %d of %d", wr + 1, nl);
    else
        snprintf(buf, n, "%d line%s", nl, ls);
}

/* ---- exporting a book (the desktop only) ---------------------------------- *
 * A text as an EPUB or a PDF to read in another reader: tools/export_book.py
 * does it (python3 + Pillow), against the same content.db, with the pinyin the
 * app shows and the English of the source chosen here. A file chooser (zenity,
 * else kdialog) asks where to save it each time; with neither, it goes to
 * ~/Documents/exports/ and the toast says so. The Kobo build has none of it.
 * A 连环画 (it has an EPUB of its own) is not offered. */
#ifdef HANZI_DESKTOP
static void redraw(void);
static int ex_fmt = 0;        /* 0 EPUB, 1 PDF */
static int ex_mode = 0;       /* 0 Chinese, 1 + pinyin, 2 + English */
static int ex_src = 0;        /* into ensrcs */
static int ex_scope = 0;      /* 0 the whole book, 1 the chapter */
static int ex_chap = 0;       /* index into tchaps */
static int ex_harness = 0;    /* app_shot: never run a chooser or a script */

static int export_offered(void) {
    char file[64];
    db_text_epub(text_id, file, sizeof file);
    return !file[0] && ntchaps > 0;
}

static void export_open(void) {
    ex_src = n_esrc ? (cur_esrc < n_esrc ? cur_esrc : 0) : 0;
    if (!n_esrc && ex_mode == 2) ex_mode = 1;
    ex_chap = 0;
    int want = tchap_id ? tchap_id : db_text_last_chapter(text_id);
    for (int k = 0; k < ntchaps; k++)
        if (tchaps[k].id == want) { ex_chap = k; break; }
    toast[0] = 0;
    scr = SCR_EXPORT;
}

static int export_selected(int id) {
    if (id == (ex_fmt ? H_EX_PDF : H_EX_EPUB)) return 1;
    if (id == H_EX_M0 + ex_mode) return 1;
    if (id == (ex_scope ? H_EX_CHAP : H_EX_BOOK)) return 1;
    return ex_mode == 2 && id == H_ENSRC + ex_src;
}

static void draw_export(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("export book", 1);
    gfx_text_clip(40, 146, 40, W - 80, BLACK, text_title);
    int y = 176, bh = 84, g = 16, x0 = 24, fw = W - 48, hw = (fw - g) / 2;
    auto lead = [&](const char *s) { gfx_text(40, y + 30, 34, 0x70, s); y += 44; };
    lead("format");
    toggle_btn(rc(x0, y, hw, bh), "EPUB", !ex_fmt, H_EX_EPUB);
    toggle_btn(rc(x0 + hw + g, y, hw, bh), "PDF", ex_fmt, H_EX_PDF);
    y += bh + 26;
    lead("pages");
    static const char *ML[3] = { "Chinese only", "Chinese + pinyin", "Chinese + pinyin + English" };
    for (int m = 0; m < 3; m++) {
        rect_t r = rc(x0, y, fw, bh);
        if (m == 2 && !n_esrc) {
            gfx_round_rect(r, 12, 2, 0xB4, WHITE);
            gfx_text_clip(r.x + 24, r.y + 56, 34, r.w - 48, 0x90, "Chinese + pinyin + English  (no English here)");
        } else {
            toggle_btn(r, ML[m], ex_mode == m, H_EX_M0 + m);
        }
        y += bh + 12;
    }
    y += 14;
    if (ex_mode == 2 && n_esrc) {
        lead("English from");
        for (int i = 0; i < n_esrc; i++) {
            int col = i % 2;
            toggle_btn(rc(x0 + col * (hw + g), y, n_esrc == 1 ? fw : hw, bh), ensrcs[i].label,
                       ex_src == i, H_ENSRC + i);
            if (col == 1 || i == n_esrc - 1) y += bh + 12;
        }
        y += 14;
    }
    lead("scope");
    toggle_btn(rc(x0, y, hw, bh), "whole book", !ex_scope, H_EX_BOOK);
    toggle_btn(rc(x0 + hw + g, y, hw, bh), "this chapter", ex_scope, H_EX_CHAP);
    y += bh + 20;
    if (ex_scope && ex_chap < ntchaps)
        gfx_text_clip(40, y + 24, 36, W - 80, 0x70, tchaps[ex_chap].title);
    toastline();
    btn_t go = { rc(24, H - 130, W - 48, 100), "export >", 0 };
    ui_btn(&go, 1);
    hotreg(go.r, H_EX_GO);
}

static std::string shq(const std::string &s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

static int have_cmd(const char *c) {
    return system((std::string("command -v ") + c + " >/dev/null 2>&1").c_str()) == 0;
}

static std::string read_cmd(const std::string &cmd, int *status) {
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) { *status = -1; return out; }
    char b[512];
    while (fgets(b, sizeof b, p)) out += b;
    *status = pclose(p);
    return out;
}

static std::string last_line(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    size_t k = s.rfind('\n');
    return k == std::string::npos ? s : s.substr(k + 1);
}

static std::string export_script(void) {
    const char *e = getenv("HANZI_EXPORT_SCRIPT");
    std::vector<std::string> c;
    if (e && *e) c.push_back(e);
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string d(exe);
        d = d.substr(0, d.rfind('/'));
        c.push_back(d + "/../share/hanzi/export_book.py");     /* the AppImage */
        c.push_back(d + "/../tools/export_book.py");           /* out/hanzi */
    }
    c.push_back("tools/export_book.py");
    c.push_back("../tools/export_book.py");
    for (auto &s : c) if (access(s.c_str(), R_OK) == 0) return s;
    return "";
}

static void export_go(void) {
    static const char *MK[3] = { "zh", "py", "en" };
    static const char *MN[3] = { "\344\270\255\346\226\207", "\346\213\274\351\237\263",
                                 "\344\270\255\350\213\261\346\213\274\351\237\263" };
    const char *ext = ex_fmt ? "pdf" : "epub";
    if (ex_harness) { snprintf(toast, sizeof toast, "export: not run in the harness"); return; }
    std::string script = export_script();
    if (script.empty()) { snprintf(toast, sizeof toast, "export_book.py not found"); return; }
    std::string title = text_title;
    if (ex_scope && ex_chap < ntchaps) title += std::string(" \302\267 ") + tchaps[ex_chap].title;
    for (char &c : title) if (c == '/') c = '-';
    char name[512];
    snprintf(name, sizeof name, "%s (%s).%s", title.c_str(), MN[ex_mode], ext);
    const char *home = getenv("HOME");
    std::string h = home && *home ? home : ".";
    std::string dir = h + "/Documents";
    if (access(dir.c_str(), W_OK) != 0) dir = h;
    std::string out;
    bool fallback = false;
    int st = 0;
    long_task("choosing where to export");   /* the dialog waits on the user */
    if (have_cmd("zenity")) {
        out = last_line(read_cmd("zenity --file-selection --save --confirm-overwrite --filename=" +
                                 shq(dir + "/" + name) + " 2>/dev/null", &st));
        if (out.empty()) { snprintf(toast, sizeof toast, "export cancelled"); return; }
    } else if (have_cmd("kdialog")) {
        out = last_line(read_cmd("kdialog --getsaveFilename " + shq(dir + "/" + name) +
                                 " " + shq(std::string("*.") + ext + "|" + (ex_fmt ? "PDF" : "EPUB")) +
                                 " 2>/dev/null", &st));
        if (out.empty()) { snprintf(toast, sizeof toast, "export cancelled"); return; }
    } else {
        std::string d = h + "/Documents/exports";
        mkdir((h + "/Documents").c_str(), 0755);
        mkdir(d.c_str(), 0755);
        out = d + "/" + name;
        fallback = true;
    }
    std::string dext = std::string(".") + ext;
    if (out.size() < dext.size() || out.compare(out.size() - dext.size(), dext.size(), dext)) out += dext;
    snprintf(toast, sizeof toast, "exporting... a long book takes a while");
    redraw();
    long_task("exporting a book");
    std::string cmd = "python3 " + shq(script) + " " + shq(db_content_path()) + " " + std::to_string(text_id) + " " +
                      (ex_scope && ex_chap < ntchaps ? std::to_string(tchaps[ex_chap].id) : std::string("all")) + " " +
                      MK[ex_mode] + " " + shq(ex_mode == 2 && n_esrc ? ensrcs[ex_src].key : "") + " " +
                      ext + " " + shq(out) + " 2>&1";
    std::string res = last_line(read_cmd(cmd, &st));
    if (st != 0) {
        snprintf(toast, sizeof toast, "export failed: %s", res.c_str());
    } else {
        std::string base = out.substr(out.rfind('/') + 1);
        snprintf(toast, sizeof toast, "wrote %s (%s)%s", base.c_str(), res.c_str(),
                 fallback ? " in ~/Documents/exports: no file chooser installed" : "");
    }
}
#endif

static void draw_textch(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header(text_title, 1);

    int by      = H - 130;
    int pgy     = by - 92;
    int rowsbot = pgy - 14;
    int y = 190, shown = 0;
    char buf[96];
    for (int i = scroll; i < ntchaps && y + 96 <= rowsbot; i++, shown++) {
        rect_t r = rc(24, y, W - 48, 88);
        gfx_frame(r, 2, GREY);
        chapter_label(&tchaps[i], buf, sizeof buf);
        int tw = gfx_text_w(38, buf);
        /* The title gets what the label leaves, rather than a fixed share:
         * "read 44 · written 12 of 225" is twice the width of "225 lines",
         * and never less than nothing whatever the label says. */
        int titlew = W - 116 - tw;
        if (titlew < 120) titlew = 120;
        gfx_text_clip(46, y + 58, 46, titlew, BLACK, tchaps[i].title);
        gfx_text(W - 46 - tw, y + 56, 38, 0x70, buf);
        hotreg(r, H_ITEM + i);
        y += 96;
    }
    page_step = shown > 0 ? shown : 1;
    pager(pgy, ntchaps, scroll, shown);

#ifdef HANZI_DESKTOP
    if (export_offered()) {
        int ew = 300;
        btn_t a = { rc(24, by, W - 48 - ew - 16, 100), "\345\206\231 write this chapter", 0 };
        ui_btn(&a, 0);
        hotreg(a.r, H_TWRITE);
        btn_t e = { rc(W - 24 - ew, by, ew, 100), "export >", 0 };
        ui_btn(&e, 0);
        hotreg(e.r, H_EXPORTBOOK);
        return;
    }
#endif
    btn_t a = { rc(24, by, W - 48, 100), "\345\206\231 write this chapter", 0 };
    ui_btn(&a, 0);
    hotreg(a.r, H_TWRITE);
}

/* The chapter itself. Lines are short, so each gets its own row at a size
 * meant for reading rather than for fitting. Every character is live: press
 * one and the word it is in opens in the dictionary (word_at). */
static unsigned u8_cp(const char *ch) {
    unsigned char c0 = (unsigned char)ch[0];
    if (c0 < 0x80) return c0;
    int len = u8_clen(c0);
    if (len == 2) return (unsigned)(ch[0] & 0x1F) << 6
                       | (unsigned)(ch[1] & 0x3F);
    if (len == 3) return (unsigned)(ch[0] & 0x0F) << 12
                       | (unsigned)(ch[1] & 0x3F) << 6
                       | (unsigned)(ch[2] & 0x3F);
    if (len == 4) return (unsigned)(ch[0] & 0x07) << 18
                       | (unsigned)(ch[1] & 0x3F) << 12
                       | (unsigned)(ch[2] & 0x3F) << 6
                       | (unsigned)(ch[3] & 0x3F);
    return 0;
}

static int is_han(const char *ch) {
    unsigned cp = u8_cp(ch);
    return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF);
}

/* Punctuation as an editor added it, not as the text was written. Classical
 * Chinese had none of it, and it is hidden unless settings says otherwise.
 * Only the marks are dropped -- anything else a text might carry stays. */
static int is_punct(const char *ch) {
    unsigned cp = u8_cp(ch);
    /* in the marks' block, but text: 〃 々 〆 〇, the Suzhou numerals, and
     * 〓, which stands for a lost character as □ does. Hidden, 一百〇七人
     * read 一百七人. */
    if (cp == 0x3003 || (cp >= 0x3005 && cp <= 0x3007) || cp == 0x3013 ||
        (cp >= 0x3021 && cp <= 0x3029) || (cp >= 0x3038 && cp <= 0x303B)) return 0;
    if (cp >= 0x3000 && cp <= 0x303F) return 1;       /* 。、《》【】… */
    if (cp >= 0x2018 && cp <= 0x201D) return 1;       /* curly quotes    */
    if (cp == 0x2014 || cp == 0x2026) return 1;       /* dash, ellipsis  */
    /* the 间隔号 (周南·《关雎》) and the dashes some editions set instead of
     * —— (――, ──, －－), and the small and vertical forms (︰﹔) */
    if (cp == 0x00B7 || cp == 0x2027 || cp == 0x30FB) return 1;
    if (cp == 0x2015 || cp == 0x2500) return 1;
    if (cp >= 0xFE30 && cp <= 0xFE6B) return 1;
    switch (cp) {                                     /* fullwidth forms */
    case 0xFF01: case 0xFF0C: case 0xFF0E: case 0xFF1A:
    case 0xFF1B: case 0xFF1F: case 0xFF08: case 0xFF09:
    case 0xFF0D:
        return 1;
    default: return 0;
    }
}

/* A mark that opens (“ 《 （): it goes to the next row with what it opens,
 * and never ends one. Every other mark closes or follows (。，》” …) and
 * never starts a row. */
static int is_opening(const char *ch) {
    switch (u8_cp(ch)) {
    case 0x201C: case 0x2018:                           /* “ ‘       */
    case 0x300A: case 0x3008: case 0x300C: case 0x300E: /* 《 〈 「 『 */
    case 0x3010: case 0x3014: case 0x3016: case 0x3018: /* 【 〔 〖 〘 */
    case 0x301A: case 0x301D: case 0xFF08:              /* 〚 〝 （   */
    case 0xFE59: case 0xFE5B: case 0xFE5D:              /* small ( { 〔 */
        return 1;
    default: return 0;
    }
}
static int is_closing(const char *ch) { return is_punct(ch) && !is_opening(ch); }

/* Whether pressing a character looks anything up. □ and ○ stand for a
 * character lost or undrawable (and for 礼记's drum score), and CC-CEDICT's
 * rows for them are placeholders for characters Unicode lacks -- biàng
 * "cool", biū "pew!" -- which would give a lost character a reading. A mark
 * or a letter has no entry. 〇 is zero, and has one. */
static int is_lookup(const char *ch) {
    return is_han(ch) || u8_cp(ch) == 0x3007;
}

/* A graded reader marks a glossed word with the footnote's number, inline:
 * 一个故事1。 The notes themselves are not in the book's text and there is
 * nothing here to open, and pressing the word gives its gloss anyway, so the
 * digits are noise. 1579 of them, and the only digits in any of the books. */
static int is_footnote(const char *ch) {
    return ch[0] >= '0' && ch[0] <= '9' && ch[1] == 0;
}

/* Should this character be drawn at all? */
static int text_visible(const char *ch) {
    if (is_footnote(ch)) return 0;
    return show_punct || !is_punct(ch);
}

/* A line as it is shown, with the marks dropped when they are hidden. */
static void display_line(const char *in, char *out, size_t outn) {
    size_t o = 0;
    int n = u8_len(in);
    for (int k = 0; k < n; k++) {
        char ch[8];
        int len = u8_at(in, k, ch);
        if (!text_visible(ch)) continue;
        if (o + (size_t)len + 1 >= outn) break;
        memcpy(out + o, ch, (size_t)len);
        o += (size_t)len;
    }
    out[o] = 0;
}

/* The reading for one line, character by character. It is the only pinyin a
 * classical line reliably has, and it is what you write from. */
/* A character's first reading, for setting under it. Cached: a page of a
 * chapter asks for a few hundred of these, and asks again every time it is
 * drawn or measured. */
static void first_reading(const char *ch, char *out, size_t outn) {
    static std::unordered_map<std::string, std::string> cache;
    out[0] = 0;
    if (!is_han(ch)) return;
    auto it = cache.find(ch);
    if (it == cache.end()) {
        charinfo_t ci;
        char one[40] = "";
        if (db_charinfo(ch, &ci) && ci.pinyin[0]) {
            snprintf(one, sizeof one, "%s", ci.pinyin);
            char *sp = strpbrk(one, " ,/");
            if (sp) *sp = 0;                 /* first reading only */
        }
        if (cache.size() > 20000) cache.clear();
        it = cache.emplace(ch, one).first;
    }
    snprintf(out, outn, "%s", it->second.c_str());
}

/* How the character at index `k` of line `li` of the chapter in hand is read
 * there: the line's reading as content.db has it (text_pinyin), which nearly
 * every text carries -- the book's own where it has one (translator B's, the
 * pinyin JTTW, Giles's 三字经), from the notes for our 道德经 and 孙子兵法
 *, and for the rest set word by word by the build
 * . So our 道德经 reads 常有欲以观其徼
 * jiào, not jiǎo, and 长生 cháng, not zhǎng. Where a line has none, or the
 * build could not place a character, the first reading. Cached per line: a
 * page asks for every character of it, twice. */
static int reading_at(int li, int k, const char *ch, char *out, size_t outn) {
    static std::unordered_map<long, std::vector<std::string>> cache;
    out[0] = 0;
    /* Switched to the dictionary in settings: its first reading, as before
     * the books' own were used. */
    if (li >= 0 && li < ntlines && text_readings) {
        long key = (long)tchap_id * 4096L + li;
        auto it = cache.find(key);
        if (it == cache.end()) {
            if (cache.size() > 3000) cache.clear();
            std::vector<std::string> v;
            char py[2048];
            db_text_pinyin(tchap_id, li, py, sizeof py);
            if (py[0]) {
                /* one syllable per hanzi, in order */
                std::vector<std::string> syl;
                for (char *save = NULL, *t = strtok_r(py, " ", &save); t;
                     t = strtok_r(NULL, " ", &save))
                    syl.push_back(strcmp(t, "-") ? t : "");
                int n = u8_len(tlines[li]);
                v.assign((size_t)n, std::string());
                size_t h = 0;
                for (int j = 0; j < n; j++) {
                    char c[8];
                    u8_at(tlines[li], j, c);
                    if (is_han(c) && h < syl.size()) v[(size_t)j] = syl[h++];
                }
            }
            it = cache.emplace(key, std::move(v)).first;
        }
        if (k >= 0 && (size_t)k < it->second.size() && !it->second[(size_t)k].empty()) {
            snprintf(out, outn, "%s", it->second[(size_t)k].c_str());
            return 1;
        }
    }
    first_reading(ch, out, outn);
    return 0;
}


/* The chapter. Hanzi only by default -- that is the point of reading it --
 * with the reading a button away, its room kept either way. Every character
 * is live: press one and the word it is in opens in the dictionary. */
/* A line set with each character's reading directly under it, wrapping
 * together, the way ruby text is set. The reading used to be one run of
 * text above the line, fitted to the width -- so on a long line it shrank
 * or was cut off, and nothing said which syllable belonged to which
 * character. A character whose syllable is wider than it gets the
 * syllable's width; a closing mark never starts a row, nor an opening one
 * ends it. Returns the rows the whole line takes, and draws at most
 * `maxrows` of them. */
/* How many rows gfx_text_wrap would take for `t`. */
static int wrap_rows(int px, int maxw, const char *t) {
    int n = 0;
    for (const char *p = t; *p && n < 64; n++) p = gfx_wrap_skip(px, maxw, p, 1);
    return n ? n : 1;
}

/* One character's width at px, remembered: measuring at a size the glyph
 * cache holds rasterises the glyph, and paging a chapter measures lines it
 * never draws -- the harness's walk over the canon spent 8 of its 12
 * seconds drawing glyphs into a cache that kept filling and starting over. */
static int glyph_w(int px, const char *ch) {
    static std::unordered_map<unsigned, int> memo;
    unsigned key = u8_cp(ch) << 10 | ((unsigned)px & 1023u);
    auto it = memo.find(key);
    if (it != memo.end()) return it->second;
    if (memo.size() > 20000) memo.clear();
    int w = gfx_text_w(px, ch);
    memo.emplace(key, w);
    return w;
}

/* The cell of a character set with its syllable under it. With the pinyin
 * hidden a character takes its own width and nothing more: the text is set
 * tight and even, as Chinese is. With it shown, the cell is the character's
 * width, widened only as far as its syllable needs: a syllable wider than
 * the character less a small gap between syllables (RUBY_GAP) is condensed
 * first, down to RUBY_MIN_SCALE, and only what is left over after that
 * widens the cell -- so a character with a long syllable (zhuāng, chuáng)
 * moves the ones after it a little along the row. Where rows and pages
 * break is always worked out from the cells with the pinyin shown, so
 * showing it or not moves no character to another row or page; only
 * across its row does it shift.
 *
 * Tried first: characters moved apart for wide syllables, whose gaps were
 * uneven with the pinyin hidden; then one cell of px + pypx/2 for every
 * character, which set the text loose. Hence this, and the larger TREAD_PX. */
#define RUBY_MIN_SCALE 0.8f
/* A quarter of pypx (10px) ran two condensed syllables together --
 * chénggōng -- since condensing narrows the letters' own spacing too; two
 * fifths keeps them apart at the floor, and a four-letter syllable still
 * fits its character's width at 100%. */
static int ruby_gap(int pypx) { return pypx * 2 / 5; }
/* The cell for a glyph gw wide over a syllable pw wide. */
static int ruby_cell(int gw, int pw, int pypx) {
    if (pw <= 0) return gw;
    int need = (int)ceilf((float)pw * RUBY_MIN_SCALE) + ruby_gap(pypx);
    return gw > need ? gw : need;
}

/* How much room character k of line li takes with its reading under it --
 * the width the rows are laid out by, pinyin shown or not (ruby_cell); a
 * mark (which has no syllable) its own width. pypx 0: no reading, the
 * glyph's own width. The reading is left in py. */
static int char_adv(int li, int k, const char *ch, int px, int pypx,
                    char *py, size_t pyn) {
    py[0] = 0;
    int gw = glyph_w(px, ch);
    if (!pypx) return gw;
    reading_at(li, k, ch, py, pyn);
    if (!py[0]) return gw;
    return ruby_cell(gw, gfx_text_w(pypx, py), pypx);
}

/* A syllable centred under its cell (x, width cell), baseline y: condensed
 * to fit where it is wider than the cell less the gap (never below
 * RUBY_MIN_SCALE, which ruby_cell leaves room for). */
static void ruby_syllable(int x, int cell, int y, int pypx, uint8_t v, const char *py) {
    int pw = gfx_text_w(pypx, py), room = cell - ruby_gap(pypx);
    if (pw <= room || room <= 0) { gfx_text(x + (cell - pw) / 2, y, pypx, v, py); return; }
    float xs = (float)room / (float)pw;
    if (xs < RUBY_MIN_SCALE) xs = RUBY_MIN_SCALE;
    gfx_text_hscale(x + (cell - (int)((float)pw * xs + 0.5f)) / 2, y, pypx, xs, v, py);
}

/* The width of what has to share a row with character k (whose own is adv):
 * from an opening mark to the character it opens, and the closing marks
 * after that. Usually just the character. */
static int mark_run_w(int li, int k, int adv, int px, int pypx) {
    const char *line = tlines[li];
    int n = u8_len(line), w = adv;
    char c[8], py[40];
    u8_at(line, k, c);
    int opening = is_opening(c);
    for (int j = k + 1; j < n; j++) {
        u8_at(line, j, c);
        if (!text_visible(c)) continue;
        if (!opening && !is_closing(c)) break;
        w += char_adv(li, j, c, px, pypx, py, sizeof py);
        opening = opening && is_opening(c);
    }
    return w;
}

/* What the harness reads back of a laid-out line: each character's row (-1
 * where it is not shown) and how far past the margin the furthest reaches. */
typedef struct { short row[TEXT_LINE_MAX]; int over; } rowprobe_t;

/* `draw`: 0 measures; RR_DRAW draws the characters and their readings;
 * RR_NOPY leaves the readings out and sets the characters at their own
 * width, but breaks the rows where they break with the readings (the cells
 * of char_adv), so the same characters are on each row with pinyin shown or
 * not; RR_BOX makes each character a tbox, a press on it the word it is in
 * (the chapter page). */
enum { RR_DRAW = 1, RR_NOPY = 2, RR_BOX = 4 };
/* Drawing, the characters the book has a note on (bnote_marks): nonzero
 * per character of the line, one value per word, and a thin line under
 * those cells, in the room kept under them -- nothing moves for it. */
static const unsigned char *rr_mark = NULL;
static int ruby_rows(int li, int x0, int maxw, int px, int pypx,
                     int top, int rowh, int maxrows, int draw, rowprobe_t *pr) {
    const char *line = tlines[li];
    /* lx, the row laid out with the readings' cells, decides the breaks;
     * x is where the characters are drawn, the same with the readings shown
     * and their own widths without */
    int n = u8_len(line), x = x0, lx = x0, row = 0, after_open = 0;
    for (int k = 0; k < n; k++) {
        char ch[8];
        u8_at(line, k, ch);
        if (pr) pr->row[k] = -1;
        if (!text_visible(ch)) continue;
        char py[40];
        int ladv = char_adv(li, k, ch, px, pypx, py, sizeof py);
        int gw  = glyph_w(px, ch);
        int adv = (draw & RR_NOPY) ? gw : ladv;
        /* A row can end before a character unless it is a closing mark or
         * follows an opening one, and must where the character and the marks
         * that go with it would pass the margin: they come down together.
         * Hung past the margin instead, 。” and 、《 ran off the panel. */
        if (lx > x0 && !after_open && !is_closing(ch) &&
            lx + mark_run_w(li, k, ladv, px, pypx) > x0 + maxw) {
            /* the last row there is room for, and more to come: say so,
             * rather than stop as if the line were done */
            if (draw && row + 1 == maxrows) {
                int ew = gfx_text_w(px, "\342\200\246");
                int ex = x + ew > x0 + maxw ? x0 + maxw - ew : x;
                gfx_text(ex, top + row * rowh + px, px, 0x70, "\342\200\246");
            }
            row++; x = lx = x0;
        }
        if (draw && row < maxrows) {
            int ry = top + row * rowh;
            gfx_text(x + (adv - gw) / 2, ry + px, px, BLACK, ch);
            if (py[0] && !(draw & RR_NOPY))
                ruby_syllable(x, adv, ry + px + pypx + 8, pypx, 0x70, py);
            if (rr_mark && rr_mark[k]) {
                /* under the character where its syllable's room is empty,
                 * under the syllable where it is shown; a word's line
                 * stops short of its neighbour's */
                int my = (draw & RR_NOPY) ? ry + px + px / 5 + 4 : ry + rowh - 5;
                int in = px / 10, x0 = x, x1 = x + adv;
                if (k == 0 || rr_mark[k - 1] != rr_mark[k]) x0 += in;
                if (k + 1 >= n || rr_mark[k + 1] != rr_mark[k]) x1 -= in;
                if (x1 > x0) gfx_fill(rc(x0, my, x1 - x0, 3), 0x60);
            }
            if ((draw & RR_BOX) && ntbox < (int)(sizeof tbox / sizeof *tbox)) {
                tbox[ntbox].r = rc(x, ry, adv, rowh);
                tbox[ntbox].line = (short)li;
                tbox[ntbox].idx  = (short)k;
                ntbox++;
            }
        }
        if (pr) {
            pr->row[k] = (short)row;
            if (lx + ladv - (x0 + maxw) > pr->over) pr->over = lx + ladv - (x0 + maxw);
        }
        x += adv;
        lx += ladv;
        after_open = is_opening(ch);
    }
    return row + 1;
}

/* Lay out one page of a chapter, and optionally draw it. Measuring and
 * drawing have to agree about where a page ends -- one decides what "next"
 * advances by and where a saved position lands, the other decides what you
 * see -- so they are the same walk with the drawing turned off. */
/* ---- your own page on a sentence ------------------------------------------ *
 * Each sentence can have one page of your handwriting: a blank page, opened
 * from the sentence ("my notes"), written on with the pen as any writing
 * screen is. Kept in user.db, every stroke as it is lifted, in coordinates
 * of the pad it was written on (so another screen scales it). On the
 * reading page, "my notes" shows which sentences have a page, and opens
 * them; it is off unless asked for. */
#define LNOTE_STROKES 2000
#define LNOTE_PTS     60000
static struct {
    int n, npt;                     /* strokes, points */
    int start[LNOTE_STROKES + 1];   /* each stroke's first point */
    short x[LNOTE_PTS], y[LNOTE_PTS];
} lnote;
static int lnote_chap = 0, lnote_line = -1;
static int mynote_back = SCR_TEXTLINE;   /* where back leaves it */
static int mynote_clear_armed = 0;       /* clear asks twice */
static int show_mynotes = 0;             /* the reading page's marks */
static unsigned char mynote_has[MAXTLINES];
static int mynote_has_for = -1;          /* the chapter mynote_has is of */
static int mynote_full = 0;              /* the page cannot take another stroke */
static int mynote_unsaved = 0;           /* the last save failed */
static int mynote_redraw = 0;            /* redraw once the stroke is lifted */
/* a stroke's point count is kept as an unsigned 16-bit number */
static_assert(LNOTE_PTS <= 65535, "lnote stroke length is stored in 16 bits");

static rect_t mynote_pad(void) {
    return rc(24, 236, W - 48, H - 150 - 236 - 16);
}

static void mynote_has_load(void) {
    if (mynote_has_for == tchap_id) return;
    db_line_ink_lines(tchap_id, mynote_has, MAXTLINES);
    mynote_has_for = tchap_id;
}

/* packed: n, then per stroke its point count and the points, int16 */
static void lnote_save(void) {
    static short buf[1 + LNOTE_STROKES + 2 * LNOTE_PTS];
    int k = 0;
    buf[k++] = (short)lnote.n;
    for (int i = 0; i < lnote.n; i++) {
        int a = lnote.start[i], b = lnote.start[i + 1];
        buf[k++] = (short)(unsigned short)(b - a);
        for (int j = a; j < b; j++) { buf[k++] = lnote.x[j]; buf[k++] = lnote.y[j]; }
    }
    rect_t pad = mynote_pad();
    const char *text = lnote_line >= 0 && lnote_line < ntlines ? tlines[lnote_line] : NULL;
    int was = mynote_unsaved;
    mynote_unsaved = !db_line_ink_put(lnote_chap, lnote_line, text, buf,
                                      lnote.n ? k * (int)sizeof(short) : 0, pad.w, pad.h);
    if (mynote_unsaved != was) mynote_redraw = 1;       /* say so, or stop saying so */
    mynote_has_for = -1;
}

static void lnote_load(int chap, int line) {
    static short buf[1 + LNOTE_STROKES + 2 * LNOTE_PTS];
    lnote.n = lnote.npt = 0;
    lnote.start[0] = 0;
    lnote_chap = chap;
    lnote_line = line;
    mynote_clear_armed = 0;
    mynote_full = 0;
    int w = 0, h = 0;
    int bytes = db_line_ink_get(chap, line, buf, (int)sizeof buf, &w, &h);
    int cnt = bytes / (int)sizeof(short), k = 0;
    if (cnt < 1 || w <= 0 || h <= 0) return;
    rect_t pad = mynote_pad();
    int ns = buf[k++];
    for (int i = 0; i < ns && i < LNOTE_STROKES && k < cnt; i++) {
        int m = (unsigned short)buf[k++];
        if (m < 0 || k + 2 * m > cnt || lnote.npt + m > LNOTE_PTS) break;
        for (int j = 0; j < m; j++, k += 2) {
            /* written on another screen: scaled to this one's pad */
            lnote.x[lnote.npt] = (short)(buf[k] * pad.w / w);
            lnote.y[lnote.npt] = (short)(buf[k + 1] * pad.h / h);
            lnote.npt++;
        }
        lnote.n++;
        lnote.start[lnote.n] = lnote.npt;
    }
}

static void lnote_down(int x, int y) {
    rect_t pad = mynote_pad();
    if (lnote.n >= LNOTE_STROKES || lnote.npt + 2 > LNOTE_PTS) { mynote_full = 1; return; }
    lnote.x[lnote.npt] = (short)(x - pad.x);
    lnote.y[lnote.npt] = (short)(y - pad.y);
    lnote.npt++;
    lnote.n++;
    lnote.start[lnote.n] = lnote.npt;
}

/* -> 1 and the segment's dirty rect when a point was added */
static int lnote_move(int x, int y, rect_t *dirty) {
    rect_t pad = mynote_pad();
    if (lnote.n == 0 || mynote_full) return 0;
    if (lnote.start[lnote.n] != lnote.npt) return 0;       /* not the live stroke */
    /* out of points mid-stroke: the rest is not kept, so the page says it
     * is full as this stroke is lifted, not at the next pen-down */
    if (lnote.npt >= LNOTE_PTS) { mynote_full = 1; return 0; }
    int px = x - pad.x, py = y - pad.y;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px >= pad.w) px = pad.w - 1;
    if (py >= pad.h) py = pad.h - 1;
    int lx = lnote.x[lnote.npt - 1], ly = lnote.y[lnote.npt - 1];
    if ((px - lx) * (px - lx) + (py - ly) * (py - ly) < 4) return 0;   /* jitter */
    lnote.x[lnote.npt] = (short)px;
    lnote.y[lnote.npt] = (short)py;
    lnote.npt++;
    lnote.start[lnote.n] = lnote.npt;
    int x0 = pad.x + lx, y0 = pad.y + ly, x1 = pad.x + px, y1 = pad.y + py;
    gfx_line(x0, y0, x1, y1, INK_W, INK);
    int t = INK_W;
    dirty->x = (x0 < x1 ? x0 : x1) - t - 2; dirty->y = (y0 < y1 ? y0 : y1) - t - 2;
    dirty->w = abs(x1 - x0) + 2 * t + 4;    dirty->h = abs(y1 - y0) + 2 * t + 4;
    return 1;
}

static void lnote_draw(void) {
    rect_t pad = mynote_pad();
    for (int i = 0; i < lnote.n; i++) {
        int a = lnote.start[i], b = lnote.start[i + 1];
        if (b - a == 1)
            gfx_line(pad.x + lnote.x[a], pad.y + lnote.y[a], pad.x + lnote.x[a],
                     pad.y + lnote.y[a], INK_W, INK);
        for (int j = a + 1; j < b; j++)
            gfx_line(pad.x + lnote.x[j - 1], pad.y + lnote.y[j - 1],
                     pad.x + lnote.x[j], pad.y + lnote.y[j], INK_W, INK);
    }
}

/* A page with writing on it, drawn in lines (the font has no such sign):
 * the mark of a sentence that has your page. */
static void page_sign(int x, int y, int w, int h, uint8_t v) {
    gfx_round_rect(rc(x, y, w, h), 6, 3, v, WHITE);
    for (int k = 1; k <= 3; k++)
        gfx_fill(rc(x + w / 5, y + k * h / 4 - 1, w * 3 / 5 - (k == 3 ? w / 5 : 0), 3), v);
}

static void mynote_open(int line, int back) {
    lnote_load(tchap_id, line);
    mynote_back = back;
    scr = SCR_MYNOTE;
}

static void draw_mynote(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char h[160];
    snprintf(h, sizeof h, "my notes  \302\267  %d/%d  \302\267  %s", lnote_line + 1, ntlines, tchap_title);
    header(h, 1);
    /* the sentence it is on, as far as one row takes it */
    if (lnote_line >= 0 && lnote_line < ntlines) {
        char shown[TEXT_LINE_MAX];
        display_line(tlines[lnote_line], shown, sizeof shown);
        gfx_text_wrap(40, 130 + 52, 52, 66, W - 80, 1, BLACK, shown);
    }
    rect_t pad = mynote_pad();
    gfx_round_rect(pad, 12, 2, 0xB4, WHITE);
    lnote_draw();
    if (lnote.n == 0)
        gfx_text(pad.x + 32, pad.y + 64, 38, 0xA8, "write here");
    if (mynote_full)
        gfx_text(pad.x + 32, pad.y + pad.h - 24, 34, 0x70, "the page is full");
    if (mynote_unsaved)
        gfx_text(pad.x + 32, pad.y + pad.h - 64, 34, BLACK, "not saved: the last stroke could not be kept");

    int by = H - 130, nb = has_pen ? 4 : 3;
    int bw = (W - 24 * (nb + 1)) / nb, x = 24;
    btn_t u = { rc(x, by, bw, 100), "undo", 0 };
    if (lnote.n) { ui_btn(&u, 0); hotreg(u.r, H_UNDO); }
    x += bw + 24;
    btn_t c = { rc(x, by, bw, 100), mynote_clear_armed ? "tap again to clear" : "clear", 0 };
    if (lnote.n) { ui_btn(&c, mynote_clear_armed); hotreg(c.r, H_CLEAR); }
    x += bw + 24;
    if (has_pen) { pen_button(rc(x, by, bw, 100)); x += bw + 24; }
    btn_t d = { rc(x, by, bw, 100), "done", 0 };
    ui_btn(&d, 1); hotreg(d.r, H_CHECK);
}

/* ---- the book's notes, marked on the reading page ----------------------- *
 * "notes" on the chapter page marks where the book's notes are, in the
 * notes source chosen for the book (the one the notes page shows). A note
 * on a word -- a reader's glossary (translator B's endnotes, the Rainbow Bridge
 * readers' glosses), a novel's 注释 -- is a thin line under the word's
 * cells, in the room kept under them; a tap there opens the word with the
 * note. Anything else -- a commentary on the line (张湛注, 王弼注, Legge,
 * translator C, translator A), a word by word of the whole line (translator B's 道德经,
 * Giles's 三字经), a gloss whose word is not in the line -- is a mark in the
 * left margin, which opens the line's notes. Nothing moves either way.
 *
 * Where a word note is: the builders keep a row per line (readers.py puts
 * each endnote on the line of the hanzi before its mark, graded.py each
 * gloss on its mark's line) and a word's note is a paragraph of the row's
 * gloss that starts with the word -- the mark's offset is not kept. So the
 * word is found in its line by its text, and marked wherever it stands
 * there: which of two 神 carried the book's mark is not recorded, and is
 * not guessed. Only the book's own notes (source '') are word notes; every
 * other source is commentary on the line. */
static int show_booknotes = 0;           /* the chapter page's "notes" */
static int notes_from_read = 0;          /* the notes page was opened from its margin mark */
static int notes_from_chap = -1;         /* in this chapter */
static int en_from_read = 0;             /* the English's page was opened from the chapter page */
static int tline_to_notes = 0;           /* the sentence was opened from that notes page */
static int tline_notes_chap = -1, tline_notes_line = -1;

typedef struct { short at, len; int entry; } bnword_t;
static struct {
    int chap = -1;
    std::string key;
    std::vector<std::string> entries;            /* each word's note, as stored */
    std::vector<std::vector<bnword_t>> words;    /* per line: where they stand */
    std::vector<unsigned char> line;             /* per line: a mark in the margin */
} bn;

/* A mark inside a glossed phrase (奶粉、糖、蛋糕、巧克力；我的天！) */
static int is_phrase_mark(const char *ch) {
    switch (u8_cp(ch)) {
    case 0xFF0C: case 0x3001: case 0xFF1B: case 0xFF01: case 0xFF1F:
    case 0x201C: case 0x201D: case 0x2018: case 0x2019:
    case 0x300A: case 0x300B: case 0x00B7:
        return 1;
    default: return 0;
    }
}

/* The word a paragraph of a gloss is about, as the books write one: its
 * hanzi (and the marks inside a phrase) up to a space, a colon, a bracket
 * or the pinyin -- 亲爱 qīn'ài – dear, 盏 (zhǎn) m.w., 鲜花： fresh
 * flowers, 帻 zé：头巾, 受不了shòu -- a part in brackets being optional
 * (认(出) [rèn(chū)]: `full` 认出, `core` 认), and after 5000言's … the end
 * of a phrase (…个个序齿：). Anything else -- 《三国演义》中，…, 一般指公元184年,
 * e.g., an English paragraph -- is not a word's, and goes with the
 * paragraph before. */
static int gloss_word(const char *p, size_t len, std::string &full, std::string &core) {
    full.clear(); core.clear();
    size_t i = 0;
    if (len >= 3 && !memcmp(p, "\342\200\246", 3)) i = 3;          /* … */
    auto ch_at = [&](size_t at, char *c) -> int {
        int cl = u8_clen((unsigned char)p[at]);
        if (cl < 1 || at + (size_t)cl > len) return 0;
        memcpy(c, p + at, (size_t)cl);
        c[cl] = 0;
        return cl;
    };
    char c[8];
    while (i < len) {
        int cl = ch_at(i, c);
        if (!cl) break;
        if (is_lookup(c) || (!full.empty() && is_phrase_mark(c))) {
            full += c; core += c; i += (size_t)cl;
            continue;
        }
        if (!full.empty() && (!strcmp(c, "(") || u8_cp(c) == 0xFF08)) {
            size_t j = i + (size_t)cl;
            std::string g;
            char d[8];
            int dl;
            while (j < len && (dl = ch_at(j, d)) && is_lookup(d)) { g += d; j += (size_t)dl; }
            if (!g.empty() && j < len && (dl = ch_at(j, d)) && (!strcmp(d, ")") || u8_cp(d) == 0xFF09)) {
                full += g;
                i = j + (size_t)dl;
                continue;
            }
        }
        break;
    }
    if (full.empty() || i >= len) return 0;
    int cl = ch_at(i, c);
    if (!cl) return 0;
    unsigned cp = u8_cp(c);
    return cp == ' ' || cp == '\t' || cp == ':' || cp == '(' || cp == '[' || cp == '-'
        || cp == 0xFF1A || cp == 0xFF08 || cp == 0x2013
        || ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') || (cp >= 0xC0 && cp <= 0x24F);
}

/* Character index of byte offset `off` in `s`. */
static int u8_index(const char *s, size_t off) {
    int n = 0;
    for (size_t b = 0; b < off && s[b]; b++)
        if (((unsigned char)s[b] & 0xC0) != 0x80) n++;
    return n;
}

static void bnotes_row(int li, const char *gloss, int more, void *ctx) {
    (void)ctx;
    if (li < 0 || li >= ntlines) return;
    int words_ok = !bn.key.size();                 /* the book's own notes */
    /* the gloss as paragraphs, each word's with the ones after that are not */
    struct ent { int word; std::string full, core, text; };
    std::vector<ent> es;
    for (const char *p = gloss; *p; ) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        std::string full, core;
        int w = words_ok && gloss_word(p, len, full, core);
        /* a word inside the word before is that note going on (他时若遂凌云志
         * ... 凌云志 means lofty), not a note of its own */
        if (w && !es.empty() && es.back().word && es.back().full.find(full) != std::string::npos)
            w = 0;
        if (w || es.empty()) es.push_back({ w, full, core, std::string(p, len) });
        else { es.back().text += "\n"; es.back().text.append(p, len); }
        p = nl ? nl + 1 : p + len;
    }
    const char *line = tlines[li];
    int nch = u8_len(line), anchored = 0;
    std::vector<bnword_t> got;
    std::vector<unsigned char> cov((size_t)(nch > 0 ? nch : 1), 0);
    int loose = more;
    for (auto &e : es) {
        if (!e.word) { loose = 1; continue; }
        /* the word as written, then without its optional part, then without
         * a closing mark (我的天！ in “我的天！”) */
        std::string cand[4] = { e.full, e.core, e.full, e.core };
        for (int k = 2; k < 4; k++)
            while (cand[k].size() >= 3) {
                char last[8];
                int n = u8_len(cand[k].c_str());
                u8_at(cand[k].c_str(), n - 1, last);
                if (!is_phrase_mark(last)) break;
                cand[k].resize(cand[k].size() - strlen(last));
            }
        const char *hit = NULL;
        std::string w;
        for (auto &cs : cand)
            if (!cs.empty() && (hit = strstr(line, cs.c_str()))) { w = cs; break; }
        if (!hit) { loose = 1; continue; }
        int id = (int)bn.entries.size();
        bn.entries.push_back(e.text);
        int wl = u8_len(w.c_str());
        for (const char *h = hit; h; h = strstr(h + 1, w.c_str())) {
            int at = u8_index(line, (size_t)(h - line));
            if (at + wl > nch) break;
            got.push_back({ (short)at, (short)wl, id });
            for (int k = at; k < at + wl; k++) cov[(size_t)k] = 1;
        }
        anchored++;
    }
    /* a gloss of every word of the line is a word by word of the line
     * (Giles's 三字经): the line's, in the margin, not two dozen lines */
    if (anchored >= 2) {
        int all = 1;
        for (int k = 0; k < nch && all; k++) {
            char c[8];
            u8_at(line, k, c);
            if (is_lookup(c) && !cov[(size_t)k]) all = 0;
        }
        if (all) { got.clear(); loose = 1; }
    }
    auto &v = bn.words[(size_t)li];
    v.insert(v.end(), got.begin(), got.end());
    if (loose) bn.line[(size_t)li] = 1;
}

/* The chapter's marks, for the notes source in hand: one query a chapter. */
static void bnotes_load(void) {
    if (bn.chap == tchap_id && bn.key == note_key() && bn.words.size() == (size_t)ntlines) return;
    bn.chap = tchap_id;
    bn.key = note_key();
    bn.entries.clear();
    bn.words.assign((size_t)ntlines, std::vector<bnword_t>());
    bn.line.assign((size_t)ntlines, 0);
    if (ntlines > 0) db_chapter_noted(tchap_id, bn.key.c_str(), bnotes_row, NULL);
}

/* Line li's word marks, a value per character (0 none); 0 if it has none. */
static int bnote_marks(int li, unsigned char *mk, int n) {
    if (li < 0 || (size_t)li >= bn.words.size() || bn.words[(size_t)li].empty()) return 0;
    memset(mk, 0, (size_t)n);
    int v = 0;
    for (auto &w : bn.words[(size_t)li]) {
        v = v % 250 + 1;
        for (int k = w.at; k < w.at + w.len && k < n; k++) mk[k] = (unsigned char)v;
    }
    return 1;
}
static int bnote_line(int li) {
    return li >= 0 && (size_t)li < bn.line.size() && bn.line[(size_t)li];
}

/* The book's note for a press on character idx of line li, into `note`:
 * `marked`, the word marked there (its word into w, where it starts into
 * *at; 2); otherwise the note on the word w pressed, where the line has
 * one on that very word (1); else 0. */
static int bnote_pick(int li, int idx, int marked, char *w, size_t wn, int *at, std::string &note) {
    note.clear();
    bnotes_load();
    if (li < 0 || (size_t)li >= bn.words.size()) return 0;
    const auto &v = bn.words[(size_t)li];
    const bnword_t *hit = NULL;
    for (auto &x : v) {
        if (marked) {
            if (idx >= x.at && idx < x.at + x.len) { hit = &x; break; }
        } else {
            char ww[64] = "";
            for (int k = x.at; k < x.at + x.len; k++) {
                char c[8];
                u8_at(tlines[li], k, c);
                strncat(ww, c, sizeof ww - strlen(ww) - 1);
            }
            if (!strcmp(ww, w)) { hit = &x; break; }
        }
    }
    if (!hit) return 0;
    /* every note on that word there, once each */
    std::vector<int> seen;
    for (auto &x : v) {
        if (x.at != hit->at || x.len != hit->len) continue;
        int dup = 0;
        for (int s : seen) dup |= s == x.entry;
        if (dup) continue;
        seen.push_back(x.entry);
        if (!note.empty()) note += "\n";
        note += bn.entries[(size_t)x.entry];
    }
    if (marked) {
        w[0] = 0;
        for (int k = hit->at; k < hit->at + hit->len; k++) {
            char c[8];
            u8_at(tlines[li], k, c);
            strncat(w, c, wn - strlen(w) - 1);
        }
        *at = hit->at;
    }
    return marked ? 2 : 1;
}

static void notes_open(void);
static void tw_load(int line);
/* The margin's mark: the line's notes, the sentence over them; back is
 * this page of the chapter. */
static void bnote_open_line(int li) {
    notes_from_read = 1;
    en_from_read = 0;
    notes_from_chap = tchap_id;
    tline_to_notes = 0;
    tw_load(li);
    notes_open();
    scr = SCR_TEXTNOTES;
}

/* The page from line `first`, up to `end` (-1: the chapter's end; an
 * apart comic's plate ends its text there): the number of lines it holds. */
static int tread_walk(int first, int draw, int end = -1) {
    /* Larger than it was (62): this is where the chapter is read. */
    const int PX = tpx(TREAD_PX), PYPX = tpx(TREAD_PYPX);
    int y = 130;                     /* top of the next line's block */
    int bot = H - 300;
    int i;
    /* The marks of your own pages go in the right margin, as on a comic's
     * page: a column for them on the left narrowed the text, and showing
     * them moved every line. The left margin, where the book's notes are
     * marked, is wider than the right on a chapter page: at 44px the
     * mark was too hard to hit. An apart comic's
     * text page keeps its 44. */
    const int LX = ntplates ? 44 : TREAD_LX, TW = W - 44 - LX;
    if (show_mynotes && draw) mynote_has_load();
    if (show_booknotes && draw) bnotes_load();
    if (end < 0 || end > ntlines) end = ntlines;
    for (i = first; i < end; i++) {
        /* Sentences run long here -- one of them is sixty characters -- so
         * they wrap rather than being cut off at the margin. Every row is
         * laid out as a row of characters with their syllables under them,
         * whether the syllables are shown or not: pinyin draws or hides
         * them, and closes up or opens out the characters within their row
         * (ruby_cell), never moving one to another. Laid out without them,
         * a row held more, so pressing
         * pinyin moved every line and every page break, and the pages
         * paged through before it no longer matched the ones after
         * (pinyin on, next, pinyin off, prev -- shifted). */
        int rowh = PX + PYPX + 24;
        /* Rows counted by the wrap that draws them: estimating from the
         * width lost the slack at the end of each row, and 1,500 lines were
         * cut with "..." on a page that had room for them. */
        int rows = ruby_rows(i, LX, TW, PX, PYPX, 0, rowh, 0, 0, NULL);
        if (rows > 8) rows = 8;
        int blockh = rows * rowh + 16;
        if (y + blockh > bot) {
            if (i > first) break;            /* the next page starts here */
            /* A line too tall for a whole page -- a long sentence at 145%
             * -- still has to be drawn, or the page shows nothing, "next"
             * advances by nothing, and the rest of the chapter is
             * unreachable. Clip it here; "sentence" opens it in full. */
            rows = (bot - y - 16) / rowh;
            if (rows < 1) rows = 1;
            blockh = rows * rowh + 16;
        }
        int top = y;

        if (draw) {
            /* Each character is live, opening the word it is in -- or,
             * with "sentence" held in, the whole line is the target and
             * opens it as a sentence. */
            /* the book's notes: a line under a word it glosses */
            unsigned char mk[TEXT_LINE_MAX];
            rr_mark = show_booknotes && bnote_marks(i, mk, (int)sizeof mk) ? mk : NULL;
            ruby_rows(i, LX, TW, PX, PYPX, top, rowh, rows,
                      RR_DRAW | (tread_py ? 0 : RR_NOPY) | (tread_pick ? 0 : RR_BOX), NULL);
            rr_mark = NULL;
            if (tread_pick) hotreg(rc(24, top, W - 48, blockh - 10), H_TLINE + i);
            /* and its other notes on the line: a bar in the left margin,
             * beside the first row's characters, opening them */
            /* Both marks' targets are their margin, edge to cells, and
             * the line's height: 56 wide, they reached 12px into the first
             * cell (and 6 into the last on the Libra), and a hot rect wins
             * over a character, so a press just off its centre opened the
             * notes instead of the word (check_targets: MARGIN_TARGET). */
            if (show_booknotes && bnote_line(i)) {
                gfx_fill(rc(LX - 28, top + PX / 5, 6, PX * 4 / 5), 0x70);
                hotreg(rc(0, top, LX, blockh - 10), H_BNOTE + i);
            }
            /* and the mark of your page on it opens that, over the line's */
            if (show_mynotes && mynote_has[i]) {
                page_sign(W - 40, top + 8, 30, 38, BLACK);
                hotreg(rc(LX + TW, top, W - (LX + TW), blockh - 10), H_MYNOTE + i);
            }
        }
        y = top + blockh;
    }
    return i - first;
}

/* The reading place is the last line of the page read, not its first:
 * reopening seeks to the page holding it, which is the same page, and a
 * chapter read to its end is marked read -- with the first line saved, the
 * chapter list said "read 66 of 67" at the end and took a one-page chapter,
 * saved as line 0, for one never opened. */
static void mark_reading(int line);
/* line, plate, and whether the page is the picture's own (no caption on
 * it): line < 2048 (MAXTLINES), plate < 512 */
static_assert(MAXPLATES <= 512 && MAXTLINES <= 2048, "comic_place_pack's fields");
static int comic_place_pack(int line, int plate, int pic) {
    return line * 1024 + plate * 2 + (pic ? 1 : 0);
}
static int comic_place_unpack(int v, int *plate, int *pic) {
    *plate = (v % 1024) / 2;
    *pic = v & 1;
    return v / 1024;
}

static void mark_page_read(void) {
    if (ntplates) {
        /* A plate's place is the last caption line on the page shown. A page
         * with none -- the picture before its caption's page -- keeps the
         * plate's first line, and a plate with no caption at all (a cover)
         * the line before it: either way the chapter is recorded, so
         * continue opens this volume, not the last one. */
        comic_layout();
        const plate_t *pl = &tplates[cplate];
        int a, b, line;
        comic_page_lines(&a, &b);
        if (b > a) line = pl->line_first + b - 1;
        else if (pl->n_lines > 0) line = pl->line_first;
        else line = pl->line_first > 0 ? pl->line_first - 1 : 0;
        mark_reading(line);
        /* and the page itself, which the line cannot always say: see
         * tread_seek */
        if (tchap_id) db_set_comic_place(tchap_id, comic_place_pack(line, cplate, b <= a));
        return;
    }
    int first = tread_start[tread_np];
    int n = tread_walk(first, 0);
    mark_reading(first + (n > 0 ? n - 1 : 0));
}

/* Put the page stack where `line` is, by paging from the top the way a
 * person would -- so "prev" walks back through the same pages. */
static void tread_seek(int line) {
    if (ntplates) {
        /* The plate whose caption holds the line; line 0 is the start of the
         * volume -- its cover, where it has one. */
        cplate = 0;
        csub = 0;
        /* The page last read, exactly, where it was saved with this line: a
         * picture page reopens on its picture, not on its caption's page
         * (the same line); plate 2 whose caption is line 0 reopens there,
         * not on the cover; a caption-less plate reopens on itself. */
        int v = db_comic_place(tchap_id), pl_ = 0, pic = 0;
        if (v >= 0 && comic_place_unpack(v, &pl_, &pic) == line && pl_ < ntplates) {
            cplate = pl_;
            if (!pic) cseek_line = line;       /* the caption page holding it */
            return;
        }
        if (line > 0) {
            for (int k = 0; k < ntplates; k++)
                if (tplates[k].n_lines > 0 && tplates[k].line_first <= line) cplate = k;
            /* the page the saved line is on: reopening at the picture saved
             * the plate's first line over the place, and rewound it */
            cseek_line = line;
        }
        return;
    }
    const int MAXP = (int)(sizeof tread_start / sizeof *tread_start);
    tread_np = 0;
    tread_start[0] = 0;
    tread_key = { tchap_id, text_scale, show_punct, text_readings };
    /* A saved position can outlive the chapter it was saved against -- a
     * rebuilt database, a book re-extracted -- and seeking past the end
     * would push the stack to its limit, leaving "prev" five hundred
     * presses from the top of a chapter showing its first page. */
    if (line >= ntlines) line = ntlines - 1;
    if (line < 0) line = 0;
    while (tread_np + 1 < MAXP && tread_start[tread_np] < ntlines) {
        int n = tread_walk(tread_start[tread_np], 0);
        if (n < 1) n = 1;
        if (tread_start[tread_np] + n > line) break;   /* this page holds it */
        tread_start[tread_np + 1] = tread_start[tread_np] + n;
        tread_np++;
    }
}

/* The pages laid out again from `first` -- the text size or punctuation
 * changed in settings (pinyin and your marks no longer change the layout)
 * -- and the page opening on that line: the stack of page starts was the
 * other layout's, and prev skipped the lines between. On
 * `first` exactly, because the new layout's page holding it can start lines
 * earlier, and toggling twice walked back a page (庄子 27-32 -> 26-27 ->
 * 21-26 on the Clara). */
static void tread_reflow(int first) {
    tread_seek(first);
    const int MAXP = (int)(sizeof tread_start / sizeof *tread_start);
    if (tread_start[tread_np] < first && tread_np + 1 < MAXP)
        tread_start[++tread_np] = first;
}

/* The stack laid out again if what it was laid out with has changed since
 * (tread_key) -- whatever screen the change was made from. settings_close
 * did it only for settings opened from the chapter page itself: from the
 * word page or the notes page over it, the stack stayed in the old layout,
 * and prev went to pages that no longer met, lines skipped between. Before
 * anything reads the stack: the chapter page drawn, and back to it. */
static void tread_relayout(void) {
    if (ntplates) return;
    tread_layout now = { tchap_id, text_scale, show_punct, text_readings };
    if (now.chap == tread_key.chap && now.scale == tread_key.scale
        && now.punct == tread_key.punct && now.readings == tread_key.readings) return;
    const int MAXP = (int)(sizeof tread_start / sizeof *tread_start);
    int first = tread_np >= 0 && tread_np < MAXP ? tread_start[tread_np] : 0;
    tread_reflow(first >= 0 && first < ntlines ? first : 0);
}

/* Remember where you are, so opening the book again puts you back. Reading
 * only: db_text_pos is how far you have written the chapter out, and the two
 * move apart -- you write what you have already read, and read on past what
 * you have written. */
static void mark_reading(int line) {
    if (!tchap_id || !text_id) return;
    if (line < 0) line = 0;
    /* This runs on every page turn, and the database is synchronous=FULL
     * because a Kobo can be yanked mid-write. Reading a value back costs no
     * fsync and writing one does, so only write what actually moved --
     * paging inside a chapter changes the line and nothing else. */
    /* _saved: with nothing saved, line 0 must be written too, or a one-line
     * chapter read is not told from one never opened */
    if (db_text_read_saved(tchap_id) != line)    db_set_text_read(tchap_id, line);
    if (db_text_last_chapter(text_id) != tchap_id)
        db_set_text_last_chapter(text_id, tchap_id);
    if (db_last_text() != text_id)               db_set_last_text(text_id);
}

/* One plate: its picture, the caption under it -- or, where the caption does
 * not fit under it, on pages of its own after it, as many as it needs. Each
 * line of the caption opens the sentence screen, as a line of any text does.
 *
 * The picture is drawn at a whole-number scale, so its pixels fall on the
 * panel's: 3x, 2x, 1:1 -- a line drawing resampled to 91% goes soft and
 * grows moire. Its size is the largest that fits the page; to keep the
 * caption under it, it may step down, but not below 1:1. A scan too wide for
 * 1:1 is fitted to the width instead (halving the 1400px
 * widest plates made the best scans the smallest pictures). */
/* 64: an apart comic's text pages -- the 前言 of the Qing-painting 西游记,
 * 1577 characters, is 16 chapter pages at 100% and twice that at 145% */
#define MAXCAPPAGES 64
static struct {
    int ok, dw, dh;                 /* the picture, and the size it is drawn */
    int under;                      /* the whole caption fits under it */
    int n0;                         /* caption lines on the picture's page */
    int ncap;                       /* caption pages after the picture's */
    int cap_start[MAXCAPPAGES + 1]; /* each caption page's first line, in the plate */
    int rowh, px, pypx;
    int pic;                        /* apart: the plate has a picture page (page 0) */
} cl;

/* An apart series' plate (comic_apart): page 0 the picture alone, as large
 * as fits, then its text a chapter page at a time (tread_walk, bounded by
 * the plate's last line), so a page of it breaks, draws and is pressed
 * exactly as a chapter's does. Pinyin and the marks do not move a line
 * there, so neither changes the pages. A plate with no picture starts on
 * its text. The picture is read only for its own page. */
static void comic_layout_apart(const plate_t *pl) {
    int top = 130, bot = H - 200;
    int maxw = W - 48, maxh = bot - top;
    int nl = pl->n_lines;
    if (pl->line_first + nl > ntlines) nl = ntlines - pl->line_first;
    if (nl < 0) nl = 0;
    cl.pic = pl->image[0] != 0 || nl == 0;
    cl.under = 0;
    cl.ncap = 0;
    cl.cap_start[0] = 0;
    int end = pl->line_first + nl;
    for (int k = pl->line_first; k < end && cl.ncap < MAXCAPPAGES; ) {
        cl.cap_start[cl.ncap++] = k - pl->line_first;
        int n = tread_walk(k, 0, end);
        k += n > 0 ? n : 1;                /* a page holds a line at least */
    }
    cl.cap_start[cl.ncap] = nl;            /* past MAXCAPPAGES, the last is clipped */
    cpages = cl.pic + cl.ncap;
    if (cseek_line >= 0) {
        /* back from a line, or pinyin or the size changed: the text page
         * holding it */
        int k = cseek_line - pl->line_first;
        csub = cl.ncap ? cl.pic : 0;
        for (int j = 0; j < cl.ncap; j++)
            if (k >= cl.cap_start[j] && k < cl.cap_start[j + 1]) csub = cl.pic + j;
        cseek_line = -1;
    }
    if (csub < 0 || csub >= cpages) csub = cpages - 1;
    cl.ok = 0;
    cl.dw = maxw; cl.dh = maxh / 3;
    if (!(cl.pic && csub == 0)) return;
    cl.ok = comic_path[0] && pl->image[0] && pic_load(comic_path, pl->image);
    if (!cl.ok) return;
    /* whole-number scales first, as a plate's picture; else fitted */
    static const int NUM[] = { 3, 2, 1 };
    int iw = pic_w(), ih = pic_h();
    for (int k = 0; k < 3; k++)
        if (iw * NUM[k] <= maxw && ih * NUM[k] <= maxh) { cl.dw = iw * NUM[k]; cl.dh = ih * NUM[k]; return; }
    cl.dw = maxw; cl.dh = (int)((long)ih * maxw / iw);
    if (cl.dh > maxh) { cl.dh = maxh; cl.dw = (int)((long)iw * maxh / ih); }
}

static int comic_line_rows(int li, int px, int pypx, int rowh) {
    char shown[TEXT_LINE_MAX];
    display_line(tlines[li], shown, sizeof shown);
    return tread_py ? ruby_rows(li, 44, W - 88, px, pypx, 0, rowh, 0, 0, NULL)
                    : wrap_rows(px, W - 88, shown);
}

static void comic_layout(void) {
    if (cplate >= ntplates) cplate = ntplates - 1;
    if (cplate < 0) cplate = 0;
    const plate_t *pl = &tplates[cplate];
    if (comic_apart) { comic_layout_apart(pl); return; }
    cl.pic = 1;
    cl.px = tpx(62); cl.pypx = tpx(44);
    cl.rowh = tread_py ? cl.px + cl.pypx + 24 : cl.px + 18;
    int top = 130, bot = H - 250;
    int maxw = W - 48, maxh = bot - top;
    int nl = pl->n_lines;
    if (pl->line_first + nl > ntlines) nl = ntlines - pl->line_first;
    if (nl < 0) nl = 0;

    int caph = 0;
    for (int k = 0; k < nl; k++)
        caph += comic_line_rows(pl->line_first + k, cl.px, cl.pypx, cl.rowh) * cl.rowh + 16;

    cl.ok = comic_path[0] && pic_load(comic_path, pl->image);
    cl.dw = maxw; cl.dh = maxh / 3;
    if (cl.ok) {
        static const int NUM[] = { 3, 2, 1 };
        int iw = pic_w(), ih = pic_h(), full = -1;
        for (int k = 0; k < 3; k++)
            if (iw * NUM[k] <= maxw && ih * NUM[k] <= maxh) { full = k; break; }
        if (full < 0) {
            /* too big for 1:1: fitted, box-averaged -- and, being resampled
             * anyway, a little smaller still (to 90%) where that keeps the
             * caption under it */
            cl.dw = maxw; cl.dh = (int)((long)ih * maxw / iw);
            if (cl.dh > maxh) { cl.dh = maxh; cl.dw = (int)((long)iw * maxh / ih); }
            int room = maxh - 24 - caph;
            if (nl > 0 && cl.dh > room && room * 10 >= cl.dh * 9) {
                cl.dw = (int)((long)cl.dw * room / cl.dh);
                cl.dh = room;
            }
        } else {
            int pick = full;
            if (nl > 0 && ih * NUM[full] + 24 + caph > maxh)
                for (int k = full + 1; k < 3; k++)
                    if (ih * NUM[k] + 24 + caph <= maxh) { pick = k; break; }
            cl.dw = iw * NUM[pick]; cl.dh = ih * NUM[pick];
        }
        if (comic_size > 0) {                 /* medium, small: a fraction of large */
            int pct = comic_size == 1 ? 80 : 65;
            cl.dw = cl.dw * pct / 100; cl.dh = cl.dh * pct / 100;
            if (cl.dw < 1) cl.dw = 1;
            if (cl.dh < 1) cl.dh = 1;
        }
    }
    cl.under = nl == 0 || cl.dh + 24 + caph <= maxh;
    cl.ncap = 0;
    cl.cap_start[0] = 0;
    cl.n0 = cl.under ? nl : 0;
    if (!cl.under && comic_split) {
        /* the whole lines that fit under the picture stay there */
        int y = top + cl.dh + 24;
        while (cl.n0 < nl) {
            int h = comic_line_rows(pl->line_first + cl.n0, cl.px, cl.pypx, cl.rowh) * cl.rowh + 16;
            if (y + h > bot) break;
            y += h;
            cl.n0++;
        }
        cl.cap_start[0] = cl.n0;
    }
    if (!cl.under) {
        /* whole lines to a page; a line taller than a page has one to itself */
        int y = top, k = cl.n0;
        while (k < nl && cl.ncap < MAXCAPPAGES - 1) {
            int h = comic_line_rows(pl->line_first + k, cl.px, cl.pypx, cl.rowh) * cl.rowh + 16;
            if (y > top && y + h > bot) {
                cl.cap_start[++cl.ncap] = k;
                y = top;
                continue;
            }
            y += h;
            k++;
        }
        cl.ncap++;
        cl.cap_start[cl.ncap] = nl;     /* the last page takes what is left: at most MAXCAPPAGES */
    }
    cpages = 1 + cl.ncap;
    if (cseek_line >= 0) {
        /* back from a line: the page its caption line is on */
        int k = cseek_line - pl->line_first;
        csub = 0;
        if (!cl.under && k >= cl.n0)
            for (int j = 0; j < cl.ncap; j++)
                if (k >= cl.cap_start[j] && k < cl.cap_start[j + 1]) csub = j + 1;
        cseek_line = -1;
    }
    if (csub < 0 || csub >= cpages) csub = cpages - 1;
}

/* The lines of the plate on the page shown: [*a, *b) in the plate's own
 * numbering; none on a picture-only page. */
static void comic_page_lines(int *a, int *b) {
    const plate_t *pl = &tplates[cplate];
    int nl = pl->n_lines;
    if (pl->line_first + nl > ntlines) nl = ntlines - pl->line_first;
    if (nl < 0) nl = 0;
    if (comic_apart) {
        int j = csub - cl.pic;
        if (j < 0 || j >= cl.ncap) { *a = *b = 0; return; }
        *a = cl.cap_start[j];
        *b = cl.cap_start[j + 1];
        return;
    }
    if (cl.under) { *a = 0; *b = csub == 0 ? nl : 0; return; }
    if (csub == 0) { *a = 0; *b = cl.n0; return; }
    *a = cl.cap_start[csub - 1];
    *b = cl.cap_start[csub];
}

static void comic_pdf_btn(rect_t r);   /* with the PDF page, below */
static void draw_comic(void) {
    nhot = 0;
    ntbox = 0;
    gfx_clear(WHITE);
    comic_layout();
    const plate_t *pl = &tplates[cplate];
    int top = 130, bot = H - 250;
    int ok = cl.ok && (!comic_apart || (cl.pic && csub == 0));
    if (ok && comic_full) {
        /* The picture alone, as large as the screen. Where filling it is
         * within 5% of a whole-number scale, that scale exactly -- pixel for
         * pixel, a few cropped at the edges if it is the larger -- so a 641px
         * scan is 2x on the 1264px Libra, not 1.97x. Otherwise smoothly: this
         * view is for seeing it bigger. A tap anywhere, or a page key, goes
         * back. */
        gfx_clear(WHITE);
        nhot = 0;
        int iw = pic_w(), ih = pic_h();
        int fw = W, fh = (int)((long)ih * W / iw);
        if (fh > H) { fh = H; fw = (int)((long)iw * H / ih); }
        int k = (fw * 2 + iw) / (2 * iw);                    /* the nearest */
        if (k >= 1 && abs(iw * k - fw) * 20 <= fw) { fw = iw * k; fh = ih * k; }
        pic_draw(rc((W - fw) / 2, (H - fh) / 2, fw, fh));
        hotreg(rc(0, 0, W, H), H_TPIC);
        return;
    }
    comic_full = 0;
    if (comic_apart) { draw_comic_apart(); return; }
    header(tchap_title, 1);

    int y = top;
    if (csub == 0) {
        rect_t ir = rc((W - cl.dw) / 2, top, cl.dw, cl.dh);
        if (ok) { pic_draw(ir); hotreg(ir, H_TPIC); }   /* pressed: full screen */
        else {
            gfx_round_rect(ir, 12, 2, 0xB4, WHITE);
            const char *msg = comic_path[0] ? "the picture could not be read"
                                            : "this series' EPUB is not on the device";
#ifdef HANZI_DESKTOP
            /* on the desktop, say where it is looked for */
            char where[400];
            if (!comic_path[0]) {
                const char *d = getenv("HANZI_COMICS_DIR");
                snprintf(where, sizeof where, "put this series' EPUB in %s", d && *d ? d : "data/comics");
                msg = where;
            }
            gfx_text_wrap(ir.x + 24, ir.y + ir.h / 2 - 20, 34, 44, ir.w - 48, 3, 0x70, msg);
#else
            gfx_text_clip(ir.x + 24, ir.y + ir.h / 2, 34, ir.w - 48, 0x70, msg);
#endif
        }
        y = top + cl.dh + 24;
    }
    int la, lb;
    comic_page_lines(&la, &lb);
    for (int k = la; k < lb; k++) {
        int li = pl->line_first + k;
        char shown[TEXT_LINE_MAX];
        display_line(tlines[li], shown, sizeof shown);
        int rows = comic_line_rows(li, cl.px, cl.pypx, cl.rowh);
        int room = (bot - y - 16) / cl.rowh;
        if (room < 1) room = 1;
        if (rows > room) rows = room;            /* the whole of it opens from the line */
        if (tread_py) ruby_rows(li, 44, W - 88, cl.px, cl.pypx, y, cl.rowh, rows, 1, NULL);
        else gfx_text_wrap(44, y + cl.px, cl.px, cl.px + 18, W - 88, rows, BLACK, shown);
        hotreg(rc(24, y, W - 48, rows * cl.rowh + 6), H_TLINE + li);
        /* your own page on the caption line: its sign in the right margin
         * (a column here would change how the captions page) */
        if (show_mynotes) {
            mynote_has_load();
            if (li < MAXTLINES && mynote_has[li]) {
                page_sign(W - 40, y + 6, 30, 38, BLACK);
                hotreg(rc(W - 56, y, 56, rows * cl.rowh + 6), H_MYNOTE + li);
            }
        }
        y += rows * cl.rowh + 16;
    }

    /* where in the volume this is, and what the next press shows */
    char hint[96];
    if (cpages > 1)
        snprintf(hint, sizeof hint, "\345\233\276 %d / %d  \302\267  %s", cplate + 1, ntplates,
                 csub == 0 ? "caption next page"
                           : cpages > 2 ? (csub + 1 < cpages ? "caption, more next page" : "caption, end")
                                        : "caption");
    else
        snprintf(hint, sizeof hint, "\345\233\276 %d / %d", cplate + 1, ntplates);
    int hw = gfx_text_w(34, hint);
    gfx_text((W - hw) / 2, H - 186, 34, 0xA0, hint);
    comic_pdf_btn(rc(W - 24 - 120, H - 204, 120, 54));

    int by  = H - 130;
    int bw4 = (W - 120) / 4;
    toggle_btn(rc(24, by, bw4, 100), "pinyin", tread_py, H_TPY);
    toggle_btn(rc(24 + bw4 + 24, by, bw4, 100), "my notes", show_mynotes, H_MYNOTESHOW);
    int at_start = cplate == 0 && csub == 0, at_end = cplate + 1 >= ntplates && csub + 1 >= cpages;
    btn_t b = { rc(24 + 2 * (bw4 + 24), by, bw4, 100), at_start ? "prev \347\253\240" : "prev", 0 };
    btn_t c = { rc(24 + 3 * (bw4 + 24), by, bw4, 100), at_end ? "next \347\253\240" : "next", 0 };
    if (!(at_start && tchap_idx == 0))            { ui_btn(&b, 0); hotreg(b.r, H_TPREV); }
    if (!(at_end && tchap_idx + 1 >= ntchaps))    { ui_btn(&c, 1); hotreg(c.r, H_TNEXT); }
}

/* The chapter page's controls, and an apart comic's text page's: the
 * toggles in a row of their own, pinyin where the sentence's is:
 * pinyin | sentence | notes | my notes, and English after them where the
 * English source has the chapter whole (it was once only under a sentence). "sentence" held in makes the
 * next line pressed open as a sentence, a press on a character being
 * its word; "notes" marks where the book has notes (bnotes_load). Under
 * them prev and next; at_start/at_end: the chapter's first/last page. */
static void tread_controls(int at_start, int at_end) {
    int by  = H - 130, pyy = by - 92;
    int en  = !ntplates && chapter_has_translation();
    int nb  = en ? 5 : 4;
    int tw3 = (W - 48 - 16 * (nb - 1)) / nb;
    toggle_btn(rc(24, pyy, tw3, 72), "pinyin", tread_py, H_TPY);
    toggle_btn(rc(24 + tw3 + 16, pyy, tw3, 72), "sentence", tread_pick, H_TSENT);
    toggle_btn(rc(24 + 2 * (tw3 + 16), pyy, tw3, 72), "notes", show_booknotes, H_BNOTESHOW);
    /* which sentences have your own page: off unless asked for */
    toggle_btn(rc(24 + 3 * (tw3 + 16), pyy, tw3, 72), "my notes", show_mynotes, H_MYNOTESHOW);
    if (en) {
        /* the chapter's translation, opened about where this page is; a
         * page away, not a toggle: under every line it would move the page */
        rect_t eb = rc(24 + 4 * (tw3 + 16), pyy, W - 24 - (24 + 4 * (tw3 + 16)), 72);
        gfx_round_rect(eb, 12, 3, BLACK, WHITE);
        int px = 36, w = gfx_text_w(px, "English");
        while (w > eb.w - 24 && px > 26) { px -= 2; w = gfx_text_w(px, "English"); }
        gfx_text(eb.x + (eb.w - w) / 2, eb.y + eb.h / 2 + px / 3, px, BLACK, "English");
        hotreg(eb, H_TCHAPEN);
    }

    /* Paging runs on through the chapter and then into the next one, the way
     * a book does. The label says which of the two the press will do. */
    int bw2 = (W - 72) / 2;
    btn_t b = { rc(24, by, bw2, 100), !at_start ? "prev" : "prev \347\253\240", 0 };
    btn_t c = { rc(48 + bw2, by, bw2, 100), !at_end ? "next" : "next \347\253\240", 0 };
    /* none at the book's ends: 叶限 is one chapter, and a "prev 章" there
     * reopened the same page */
    if (!(at_start && tchap_idx == 0))            { ui_btn(&b, 0); hotreg(b.r, H_TPREV); }
    if (!(at_end && tchap_idx + 1 >= ntchaps))    { ui_btn(&c, 1); hotreg(c.r, H_TNEXT); }
}

/* An apart comic's page (comic_layout_apart): the picture alone, or a page
 * of its text as the chapter page draws one. */
static void draw_comic_apart(void) {
    const plate_t *pl = &tplates[cplate];
    header(tchap_title, 1);
    int at_start = cplate == 0 && csub == 0, at_end = cplate + 1 >= ntplates && csub + 1 >= cpages;
    /* 图 counts the pictures, the cover the first: a 前言 is none of them */
    int pics = 0, pic_at = 0;
    for (int k = 0; k < ntplates; k++)
        if (tplates[k].image[0]) { pics++; if (k <= cplate) pic_at = pics; }
    char hint[96];
    if (cl.pic && csub == 0) {
        int top = 130, bot = H - 200;
        rect_t ir = rc((W - cl.dw) / 2, top + (bot - top - cl.dh) / 2, cl.dw, cl.dh);
        if (cl.ok) { pic_draw(ir); hotreg(ir, H_TPIC); }   /* pressed: full screen */
        else {
            gfx_round_rect(ir, 12, 2, 0xB4, WHITE);
            gfx_text_clip(ir.x + 24, ir.y + ir.h / 2, 34, ir.w - 48, 0x70,
                          comic_path[0] ? "the picture could not be read"
                                        : "this series' EPUB is not on the device");
        }
        snprintf(hint, sizeof hint, "\345\233\276 %d / %d%s", pic_at, pics,
                 cl.ncap ? "  \302\267  text next page" : "");
        int hw = gfx_text_w(34, hint);
        gfx_text((W - hw) / 2, H - 150, 34, 0xA0, hint);
        comic_pdf_btn(rc(W - 24 - 120, H - 168, 120, 54));
        /* prev and next where the text pages have them */
        int by = H - 130, bw2 = (W - 72) / 2;
        btn_t b = { rc(24, by, bw2, 100), at_start ? "prev \347\253\240" : "prev", 0 };
        btn_t c = { rc(48 + bw2, by, bw2, 100), at_end ? "next \347\253\240" : "next", 0 };
        if (!(at_start && tchap_idx == 0))            { ui_btn(&b, 0); hotreg(b.r, H_TPREV); }
        if (!(at_end && tchap_idx + 1 >= ntchaps))    { ui_btn(&c, 1); hotreg(c.r, H_TNEXT); }
        return;
    }
    int a, b;
    comic_page_lines(&a, &b);
    tread_walk(pl->line_first + a, 1, pl->line_first + b);
    /* where in the volume this is: the plate, and the page of its text */
    int j = csub - cl.pic;
    if (!cl.pic)
        snprintf(hint, sizeof hint, cl.ncap > 1 ? "%d / %d" : "", j + 1, cl.ncap);
    else if (cl.ncap > 1)
        snprintf(hint, sizeof hint, "\345\233\276 %d / %d  \302\267  %d / %d", pic_at, pics, j + 1, cl.ncap);
    else
        snprintf(hint, sizeof hint, "\345\233\276 %d / %d", pic_at, pics);
    int hw = gfx_text_w(34, hint);
    if (hint[0]) gfx_text((W - hw) / 2, H - 246, 34, 0xA0, hint);
    tread_controls(at_start, at_end);
}

static void draw_textread(void) {
    if (ntplates) { draw_comic(); return; }
    tread_relayout();
    nhot = 0;
    ntbox = 0;
    gfx_clear(WHITE);
    header(tchap_title, 1);

    int first = tread_np < (int)(sizeof tread_start / sizeof *tread_start)
              ? tread_start[tread_np] : 0;
    if (first >= ntlines) first = 0;
    tread_shown = tread_walk(first, 1);
    int i = first + tread_shown;
    if (ntlines == 0) gfx_text(44, 260, 40, GREY, "empty chapter");

    /* A chapter runs to a few hundred lines, so say where in it this is. */
    char hint[128];
    if (ntlines > tread_shown)
        snprintf(hint, sizeof hint, "%d\342\200\223%d of %d", first + 1, i, ntlines);
    else
        hint[0] = 0;
    if (hint[0]) {
        int hw = gfx_text_w(34, hint);
        gfx_text((W - hw) / 2, H - 246, 34, 0xA0, hint);
    }

    tread_controls(tread_np == 0, first + tread_shown >= ntlines);
}


/* Writing a chapter out, a character at a time.
 *
 * The box is one character wide because that is how you write: what has been
 * written already sits above it in a row, in your own hand, so the line
 * assembles itself as a sentence as you go.
 *
 * Practising, the stroke order for the character is in front of you. Testing,
 * you get the reading and nothing else until you ask to see it. Either way
 * the chapter carries on to the next line, so it can be worked through. */

/* Reading takes a sentence whole; writing takes it a phrase at a time.
 *
 * A sentence here runs to nearly sixty characters, and nobody writes that in
 * one go -- but nor is a comma the end of a thought. So the line is the unit
 * you read and the phrase is the unit you write, and the screen shows both:
 * the whole sentence above, the phrase in hand below it.
 *
 * `tw_pos` holds the characters of the current phrase that actually get
 * written. Punctuation belongs on the page -- it is how the clause reads --
 * but nobody wants to draw a comma, so it is shown and stepped over. */
static int tw_pos[MAXTCH];      /* index in the line of each writable char */
static int tw_npos = 0;
static int tw_seg = 0, tw_nseg = 0;      /* which phrase, and how many */
static int tw_seg_start[MAXTCH];         /* first character index of each */
static int tw_seg_end[MAXTCH];           /* one past its last            */

/* Where a row of a sentence ends when it is read. Only the semicolon: a
 * comma is a breath inside a clause, not the end of one, and breaking there
 * chops the sentence into pieces too small to read as a thought. */
static int is_rowbreak(const char *ch) {
    return u8_cp(ch) == 0xFF1B;
}

static int is_break(const char *ch) {
    unsigned cp = u8_cp(ch);
    return cp == 0xFF1B || cp == 0xFF0C || cp == 0x3001;   /* ；，、 */
}

/* The first character shown at or after k, or n. */
static int next_shown(const char *line, int n, int k) {
    for (; k < n; k++) {
        char c[8];
        u8_at(line, k, c);
        if (text_visible(c)) return k;
    }
    return n;
}

/* Whether a row that began at rowstart may end before b: not before a
 * closing mark, not after an opening one, and not with nothing on it to see.
 * Hidden characters (a footnote's digit, marks switched off) do not count. */
static int row_can_end(const char *line, int n, int rowstart, int b) {
    char c[8];
    int j = next_shown(line, n, b);
    if (j < n) {
        u8_at(line, j, c);
        if (is_closing(c)) return 0;
    }
    for (int i = b - 1; i >= rowstart; i--) {
        u8_at(line, i, c);
        if (text_visible(c)) return !is_opening(c);
    }
    return 0;
}

/* Where the rows of sentence li start on the sentence screen: starts[r] is
 * row r's first character and starts[rows] the line's length (room for
 * u8_len + 1). Measuring a page and drawing it both come here, so they
 * cannot disagree about where the rows fall.
 *
 * A row ends at a semicolon, where the sentence itself breaks, or where
 * the next character would pass the margin -- and there it backs up to
 * the last comma rather than breaking mid-phrase, unless that leaves a
 * stub. Every break keeps the typesetting rule: a closing mark (。，》”)
 * never starts a row and an opening one (“《) never ends one. The closing
 * marks after a comma or a semicolon stay on its row (，” ends a row, ”
 * does not begin one); at the margin the break moves back until it may
 * be made, so the character before a run of marks comes down with them --
 * hung past the margin instead, a mark this size ran off the panel. */
static int sentence_rows(int li, int px, int pypx, int maxw, int *starts) {
    const char *line = tlines[li];
    int n = u8_len(line), rows = 0;
    int rowstart = 0, wsum = 0, comma = -1, comma_w = 0, semi = 0;
    char py[40];
    starts[rows++] = 0;
    for (int k = 0; k < n; ) {
        char ch[8];
        u8_at(line, k, ch);
        int cw = text_visible(ch) ? char_adv(li, k, ch, px, pypx, py, sizeof py) : 0;
        if (wsum + cw > maxw && wsum > 0) {
            int b = -1;
            if (comma > rowstart && comma < k && comma_w * 3 > maxw) {
                /* measured on this row already, so they fit */
                for (b = comma; b < k; b++) {
                    char c[8];
                    u8_at(line, b, c);
                    if (text_visible(c) && !is_closing(c)) break;
                }
                if (b == k) b = -1;        /* marks up to the margin: break there */
            }
            if (b < 0) {
                for (b = k; b > rowstart && !row_can_end(line, n, rowstart, b); b--) {}
                /* nowhere it may -- a run of marks wider than the row, as
                 * 道，……’” at 145% on the Clara: the margin */
                if (b == rowstart) b = k;
            }
            starts[rows++] = b;
            rowstart = k = b;
            wsum = 0; comma = -1; comma_w = 0; semi = 0;
            continue;                      /* on the new row, from b */
        }
        wsum += cw;
        k++;
        if (is_break(ch) && !is_rowbreak(ch)) { comma = k; comma_w = wsum; }
        /* A semicolon ends the row it finishes, after its closing marks. */
        if (is_rowbreak(ch)) semi = 1;
        else if (text_visible(ch) && !is_closing(ch)) semi = 0;
        if (semi && wsum > 0 && k < n) {
            int j = next_shown(line, n, k);
            char c[8] = "";
            if (j < n) u8_at(line, j, c);
            if (j < n && !is_closing(c)) {
                starts[rows++] = k;
                rowstart = k;
                wsum = 0; comma = -1; comma_w = 0; semi = 0;
            }
        }
    }
    starts[rows] = n;
    return rows;
}

/* The row of written characters, sized to the phrase in hand. The writing box
 * sits below it, so both come from here: with the strip at a fixed height and
 * the box at a fixed position, a short phrase grew big boxes that ran down
 * into the writing box and overlapped it. */
#define TW_STRIP_Y 230

/* A phrase can run to 64 characters (MAXTCH), and at the smallest legible
 * cell only about 21 fit across the Libra (18 on the Clara): the rest were
 * drawn off the edge -- the box being written, its reading and its marker
 * with them. So the strip is a window onto the phrase that follows the
 * character in hand: `first` is the phrase index of its leftmost cell,
 * `count` how many cells it shows. */
static void tw_strip_geom(int *ncell, int *ngap, int *nx,
                          int *nfirst = NULL, int *ncount = NULL) {
    int cnt  = tw_npos > 0 ? tw_npos : 1;
    int gap  = cnt <= 12 ? 22 : 12;
    int cell = (W - 80 - (cnt - 1) * gap) / cnt;
    if (cell > 130) cell = 130;      /* a short phrase gets big boxes */
    if (cell < 44)  cell = 44;
    int cap = (W - 80 + gap) / (cell + gap);
    if (cap < 1) cap = 1;
    int count = cnt < cap ? cnt : cap;
    int first = 0;
    if (cnt > count) {
        first = tw_i - count / 2;
        if (first > cnt - count) first = cnt - count;
        if (first < 0) first = 0;
    }
    int total = count * cell + (count - 1) * gap;
    int x = (W - total) / 2;
    if (x < 40) x = 40;
    if (ncell)  *ncell  = cell;
    if (ngap)   *ngap   = gap;
    if (nx)     *nx     = x;
    if (nfirst) *nfirst = first;
    if (ncount) *ncount = count;
}

/* One size and position for a given phrase, shared with the hit test. It only
 * ever moves when the phrase changes, and the ink is cleared when it does. */
static rect_t tw_box(void) {
    int cell;
    tw_strip_geom(&cell, NULL, NULL);
    int top  = TW_STRIP_Y + cell + 20;
    int size = W - 48;
    if (size > 620) size = 620;
    return rc((W - size) / 2, top, size, size);
}


/* The g-th writable character of the sentence in hand. */
static void tw_char_at(int g, char *out) {
    out[0] = 0;
    if (tw_line >= ntlines) return;
    int n = u8_len(tlines[tw_line]), seen = 0;
    for (int k = 0; k < n; k++) {
        char ch[8];
        u8_at(tlines[tw_line], k, ch);
        if (!is_han(ch)) continue;
        if (seen++ == g) { snprintf(out, 8, "%s", ch); return; }
    }
}

/* The writable characters of the phrase in hand. */
static void tw_seg_load(void) {
    tw_npos = 0;
    tw_i = 0;
    tw_end_shown = 0;
    ui_ink_clear(&ink);
    guided_reset();
    if (tw_line >= ntlines) { tw_views(0); return; }
    /* Where this phrase starts among the sentence's writable characters.
     * Nothing is cleared: coming back to a phrase finds it as you left it. */
    int base = 0;
    for (int k = 0; k < tw_seg_start[tw_seg]; k++) {
        char ch[8];
        u8_at(tlines[tw_line], k, ch);
        if (is_han(ch)) base++;
    }
    tw_views(base);
    for (int k = tw_seg_start[tw_seg]; k < tw_seg_end[tw_seg] && tw_npos < MAXTCH; k++) {
        char ch[8];
        u8_at(tlines[tw_line], k, ch);
        if (is_han(ch)) tw_pos[tw_npos++] = k;
    }
}

/* The helper row's top, and the line of page just above it where the
 * verdict goes. One place, so the two cannot drift apart. */
static int tw_helper_y(void) { return H - 130 - 92; }
static int cy2_band(void)    { return tw_helper_y() - 22; }


/* Judge what is in the box against the character it was meant to be, the
 * same way a write card is judged: run the handwriting search over the
 * strokes and see where the right answer comes. */
static void tw_check_now(void) {
    if (tw_i >= tw_npos || tw_i >= MAXTCH) return;
    if (tw_magic[tw_i]) return;        /* already written against the model */
    if (ink.n == 0) { tw_rank[tw_i] = TW_UNCHECKED; return; }

    char want[8];
    u8_at(tlines[tw_line], tw_pos[tw_i], want);

    unsigned char sig[HW_MAX_STROKES * 6];
    int ns = ui_ink_signature(&ink, sig, HW_MAX_STROKES);
    hwcand_t cand[8];
    int got = ns > 0 ? db_hw_search(sig, ns, cand, 8) : 0;

    tw_rank[tw_i] = TW_WRONG;
    tw_guess[tw_i][0] = 0;
    for (int k = 0; k < got; k++)
        if (!strcmp(cand[k].ch, want)) { tw_rank[tw_i] = k; break; }
    if (tw_rank[tw_i] == TW_WRONG && got > 0)
        snprintf(tw_guess[tw_i], sizeof tw_guess[tw_i], "%s", cand[0].ch);
    /* Keep the ink for the strip whether it was right or not: the sentence
     * assembling itself in your own hand is the point. */
    ui_ink_thumb(&tw_ink[tw_i], &ink, tw_box());
}

/* Whatever is in the box, judged and kept, before leaving it: a character
 * written freehand is checked; one finished stroke by stroke was already
 * judged as it went. Either way it becomes the one the verdict line talks
 * about. An empty box is left alone -- skipping is allowed. */
static void tw_settle(void) {
    if (tw_i >= tw_npos || tw_i >= MAXTCH) return;
    if (tw_magic[tw_i]) { tw_said = tw_base + tw_i; return; }
    /* Whatever is in the box is judged, even after an earlier verdict: a
     * character got wrong is written again, and the new attempt counts. */
    if (ink.n > 0) tw_check_now();
    if (tw_rank[tw_i] != TW_UNCHECKED) tw_said = tw_base + tw_i;
}


static void tw_load(int line) {
    if (line < 0) line = 0;
    if (line >= ntlines) line = ntlines > 0 ? ntlines - 1 : 0;
    tw_line = line;
    tw_i = 0;
    tw_reveal = 0;
    tw_auto_help = 0;
    tline_page = 0;
    tline_seek = -1;
    tw_forget_sentence();
    tw_views(0);
    ui_ink_clear(&ink);

    /* Cut the sentence into phrases at its clause marks. Each is a breath:
     * 道可道， then 非常道； then 名可名， then 非常名。 */
    tw_nseg = 0;
    tw_seg  = 0;
    if (tw_line < ntlines) {
        int n = u8_len(tlines[tw_line]);
        int start = 0, han = 0;
        for (int k = 0; k < n; k++) {
            char ch[8];
            u8_at(tlines[tw_line], k, ch);
            if (is_han(ch)) han++;
            /* A phrase ends at a clause mark, at the end of the sentence, or
             * at MAXTCH characters: the 法华经 has a phrase of 165, and past
             * 64 the rest could never be written. */
            if (!(is_break(ch) || k == n - 1 || han >= MAXTCH)) continue;
            if (han == 0) {
                /* Nothing to write in it -- a closing quote cut off by the
                 * comma before it. On its own it was a writing screen with
                 * nothing in it and no way on; it goes with its neighbour. */
                if (tw_nseg > 0) { tw_seg_end[tw_nseg - 1] = k + 1; start = k + 1; }
                continue;                  /* at the start: joins the next */
            }
            if (tw_nseg < MAXTCH) {
                tw_seg_start[tw_nseg] = start;
                tw_seg_end[tw_nseg]   = k + 1;
                tw_nseg++;
            } else {
                tw_seg_end[tw_nseg - 1] = k + 1;   /* no room for more: extend */
            }
            start = k + 1;
            han = 0;
        }
        if (start < n && tw_nseg > 0) tw_seg_end[tw_nseg - 1] = n;   /* trailing marks */
    }
    if (tw_nseg == 0) { tw_seg_start[0] = 0; tw_seg_end[0] = 0; tw_nseg = 1; }
    tw_seg_load();
    /* No db_set_text_pos here: this loads a sentence for the sentence view
     * too, and saving it made reading move the writing place ("write this
     * chapter" resumed at the last sentence read), and a notes-page skip
     * over 80 sentences 400 fsyncs. tw_begin saves it, where writing starts. */
}

/* Start writing a sentence: the one place the writing position is kept. */
static void tw_begin(int line) {
    tw_load(line);
    db_set_text_pos(tchap_id, tw_line);
}



/* One sentence, closely.
 *
 * This is where a line is actually read: large enough to look at properly,
 * with every character live for the dictionary, and one button -- write this
 * one. Moving through the chapter is prev and next; the chapter screen is for
 * finding your place, this is for being in it. */
#define MAXTNOTES 12
static textnote_t tnotes[MAXTNOTES];
static int  ntnotes = 0;

/* A line the English source translates inside its sentence on another line
 * (db_text_en_cover: the source put that sentence whole on the line where
 * it starts) is shown that sentence, which translates it too, under a word
 * saying whose it is -- sentences are numbered from 1, as in the header.
 * "not translated here" is only for a line the source leaves out. */
static void cover_note(int line, int cover, char *out, size_t n) {
    if (cover == line - 1) snprintf(out, n, "with the sentence before");
    else if (cover == line + 1) snprintf(out, n, "with the sentence after");
    else snprintf(out, n, "with sentence %d", cover + 1);
}

static void draw_textline(void) {
    nhot = 0;
    ntbox = 0;
    gfx_clear(WHITE);
    char h[240];
    /* the count first: a long chapter title must not clip it away */
    snprintf(h, sizeof h, "%d/%d  \302\267  %s", tw_line + 1, ntlines, tchap_title);
    header(h, 1);

    if (tw_line >= ntlines) { gfx_text(44, 260, 40, GREY, "empty chapter"); return; }
    const char *line = tlines[tw_line];

    /* With the reading shown, each syllable sits under its own character,
     * in the rows below -- not as a run of text above the sentence, where
     * a long sentence's pinyin was squeezed or cut off and nothing lined
     * it up with the character it belonged to. */
    int top = 150;
    /* The English, when asked for, is the English source's: a switch of
     * them under the header, where the book has more than one, and the
     * toggle to the source's translation of the chapter, where it has one. */
    int en_lines = src_lines(en_key(), NOTES_EN);   /* the source translates lines */
    int en_page = chapter_has_translation();        /* and has this chapter whole */
    int en_btn = en_lines || n_esrc > 1;            /* the English toggle */
    int ch_en = !en_btn && en_page;                 /* only the chapter's: "English" opens it */
    int en_on = tread_en && en_btn;
    if (en_on && (n_esrc > 1 || en_page)) {
        draw_src_row(ensrcs, n_esrc, cur_esrc, H_ENSRC, "English", en_page, 0, en_slot_book());
        top = 214;
    }
    /* Large, because these are small panels: 38px was too small to read.
     * The cells are the chapter page's (ruby_cell): a syllable wider than
     * its character is condensed a little and then widens its cell, and
     * with the pinyin hidden the characters are set at their own width. */
    const int PYPX = tpx(54);
    /* The syllables' room is kept whether they are shown or not, and the
     * rows break by their cells either way (sentence_rows), so pressing
     * pinyin draws or hides them and moves no character to another row or
     * page; within a row they close up. */
    const int PYH = PYPX + 22;
    const int PYA = PYPX;                    /* char_adv's: the reading's width */
    int bot = H - 300;
    const int bot_bare = bot, top_bare = 150;   /* with no English: see y below */

    /* The translation, when asked for: under the sentence, on this screen,
     * taking the space it needs from the bottom -- up to half of it; the
     * rest is on the notes page. Measured the way it is drawn: wrapped
     * well below the panel, where nothing shows. */
    /* The English toggle only where the source translates lines: a novel's
     * line has word notes and nothing to translate it. Where this line is
     * one it leaves out, it says so, quietly -- never another's English in
     * its place. */
    int has_en = en_on && line_has_english(tw_line);
    /* or the line is in the source's sentence on another line */
    int en_cover = en_on && !has_en ? db_text_en_cover(tchap_id, tw_line, en_key()) : -1;
    const int EN_PX = tpx(46), EN_LH = EN_PX * 13 / 10;   /* as the notes page */
    const int CN_PX = tpx(34) < 34 ? tpx(34) : 34, CN_H = en_cover >= 0 ? CN_PX * 13 / 10 + 8 : 0;
    int en_h = 0, en_lines_max = 0, en_over = 0;
    if (en_on && !has_en && en_cover < 0) {
        en_h = EN_LH + 24;
        bot -= en_h;
    }
    if (has_en || en_cover >= 0) {
        ntnotes = db_text_notes_of(tchap_id, has_en ? tw_line : en_cover, en_key(), NOTES_EN, tnotes, MAXTNOTES);
        tnotes_laid_out = 0;     /* the notes page must lay these out afresh */
        en_lines_max = ((bot - top) / 2 - CN_H) / EN_LH;
        if (en_lines_max < 1) en_lines_max = 1;
        int lines = 0;
        for (int i = 0; i < ntnotes; i++) {
            /* static: 6KB each, twice, was most of the stack this frame had */
            static char buf[sizeof tnotes[0].english];
            char *save = NULL;
            snprintf(buf, sizeof buf, "%s", tnotes[i].english);
            for (char *ln = strtok_r(buf, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
                int y0 = H + 4000;
                lines += (gfx_text_wrap(40, y0, EN_PX, EN_LH, W - 80, 20, WHITE, ln) - y0) / EN_LH;
            }
        }
        if (lines > en_lines_max) { lines = en_lines_max; en_over = 1; }
        en_h = lines * EN_LH + 24 + CN_H;
        bot -= en_h;
    }

    /* As large as the sentence will go: reading it is the point of the
     * screen, so the text takes the space and everything else works round
     * it. Then centre the block in what is left, because one short line
     * pinned to the top of a tall panel looks like a mistake. */

    /* One size for every sentence, so the page does not change weight as you
     * move through a chapter. Rows break at the semicolons -- where the
     * sentence itself breaks -- and a clause longer than a row wraps.
     *
     * 92px fits every sentence in 道德经 without shrinking; the step down is
     * only there so an imported book with a longer one cannot run off the
     * page. Counting and drawing take the same rows (sentence_rows), so
     * they cannot disagree about where the rows fall. */
    /* One size for every sentence, so the page does not change weight as you
     * move through a chapter, and large enough to be worth calling reading.
     * A sentence that will not fit is paged rather than shrunk -- shrinking
     * it is the one thing that would break the first rule. At 116px exactly
     * one of the 425 sentences in 道德经 needs a second page. */
    const int PX = tpx(116);
    int LH = PX + 30 + PYH, rows = 0, y = 0;
    int per_page = (bot - top) / LH;
    if (per_page < 1) per_page = 1;
    int first = 0, last = 0;       /* the rows this page shows */

    static int rs[TEXT_LINE_MAX + 1];  /* each row's first character */
    rows = sentence_rows(tw_line, PX, PYA, W - 88, rs);

    /* Paged, the "page 1 of 4" label needs its own line under the rows: at
     * the foot of a full page it sat in the last row's pinyin. */
    int rbot = bot;                  /* the rows' own foot; bot stays the English's top */
    if (rows > per_page) {
        rbot = bot - 44;
        per_page = (rbot - top) / LH;
        if (per_page < 1) per_page = 1;
    }
    int pages = (rows + per_page - 1) / per_page;
    if (pages < 1) pages = 1;
    if (tline_seek >= 0) {
        /* back from settings at another size: the page holding the
         * character at the top of the one left (settings_open) */
        int r = 0;
        while (r + 1 < rows && rs[r + 1] <= tline_seek) r++;
        tline_page = r / per_page;
        tline_seek = -1;
    }
    if (tline_page >= pages) tline_page = pages - 1;
    first = tline_page * per_page;
    last  = first + per_page;
    if (last > rows) last = rows;
    tline_top = first < rows ? rs[first] : 0;
    tline_end = rs[last];

    int shown = last - first;
    /* Centred where it would be with no English, and moved up only as far
     * as the English under it needs: showing the English moved even a
     * short sentence half the English's height. */
    y = top_bare + (bot_bare - (rbot != bot ? 44 : 0) - top_bare - shown * LH) / 2 + PX;
    if (y - PX + shown * LH > rbot) y = rbot - shown * LH + PX;
    if (y < top + PX) y = top + PX;
    for (int r = first, yy = y; r < last; r++, yy += LH) {
        char c2[8], sy[40];
        int wsum = 0;
        /* a character's cell as drawn: its syllable's with the pinyin
         * shown, its own width without */
        auto cell = [&](int j) {
            int a = char_adv(tw_line, j, c2, PX, PYA, sy, sizeof sy);
            return tread_py ? a : glyph_w(PX, c2);
        };
        for (int j = rs[r]; j < rs[r + 1]; j++) {
            u8_at(line, j, c2);
            if (text_visible(c2)) wsum += cell(j);
        }
        int dx = (W - wsum) / 2;             /* centred */
        for (int j = rs[r]; j < rs[r + 1]; j++) {
            u8_at(line, j, c2);
            if (!text_visible(c2)) continue;
            int w2 = cell(j), g2 = glyph_w(PX, c2);
            gfx_text(dx + (w2 - g2) / 2, yy, PX, BLACK, c2);
            if (sy[0] && tread_py)
                ruby_syllable(dx, w2, yy + PYPX + 16, PYPX, 0x70, sy);
            if (ntbox < (int)(sizeof tbox / sizeof *tbox)) {
                tbox[ntbox].r = rc(dx, yy - PX + 10, w2, PX + 12);
                tbox[ntbox].line = (short)tw_line;
                tbox[ntbox].idx  = (short)j;
                ntbox++;
            }
            dx += w2;
        }
    }

    if (pages > 1) {
        char pg[64];
        snprintf(pg, sizeof pg, "page %d of %d", tline_page + 1, pages);
        int w = gfx_text_w(36, pg);
        gfx_text((W - w) / 2, rbot + 36, 36, 0x90, pg);
    }
    tline_pages = pages;


    int by  = H - 130;
    int pyy = by - 92;

    if (en_on && !has_en && en_cover < 0) {
        /* the source leaves this line out: said in grey, the chapter's
         * page a press away where it has one (the toggle above) */
        char none[96];
        en_none_text(none, sizeof none);
        gfx_fill(rc(40, bot + 4, W - 80, 1), FAINT);
        gfx_text(40, bot + 24 + EN_PX, tpx(40) < 40 ? tpx(40) : 40, GREY, none);
    } else if (en_h > 0 && getenv("HANZI_NO_EXAMPLES")) {
        /* Not ours to publish: a screenshot shows where it would be. */
        gfx_fill(rc(40, bot + 4, W - 80, 1), FAINT);
        gfx_text(40, bot + 24 + EN_PX, 34, 0x70,
                 "(the translation is omitted from published screenshots)");
    } else if (en_h > 0) {
        /* Rule, then the English a line at a time, as the book sets it --
         * under a word on whose sentence it is, where it is another line's */
        int ey = bot + 24;
        gfx_fill(rc(40, bot + 4, W - 80, 1), FAINT);
        if (en_cover >= 0) {
            char cn[64];
            cover_note(tw_line, en_cover, cn, sizeof cn);
            gfx_text(40, ey + CN_PX, CN_PX, GREY, cn);
            ey += CN_H;
        }
        /* English that will not all fit gives its last row to saying where
         * the rest is, rather than writing that over its last line. */
        int left = en_lines_max - (en_over ? 1 : 0), cut = 0;
        for (int i = 0; i < ntnotes && !cut; i++) {
            /* static: 6KB each, twice, was most of the stack this frame had */
            static char buf[sizeof tnotes[0].english];
            char *save = NULL;
            snprintf(buf, sizeof buf, "%s", tnotes[i].english);
            for (char *ln = strtok_r(buf, "\n", &save); ln && !cut; ln = strtok_r(NULL, "\n", &save)) {
                int y0 = H + 4000;
                int need = (gfx_text_wrap(40, y0, EN_PX, EN_LH, W - 80, 20, WHITE, ln) - y0) / EN_LH;
                if (need > left) { need = left; cut = 1; }
                if (need <= 0) { cut = 1; break; }
                /* the last line that fits says there is more, where it is */
                ey = gfx_text_wrap(40, ey + EN_PX, EN_PX, EN_LH, W - 80, need, 0x20, ln)
                     - EN_PX;
                left -= need;
            }
        }
        if (cut || en_over) {
            /* the rest, on the English's own page: a button in the row
             * kept for it */
            int mw = gfx_text_w(34, "more") + 56, mh = 60;
            rect_t mr = rc(W - 40 - mw, ey + (EN_LH - mh) / 2, mw, mh);
            if (mr.y + mr.h > H - 222 - 8) mr.y = H - 222 - 8 - mr.h;
            gfx_round_rect(mr, 12, 3, BLACK, WHITE);
            gfx_text(mr.x + 28, mr.y + mr.h / 2 + 12, 34, BLACK, "more");
            hotreg(mr, H_ENMORE);
        }
    }

    /* pinyin | English | notes: the two toggles over the sentence, and the
     * translator's notes -- word by word and commentary -- a page away.
     * The English is off unless asked for: reading the Chinese comes first. */
    int tw3 = (W - 48 - 48) / 4;       /* pinyin | English | notes | my notes */
    toggle_btn(rc(24, pyy, tw3, 72), "pinyin", tread_py, H_TPY);
    if (en_btn) toggle_btn(rc(24 + tw3 + 16, pyy, tw3, 72), "English", tread_en, H_TEN);
    if (ch_en) {
        /* the chapter's translation, opened about where this line is */
        rect_t eb = rc(24 + tw3 + 16, pyy, tw3, 72);
        gfx_round_rect(eb, 12, 3, BLACK, WHITE);
        int w = gfx_text_w(36, "English");
        gfx_text(eb.x + (eb.w - w) / 2, eb.y + 48, 36, BLACK, "English");
        hotreg(eb, H_TCHAPEN);
    }
    /* With a choice of sources the notes page is where the switch is, so
     * its button stays even when the chosen source (Linnell, Giles) has
     * nothing on this line: it then opens that source's chapter page. */
    if (line_offers_notes(tw_line)) {
        rect_t tb = rc(24 + 2 * (tw3 + 16), pyy, tw3, 72);
        gfx_round_rect(tb, 12, 3, BLACK, WHITE);
        int w = gfx_text_w(36, "notes");
        gfx_text(tb.x + (tb.w - w) / 2, tb.y + 48, 36, BLACK, "notes");
        hotreg(tb, H_TNOTES);
    }
    {
        /* your own page on this sentence; the sign says there is one */
        rect_t mb = rc(24 + 3 * (tw3 + 16), pyy, W - 24 - (24 + 3 * (tw3 + 16)), 72);
        mynote_has_load();
        int has = tw_line >= 0 && tw_line < MAXTLINES && mynote_has[tw_line];
        gfx_round_rect(mb, 12, 3, BLACK, WHITE);
        const char *ml = "my notes";
        int w = gfx_text_w(36, ml), sw = has ? 30 : 0;
        int x0 = mb.x + (mb.w - w - (has ? sw + 12 : 0)) / 2;
        if (has) page_sign(x0, mb.y + 18, sw, 38, BLACK);
        gfx_text(x0 + (has ? sw + 12 : 0), mb.y + 48, 36, BLACK, ml);
        hotreg(mb, H_MYNOTES);
    }

    int bw3 = (W - 96) / 3;
    btn_t pv = { rc(24, by, bw3, 100), "prev", 0 };
    btn_t wr = { rc(24 + bw3 + 24, by, bw3, 100), "\345\206\231 write this", 0 };
    /* At the end of a chapter the forward button leaves it, so it says so
     * rather than looking like it does nothing. */
    int at_end  = tline_page + 1 >= tline_pages && tw_line + 1 >= ntlines;
    int at_top  = tline_page == 0 && tw_line == 0;
    btn_t nx = { rc(24 + 2*(bw3 + 24), by, bw3, 100),
                 at_end && tchap_idx + 1 < ntchaps ? "next \347\253\240" : "next", 0 };
    if (at_top && tchap_idx > 0) pv.label = "prev \347\253\240";
    /* At the first sentence of the book there is nothing to go back to, and
     * at the last nothing to go on to: no button, rather than one that does
     * nothing. */
    int no_prev = at_top && tchap_idx == 0;
    int no_next = at_end && tchap_idx + 1 >= ntchaps;
    if (!no_prev) { ui_btn(&pv, 0); hotreg(pv.r, H_TPREV); }
    ui_btn(&wr, 1); hotreg(wr.r, H_TWRITE);
    if (!no_next) { ui_btn(&nx, 0); hotreg(nx.r, H_TNEXT); }
}

/* ---- a translation, sentence by sentence -------------------------------- *
 * For the texts that come with one -- translator B's step-by-step 道德经 and 孙子兵法,
 * Giles's 三字经, Legge's 论语, 庄子 and 左传 -- for the sentence in hand, each
 * passage it belongs to: the English, the Chinese of the passage, its
 * pinyin, word by word, and the translator's commentary, whichever of those
 * the translator gives. A sentence can carry several passages, and a passage
 * a long commentary, so it pages; next at the end of the notes moves on to
 * the next sentence's, the way reading does.
 *
 * The open database carries no translations (text_notes is empty). */
/* A whole chapter's translation: Legge's introductions were a few KB, a
 * chapter of Brewitt-Taylor's 三国, Joly's 红楼梦 or Buck's 水浒传 is up
 * to 88KB, and translator E's 庄子 2 with all of 郭象's commentary and his notes
 * 148KB (the build reads this size; the other modules and check.sh
 * hold their pages under it). */
static char tchapnote[262144] = "";
static char tchaplabel[64] = "";
static int  tnote_page = 0, tnote_pages = 1;
/* The page is the notes' or the English's (tnote_en): the notes source's
 * commentary on the sentence, or the English source's translation of it in
 * full -- where the few lines under the sentence could not hold it -- with
 * each its own switch, its chapter page and its paging on to the next
 * sentence that has some. The chapter page of the English is its source's
 * translation of the chapter whole. */
static int  tnote_en = 0;
/* the English's page of a line in the source's sentence on another line:
 * that line (its sentence is what the page shows, under a word saying so),
 * else -1 */
static int  tnote_cover = -1;
/* the English's chapter page, opened by the toggle under the sentence's
 * header: the toggle again goes back there, as it came */
static int  tnote_en_from_line = 0;
/* The notes page shows one of two things: the sentence's notes, or the
 * chapter's own -- translator B's chapter in English,
 * Legge's introduction, a commentator's note on its title, a whole
 * translation (Linnell's, a novel's). Those used to come with the chapter's
 * first sentence, which made 道德经 1.1 ten pages on the Clara and a novel's
 * first line 45; now they are a page of their own, the chapter page,
 * reached from the notes page's "chapter" toggle (tnote_chap_want) or, for
 * a sentence with nothing of its own where the source has only chapters,
 * opened directly. */
static int  tnote_chap = 0;        /* the page shown is the chapter's */
static int  tnote_chap_want = 0;   /* and it was asked for, with the toggle */
/* Each page is where it was left: the chapter's, per source (the notes' and
 * the English's apart), while the chapter is the same; the sentence's, while
 * the sentence and source are. */
static int  tnote_chap_page[2 * MAXNSRC];
static int  tnote_chap_for = -1;   /* the chapter those are for */
static int  tnote_line_page = -1, tnote_line_for_c = -1, tnote_line_for_l = -1, tnote_line_for_s = -1;
static int  tnote_near = -1;       /* per mille through the chapter, to open at */
static int  tnote_about = 0;       /* the page shown is the "about here" one */
/* The pages of the page's text, from its first after the translator's
 * introduction to its last before his notes, where it has them (text_label,
 * notes_label): what "about here" counts through. All of them otherwise. */
static int  tnote_text_first = 0, tnote_text_pages = 1;
/* What the notes page last drawn starts with: its paragraph, the line of it,
 * and how many lines it wraps to. For the harness, which checks that a page
 * laid out again at another size still holds what was at its top. */
static int  tnote_top_b = -1, tnote_top_f = 0, tnote_top_n = 1;
static int  tnote_picking = 0;     /* the page-number grid is up */
static int  tnote_grid_first = -1; /* the first page number the grid shows; -1 the current page's block */
static int  tnote_grid_per = 100;  /* how many numbers a block of the grid holds */

/* The source the page reads, which of its rows, and its slot in the pages
 * kept (tnote_chap_page, tnote_line_for_s). */
static const char *page_key(void) { return tnote_en ? en_key() : note_key(); }
static int page_what(void) { return tnote_en ? NOTES_EN : NOTES_NOTED; }
static int page_slot(void) { return tnote_en ? MAXNSRC + cur_esrc : cur_nsrc; }

/* What a source's page of a chapter is: 0 none, 1 a note on it (Legge's
 * introduction, a commentator's note on its title), 2 its
 * translation -- whole ("Joly's translation, 1892"; translator B's, which has no
 * label, and 六祖坛经's, "the chapter, in English"), or in outline
 * ("Richard's outline, 1913"). That is all a page's label is asked: which
 * sources give English is known from their rows (db_source_gives), since
 * translator C's 注's page, the commentaries, is "translations" too. Kept for the
 * last few asked -- the English's and the notes' sources are asked in turn,
 * and the notes page asks it of every chapter it passes on the way to the
 * next sentence with notes. */
static int chapter_note_kind_of(int chapter_id, const char *key) {
    static struct { int chap; char key[sizeof ((notesrc_t *)0)->key]; int kind; } kc[4] = {
        { -1, "", 0 }, { -1, "", 0 }, { -1, "", 0 }, { -1, "", 0 } };
    static int next = 0;
    for (auto &e : kc) if (e.chap == chapter_id && !strcmp(e.key, key)) return e.kind;
    static char note[8];
    char label[64];
    db_text_chapter_note_of(chapter_id, key, note, sizeof note, label, sizeof label);
    int kind = !note[0] ? 0 : (!label[0] || strstr(label, "translation") || strstr(label, "outline")
                               || strstr(label, "in English")) ? 2 : 1;
    auto &e = kc[next++ % 4];
    e.chap = chapter_id;
    snprintf(e.key, sizeof e.key, "%s", key);
    e.kind = kind;
    return kind;
}
/* the notes source's page of the chapter */
static int chapter_note_kind(int chapter_id) { return chapter_note_kind_of(chapter_id, note_key()); }
/* the English source's page is the chapter's translation */
static int chapter_has_translation(void) { return chapter_note_kind_of(tchap_id, en_key()) == 2; }
/* the page's own source's, as the page has it: the English has only a
 * translation of the chapter to go to, never an essay on it */
static int page_kind(int chapter_id) {
    int k = chapter_note_kind_of(chapter_id, page_key());
    return tnote_en ? (k == 2 ? 2 : 0) : k;
}
/* A sentence's own notes, in the notes source; its English, in the
 * English's. The chapter's are on its page, not on the first sentence's. */
static int line_has_notes(int line) { return db_text_has_of(tchap_id, line, note_key(), NOTES_NOTED); }
static int line_has_english(int line) { return db_text_has_of(tchap_id, line, en_key(), NOTES_EN); }
/* the page's kind of row, on a line of any chapter */
static int page_line_has(int chapter_id, int line) {
    return db_text_has_of(chapter_id, line, page_key(), page_what());
}
/* Whether a source gives any line of the book its notes, or its English --
 * or has only chapter pages (Linnell and Giles did; a novel's translation
 * does) -- kept for the book open. */
static int src_lines(const char *key, int what) {
    static struct { int text; char key[sizeof ((notesrc_t *)0)->key]; int what, has; } sc[4] = {
        { -1, "", 0, 0 }, { -1, "", 0, 0 }, { -1, "", 0, 0 }, { -1, "", 0, 0 } };
    static int next = 0;
    for (auto &e : sc) if (e.text == text_id && e.what == what && !strcmp(e.key, key)) return e.has;
    auto &e = sc[next++ % 4];
    e.text = text_id; e.what = what;
    snprintf(e.key, sizeof e.key, "%s", key);
    e.has = db_source_gives(text_id, key, what);
    return e.has;
}
static int page_src_lines(void) { return src_lines(page_key(), page_what()); }
/* A line with none of the English source's English, and no sentence of his
 * covering it, says so in grey, and says which: a line he leaves out ("Buck:
 * not translated here"); a chapter past the last he translated ("Joly's
 * translation ends at chapter 56" -- not "not translated here", which says
 * he skipped it); or, for a source with only chapter pages (Richard's
 * 西游记), that it goes a chapter at a time, the page a toggle away. */
/* Where a translator ends, as the book numbers it: a novel's list has 引首 and
 * 附录 in it, and 第十七回至十八回 is one entry, so the place in the list is not
 * the chapter's number. Anything else says the place. */
static void en_end_text(char *out, size_t n, const char *lab, int last) {
    const char *t = last >= 0 && last < ntchaps ? tchaps[last].title : "";
    const char *sp = strchr(t, ' ');
    const char *hui = strstr(t, "\345\233\236");       /* 回 */
    if (!strncmp(t, "\347\254\254", 3) && hui && sp && hui < sp)     /* 第 */
        snprintf(out, n, "%s's translation ends at %.*s", lab, (int)(sp - t), t);
    else
        snprintf(out, n, "%s's translation ends at chapter %d", lab, last + 1);
}

static void en_none_text(char *out, size_t n) {
    const char *lab = en_label();
    static int for_text = -1, last = -1;
    static char for_key[sizeof ((notesrc_t *)0)->key] = "";
    if (for_text != text_id || strcmp(for_key, en_key())) {
        last = db_source_last_chapter(text_id, en_key());
        for_text = text_id;
        snprintf(for_key, sizeof for_key, "%s", en_key());
    }
    if (!lab[0]) snprintf(out, n, "not translated here");
    else if (last >= 0 && tchap_idx > last)
        en_end_text(out, n, lab, last);
    else if (!src_lines(en_key(), NOTES_EN) && chapter_note_kind_of(tchap_id, en_key()) == 2)
        snprintf(out, n, "%s: by the chapter, not the sentence", lab);
    else snprintf(out, n, "%s: not translated here", lab);
}
/* Whether the sentence shown has the page's kind of row in its source,
 * cached per chapter, line and source: the page asks it for its layout, a
 * line at a time. */
static int sentence_has_notes(void) {
    static int for_chap = -1, for_line = -1, for_src = -1, has = 0;
    if (for_chap != tchap_id || for_line != tw_line || for_src != page_slot()) {
        has = page_line_has(tchap_id, tw_line);
        for_chap = tchap_id;
        for_line = tw_line;
        for_src = page_slot();
    }
    return has;
}
/* A sentence with nothing of its own opens the chapter page when that is
 * all the source has (a novel's translation), and at a chapter's first line
 * where the page is a note on the chapter (列子's 题注: the note on its
 * title comes first). Anywhere else it says so -- "nothing noted on this
 * sentence", "translator D: not translated here" -- with the toggle beside it:
 * translator D leaves out most of some chapters, and his page of the chapter is
 * not his notes on the sentence. */
static int page_forced(void) {
    return !sentence_has_notes() && (!page_src_lines() || (page_kind(tchap_id) == 1 && tw_line == 0));
}
/* The page's "chapter" toggle: where the source has the chapter's page and
 * there is a sentence page to come back to. */
static int chapter_toggle_shown(void) {
    return page_kind(tchap_id) != 0 && !page_forced();
}
/* The sentence screen's notes button: its own notes, a note on the chapter
 * where it opens (a chapter's first line; a source with only chapters), or,
 * with a choice of sources, the switch that finds another. A chapter's
 * translation alone is not notes: "English" beside it opens that (the
 * novels' Brewitt-Taylor). */
static int line_offers_notes(int line) {
    int k = chapter_note_kind(tchap_id);
    return line_has_notes(line) || n_nsrc > 1
        || (k == 1 && (line == 0 || !src_lines(note_key(), NOTES_NOTED)));
}

/* The next (dir 1) or previous (-1) sentence with notes -- or, on the
 * English's page, with English -- across chapter ends: found first, by the
 * chapter list and a look-up each, and only then opened -- loading every one
 * on the way was the 左传's 80-sentence gaps loaded one by one. The answer
 * for the line is kept, since the page asks it to decide whether to draw the
 * button at all: a book whose notes are few (中庸's one) must not offer a
 * button that goes nowhere. */
static int notes_step(int dir, int *oc, int *ol) {
    static int k_chap = -1, k_line = -1, k_src = -1, k_found[2], k_c[2], k_l[2], k_done[2];
    if (k_chap != tchap_id || k_line != tw_line || k_src != page_slot()) {
        k_chap = tchap_id; k_line = tw_line; k_src = page_slot();
        k_done[0] = k_done[1] = 0;
    }
    int d = dir > 0;
    if (!k_done[d]) {
        int c = tchap_idx, l = tw_line, found = 0;
        for (int guard = 0; guard < 5000 && !found; guard++) {
            if (dir > 0) {
                if (++l >= tchaps[c].n_line) {
                    if (++c >= ntchaps) break;
                    l = 0;
                }
            } else if (--l < 0) {
                if (--c < 0) break;
                l = tchaps[c].n_line - 1;
                if (l < 0) continue;
            }
            found = page_line_has(tchaps[c].id, l);
        }
        k_found[d] = found; k_c[d] = c; k_l[d] = l; k_done[d] = 1;
    }
    if (oc) *oc = k_c[d];
    if (ol) *ol = k_l[d];
    return k_found[d];
}

static void tnotes_load(void) {
    tnotes_laid_out = 0;
    tnote_picking = 0;
    tnote_cover = -1;
    tchapnote[0] = 0;
    tchaplabel[0] = 0;          /* the chapter's page sets its own */
    if (tnote_chap) {
        ntnotes = 0;
        db_text_chapter_note_of(tchap_id, page_key(), tchapnote, sizeof tchapnote,
                                tchaplabel, sizeof tchaplabel);
        return;
    }
    ntnotes = db_text_notes_of(tchap_id, tw_line, page_key(), page_what(), tnotes, MAXTNOTES);
    if (tnote_en && !ntnotes) {
        int a = db_text_en_cover(tchap_id, tw_line, page_key());
        if (a >= 0) ntnotes = db_text_notes_of(tchap_id, a, page_key(), NOTES_EN, tnotes, MAXTNOTES);
        if (ntnotes) tnote_cover = a;
    }
}

/* Which page the notes page shows -- the sentence's or the chapter's -- and
 * where in it: the chapter's when it was asked for and the source has it,
 * or when the sentence has nothing of its own there and the source nothing
 * else (page_forced); each page where it was left (tnote_chap_page,
 * tnote_line_page), a translation opened first about as far through as the
 * sentence is. Leaving a page keeps its place. */
static void notes_keep_place(void) {
    if (tnote_chap) {
        if (tnote_chap_for != tchap_id) {
            for (int i = 0; i < 2 * MAXNSRC; i++) tnote_chap_page[i] = -1;
            tnote_chap_for = tchap_id;
        }
        tnote_chap_page[page_slot()] = tnote_page;
    } else {
        tnote_line_page = tnote_page;
        tnote_line_for_c = tchap_id; tnote_line_for_l = tw_line; tnote_line_for_s = page_slot();
    }
}
static void notes_view(void) {
    int kind = page_kind(tchap_id);
    tnote_chap = kind && (tnote_chap_want || page_forced());
    tnote_picking = 0;
    tnote_about = 0;
    tnote_near = -1;
    tnotes_load();
    tnote_page = 0;
    if (tnote_chap) {
        if (tnote_chap_for != tchap_id) {
            for (int i = 0; i < 2 * MAXNSRC; i++) tnote_chap_page[i] = -1;
            tnote_chap_for = tchap_id;
        }
        if (tnote_chap_page[page_slot()] >= 0) tnote_page = tnote_chap_page[page_slot()];
        else if (kind == 2) tnote_near = ntlines > 1 ? (int)(1000L * tw_line / (ntlines - 1)) : 0;
    } else if (tnote_line_for_c == tchap_id && tnote_line_for_l == tw_line &&
               tnote_line_for_s == page_slot() && tnote_line_page >= 0) {
        tnote_page = tnote_line_page;
    }
}

/* One paragraph of the notes page, and one page's share of it. */
typedef struct { const char *text; int px, lineh, gap; uint8_t ink; int keep; int nlines; } nblock_t;
typedef struct { int block, first, count, page, y; } nslice_t;
#define MAXNBLOCKS 2048
#define NOTES_POOL (sizeof tchapnote + 65536)   /* the chapter note and a passage's */
#define MAXNSLICES (2 * MAXNBLOCKS)
/* below the switch between sources of notes, where the book has more than one */
static int notes_row_shown(void);
/* and below the sentence, where the page was opened from the chapter page */
static int notes_head_h(void);
#define NOTES_TOP  ((notes_row_shown() ? 200 : 124) + notes_head_h())
#define NOTES_BOT  (H - 150)

/* Lines a paragraph wraps to, every one of them: laid out a line at a time,
 * a paragraph is never cut short, and runs on onto the next page from the
 * line it stopped at -- where cutting Legge's long notes into pieces made
 * false paragraph breaks, some mid-sentence, and left pages a third empty. */
static int nblock_lines(const nblock_t *b) {
    int n = 0;
    for (const char *p = b->text; *p && n < 4000; n++)
        p = gfx_wrap_skip(b->px, W - 80, p, 1);
    return n;
}

/* Where a chapter page's text begins and ends, for "about here", which
 * counts only that: a translation after the translator's introduction to
 * the chapter (translator D's Lieh-tzu) is under a label (§) whose first word is
 * Translation, and his notes printed after his text -- translator E's "Notes",
 * translator D's "NOTES TO PART THREE", translator C's "ENDNOTES" and the "Notes"
 * over his footnotes -- under one whose first word is Notes or Endnotes.
 * Any case. `t` is the label's text, after the §; `w` in lower case. */
static int label_word(const char *t, size_t len, const char *w) {
    size_t k = 0;
    for (; w[k]; k++)
        if (k >= len || (t[k] | 0x20) != w[k]) return 0;
    char c = k < len ? (char)(t[k] | 0x20) : 0;
    return !(c >= 'a' && c <= 'z');
}
static int notes_label(const char *t, size_t len) {
    return label_word(t, len, "notes") || label_word(t, len, "endnotes");
}
static int text_label(const char *t, size_t len) { return label_word(t, len, "translation"); }

/* The page as paragraphs, and the slices of them each page shows. Copies of
 * the text live in `pool`, one paragraph per line of the source text. An
 * empty line is the space the book leaves -- between sections, before and
 * after verse, between a translation and its commentary -- and widens the
 * gap after the paragraph before it. `text_at` and `notes_at`, if given,
 * are where the chapter page's text begins and its notes (text_label,
 * notes_label): a block, or -1. */
static int nblocks_build(nblock_t *out, nslice_t *sl, int *nsl, int *npages,
                         char *pool, size_t pooln, int *text_at = NULL, int *notes_at = NULL) {
    int n = 0, in_chapter = 0, text = -1, notes = -1;
    size_t used = 0;
    auto add = [&](const char *t, int px, int lineh, int gap, uint8_t ink, int keep) {
        while (t && *t && n < MAXNBLOCKS) {
            const char *nl = strchr(t, '\n');
            size_t len = nl ? (size_t)(nl - t) : strlen(t);
            if (used + len + 1 > pooln) return;
            memcpy(pool + used, t, len);
            pool[used + len] = 0;
            /* "§key term": a label within a note, small and grey, kept with
             * what follows it */
            int label = len > 2 && !memcmp(t, "\xc2\xa7", 2);
            /* the book's space: two thirds of a line, what lies between
             * one passage's notes and the next's at 100% */
            int sp = lineh * 2 / 3;
            if (len > (size_t)(label ? 2 : 0)) {
                int lp = tpx(38);
                out[n].text = pool + used + (label ? 2 : 0);
                out[n].px = label ? lp : px; out[n].lineh = label ? lp * 13 / 10 : lineh;
                out[n].gap = label ? 2 : gap; out[n].ink = label ? 0x90 : ink;
                out[n].keep = label ? 1 : keep;
                if (label && in_chapter && notes < 0 && notes_label(t + 2, len - 2)) {
                    notes = n;              /* and the notes start after the space */
                    if (n > 0 && out[n - 1].gap < sp) out[n - 1].gap = sp;
                }
                if (label && in_chapter && text < 0 && notes < 0 && text_label(t + 2, len - 2))
                    text = n;
                n++;
            } else if (len == 0 && n > 0 && !out[n - 1].keep) {
                /* never between a label and what it labels */
                if (out[n - 1].gap < sp) out[n - 1].gap = sp;
            }
            used += len + 1;
            t = nl ? nl + 1 : NULL;
        }
    };
    /* Set at the text size, like everything else that is read: the notes
     * are reading too, and these are 6-7" panels: the pinyin and the
     * commentary at 34-36px were too small to read. A line is 1.3 of its size. */
    auto addt = [&](const char *t, int px, int gap, uint8_t ink, int keep) {
        int s = tpx(px);
        add(t, s, s * 13 / 10, gap, ink, keep);
    };
    /* The chapter's note is the chapter page's only content (tnote_chap:
     * tnotes_load loads it or the line's notes, never both). */
    auto chapter_note = [&]() {
        if (!tchapnote[0]) return;
        if (n) out[n - 1].gap = 44;
        addt(tchaplabel[0] ? tchaplabel : "the whole chapter", 38, 4, 0x90, 1);
        /* a commentator's note on the title is set as his notes on the
         * lines are, not as a translation is */
        in_chapter = 1;
        if (strstr(tchaplabel, "题注")) addt(tchapnote, 50, 10, 0x40, 0);
        else addt(tchapnote, 52, 4, BLACK, 0);
        in_chapter = 0;
        if (n) out[n - 1].gap = 34;
    };
    chapter_note();
    /* the English's page of a line inside another line's sentence: that
     * sentence, under a word saying so, as under the sentence */
    if (tnote_en && !tnote_chap && tnote_cover >= 0 && ntnotes) {
        char cn[64];
        cover_note(tw_line, tnote_cover, cn, sizeof cn);
        addt(cn, 38, 4, 0x90, 1);
    }
    /* A passage: its English -- on the notes' page too, where it is what
     * the notes are on: their numbers are in it (translator C's), their word by
     * word is of it (Linnell's, translator B's), their commentary follows it
     * (郭象 in translator E's) -- then its Chinese and pinyin where the translator
     * gives them, and on the notes' page the notes. The English's page is
     * the translation alone; a passage with nothing but English is never on
     * the notes' (db_text_notes_of, NOTES_NOTED). */
    for (int i = 0; i < ntnotes; i++) {
        const textnote_t *t = &tnotes[i];
        addt(t->english, 52, 6, BLACK, 0);
        if (n) out[n - 1].gap = 20;
        addt(t->zh, 64, 4, BLACK, 1);
        addt(t->pinyin, 54, 16, 0x50, 1);
        if (tnote_en) {
            if (n) out[n - 1].gap = 44;
            continue;
        }
        if (t->gloss[0]) {
            addt("word by word", 38, 2, 0x90, 1);
            addt(t->gloss, 50, 2, 0x30, 0);
            if (n) out[n - 1].gap = 16;
        }
        if (t->notes[0]) {
            /* notes that label their own parts (§key term, §grammar ...)
             * need no label over them all */
            if (strncmp(t->notes, "\xc2\xa7", 2)) addt("commentary", 38, 2, 0x90, 1);
            addt(t->notes, 50, 10, 0x40, 0);
        }
        if (n) out[n - 1].gap = 44;                   /* between passages */
    }
    for (int i = 0; i < n; i++) out[i].nlines = nblock_lines(&out[i]);

    /* Into pages, a line at a time. A label goes with the first line of
     * what it labels; a paragraph that does not fit fills the page and goes
     * on over the page, never leaving a single line behind on either. */
    int page = 0, y = NOTES_TOP, k = 0;
    auto slice = [&](int i, int first, int count) {
        if (k >= MAXNSLICES) return;
        sl[k].block = i; sl[k].first = first; sl[k].count = count;
        sl[k].page = page; sl[k].y = y;
        k++;
    };
    for (int i = 0; i < n; i++) {
        const nblock_t *b = &out[i];
        int left = b->nlines, first = 0;
        if (b->keep) {
            int need = left * b->lineh;
            if (i + 1 < n) need += out[i + 1].lineh * (out[i + 1].nlines > 1 ? 2 : 1);
            if (y + need > NOTES_BOT && y > NOTES_TOP) { page++; y = NOTES_TOP; }
        }
        while (left > 0) {
            int fit = (NOTES_BOT - y) / b->lineh;
            if (fit >= left) {
                slice(i, first, left);
                y += left * b->lineh + b->gap;
                left = 0;
            } else if (fit >= 2 || (y == NOTES_TOP && fit >= 1)) {
                int take = fit;
                if (left - take == 1 && take > 2) take--;     /* no lone line over the page */
                slice(i, first, take);
                first += take;
                left -= take;
                page++;
                y = NOTES_TOP;
            } else {
                page++;
                y = NOTES_TOP;
            }
        }
    }
    *nsl = k;
    *npages = page + 1;
    if (text_at) *text_at = text;
    if (notes_at) *notes_at = notes;
    return n;
}

/* The notes page for the line: its notes in the chosen source, or, where
 * that source has none on it but has the chapter's own (Linnell, Giles, a
 * note on the chapter's title), the chapter page -- a translation opened
 * about as far through as the line is. */
static void notes_open(void) {
    tnote_en = tnote_en_from_line = 0;
    tnote_chap_want = 0;
    tnote_chap_for = -1;              /* opened afresh: each page from its start */
    tnote_line_for_c = -1;
    notes_view();
}

/* Back from the sentence opened from the notes page's own (H_NOTESLINE):
 * that page where it was left, or, moved on to another sentence, its notes. */
static void notes_return(void) {
    tline_to_notes = 0;
    tnote_en = tnote_en_from_line = 0;
    if (tchap_id == tline_notes_chap && tw_line == tline_notes_line) notes_view();
    else notes_open();
    scr = SCR_TEXTNOTES;
}

/* One of the notes page's switch buttons, the one shown in black, all of
 * them in one type size: side by side, a label shrunk alone looked wrong. */
static void switch_btn(rect_t r, const char *label, int on, int px) {
    gfx_fill(r, on ? BLACK : WHITE);
    gfx_frame(r, 3, BLACK);
    int w = gfx_text_w(px, label);
    gfx_text(r.x + (r.w - w) / 2, r.y + r.h / 2 + px / 3, px, on ? WHITE : BLACK, label);
}

/* Under the header: the switch between the book's sources, where it has
 * more than one, and at the right the "chapter" toggle, held in on the
 * chapter page, where the source has one to go to and back from. The notes'
 * page has the notes' sources; the English's page, and the sentence screen
 * while its English is shown, the English's. */
static int notes_row_shown(void) { return (tnote_en ? n_esrc : n_nsrc) > 1 || chapter_toggle_shown(); }
/* Opened from the chapter page's margin, the notes have their sentence over
 * them: smaller than on the sentence screen, with its pinyin as the pinyin
 * toggle has it, two rows at most (the rest behind "…"), and a press on it
 * opens it as a sentence. */
static int notes_head_shown(void) {
    return notes_from_read && !tnote_en && !tnote_chap && tw_line >= 0 && tw_line < ntlines;
}
static void notes_head_size(int *px, int *pypx, int *rowh, int *rows) {
    *px = tpx(60); *pypx = tpx(34);
    /* the syllables' room kept with pinyin off too (drawn RR_NOPY), as on
     * the chapter page: a row shorter without them moved the notes under
     * the sentence, and the page kept to come back to (tnote_line_page)
     * opened on other notes than it was left on */
    *rowh = *px + *pypx + 22;
    *rows = ruby_rows(tw_line, 40, W - 80, *px, *pypx, 0, *rowh, 0, 0, NULL);
    if (*rows > 2) *rows = 2;
}
static int notes_head_h(void) {
    if (!notes_head_shown()) return 0;
    int px, pypx, rowh, rows;
    notes_head_size(&px, &pypx, &rowh, &rows);
    return 12 + rows * rowh + 30;
}
static void draw_notes_head(void) {
    if (!notes_head_shown()) return;
    int px, pypx, rowh, rows;
    notes_head_size(&px, &pypx, &rowh, &rows);
    int top = (notes_row_shown() ? 200 : 124) + 12;
    ruby_rows(tw_line, 40, W - 80, px, pypx, top, rowh, rows, RR_DRAW | (tread_py ? 0 : RR_NOPY), NULL);
    hotreg(rc(24, top, W - 48, rows * rowh), H_NOTESLINE);
    gfx_fill(rc(40, top + rows * rowh + 12, W - 80, 2), FAINT);
}
static int nsrc_px = 0;            /* the row's type size, as last drawn (the harness) */
/* `slot`: the toggle's room is kept wherever the book can show it, so the
 * row keeps one type size and one shape from source to source and sentence
 * to sentence: on e-ink a row that lays itself out again on every press
 * moves under the finger. `tog` draws the toggle, held in if `held`; `base`
 * is the switch's hot-rect range; `lead` names the row where it fits. */
static void draw_src_row(const notesrc_t *list, int n, int cur, int base, const char *lead,
                         int tog, int held, int slot) {
    int y = 128, h = 60, gap = 12;
    slot = tog || (n >= 2 && slot);
    const char *cl = "chapter";
    int lw = gfx_text_w(34, lead) + 20;
    /* One type size for the row -- the switch's labels and the toggle's --
     * as large as lets every label fit its button: 42 with three sources,
     * less with four and the toggle on the Clara. The lead ("notes",
     * "English") before the switch where it still leaves 34 or more. */
    auto cw_at = [&](int px) { return slot ? gfx_text_w(px, cl) + 48 : 0; };
    auto fits = [&](int left, int px) {
        if (n < 2) return 1;
        int right = W - 24 - (slot ? cw_at(px) + 24 : 0);
        int bw = (right - left - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++)
            if (gfx_text_w(px, list[i].label) > bw - 24) return 0;
        return 1;
    };
    /* Five sources or more (庄子's notes: Legge, translator C, translator C's 注, translator D,
     * translator E) beside the toggle would need equal buttons in type too small to
     * read on the Clara: each is then as wide as its label and a margin, the
     * room left over shared out equally, at the largest size that fits --
     * with the lead before the switch where that costs the labels no size. */
    int prop = n >= 5;
    auto pneed = [&](int px) {
        int need = gap * (n - 1);
        for (int i = 0; i < n; i++) need += gfx_text_w(px, list[i].label) + 24;
        return need;
    };
    auto pfits = [&](int left, int px) {
        return pneed(px) <= W - 24 - (slot ? cw_at(px) + 24 : 0) - left;
    };
    int left = 24 + lw, px = 42;
    if (!prop) {
        while (px > 34 && !fits(left, px)) px -= 2;
        if (!fits(left, px)) {
            left = 24;
            px = 42;
            while (px > 28 && !fits(left, px)) px -= 2;
        }
        /* Four sources whose labels will not read in equal buttons (孙子兵法's
         * "Ames, 1993", "Denma, 2001" ...) are laid out as five are */
        if (n >= 4 && px < 34) { prop = 1; left = 24 + lw; px = 42; }
    }
    if (prop) {
        int pn = 42, pl = 42;
        while (pn > 28 && !pfits(24, pn)) pn -= 2;
        while (pl > 28 && !pfits(24 + lw, pl)) pl -= 2;
        if (pl >= pn && pfits(24 + lw, pl)) px = pl;
        else { left = 24; px = pn; }
    }
    if (n < 2) px = 36;                   /* the toggle alone: as toggles are */
    nsrc_px = px;
    int right = W - 24 - (slot ? cw_at(px) + 24 : 0);
    if (tog) {
        int cw = cw_at(px);
        rect_t r = rc(W - 24 - cw, y, cw, h);
        /* toggle_btn's look, in the row's size */
        if (held) gfx_round_rect(r, 12, 6, BLACK, 0xC8);
        else      gfx_round_rect(r, 12, 3, BLACK, WHITE);
        int w = gfx_text_w(px, cl);
        gfx_text(r.x + (r.w - w) / 2, r.y + r.h / 2 + px / 3, px, BLACK, cl);
        hotreg(r, H_TCHAP);
    }
    if (n < 2) return;
    if (left > 24) gfx_text(28, y + h / 2 + 12, 34, 0x70, lead);
    int bw = (right - left - gap * (n - 1)) / n;
    int spare = prop ? right - left - pneed(px) : 0;
    for (int i = 0, x = left; i < n; i++) {
        int w = prop ? gfx_text_w(px, list[i].label) + 24 + spare / n
                       + (i == n - 1 ? spare % n : 0) : bw;
        rect_t r = rc(x, y, w, h);
        switch_btn(r, list[i].label, i == cur, px);
        hotreg(r, base + i);
        x += w + gap;
    }
}
/* Whether a source of the book's English has a page of any chapter, so the
 * English's row keeps the toggle's room; kept for the book open. */
static int en_slot_book(void) {
    static int for_text = -1, has = 0;
    if (for_text != text_id) {
        has = 0;
        for (int i = 0; i < n_esrc && !has; i++) has = db_source_has_chapter_pages(text_id, ensrcs[i].key);
        for_text = text_id;
    }
    return has;
}
static void draw_note_sources(void) {
    if (!notes_row_shown()) return;
    static int slot_for = -1, slot_book = 0;
    if (slot_for != text_id) { slot_book = db_text_has_chapter_toggle(text_id); slot_for = text_id; }
    if (tnote_en) draw_src_row(ensrcs, n_esrc, cur_esrc, H_ENSRC, "English", chapter_toggle_shown(),
                               tnote_chap, en_slot_book());
    else          draw_src_row(notesrcs, n_nsrc, cur_nsrc, H_NOTESRC, "notes", chapter_toggle_shown(),
                               tnote_chap, slot_book);
}

/* Another source's notes, on the same line: its sentence page, or its
 * chapter page where that was asked for, or where the sentence has nothing
 * of its own and the source has only chapters; back on one with notes on
 * the line, the line's notes again. */
static void note_source_pick(int i) {
    if (i == cur_nsrc || i < 0 || i >= n_nsrc) return;
    notes_keep_place();
    note_source_use(i);
    db_set_note_source_choice(text_id, notesrcs[i].key);
    notes_view();
}
/* Another source's English: under the sentence, or on the English's page. */
static void en_source_pick(int i) {
    if (i == cur_esrc || i < 0 || i >= n_esrc) return;
    if (scr == SCR_TEXTNOTES) notes_keep_place();
    en_source_use(i);
    db_set_en_source_choice(text_id, ensrcs[i].key);
    if (scr == SCR_TEXTNOTES) notes_view();
    else tline_page = 0;              /* the English's room moves the sentence's pages */
}

/* The chapter toggle: the chapter's page, or back to the sentence's -- or,
 * for the English's chapter page opened from under the sentence, back to
 * the sentence, where its toggle was. */
static void notes_chapter_toggle(void) {
    notes_keep_place();
    if (tnote_en && tnote_chap && tnote_en_from_line) {
        tnote_chap = tnote_chap_want = 0;
        tnote_en = tnote_en_from_line = 0;
        tline_page = 0;
        scr = SCR_TEXTLINE;
        return;
    }
    tnote_chap_want = !tnote_chap;
    notes_view();
}

/* The English's page, from the sentence: its translation of the sentence in
 * full (`chap` 0), or of the chapter, about where the sentence is (1). */
static void en_page_open(int chap, int from_line) {
    tnote_en = 1;
    tnote_en_from_line = from_line;
    tnote_chap_want = chap;
    tnote_chap_for = -1;              /* opened afresh: each page from its start */
    tnote_line_for_c = -1;
    notes_view();
    scr = SCR_TEXTNOTES;
}

static void draw_textnotes(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char h[240];
    snprintf(h, sizeof h, "%d/%d  \302\267  %s", tw_line + 1, ntlines, tchap_title);
    header(h, 1);
    if (!tnote_picking) draw_note_sources();
    if (!tnote_picking) draw_notes_head();

    static nblock_t blocks[MAXNBLOCKS];
    static nslice_t slices[MAXNSLICES];
    static char pool[NOTES_POOL];
    static char part[16384];     /* one page's share of a paragraph */
    /* Laid out once per line and text size, not on every page turn: a
     * chapter's translation is thousands of wrapped lines to measure. */
    static int n = 0, nsl = 0, laid_scale = -1, laid_w = -1, text_at = -1, notes_at = -1;
    /* The same notes at another text size -- back from settings -- open on
     * the page holding what was at the top of this one: its paragraph, as
     * far into it; a page number means other words at another size. The
     * point is kept for as long as it is on the page shown, so larger and
     * back again is the page it was, not the one before it. */
    static int anc_b = -1, anc_f = 0, anc_n = 1;
    if (!tnotes_laid_out || laid_scale != text_scale || laid_w != W) {
        auto nl = [&](int b) { return blocks[b].nlines > 0 ? blocks[b].nlines : 1; };
        auto le = [](int b1, long f1, long n1, int b2, long f2, long n2) {
            return b1 != b2 ? b1 < b2 : f1 * n2 <= f2 * n1;
        };
        int keep = 0;
        if (tnotes_laid_out && laid_w == W) {
            int i0 = -1, i1 = -1;
            for (int i = 0; i < nsl; i++) {
                if (slices[i].page == tnote_page && i0 < 0) i0 = i;
                if (slices[i].page == tnote_page + 1) { i1 = i; break; }
            }
            if (i0 >= 0) {
                int tb = slices[i0].block, tf = slices[i0].first, tn = nl(tb);
                int on = anc_b >= 0 && le(tb, tf, tn, anc_b, anc_f, anc_n)
                      && (i1 < 0 || !le(slices[i1].block, slices[i1].first,
                                        nl(slices[i1].block), anc_b, anc_f, anc_n));
                if (!on) { anc_b = tb; anc_f = tf; anc_n = tn; }
                keep = 1;
            }
        }
        nsl = 0;
        n = nblocks_build(blocks, slices, &nsl, &tnote_pages, pool, sizeof pool, &text_at, &notes_at);
        tnotes_laid_out = 1; laid_scale = text_scale; laid_w = W;
        /* the text's first page: its label's; its last: the one its last
         * paragraph before the notes ends on */
        tnote_text_first = 0;
        tnote_text_pages = tnote_pages;
        for (int i = 0; text_at > 0 && i < nsl; i++)
            if (slices[i].block >= text_at) { tnote_text_first = slices[i].page; break; }
        for (int i = 1; notes_at > 0 && i < nsl; i++)
            if (slices[i].block >= notes_at) { tnote_text_pages = slices[i - 1].page + 1; break; }
        if (keep && anc_b < n) {
            int f = (int)((long)anc_f * nl(anc_b) / anc_n);
            for (int i = 0; i < nsl; i++)
                if (slices[i].block == anc_b && f < slices[i].first + slices[i].count) {
                    tnote_page = slices[i].page;
                    break;
                }
        }
    }
    if (tnote_near >= 0) {
        /* as far through the translation as the line is through the chapter:
         * through its text, not the introduction before it (translator D's, up to
         * a quarter of his 列子's page) nor the notes a translator prints
         * after it (translator E's are up to a third of his page, translator C's up to
         * two thirds) */
        tnote_page = tnote_text_first
                   + (int)((long)tnote_near * (tnote_text_pages - 1 - tnote_text_first) / 1000);
        tnote_near = -1;
        tnote_about = 1;
    }
    if (tnote_page >= tnote_pages) tnote_page = tnote_pages - 1;
    if (tnote_page < 0) tnote_page = 0;
    tnote_top_b = -1;
    for (int i = 0; i < nsl; i++)
        if (slices[i].page == tnote_page) {
            tnote_top_b = slices[i].block;
            tnote_top_f = slices[i].first;
            tnote_top_n = blocks[slices[i].block].nlines > 0 ? blocks[slices[i].block].nlines : 1;
            break;
        }

    if (tnote_picking) {
        /* Every page a tap or two away: a grid of their numbers, ten to a
         * row, as many rows as fit at a size a finger can hit -- never
         * below 72px -- and at most 100 at a time (the hot-rect table holds
         * 128); more than that pages with earlier / later. */
        int cols = 10, gap = 8, cw = (W - 48 - (cols - 1) * gap) / cols;
        int avail = H - NOTES_TOP - 60 - 150;
        int rows_fit = (avail + gap) / (72 + gap);
        int per = rows_fit * cols;
        if (per > 100) per = 100;
        if (per < cols) per = cols;
        tnote_grid_per = per;
        if (tnote_grid_first < 0 || tnote_grid_first >= tnote_pages)
            tnote_grid_first = (tnote_page / per) * per;
        int first = tnote_grid_first, last = first + per < tnote_pages ? first + per : tnote_pages;
        int rows = (last - first + cols - 1) / cols;
        int ch = rows ? (avail - (rows - 1) * gap) / rows : 96;
        if (ch > 96) ch = 96;
        int px = ch >= 60 ? 34 : 28;
        char lab[64];
        snprintf(lab, sizeof lab, tnote_pages > per ? "go to page  (%d\342\200\223%d of %d)"
                                                    : "go to page", first + 1, last, tnote_pages);
        gfx_text(40, NOTES_TOP + 10, 34, 0x70, lab);
        for (int k = first; k < last; k++) {
            int j = k - first;
            rect_t r = rc(24 + (j % cols) * (cw + gap),
                          NOTES_TOP + 40 + (j / cols) * (ch + gap), cw, ch);
            int cur = k == tnote_page;
            gfx_round_rect(r, 8, cur ? 4 : 2, cur ? BLACK : 0x90, WHITE);
            char num[8];
            snprintf(num, sizeof num, "%d", k + 1);
            int w = gfx_text_w(px, num);
            gfx_text(r.x + (r.w - w) / 2, r.y + (r.h + px) / 2 - 4, px, BLACK, num);
            hotreg(r, H_ITEM + k);
        }
        if (tnote_pages > per) {
            int by = H - 130, bw = (W - 72) / 2;
            btn_t e = { rc(24, by, bw, 100), "earlier", 0 };
            btn_t l = { rc(48 + bw, by, bw, 100), "later", 0 };
            if (first > 0)                  { ui_btn(&e, 0); hotreg(e.r, H_TPGPREV); }
            if (last < tnote_pages)         { ui_btn(&l, 0); hotreg(l.r, H_TPGNEXT); }
        }
        return;
    }

    if (n == 0) {
        char none[96];
        if (tnote_chap) snprintf(none, sizeof none, "nothing on this chapter");
        else if (tnote_en) en_none_text(none, sizeof none);
        else snprintf(none, sizeof none, "nothing noted on this sentence");
        gfx_text(44, NOTES_TOP + 60, 40, GREY, none);
    } else if (getenv("HANZI_NO_EXAMPLES")) {
        /* The translation is not ours to publish: in a screenshot the page
         * keeps its shape and says what would be there. */
        for (int i = 0; i < nsl; i++) {
            if (slices[i].page != tnote_page) continue;
            const nblock_t *b = &blocks[slices[i].block];
            int bh = slices[i].count * b->lineh;
            gfx_fill(rc(40, slices[i].y + 8, (W - 80) * (b->px >= 40 ? 9 : 6) / 10,
                        bh - 12 > 8 ? bh - 12 : 8), 0xE8);
        }
        gfx_text(44, NOTES_TOP + 40, 34, 0x70,
                 "(the translation is omitted from published screenshots)");
    } else {
        for (int i = 0; i < nsl; i++) {
            if (slices[i].page != tnote_page) continue;
            const nblock_t *b = &blocks[slices[i].block];
            /* the lines this page has: from where the last page stopped to
             * where the next begins, wrapped again exactly as they were */
            const char *from = gfx_wrap_skip(b->px, W - 80, b->text, slices[i].first);
            const char *to   = gfx_wrap_skip(b->px, W - 80, from, slices[i].count);
            size_t len = (size_t)(to - from);
            if (len >= sizeof part) len = sizeof part - 1;
            memcpy(part, from, len);
            part[len] = 0;
            gfx_text_wrap(40, slices[i].y + b->px, b->px, b->lineh, W - 80,
                          slices[i].count + 1, b->ink, part);
        }
    }

    /* Paging runs on into the next sentence's notes, as reading does --
     * but a chapter's translation pages only through itself. */
    int by = H - 130, bw3 = (W - 96) / 3;
    int first = tnote_page == 0, last = tnote_page + 1 >= tnote_pages;
    btn_t pv = { rc(24, by, bw3, 100), first ? "prev sentence" : "prev", 0 };
    btn_t nx = { rc(24 + 2 * (bw3 + 24), by, bw3, 100), last ? "next sentence" : "next", 0 };
    /* Nothing before the book's first sentence or after its last: no button,
     * as on the sentence screen, rather than one that does nothing. */
    int none_before = first && (tnote_chap || !notes_step(-1, NULL, NULL));
    int none_after  = last && (tnote_chap || !notes_step(1, NULL, NULL));
    if (!none_before) { ui_btn(&pv, 0); hotreg(pv.r, H_TPREV); }
    if (!none_after)  { ui_btn(&nx, 1); hotreg(nx.r, H_TNEXT); }
    if (tnote_pages > 1) {
        /* the page number is also the way to any other page */
        char pg[64];
        snprintf(pg, sizeof pg, "%d of %d", tnote_page + 1, tnote_pages);
        int w = gfx_text_w(36, pg);
        rect_t pr = rc(24 + bw3 + 24, by, bw3, 100);
        gfx_round_rect(pr, 12, 2, 0xB4, WHITE);
        gfx_text(pr.x + (pr.w - w) / 2, by + (tnote_about ? 48 : 62), 36, 0x50, pg);
        if (tnote_about) {
            const char *ab = "about here";
            int aw = gfx_text_w(28, ab);
            gfx_text(pr.x + (pr.w - aw) / 2, by + 86, 28, 0x80, ab);
        }
        hotreg(pr, H_TPAGES);
    }
}

/* ---- guided writing ----------------------------------------------------- *
 * Where the app knows which character is wanted, it can check each stroke as
 * it is drawn rather than waiting to be told how it went. A stroke that is
 * right is kept and drawn from the model; one that is wrong is rubbed out
 * with a word about why.
 *
 * Off by default: writing freely and judging it yourself is the older
 * behaviour and still the better one for some people, so it is a setting
 * rather than a replacement. */

/* Milliseconds, 64-bit: `long` is 32 bits on the Kobo, where it wrapped
 * after 25 days awake -- and then the power button and autosleep, which
 * compare against it, stopped working. */
typedef long long msec_t;
static msec_t now_ms(void);        /* defined with the dispatch below */

static strokes_t g_model;          /* the character being written */
static char g_ch[8]   = "";
static int  g_next    = 0;         /* the stroke expected next    */
static int  g_have    = 0;         /* a model is loaded           */
static char g_msg[80] = "";
static msec_t g_msg_ms = 0;
/* Wrong attempts at the stroke in hand. Three of them and the stroke that was
 * wanted appears in grey: being told "no" over and over without ever being
 * shown what was meant is how someone gives up on a thing. */
static int  g_wrong = 0;
#define GUIDED_HINT_AFTER 3

void guided_reset(void) {
    if (g_have) db_strokes_free(&g_model);
    g_have = 0; g_next = 0; g_ch[0] = 0; g_msg[0] = 0;
    g_wrong = 0;
}

/* The box the run is in. By character alone, 谢谢's second box took over
 * the first's finished strokes and drew the whole character in it. */
static rect_t g_box = { 0, 0, 0, 0 };
static int same_box(rect_t a, rect_t b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void guided_begin(const char *ch, rect_t box) {
    if (g_have && strcmp(g_ch, ch) == 0 && same_box(g_box, box)) return;  /* on it */
    guided_reset();
    snprintf(g_ch, sizeof g_ch, "%s", ch);
    g_box  = box;
    g_have = db_strokes(ch, &g_model) > 0;
    g_next = 0;
}

/* The medians are in a 1024 box; the pen is in screen coordinates. */
static void ink_to_model(const inkstroke_t *st, rect_t box, mstroke_t *out) {
    int n = st->n;
    if (n > MATCH_MAXPT) n = MATCH_MAXPT;
    out->n = n;
    for (int i = 0; i < n; i++) {
        int src = (n == st->n) ? i : i * (st->n - 1) / (n - 1);
        out->p[i].x = (float)(st->p[src].x - box.x) * 1024.0f / (float)box.w;
        out->p[i].y = (float)(st->p[src].y - box.y) * 1024.0f / (float)box.h;
    }
}

static void model_stroke(int i, mstroke_t *out) {
    int n = g_model.nmed[i];
    if (n > MATCH_MAXPT) n = MATCH_MAXPT;
    out->n = n;
    for (int j = 0; j < n; j++) {
        out->p[j].x = (float)g_model.med[i][j*2];
        out->p[j].y = (float)g_model.med[i][j*2 + 1];
    }
}

/* The character being written and the box it belongs to, on whichever screen
 * is up, found from where the stroke started. Returns 0 where guided writing
 * does not apply -- the dictionary's draw pad has no target to check against,
 * which is the whole point of it. */
static int writing_target(int x, int y, char *ch, size_t chn, rect_t *box);

/* The stroke it wants may be shown once the answer is out, or when the
 * point is practice. On a write card still being answered it would be the
 * answer, so there it only says no. */
static int guided_may_hint(void) {
    return !(scr == SCR_REVIEW && mode == MODE_WRITE && !revealed
             && !practice_active);
}

/* Judge the stroke just finished. Returns 1 if it was kept. */
static int guided_try(ink_t *k, rect_t box) {
    if (!g_have || k->n < 1 || g_next >= g_model.n) return 1;

    mstroke_t model[64];
    int nm = 0;
    for (int i = 0; i < g_model.n && nm < 64; i++) model_stroke(i, &model[nm++]);

    mstroke_t u;
    ink_to_model(&k->s[k->n - 1], box, &u);
    matchout_t r = match_stroke(model, nm, g_next, &u, 1.0f);

    g_msg_ms = now_ms();
    if (r.res == MATCH_OK) {
        g_next++;
        g_wrong = 0;
        g_msg[0] = 0;
        /* Take the pen's version off the page: guided_draw puts the real
         * stroke there, drawn from the character's own outline, so what
         * builds up under your hand is the character as it is written and
         * not a skeleton of it.
         *
         * This is why every screen you write on has to call guided_draw --
         * the version that only one screen called is what made this look
         * like strokes disappearing. */
        k->n--;
        if (g_next >= g_model.n)
            snprintf(g_msg, sizeof g_msg, "%s \342\200\224 all %d strokes",
                     g_ch, g_model.n);
        return 1;
    }
    /* Refused: the stroke comes straight off the page and a line of text says
     * why. Nothing blinks -- on e-ink a flash is a full refresh, slow and
     * ugly, and over before you have looked at it. */
    k->n--;
    g_wrong++;
    if (r.res == MATCH_BACKWARDS)
        snprintf(g_msg, sizeof g_msg, "right shape, drawn backwards");
    else if (r.res == MATCH_ELSEWHERE && r.which >= 0)
        snprintf(g_msg, sizeof g_msg, "that is stroke %d \302\267 %d comes first",
                 r.which + 1, g_next + 1);
    else
        snprintf(g_msg, sizeof g_msg, "stroke %d of %d, again",
                 g_next + 1, g_model.n);
    if (g_wrong >= GUIDED_HINT_AFTER && guided_may_hint())
        snprintf(g_msg, sizeof g_msg, "stroke %d of %d \302\267 here it is",
                 g_next + 1, g_model.n);
    return 0;
}

/* Called after a stroke has been judged, by the pen's path and by the
 * tests alike. Keeping this in the event loop is what hid the last bug:
 * anything a test cannot call is a thing the tests do not cover. */
static void guided_stroke_done(void) {
    if (scr != SCR_TEXTWRITE || !g_have || g_next < g_model.n) return;
    if (tw_i >= tw_npos || tw_i >= MAXTCH) return;
    /* Finished stroke by stroke against the model: it is the character, and
     * there is no ink to judge -- every stroke on the page came from the
     * model, which is the whole point of writing it this way. */
    tw_magic[tw_i]    = 1;
    tw_rank[tw_i]     = 0;
    tw_guess[tw_i][0] = 0;
}

/* What has been accepted so far, drawn from the model. */
static void guided_draw(rect_t box) {
    if (!g_have) return;
    /* What has been accepted, drawn from the character's own outline. After
     * three wrong tries the stroke that was wanted joins it in grey, so
     * there is something to copy rather than only something to fail at. */
    int hint = g_wrong >= GUIDED_HINT_AFTER && g_next < g_model.n
            && guided_may_hint();
    if (g_next <= 0 && !hint) return;
    ui_char(box, &g_model, hint ? g_next + 1 : g_next,
            hint ? g_next : -1, BLACK);
}

/* The toggle, wherever you write. It was on one screen, which is how a
 * write card came to reject strokes with no way to turn that off and nothing
 * drawn to say why. Every writing screen draws this button and no screen
 * spells it out for itself. */
/* An on/off control: an ordinary button that stays held in while it is on
 * -- a grey fill and a heavy border -- and is plain when it is off. It is
 * the same shape as clear, prev and next beside it; a drawn switch looked
 * like something else, and a black slab is the loudest thing on e-ink. */
static void toggle_btn(rect_t r, const char *label, int on, int id) {
    if (on) gfx_round_rect(r, 12, 6, BLACK, 0xC8);
    else    gfx_round_rect(r, 12, 3, BLACK, WHITE);
    int px = 36, w = gfx_text_w(px, label);
    while (w > r.w - 24 && px > 26) { px -= 2; w = gfx_text_w(px, label); }
    gfx_text(r.x + (r.w - w) / 2, r.y + r.h / 2 + px / 3, px, BLACK, label);
    hotreg(r, id);
}

/* Guided writing: each stroke is checked as it is drawn, a wrong one is
 * taken back off, and a right one is redrawn as the character's own stroke. */
static void guided_button(rect_t r) {
    toggle_btn(r, "guided", guided_on, H_GUIDED);
}

/* Pen only, beside it on every writing screen. Not drawn on a Kobo with no
 * stylus -- the Clara -- where a finger is the only way to write. */
static void pen_button(rect_t r) {
    if (!has_pen) return;
    toggle_btn(r, "pen only", pen_only, H_PENONLY);
}

/* The two writing toggles, side by side in `r`, the same on every screen
 * that takes a pen. Without a stylus, guided has the whole of it. */
static void writing_toggles(rect_t r) {
    if (!has_pen) { guided_button(r); return; }
    int w = (r.w - 16) / 2;
    guided_button(rc(r.x, r.y, w, r.h));
    pen_button(rc(r.x + w + 16, r.y, r.w - w - 16, r.h));
}

/* And the overlay: the hint, when it is owed. Called by every writing
 * screen for the box the pen is in. */
static void guided_overlay(rect_t box, const char *ch) {
    if (!guided_on || !ch || !ch[0]) return;
    /* Only for the character actually in progress. Calling guided_begin()
     * here would reset the run every time another cell of the same card was
     * drawn, which on a two-character word means the first one's strokes
     * stop being accepted halfway through the second. */
    if (!g_have || strcmp(g_ch, ch) != 0 || !same_box(g_box, box)) return;
    guided_draw(box);
}

/* Where the note went on the last render, so that a stroke's repaint can
 * include it: the note sits below the box on some screens and far from it on
 * others, and a repaint of the box alone left it never shown. */
static rect_t g_note_rect = { 0, 0, 0, 0 };

static void guided_note_at(int x, int y, int w) {
    g_note_rect = rc(x, y - 40, w, 52);        /* whether or not there is one */
    if (!guided_on || !g_msg[0]) return;
    if (now_ms() - g_msg_ms > 6000) { g_msg[0] = 0; return; }
    gfx_text_fit(x, y, 38, 28, w, 0x40, g_msg);
}

static void guided_note(int y) { guided_note_at(40, y, W - 80); }

static void draw_textwrite(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char h[240];
    /* No "part 2/4": phrases are how the sentence is cut for writing, not
     * something to step through, and next crosses them without asking. */
    /* the count first: a long chapter title must not clip it away */
    snprintf(h, sizeof h, "%d/%d  \302\267  %s", tw_line + 1, ntlines, tchap_title);
    header(h, 1);

    if (tw_line >= ntlines || tw_npos == 0) {
        gfx_text(44, 260, 40, GREY, "nothing to write here");
        return;
    }
    const char *line = tlines[tw_line];
    int nall = u8_len(line);
    int cur  = tw_pos[tw_i < tw_npos ? tw_i : tw_npos - 1];

    /* The line itself, once asked for. Punctuation is shown -- it is how the
     * clause reads -- but it is never a character to write, so the marker
     * only ever lands on a hanzi. */
    if (tw_reveal >= 1) {
        /* The whole sentence, with the phrase in hand in black and the rest
         * of it faint: you can see what you are inside. */
        /* A window onto a sentence too long for the row, beginning a little
         * before the phrase in hand: from the start, a long sentence's later
         * phrases -- the one being written -- were past the edge. */
        int from = 0, total = 0;
        for (int k = 0; k < nall; k++) {
            char ch[8];
            u8_at(line, k, ch);
            if (text_visible(ch)) total += gfx_text_w(52, ch);
        }
        if (total > W - 84) {
            from = tw_seg_start[tw_seg] - 3;
            if (from < 0) from = 0;
        }
        int x = 40;
        if (from > 0) {
            gfx_text(x, 150, 52, 0xB0, "\342\200\246");
            x += gfx_text_w(52, "\342\200\246");
        }
        for (int k = from; k < nall; k++) {
            char ch[8];
            u8_at(line, k, ch);
            if (!text_visible(ch)) continue;
            int cw = gfx_text_w(52, ch);
            if (x + cw > W - 44) break;
            int inseg = k >= tw_seg_start[tw_seg] && k < tw_seg_end[tw_seg];
            gfx_text(x, 150, 52, inseg ? BLACK : 0xB0, ch);
            if (k == cur) gfx_fill(rc(x, 162, cw, 5), BLACK);
            x += cw;
        }
    }

    /* What has been written, in your own hand: the line assembling itself.
     * One cell per character you actually write, punctuation aside. */
    {
        int cell, gap, x, first, count;
        tw_strip_geom(&cell, &gap, &x, &first, &count);
        /* Three dots at an end the phrase runs on past, so a window onto a
         * long phrase does not read as the whole of it. */
        {
            int dy = TW_STRIP_Y + cell / 2 - 3;
            int right = x + count * cell + (count - 1) * gap;
            for (int d = 0; d < 3; d++) {
                if (first > 0)                gfx_fill(rc(x - 30 + d * 8, dy, 5, 5), 0x90);
                if (first + count < tw_npos)  gfx_fill(rc(right + 9 + d * 8, dy, 5, 5), 0x90);
            }
        }
        for (int k = first; k < first + count && k < tw_npos; k++) {
            rect_t c = rc(x, TW_STRIP_Y, cell, cell);
            /* The reading, over the box it belongs to -- what you write
             * from, and which box it is for, at a glance. The one being
             * written is black. */
            {
                char c5[8], sy[40];
                u8_at(line, tw_pos[k], c5);
                reading_at(tw_line, tw_pos[k], c5, sy, sizeof sy);
                if (sy[0]) {
                    int spx = 36, sw = gfx_text_w(spx, sy);
                    while (sw > cell + gap - 6 && spx > 22) { spx -= 2; sw = gfx_text_w(spx, sy); }
                    gfx_text(c.x + (cell - sw) / 2, TW_STRIP_Y - 14, spx,
                             k == tw_i ? BLACK : 0x90, sy);
                }
            }
            gfx_frame(c, k == tw_i ? 3 : 2, k == tw_i ? BLACK : 0xC8);
            /* The box opens its character in the dictionary, the one being
             * written too: the reading over it and
             * half the gap each side are part of it, so a 44px box on a
             * long phrase is still a 56px target. */
            hotreg(rc(c.x - gap / 2, TW_STRIP_Y - 50, cell + gap, cell + 50), H_TWCELL + k);
            if (tw_magic[k]) {
                char mc[8];
                strokes_t ms;
                u8_at(tlines[tw_line], tw_pos[k], mc);
                if (db_strokes(mc, &ms) > 0) {
                    ui_char(c, &ms, -1, -1, BLACK);
                    db_strokes_free(&ms);
                }
            } else if (tw_ink[k].n > 0) {
                ui_ink_thumb_draw(c, &tw_ink[k], 4);
            }
            /* No tick per cell. One acknowledgment when a character is
             * checked and one when the phrase is finished is enough; a tick
             * in every box turns writing out 道德经 into collecting stars. */
            x += cell + gap;
        }
    }

    char ch[8];
    u8_at(line, cur, ch);

    rect_t box = tw_box();
    ui_cell(box);
    strokes_t st;
    int have = db_strokes(ch, &st) > 0;
    if (tw_reveal >= 1 && have) ui_char(box, &st, -1, -1, FAINT);
    if (guided_on) { guided_begin(ch, box); guided_draw(box); }
    ui_ink_draw(&ink, INK_W);
    if (guided_on) guided_note(box.y + box.h + 44);

    /* Stroke order, a stroke at a time, when that is what is wanted. */
    int y = box.y + box.h + 22;
    /* The frames stop above the verdict line, which sits just over the
     * helper row: a fixed H - 244 let the last row run through it. */
    int ybot = cy2_band() - 44;
    if (tw_reveal >= 2 && have) {
        /* The character plainly, in black. The frames beside it already carry
         * the order a stroke at a time, so numbering this one as well is a
         * second answer to a question already answered. */
        /* As large as the space above the verdict allows, up to 260: at a
         * fixed 260 it ran into the buttons on the Clara. */
        int msz = ybot - y;
        if (msz > 260) msz = 260;
        if (msz < 100) msz = 100;
        rect_t m = rc(24, y, msz, msz);
        ui_cell(m);
        ui_char(m, &st, -1, -1, BLACK);
        hotreg(m, H_STROKEVIEW);

        int cell = 130, gap = 10;
        int fx = 24 + msz + 20, fw = W - 24 - fx;
        int per = (fw + gap) / (cell + gap);
        if (per < 1) per = 1;
        int want = (st.n + 1 + per - 1) / per;
        int fit  = (ybot - y + gap) / (cell + gap);
        int rows = want > fit ? fit : want;
        if (rows > 0) {
            rect_t fr = rc(fx, y, fw, rows * (cell + gap));
            ui_frames(fr, &st, cell, gap);
            /* Cut short: the last cell becomes an ellipsis, which reads as
             * "there is more" rather than as the end of the character, and
             * pressing it gives the character a page of its own. */
            if (want > rows) frames_more(fr, cell, gap, rows, per);
            hotreg(fr, H_STROKEVIEW);
        }
    }
    if (have) db_strokes_free(&st);

    /* How the character you just left went, said plainly in the band above
     * the buttons: next judges what is in the box and moves on in one press,
     * so the verdict for it is read on the screen that follows. Right gets
     * the character and a tick. Wrong gets what it came out closer to --
     * never a cross; being told off by a flashcard helps nobody. */
    if (tw_said >= 0) {
        int vy = cy2_band();
        char said[8], v[120] = "";
        int good = 0;
        tw_char_at(tw_said, said);
        if (tw_all_magic[tw_said] || tw_all_rank[tw_said] >= 0) {
            snprintf(v, sizeof v, "%s", said);
            good = 1;
        } else if (tw_all_rank[tw_said] == TW_WRONG) {
            int here = tw_said == tw_base + tw_i;
            if (tw_all_guess[tw_said][0])
                snprintf(v, sizeof v, "%s came out closer to %s%s", said,
                         tw_all_guess[tw_said], here ? " \302\267 write it again" : "");
            else if (here)
                snprintf(v, sizeof v, "write %s again", said);
        }
        /* Right: large, in the space under the box -- what you wrote beside
         * the character itself, and a tick. A line of small type at the
         * foot of the page was too little to notice for the one thing
         * worth acknowledging. Only while that space is free: with the
         * stroke order open, the frames are in it and the line stays. */
        int top = box.y + box.h + 24, room = cy2_band() - 30 - top;
        int big = good && tw_reveal < 2 && room >= 120;
        if (big) {
            int sz = room < 280 ? room : 280;
            int magic = tw_all_magic[tw_said];
            int tick = sz / 3, gap = 30;
            /* The boxes are centred on the page, under the writing box; the
             * tick hangs to the right of them rather than being counted in,
             * which pulled both boxes off centre. */
            int boxes = magic || tw_all_ink[tw_said].n <= 0 ? sz : 2 * sz + gap;
            int x = (W - boxes) / 2, y = top + (room - sz) / 2;
            if (!magic && tw_all_ink[tw_said].n > 0) {      /* yours */
                rect_t yc = rc(x, y, sz, sz);
                gfx_frame(yc, 2, 0xC8);
                ui_ink_thumb_draw(rc(x + sz / 12, y + sz / 12, sz * 5 / 6, sz * 5 / 6),
                                  &tw_all_ink[tw_said], sz > 200 ? 7 : 5);
                x += sz + gap;
            }
            strokes_t ms;                                    /* the character */
            rect_t mc = rc(x, y, sz, sz);
            gfx_frame(mc, 2, 0xC8);
            if (db_strokes(said, &ms) > 0) {
                ui_char(rc(x + sz / 12, y + sz / 12, sz * 5 / 6, sz * 5 / 6), &ms, -1, -1, BLACK);
                db_strokes_free(&ms);
            } else {
                int w = gfx_text_w(sz * 3 / 4, said);
                gfx_text(x + (sz - w) / 2, y + sz * 4 / 5, sz * 3 / 4, BLACK, said);
            }
            tick_mark(x + sz + gap, y + (sz - tick) / 2, tick, BLACK);
            /* The character opens in the dictionary; back comes back here. */
            hotreg(mc, H_VERDICTCH);
        } else if (v[0]) {
            gfx_text_fit(40, vy, 42, 32, W - 120, good ? BLACK : 0x60, v);
            if (good) tick_mark(40 + gfx_text_w(42, v) + 16, vy - 28, 26, BLACK);
        }
    }

    /* Three helpers: rub it out, a step of help, and guided writing. Pen or
     * finger is a setting now -- it is decided once, not per screen. */
    /* Shared out by proportion, not fixed widths: at fixed widths the
     * Clara cut "pen only" to "pen ...". */
    int cy2 = tw_helper_y();
    int avail = W - 48 - 3 * 16;
    int w_cl = avail * 18 / 100, w_hp = avail * 26 / 100;
    rect_t cl = rc(24, cy2, w_cl, 72);
    gfx_round_rect(cl, 12, 3, BLACK, WHITE);
    { int w = gfx_text_w(36, "clear");
      gfx_text(cl.x + (cl.w - w)/2, cl.y + 48, 36, BLACK, "clear"); }
    hotreg(cl, H_CLEAR);

    {   /* Help, a step at a time: the character, then its stroke order. */
        rect_t ck = rc(24 + w_cl + 16, cy2, w_hp, 72);
        gfx_round_rect(ck, 12, 3, BLACK, WHITE);
        const char *lb = tw_reveal == 0 ? "outline"
                       : tw_reveal == 1 ? "stroke order" : "hide";
        int w = gfx_text_w(36, lb);
        gfx_text(ck.x + (ck.w - w)/2, ck.y + 48, 36, BLACK, lb);
        hotreg(ck, H_TREVEAL);
    }
    int tx = 24 + w_cl + 16 + w_hp + 16;
    writing_toggles(rc(tx, cy2, W - 24 - tx, 72));

    /* prev at the left edge, next at the right, done between. Next judges
     * and moves on in one press, across phrases, and past the last
     * character it shows the sentence you wrote. */
    int by  = H - 130;
    int bw3 = (W - 24 * 4) / 3;
    btn_t pv = { rc(24, by, bw3, 100), "prev", 0 };
    btn_t dn = { rc(24 + bw3 + 24, by, bw3, 100), "done", 0 };
    btn_t nx = { rc(24 + 2 * (bw3 + 24), by, bw3, 100), "next", 0 };
    ui_btn(&pv, 0); ui_btn(&dn, 0); ui_btn(&nx, 1);
    hotreg(pv.r, H_TPREV); hotreg(dn.r, H_TDONE); hotreg(nx.r, H_TNEXT);
}

/* ---- where a list stands ------------------------------------------------ */

static void bar(rect_t r, int a, int b, int c, int total) {
    gfx_frame(r, 2, 0xB4);
    if (total <= 0) return;
    int x = r.x + 2, w = r.w - 4;
    int wa = a * w / total, wb = b * w / total, wc = c * w / total;
    gfx_fill(rc(x, r.y + 2, wa, r.h - 4), 0xE0);          /* unseen */
    gfx_fill(rc(x + wa, r.y + 2, wb, r.h - 4), 0x90);     /* learning */
    gfx_fill(rc(x + wa + wb, r.y + 2, wc, r.h - 4), BLACK); /* known */
}

static void draw_stats(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char h[240];
    snprintf(h, sizeof h, "%s  \302\267  progress", words_label);
    header(h, 1);

    liststats_t st;
    db_list_stats(words_list, today(), &st);

    char buf[160];
    words_writable(buf, sizeof buf, st.total, st.writable);
    gfx_text_fit(40, 150, 40, 32, W - 80, 0x70, buf);

    int y = 210;
    for (int m = 0; m < 2; m++) {
        int scope = m == MODE_WRITE ? st.writable : st.total;
        rect_t box = rc(24, y, W - 48, 300);
        gfx_round_rect(box, 16, 2, 0xB4, WHITE);

        gfx_text(46, y + 62, 52, BLACK,
                 m == MODE_WRITE ? "\345\206\231 writing" : "\350\257\273 reading");
        snprintf(buf, sizeof buf, "%d due now", st.due[m]);
        int tw = gfx_text_w(44, buf);
        gfx_text(W - 70 - tw, y + 62, 44, st.due[m] ? BLACK : 0x90, buf);

        bar(rc(46, y + 92, W - 92, 46), st.unseen[m], st.learning[m],
            st.known[m], scope);

        int pct = scope > 0 ? (st.known[m] * 100 / scope) : 0;
        snprintf(buf, sizeof buf, "%d%% known", pct);
        gfx_text(46, y + 190, 48, BLACK, buf);

        snprintf(buf, sizeof buf, "%d not started  \302\267  %d learning  \302\267  %d known",
                 st.unseen[m], st.learning[m], st.known[m]);
        gfx_text_fit(46, y + 246, 38, 32, W - 92, 0x60, buf);
        y += 320;
    }
    toastline();
}

/* One book: the two ways into it, and how far through it you are. The home
 * row carries a text and its word decks together, and they are different
 * destinations, so the choice is made here rather than by crowding three
 * buttons onto a row that has to fit a Clara. */
static void draw_book(void) {
    nhot = 0;
    gfx_clear(WHITE);
    const source_t *sv = cur_source();
    /* Every way here has checked the book is still there. Should one not,
     * home is drawn: returning drew a blank page with nothing to press. */
    if (!sv) { scr = SCR_HOME; draw_home(); return; }
    header(sv->name, 1);

    char buf[200];
    int y = 170;
    int has_text = 0;

    if (sv->text_i >= 0) {
        /* Where you left off, if you have been here before. Looked up by
         * chapter id rather than by index, because a rebuilt database
         * renumbers nothing but the app should not depend on that. */
        int last = db_text_last_chapter(texts[sv->text_i].id);
        char where[160] = "";
        textchap_t c;
        if (last && db_text_chapter(last, &c))
            snprintf(where, sizeof where, "%s  \302\267  line %d of %d",
                     c.title, db_text_read(last) + 1, c.n_line);
        if (where[0]) {
            rect_t r = rc(24, y, W - 48, 118);
            gfx_round_rect(r, 16, 3, BLACK, WHITE);
            gfx_text(48, y + 60, 46, BLACK, "continue reading");
            gfx_text_clip(48, y + 100, 36, W - 100, 0x70, where);
            hotreg(r, H_TCONTINUE);
            y += 138;
        }
        rect_t r = rc(24, y, W - 48, 118);
        gfx_round_rect(r, 16, where[0] ? 2 : 3, where[0] ? 0xB4 : BLACK, WHITE);
        gfx_text(48, y + 72, 46, BLACK, where[0] ? "all chapters" : "read the text");
        snprintf(buf, sizeof buf, texts[sv->text_i].n_chap == 1 ? "%d chapter" : "%d chapters",
                 texts[sv->text_i].n_chap);
        int tw = gfx_text_w(38, buf);
        gfx_text(W - 52 - tw, y + 70, 38, 0x70, buf);
        hotreg(r, H_TREAD);
        y += 138;
        has_text = 1;

        /* The other edition, where there is one: his text, his pinyin, and
         * his notes on every line. Its own place, like any text. */
        int ed = edition_of(sv->name);
        if (ed >= 0) {
            int last = db_text_last_chapter(texts[ed].id);
            textchap_t c;
            char where2[200];
            if (last && db_text_chapter(last, &c))
                snprintf(where2, sizeof where2, "%s  \302\267  line %d of %d",
                         c.title, db_text_read(last) + 1, c.n_line);
            else
                snprintf(where2, sizeof where2, "his text, pinyin and notes on every line");
            rect_t r2 = rc(24, y, W - 48, 118);
            gfx_round_rect(r2, 16, 2, 0xB4, WHITE);
            gfx_text(48, y + 60, 46, BLACK, "the other edition");
            gfx_text_clip(48, y + 100, 36, W - 100, 0x70, where2);
            chevron(r2);
            hotreg(r2, H_TEDITION);
            y += 138;
        }
    }
    if (sv->book_i >= 0) {
        /* Light unless it is the only way in: one heavy box on a page says
         * which one you came for, three say nothing. */
        rect_t r = rc(24, y, W - 48, 118);
        gfx_round_rect(r, 16, has_text ? 2 : 3, has_text ? 0xB4 : BLACK, WHITE);
        gfx_text(48, y + 72, 46, BLACK, "word decks");
        snprintf(buf, sizeof buf, books[sv->book_i].n_lists == 1
                 ? "%d list" : "%d lists", books[sv->book_i].n_lists);
        int tw = gfx_text_w(38, buf);
        gfx_text(W - 52 - tw, y + 70, 38, 0x70, buf);
        hotreg(r, H_DECKS);
        y += 138;
    }

    int dw = 0, dr = 0;
    if (sv->book_i >= 0) {
        liststats_t st;
        db_book_stats(books[sv->book_i].name, today(), &st);
        y += 24;
        words_writable(buf, sizeof buf, st.total, st.writable);
        gfx_text_fit(40, y, 38, 32, W - 80, 0x70, buf);
        y += 46;

        const int boxh = 220;   /* the tallest the three lines in it need */
        for (int m = 0; m < 2; m++) {
            int scope = m == MODE_WRITE ? st.writable : st.total;
            rect_t box = rc(24, y, W - 48, boxh);
            gfx_round_rect(box, 16, 2, 0xB4, WHITE);
            gfx_text(46, y + 62, 50, BLACK,
                     m == MODE_WRITE ? "\345\206\231 writing" : "\350\257\273 reading");
            snprintf(buf, sizeof buf, "%d due now", st.due[m]);
            int tw = gfx_text_w(42, buf);
            gfx_text(W - 70 - tw, y + 62, 42, st.due[m] ? BLACK : 0x90, buf);
            bar(rc(46, y + 92, W - 92, 42), st.unseen[m], st.learning[m],
                st.known[m], scope);
            int pct = scope > 0 ? (st.known[m] * 100 / scope) : 0;
            snprintf(buf, sizeof buf,
                     "%d%% known   \302\267   %d not started   \302\267   %d learning",
                     pct, st.unseen[m], st.learning[m]);
            gfx_text_fit(46, y + 186, 37, 32, W - 92, 0x60, buf);
            y += boxh + 20;
        }
        /* What the buttons offer is due *and* new, the same sum the home
         * screen shows, not the due column above it. */
        int fw2 = 0, fr2 = 0;
        db_counts(0, books[sv->book_i].name, MODE_WRITE, today(), &dw, &fw2);
        db_counts(0, books[sv->book_i].name, MODE_READ,  today(), &dr, &fr2);
        dw += fw2; dr += fr2;
    }
    toastline();

    /* Starting a test on this book, the same two buttons as the home screen
     * so the gesture is the same wherever you start from. */
    if (sv->book_i >= 0) {
        int by = H - 130, bw2 = (W - 72) / 2;
        for (int m = 0; m < 2; m++) {
            int size  = m == MODE_WRITE ? TEST_WRITE : TEST_READ;
            int avail = m == MODE_WRITE ? dw : dr;
            int nn    = size < avail ? size : avail;
            rect_t b  = rc(24 + m * (bw2 + 24), by, bw2, 104);
            gfx_round_rect(b, 16, 3, BLACK, WHITE);
            const char *nm2 = m ? "\350\257\273 read" : "\345\206\231 write";
            if (nn > 0) snprintf(buf, sizeof buf, "%s  %d card%s", nm2, nn, nn == 1 ? "" : "s");
            else        snprintf(buf, sizeof buf, "%s  nothing due today", nm2);
            label_in(b, 42, 66, BLACK, buf);
            hotreg(b, m ? H_GO_R : H_GO_W);
        }
    }
}

/* A sentence written out: the sentence as the book has it, and under it
 * what you wrote, in the same order -- the whole of it, not only the phrase
 * you ended on. Then the three things you might want next. "done" used to
 * be another way back, and the writing simply vanished. */
static void draw_textdone(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char h[240];
    /* the count first: a long chapter title must not clip it away */
    snprintf(h, sizeof h, "%d/%d  \302\267  %s", tw_line + 1, ntlines, tchap_title);
    header(h, 1);
    if (tw_line >= ntlines) return;

    int nhan = 0, right = 0;
    {
        int n = u8_len(tlines[tw_line]);
        for (int k = 0; k < n; k++) {
            char ch[8];
            u8_at(tlines[tw_line], k, ch);
            if (!is_han(ch) || nhan >= MAXLINECH) continue;
            if (tw_all_magic[nhan] || tw_all_rank[nhan] >= 0) right++;
            nhan++;
        }
    }

    /* The sentence, and a tick when every character in it came back right.
     * Nothing when some did not: the cells below show which. */
    char shown[TEXT_LINE_MAX];
    display_line(tlines[tw_line], shown, sizeof shown);
    int ty = gfx_text_wrap(40, 168, 52, 66, W - 110, 3, BLACK, shown);
    if (nhan > 0 && right == nhan) tick_mark(W - 64, 128, 32, BLACK);

    /* What you wrote, one cell per character, as large as the page allows. */
    int top = ty + 10, bot = H - 160, gap = 12;
    int cell = 240, per = 1, rows = 1;
    int fits = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (cell = 240; cell > 40; cell -= 4) {
            per  = (W - 48 + gap) / (cell + gap);
            if (per < 1) per = 1;
            rows = (nhan + per - 1) / per;
            if (rows * (cell + gap) <= bot - top) break;
        }
        fits = ((bot - top) / (cell + gap)) * per;
        if (nhan <= fits || pass) break;
        bot -= 48;              /* room for saying what is left out */
    }
    if (nhan > fits) {          /* never dropped without a word about it */
        char more[48];
        snprintf(more, sizeof more, "+%d more not shown", nhan - fits);
        int mw = gfx_text_w(34, more);
        gfx_text(W - 24 - mw, bot + 40, 34, 0x70, more);
    }
    for (int g = 0; g < nhan && g < fits; g++) {
        rect_t c = rc(24 + (g % per) * (cell + gap), top + (g / per) * (cell + gap),
                      cell, cell);
        ui_cell(c);
        if (tw_all_magic[g]) {
            char mc[8];
            strokes_t ms;
            tw_char_at(g, mc);
            if (db_strokes(mc, &ms) > 0) {
                ui_char(c, &ms, -1, -1, BLACK);
                db_strokes_free(&ms);
            }
        } else if (tw_all_ink[g].n > 0) {
            ui_ink_thumb_draw(c, &tw_all_ink[g], cell > 110 ? 5 : 4);
        }
    }

    int by = H - 130, bw3 = (W - 24 * 4) / 3;
    btn_t bk = { rc(24, by, bw3, 100), "back", 0 };
    btn_t ag = { rc(24 + bw3 + 24, by, bw3, 100), "write again", 0 };
    btn_t nx = { rc(24 + 2 * (bw3 + 24), by, bw3, 100),
                 tw_line + 1 < ntlines ? "next sentence"
                 : (tchap_idx + 1 < ntchaps ? "next chapter" : "back"), 0 };
    ui_btn(&bk, 0); ui_btn(&ag, 0); ui_btn(&nx, 1);
    hotreg(bk.r, H_BACK); hotreg(ag.r, H_TAGAIN);
    hotreg(nx.r, tw_line + 1 < ntlines || tchap_idx + 1 < ntchaps ? H_TNEXTLINE : H_BACK);
}

/* ---- practice overlay -------------------------------------------------- *
 * A free writing box with no grading attached, opened from a read card, the
 * study screen or a dictionary entry. It takes the whole word, not just its
 * first character: being shown 声音 and only allowed to write 声 is no use. */

static strokes_t prac_s[8];
static int  prac_n = 0;
static char prac_word[64] = "";
static ink_t prac_ink;
static int prac_reveal = 0;

/* Boxes, sized to the number of characters, the same way a write card does
 * it. Shared with the hit test so ink lands where the boxes are drawn. */
static void prac_geom(int *nn, int *ny, int *nsize) {
    int n = prac_n ? prac_n : 1;
    int maxcell = (n == 1 ? 740 : (n == 2 ? 560 : 420));
    int size = (W - 48 - (n - 1) * 20) / n;
    if (size > maxcell) size = maxcell;
    if (nn) *nn = n;
    if (ny) *ny = 170;
    if (nsize) *nsize = size;
}

static void draw_practice(void) {
    nhot = 0;
    gfx_clear(WHITE);
    char h[160];
    snprintf(h, sizeof h, "practise %s  \302\267  not scored", prac_word);
    header(h, 1);

    int n, py, size;
    prac_geom(&n, &py, &size);
    for (int i = 0; i < n; i++) {
        rect_t c = cellrect(i, n, py, size);
        ui_cell(c);
        /* Blank unless the outline is asked for. */
        if (prac_reveal && i < prac_n) ui_char(c, &prac_s[i], -1, -1, FAINT);
    }
    for (int i = 0; i < prac_n; i++) {
        char pch[8];
        if (u8_at(prac_word, i, pch)) {
            int pn, py2, psz;
            prac_geom(&pn, &py2, &psz);
            guided_overlay(cellrect(i, pn, py2, psz), pch);
        }
    }
    ui_ink_draw(&prac_ink, INK_W);

    int y    = py + size + 24;
    int ybot = H - 130 - 92;

    if (prac_reveal) {
        /* Stroke order for every character, frames first. */
        int cell = 132, gap = 12;
        for (int i = 0; i < prac_n; i++) {
            if (prac_s[i].n <= 0) continue;
            int per  = (W - 48 + gap) / (cell + gap);
            if (per < 1) per = 1;
            int rows = (prac_s[i].n + 1 + per - 1) / per;
            int need = rows * (cell + gap);
            if (y + need > ybot) break;
            y += ui_frames(rc(24, y, W - 48, need), &prac_s[i], cell, gap)
                 * (cell + gap) + 8;
        }
        int nsz = ybot - y - 16;
        if (nsz > 240) nsz = 240;
        if (nsz >= 120)
            for (int i = 0; i < n; i++) {
                rect_t m = cellrect(i, n, y, nsz);
                ui_cell(m);
                /* Plainly, as everywhere else: the frames above already give
                 * the order a stroke at a time. */
                if (i < prac_n) ui_char(m, &prac_s[i], -1, -1, BLACK);
            }
    }

    /* Same controls as a write card, in the same place -- with no stylus
     * too. The boxes take a finger on the Clara and guided judges what it
     * writes there, so without the toggle and its note it refused strokes
     * with no word why and no way to turn it off. */
    {
        int ty = H - 130 - 92;
        writing_toggles(rc(24, ty, has_pen ? 620 : 420, 72));
        guided_note_at(has_pen ? 670 : 470, ty + 48, W - (has_pen ? 694 : 494));
    }

    int by = H - 130;
    int bw = (W - 72) / 2;
    /* "show", not "outline": it brings up the stroke order as well. */
    btn_t sh = { rc(24, by, bw, 100), prac_reveal ? "hide" : "show", 0 };
    btn_t cl = { rc(24 + bw + 24, by, bw, 100), "clear", 0 };
    ui_btn(&sh, 1); ui_btn(&cl, 0);
    hotreg(sh.r, H_CHECK); hotreg(cl.r, H_CLEAR);
}

static int writing_target(int x, int y, char *ch, size_t chn, rect_t *box) {
    if (!guided_on) return 0;

    if (practice_active) {
        int n, py, size;
        prac_geom(&n, &py, &size);
        for (int i = 0; i < n && i < prac_n; i++) {
            rect_t c = cellrect(i, n, py, size);
            if (!ui_hit(c, x, y)) continue;
            u8_at(prac_word, i, ch);
            (void)chn;
            *box = c;
            return ch[0] != 0;
        }
        return 0;
    }
    if (scr == SCR_TEXTWRITE) {
        if (tw_line >= ntlines || tw_i >= tw_npos) return 0;
        rect_t b = tw_box();
        if (!ui_hit(b, x, y)) return 0;
        u8_at(tlines[tw_line], tw_pos[tw_i], ch);
        *box = b;
        return ch[0] != 0;
    }
    if (scr == SCR_REVIEW && mode == MODE_WRITE && have_card && !no_pen) {
        int n, cy, size;
        write_geom(&n, &cy, &size);
        for (int i = 0; i < n; i++) {
            rect_t c = cellrect(i, n, cy, size);
            if (!ui_hit(c, x, y)) continue;
            u8_at(card.headword, i, ch);
            *box = c;
            return ch[0] != 0;
        }
    }
    return 0;
}

/* ---- dispatch ---------------------------------------------------------- */

/* Resident size in KB, straight from /proc. Cheap, and it settles whether a
 * disappearance was the kernel reclaiming memory or something else. */
/* Monotonic milliseconds, for pacing the screen and for timing the log. */
static msec_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (msec_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long rss_kb(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return -1;
    long total = 0, res = 0;
    if (fscanf(f, "%ld %ld", &total, &res) != 2) res = -1;
    fclose(f);
    return res < 0 ? -1 : res * (sysconf(_SC_PAGESIZE) / 1024);
}

/* ---- PDFs: the list ------------------------------------------------------ *
 * Every PDF under the roots (pdf_default_roots: the Kobo's /mnt/onboard,
 * the desktop's $HANZI_PDF_DIRS or ~/Documents), found once a session and
 * again on "rescan", searched by name -- Latin from the keyboard, hanzi from
 * the draw pad, since many of the names are Chinese -- with the pinned ones
 * first. A path is a std::string throughout: they run past 200 bytes. */
#define PDF_MAXROWS    32          /* rows one page of the list can show */
#define PDF_SCAN_DEPTH 12
#define PDF_SCAN_MAX   20000
#define PDF_ROW_H      108
static_assert(H_PDFPIN + PDF_MAXROWS <= H_PV_QPICK, "H_PDFPIN range overlaps H_PV_QPICK");

struct pdfrow_t {
    std::string path, name, where;
    int pinned = 0, missing = 0;
    int spot_page = -1;            /* a pin's saved page, 0-based; -1 none */
};
static std::vector<pdf_file> pdf_found;   /* the last scan, sorted by name */
static int  pdf_scanned = 0;              /* scanned this session */
static char pdf_q[128] = "";              /* the search */
static int  pdf_kb = 0;                   /* the keyboard is up */
static std::vector<pdfrow_t> pdf_rows;    /* what the search holds: pins first */
static int  pdf_npinned = 0;

static std::string pdf_name_of(const std::string &path) {
    size_t sl = path.find_last_of('/');
    std::string n = sl == std::string::npos ? path : path.substr(sl + 1);
    if (n.size() > 4 && strcasecmp(n.c_str() + n.size() - 4, ".pdf") == 0) n.resize(n.size() - 4);
    return n;
}

/* The folder, said as briefly as it can be: with one place to look (the
 * status line names it), from there -- "" at its top; else under home
 * "~/...", and on the Kobo from the top of its storage. */
static std::vector<std::string> pdf_roots;      /* as the list was built */
static std::string pdf_where_of(const std::string &path) {
    size_t sl = path.find_last_of('/');
    std::string d = sl == std::string::npos ? "" : path.substr(0, sl);
    if (pdf_roots.size() == 1) {
        const std::string &r = pdf_roots[0];
        if (d == r) return "";
        if (d.size() > r.size() && d.compare(0, r.size(), r) == 0 && d[r.size()] == '/')
            return d.substr(r.size() + 1);
    }
#ifdef HANZI_DESKTOP
    const char *home = getenv("HOME");
    size_t hl = home ? strlen(home) : 0;
    if (hl > 1 && d.compare(0, hl, home) == 0 && (d.size() == hl || d[hl] == '/'))
        d = "~" + d.substr(hl);
#else
    static const char ON[] = "/mnt/onboard";
    if (d.compare(0, sizeof ON - 1, ON) == 0)
        d = d.size() > sizeof ON - 1 ? d.substr(sizeof ON) : "";
#endif
    return d;
}

/* Does the name hold the search? Latin letters in either case, anything
 * else (hanzi) byte for byte. */
static int pdf_matches(const std::string &name, const char *q) {
    if (!q[0]) return 1;
    std::string a(name), b(q);
    for (char &c : a) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    for (char &c : b) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return a.find(b) != std::string::npos;
}

/* The rows for the search: the pins that match (a pin whose file has gone
 * is listed, marked, so it can be unpinned), then every other match. One
 * query for the pins and one per pin for its place: pins are few. */
static void pdf_list_build(void) {
    pdf_rows.clear();
    pdf_roots = pdf_default_roots();
    std::unordered_map<std::string, int> pinned;
    for (const std::string &p : db_pdf_pins()) {
        pinned[p] = 1;
        pdfrow_t r;
        r.path = p;
        r.name = pdf_name_of(p);
        if (!pdf_matches(r.name, pdf_q)) continue;
        r.where = pdf_where_of(p);
        r.pinned = 1;
        r.missing = access(p.c_str(), R_OK) != 0;
        pdf_spot_t s;
        if (db_pdf_spot(p.c_str(), &s)) r.spot_page = s.page;
        pdf_rows.push_back(std::move(r));
    }
    pdf_npinned = (int)pdf_rows.size();
    for (const pdf_file &f : pdf_found) {
        if (pinned.count(f.path) || !pdf_matches(f.name, pdf_q)) continue;
        pdfrow_t r;
        r.path = f.path;
        r.name = f.name;
        r.where = pdf_where_of(f.path);
        pdf_rows.push_back(std::move(r));
    }
}

/* Where it looks, for the screen to say: "~/Documents", "the Kobo". */
static std::string pdf_roots_said(void) {
    std::string s;
    std::vector<std::string> keep;
    keep.swap(pdf_roots);                 /* each root said in full, not as "" */
    for (const std::string &r : pdf_default_roots()) {
        std::string w = pdf_where_of(r + "/x");
        if (w.empty()) w = "the Kobo";
        s += (s.empty() ? "" : ", ") + w;
    }
    pdf_roots.swap(keep);
    return s;
}

/* Find them all again. Said on the panel first: on the Kobo a card of books
 * takes a while, and a panel still showing the menu reads as a press that
 * did nothing. Inside long_task, so the watchdog leaves it be. */
static void pdf_rescan(void) {
    std::vector<std::string> roots = pdf_default_roots();
    nhot = 0;
    gfx_clear(WHITE);
    header("PDFs", 0);
    std::string msg = "looking for PDFs in " + pdf_roots_said();
    gfx_text_fit(40, 220, 41, 30, W - 80, 0x40, msg.c_str());
    if (!sleeping) surf_present(rc(0, 0, W, H), RF_PART);
    long_task("looking for PDFs");
    msec_t t0 = now_ms();
    pdf_found = pdf_scan(roots, PDF_SCAN_DEPTH, PDF_SCAN_MAX);
    pdf_scanned = 1;
    fprintf(stderr, "pdf: %zu PDFs in %s (%ld ms)\n", pdf_found.size(),
            pdf_roots_said().c_str(), (long)(now_ms() - t0));
    nhot = 0;
}

static void pdfs_open(void) {
    toast[0] = 0;
    if (!pdf_scanned) pdf_rescan();
    pdf_kb = 0;
    scroll = 0;
    pdf_list_build();
    scr = SCR_PDFS;
}

/* A button of the PDF screens: framed like the header's, label set to fit. */
static void pdf_btn(rect_t r, const char *label, int id) {
    gfx_round_rect(r, 12, 3, BLACK, WHITE);
    label_in(r, 36, r.h / 2 + 13, BLACK, label);
    hotreg(r, id);
}

static void draw_pdfs(void) {
    nhot = 0;
    gfx_clear(WHITE);
    header("PDFs", 1);

    /* the search, then the two ways to fill it that the keyboard has not:
     * hanzi drawn, and the card looked through again */
    int dw = 134, rw = 160;
    rect_t f = rc(24, 112, W - 48 - dw - rw - 24, 82);
    gfx_round_frame(f, 14, pdf_kb ? 5 : 3, BLACK);
    gfx_text_clip(f.x + 22, f.y + 56, 46, f.w - 40, pdf_q[0] ? BLACK : GREY,
                  pdf_q[0] ? pdf_q : "search by name");
    hotreg(f, H_PDFFIELD);
    pdf_btn(rc(f.x + f.w + 12, 112, dw, 82), "draw", H_PDFDRAW);
    pdf_btn(rc(f.x + f.w + 24 + dw, 112, rw, 82), "rescan", H_PDFRESCAN);

    char buf[256];
    int total = (int)pdf_found.size();
    if (pdf_q[0])
        snprintf(buf, sizeof buf, "%d of %d PDF%s %s \"%s\"", (int)pdf_rows.size(), total,
                 total == 1 ? "" : "s", (int)pdf_rows.size() == 1 ? "holds" : "hold", pdf_q);
    else
        snprintf(buf, sizeof buf, "%d PDF%s in %s \302\267 %d pinned", total, total == 1 ? "" : "s",
                 pdf_roots_said().c_str(), pdf_npinned);
    gfx_text_fit(40, 244, 36, 28, W - 80, 0x70, buf);

    int kbtop = pdf_kb ? H - KB_HEIGHT : H;
    int pgy = kbtop - (pdf_kb ? 96 : 110);
    int rowsbot = pgy - 10 - (toast[0] ? 56 : 0);
    if (toast[0]) toast_y = pgy - 20;

    int n = (int)pdf_rows.size();
    if (scroll >= n) scroll = n > 0 ? n - 1 : 0;
    int y = 266, shown = 0;
    for (int i = scroll; i < n && shown < PDF_MAXROWS && y + PDF_ROW_H <= rowsbot; i++, shown++) {
        const pdfrow_t &row = pdf_rows[i];
        rect_t r = rc(24, y, W - 48, PDF_ROW_H - 12);
        gfx_frame(r, 2, row.pinned ? 0x80 : GREY);
        hotreg(r, H_ITEM + shown);                   /* the row first: its pin shadows it */
        rect_t pb = rc(r.x + r.w - 150 - 14, r.y + (r.h - 70) / 2, 150, 70);
        toggle_btn(pb, row.pinned ? "pinned" : "pin", row.pinned, H_PDFPIN + shown);
        int tw = pb.x - 44 - 56;
        gfx_text_clip(44, y + 46, 40, tw, row.missing ? GREY : BLACK, row.name.c_str());
        std::string sub = row.missing ? std::string("not found") : row.where;
        if (!row.missing && row.spot_page >= 0) {
            snprintf(buf, sizeof buf, "%sp. %d", sub.empty() ? "" : "  \302\267  ", row.spot_page + 1);
            sub += buf;
        }
        gfx_text_clip(44, y + 82, 32, tw, 0x70, sub.c_str());
        chevron(rc(r.x, r.y, pb.x - r.x - 8, r.h));
        y += PDF_ROW_H;
    }
    if (n == 0) {
        std::string e = total == 0 && pdf_npinned == 0 && !pdf_q[0]
                      ? "no PDFs in " + pdf_roots_said()
                      : std::string("no name holds \"") + pdf_q + "\"";
        gfx_text_fit(44, 330, 39, 30, W - 88, GREY, e.c_str());
    }
    page_step = shown > 0 ? shown : 1;
    pager(pgy, n, scroll, shown);
    toastline();

    if (pdf_kb) {
        rect_t kb = rc(0, kbtop, W, KB_HEIGHT);
        ui_keyboard(kb);
        hotreg(kb, H_KEY);
    }
}

/* ---- PDFs: a page -------------------------------------------------------- *
 * The page fills the screen between two thin bars: back, the title, the
 * page and the press-on-the-page toggles over it; prev, next and the zoom
 * under it. Zoom comes in steps, never pinched: the whole page (fit); a
 * quarter of it at twice that (quarter: chosen by pressing it on the page
 * split in four, then stepped through by prev/next and the page keys); a
 * half (half: top and bottom for a page taller than wide, else left and
 * right, at the size that fills the screen with it, stepped the same way --
 * where the screen is the wrong shape for a half, as a portrait one is, it
 * shows as much more of the page as fits and steps down it); and under all
 * three the margins trimmed in steps towards the middle (trim +/-). The
 * page can be turned in quarter turns (rotate), which MuPDF does (pdf.h's
 * view rotation), and on the Kobo the half view can FLIP the screen to
 * landscape (surf_set_rotation), so a half fills it. The rendered view is
 * kept (pv_cache) and drawn again from there: only a new page, zoom, trim,
 * rotation or screen shape renders, never a redraw.
 *
 * A pinned PDF's spot -- page, zoom and its part, trim, rotation, flip --
 * is kept in user.db as it changes (db_pdf_set_spot reads before it writes)
 * and is where it opens again. */
#define PV_TOP  84               /* the bar over the page */
#define PV_BOT 100               /* and the one under it */
#define PV_PAD   6
#define PV_TRIM_MAX  6
#define PV_TRIM_STEP 0.04f       /* of the page's width and height, each side */
#define PV_LAST 9999             /* a part: the last one there is */
enum { PZ_FIT, PZ_QUAD, PZ_HALF, PZ_N };
/* What a press on the page does: nothing yet, read the character there
 * (tap OCR), or take ink (drawing over the page). The hooks for both are
 * pdfv_page_rect, pdfv_to_page, pdfv_pixels and pdfv_page_tap below. */
enum { PT_NONE, PT_OCR, PT_DRAW };

struct pdfview_t {
    int open = 0;
    std::string path, name;
    int page = 0, npages = 0;
    int zoom = PZ_FIT, part = 0, trim = 0, rot = 0, flip = 0;
    int choosing = 0;            /* the page at fit, split in four: pick a quarter */
    int tapmode = PT_NONE;
    int back = SCR_PDFS;
    int comic = 0;               /* opened from the comic reader: a way back to its plate */
};
static pdfview_t pv;
static int pv_force_flip = 0;    /* the harness: offer flip where it is not (the desktop) */
static int pv_renders = 0;       /* pages actually rendered, for the harness */

/* The layout of the page on screen, from pv and the screen's size alone,
 * shared by the drawing and every press. Points are the page's after the
 * view's rotation ("turned points"). */
struct pv_geom_t {
    rect_t vp;                   /* the room between the bars */
    float scale;                 /* pixels a point */
    int sx, sy, sw, sh;          /* the part of the view (pdf.h) shown */
    int dx, dy;                  /* where that lands on screen */
    int nparts;                  /* 4 quarters, the half view's steps, or 1 */
};

static int pv_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* The page MuPDF has in hand (pdf.cpp keeps the last one parsed, and its
 * text once asked for). Parsing a page into its display list, or reading
 * its text, takes the whole page through MuPDF, and a heavy one (a map, a
 * drawing of many thousand paths) can outlast the watchdog on the Kobo's
 * CPU: so each is a long_task -- once a page, not on every press. */
static struct { std::string path; int page = -1, text = 0; } pv_held;
static void pv_hold_page(int text) {
    if (pv_held.page != pv.page || pv_held.path != pv.path) {
        pv_held.path = pv.path;
        pv_held.page = pv.page;
        pv_held.text = 0;
        long_task("parsing a PDF page");
    }
    if (text && !pv_held.text) {
        pv_held.text = 1;
        long_task("reading the text of a PDF page");
    }
}

static int pdfv_geom(pv_geom_t *g) {
    memset(g, 0, sizeof *g);
    g->vp = rc(0, PV_TOP, W, H - PV_TOP - PV_BOT);
    g->nparts = 1;
    float pw = 0, ph = 0;
    if (!pv.open) return 0;
    pv_hold_page(0);
    if (!pdf_page_size(pv.page, &pw, &ph) || pw <= 0 || ph <= 0) return 0;
    if (pv.rot == 90 || pv.rot == 270) { float t = pw; pw = ph; ph = t; }
    float t = pv.trim * PV_TRIM_STEP;
    float cx = pw * t, cy = ph * t, cw = pw * (1 - 2 * t), ch = ph * (1 - 2 * t);
    float vw = (float)(g->vp.w - 2 * PV_PAD), vh = (float)(g->vp.h - 2 * PV_PAD);
    int zoom = pv.choosing ? PZ_FIT : pv.zoom;
    float s, fx, fy, fw, fh;     /* what is shown, in view pixels */
    if (zoom == PZ_HALF) {
        int down = ch >= cw;     /* taller than wide: halved top and bottom */
        s = down ? fminf(vw / cw, vh / (ch / 2)) : fminf(vw / (cw / 2), vh / ch);
        float len = (down ? ch : cw) * s, room = down ? vh : vw;
        int n = (int)ceilf(len / room - 0.02f);
        if (n < 1) n = 1;
        /* up to 2% longer than its steps show: drawn that much smaller
         * rather than given a step more -- and never left with a strip
         * that no step reaches */
        if (len > n * room) { s *= n * room / len; len = n * room; }
        g->nparts = n;
        int k = pv_clampi(pv.part, 0, n - 1);
        float shown = len < room ? len : room;
        float off = n > 1 ? k * (len - shown) / (n - 1) : 0;
        if (down) { fx = cx * s; fy = cy * s + off; fw = fminf(cw * s, vw); fh = shown; }
        else      { fx = cx * s + off; fy = cy * s; fw = shown; fh = fminf(ch * s, vh); }
    } else {
        float rx = cx, ry = cy, rw = cw, rh = ch;
        if (zoom == PZ_QUAD) {
            rw = cw / 2; rh = ch / 2;
            int q = pv_clampi(pv.part, 0, 3);
            rx = cx + (q % 2) * rw;
            ry = cy + (q / 2) * rh;
            g->nparts = 4;
        }
        s = fminf(vw / rw, vh / rh);
        fx = rx * s; fy = ry * s; fw = rw * s; fh = rh * s;
    }
    g->scale = s;
    g->sx = (int)lroundf(fx);
    g->sy = (int)lroundf(fy);
    g->sw = pv_clampi((int)fw, 1, (int)vw);
    g->sh = pv_clampi((int)fh, 1, (int)vh);
    g->dx = g->vp.x + (g->vp.w - g->sw) / 2;
    g->dy = g->vp.y + (g->vp.h - g->sh) / 2;
    return 1;
}

static int pdfv_can_flip(void) { return surf_can_rotate() || pv_force_flip; }

/* The screen the PDF page wants: turned a quarter for the flipped half view
 * on the Kobo, upright for everything else. Called by render() before any
 * screen draws, so leaving the page (back, settings) always comes upright,
 * and coming back flips again. */
static int pdfv_flip_wanted(void) {    /* the view's own: flip on, in the half view */
    return pv.open && pv.flip && pv.zoom == PZ_HALF && pdfv_can_flip();
}
static void pdfv_orient(void) {
    int want = scr == SCR_PDFVIEW && !practice_active && !sleeping && !pv.choosing
            && pdfv_flip_wanted() ? 1 : 0;
    if (surf_rotation() == want) return;
    if (surf_set_rotation(want) == 0) {
        W = gfx_w;
        H = gfx_h;
        fprintf(stderr, "pdf: screen %s, %dx%d\n", want ? "flipped" : "upright", W, H);
    } else {
        fprintf(stderr, "pdf: the screen would not turn\n");
        pv.flip = 0;
    }
}

/* The rendered view, kept: drawn again only when what it shows changes. */
static struct {
    std::string path;
    int page = -1, rot = 0, sx = 0, sy = 0, sw = 0, sh = 0, ok = 0;
    float scale = 0;
    std::vector<uint8_t> px;
    std::string err;             /* why it could not be drawn: pdf_error() is
                                    cleared by the next call, and a redraw of
                                    the kept failure makes none */
} pv_cache;

static void pdfv_cache_drop(void) {
    pv_cache.path.clear();
    pv_cache.page = -1;
    pv_cache.err.clear();
    std::vector<uint8_t>().swap(pv_cache.px);
    pv_held.page = -1;           /* a document opened again starts with none */
}

static int pdfv_render(const pv_geom_t &g) {
    if (pv_cache.page == pv.page && pv_cache.path == pv.path && pv_cache.rot == pv.rot
        && pv_cache.scale == g.scale && pv_cache.sx == g.sx && pv_cache.sy == g.sy
        && pv_cache.sw == g.sw && pv_cache.sh == g.sh)
        return pv_cache.ok;
    pv_cache.px.assign((size_t)g.sw * g.sh, WHITE);
    long_task("drawing a PDF page");      /* a big scan, on the Kobo's CPU */
    msec_t t0 = now_ms();
    pv_cache.ok = pdf_render(pv.page, g.scale, pv.rot, g.sx, g.sy, g.sw, g.sh,
                             pv_cache.px.data(), g.sw);
    pv_cache.err = pv_cache.ok ? "" : pdf_error();
    pv_renders++;
    fprintf(stderr, "pdf: page %d drawn, %dx%d at %.2f px/pt, %ld ms%s%s\n", pv.page + 1,
            g.sw, g.sh, g.scale, (long)(now_ms() - t0),
            pv_cache.ok ? "" : ": ", pv_cache.ok ? "" : pdf_error());
    pv_cache.path = pv.path;
    pv_cache.page = pv.page;
    pv_cache.rot = pv.rot;
    pv_cache.scale = g.scale;
    pv_cache.sx = g.sx; pv_cache.sy = g.sy; pv_cache.sw = g.sw; pv_cache.sh = g.sh;
    return pv_cache.ok;
}

/* ---- hooks for pressing on the page (tap OCR, drawing over it) ---------- */
/* Where the page is on screen. */
static rect_t pdfv_page_rect(void) {
    pv_geom_t g;
    if (!pdfv_geom(&g)) return rc(0, 0, 0, 0);
    return rc(g.dx, g.dy, g.sw, g.sh);
}
/* A point on screen in the page's own coordinates (pdf.h's "page": points,
 * before the view's rotation): what pdf_char_at and the text layer take.
 * 0 off the page. */
static int pdfv_to_page(int x, int y, float *px, float *py) {
    pv_geom_t g;
    if (!pdfv_geom(&g) || !ui_hit(rc(g.dx, g.dy, g.sw, g.sh), x, y)) return 0;
    return pdf_view_to_page(pv.page, g.scale, pv.rot, (float)(g.sx + x - g.dx),
                            (float)(g.sy + y - g.dy), px, py);
}
/* The grey pixels of the page as drawn (sw x sh, a row sw bytes), and where
 * they sit on screen; NULL before anything is drawn. For an OCR crop: it is
 * the page as seen, at the zoom it is read at. */
static const uint8_t *pdfv_pixels(rect_t *where) {
    pv_geom_t g;
    if (!pdfv_geom(&g) || !pv_cache.ok || pv_cache.page != pv.page || pv_cache.sw != g.sw
        || pv_cache.sh != g.sh || pv_cache.px.empty()) return NULL;
    if (where) *where = rc(g.dx, g.dy, g.sw, g.sh);
    return pv_cache.px.data();
}
/* ---- the PDF page: what a press on it reads, and what is drawn over it --- *
 * OCR on: a press on the page reads the character under it. Where the page's
 * text layer can be believed (pdf_is_scan says it is not mostly picture) and
 * the character there is a hanzi, that is the answer, at once. Anything else
 * -- a scan, whose layer is somebody's OCR and often garbage, or a picture of
 * text on a text page -- is read from the picture (ocr.h), the layer's
 * character only one candidate more. The candidates are the draw pad's grid
 * (cand_grid), in a panel on the half of the page away from the press, under
 * the line they were read from (each of its hanzi pressable too); choosing
 * one opens the character screen, whose back comes back here, to the same
 * spot with the panel still up. Only hanzi are offered: nothing else has
 * anything to look up.
 *
 * draw on: the page is ink paper, greyed so the ink shows on it. Strokes are
 * recognised on every lift by the handwriting search; a word written in one
 * go is cut into characters by where its strokes are (ink_clusters) and each
 * is recognised on its own. The panel is the draw pad's: the word read so
 * far, the candidates of one of its characters, undo, clear, pen only and
 * look up. Its side is chosen by the first stroke and kept while there is
 * ink, so nothing moves under what was written. A lift repaints the stroke
 * and the panel over the kept page (pv_cache), never the PDF.
 *
 * Leaving the view as it was drawn -- a page turned, a zoom, a turn, the
 * other mode -- drops both (pdfv_marks_drop): ink written over one view means
 * nothing over another, and a character read means nothing once it has
 * moved. */
static void render(void);
static rect_t rect_join(rect_t a, rect_t b);
#ifdef HANZI_DESKTOP
static void kfocus_draw(void);
#endif
#define PV_OCR_CHAR_PX 56        /* a glyph's height the recogniser reads well */
#define PV_OCR_THIN    30        /* a line thinner than this is read again, larger */
#define PV_OCR_SIDE    768       /* the square rendered around a press, to read */
#define PV_SLACK_PX    28        /* a finger's slack, in screen pixels */
#define PV_CTX_MAX     64        /* characters of the line kept around the press */
#define PV_CTX_CELL    64
#define PV_CTX_H       76
#define PV_WORD_H      96
#define PV_PANEL_PAD   12
#define PV_MAXCL       8         /* characters of a word drawn in one go */
static_assert(H_PV_QPICK + 4 <= H_PV_CTX, "H_PV_QPICK range overlaps H_PV_CTX");
static_assert(H_PV_CTX + PV_CTX_MAX <= H_PV_WCH, "H_PV_CTX range overlaps H_PV_WCH");
static_assert(H_PV_WCH + PV_MAXCL <= H_TWCELL, "H_PV_WCH range is not its own");

struct pv_read_t {
    int on = 0;                  /* a reading, or why there is none, is shown */
    int n = 0;
    hwcand_t cand[HW_CANDS];
    std::vector<std::string> ctx;   /* the line, a character each */
    int at = -1;                 /* the character pressed, in ctx; -1 none */
    int marked = 0;
    pdf_box mark = { 0, 0, 0, 0 };  /* where it was read, in page coordinates */
    char why[200] = "";          /* with nothing to offer: why */
};
static pv_read_t pv_read;
static int pv_side = 0;          /* the panel: 0 at the bottom of the page, 1 at its top */
static int pv_ocr_runs = 0;      /* reads of the picture, for the harness */

static ink_t pv_ink;             /* the ink over the page, in screen pixels */
static char  pv_built[64] = "";  /* characters accepted, as the draw pad's hw_built */
static struct {
    int n = 0, sel = 0;          /* characters in the ink; the one whose candidates show */
    int nc[PV_MAXCL];
    int pick[PV_MAXCL];
    int chosen[PV_MAXCL];        /* pick is the writer's, not the first by default */
    int full = 0;                /* the word would not fit pv_built: kept, and said */
    hwcand_t cand[PV_MAXCL][HW_CANDS];
} pvw;

static int pv_hanzi(const char *ch) { return ch && ch[0] && ocr_is_hanzi(u8_cp(ch)); }

static void pv_word_clear(void) {
    ui_ink_clear(&pv_ink);
    pvw.n = 0;
    pvw.sel = 0;
    pvw.full = 0;
}
/* Everything pressed or drawn on the page: gone with the view it was on. */
static void pdfv_marks_drop(void) {
    pv_read = pv_read_t();
    pv_word_clear();
    pv_built[0] = 0;
}

/* The panel over the page: the width of the screen, at the bottom or the top
 * of the room between the bars, a fixed height for each mode -- so neither
 * the ink nor the page under a press moves when what is in it changes. */
static int pv_panel_shown(void) {
    if (!pv.open || pv.choosing) return 0;
    if (pv.tapmode == PT_OCR) return pv_read.on;
    if (pv.tapmode == PT_DRAW) return pv_ink.n > 0 || pv_built[0];
    return 0;
}
/* The draw panel's rows of candidates: two, or one where two would make it
 * reach within PV_INK_MARGIN of the middle of the room (the flipped screens:
 * 888 pixels between the bars on the Clara, 1080 on the Libra), where it
 * would cover ink written near the middle on the other side. */
#define PV_INK_MARGIN 64
static int pv_draw_rows(void) {
    int two = PV_PANEL_PAD + PV_WORD_H + 10 + 2 * CAND_BOX + CAND_GAP + 12 + 100 + PV_PANEL_PAD;
    return two > (H - PV_TOP - PV_BOT) / 2 - PV_INK_MARGIN ? 1 : 2;
}
static rect_t pv_panel_rect(void) {
    if (!pv_panel_shown()) return rc(0, 0, 0, 0);
    int rows = pv.tapmode == PT_OCR ? 2 : pv_draw_rows();
    int grid = rows * CAND_BOX + (rows - 1) * CAND_GAP;
    int h = pv.tapmode == PT_OCR ? PV_PANEL_PAD + PV_CTX_H + 10 + grid + PV_PANEL_PAD
                                 : PV_PANEL_PAD + PV_WORD_H + 10 + grid + 12 + 100 + PV_PANEL_PAD;
    int top = PV_TOP, room = H - PV_TOP - PV_BOT;
    if (h > room) h = room;
    return pv_side ? rc(0, top, W, h) : rc(0, top + room - h, W, h);
}
/* `r` less the panel's band: what is left of the page to press or write on. */
static rect_t pv_minus_panel(rect_t r) {
    rect_t p = pv_panel_rect();
    if (p.w <= 0) return r;
    int y0 = r.y, y1 = r.y + r.h;
    if (pv_side) { if (p.y + p.h > y0) y0 = p.y + p.h; }
    else         { if (p.y < y1) y1 = p.y; }
    return rc(r.x, y0, r.w, y1 > y0 ? y1 - y0 : 0);
}
/* Where a stroke may start: the room between the bars, less the panel. The
 * one geometry for the hit test (in_writearea) and the repaint on a lift. */
static rect_t pdfv_ink_area(void) {
    return pv_minus_panel(rc(0, PV_TOP, W, H - PV_TOP - PV_BOT));
}
static int pdfv_in_ink(int x, int y) {
    return scr == SCR_PDFVIEW && pv.open && pv.tapmode == PT_DRAW && !pv.choosing
        && ui_hit(pdfv_ink_area(), x, y);
}
/* The first stroke of some ink picks the panel's side: away from where it
 * starts, at the pen-down; then, as it is lifted (before the panel is first
 * drawn), the side that covers less of the stroke as written -- a stroke
 * begun just above the middle can run well below it. */
static void pdfv_ink_begin(int x, int y) {
    (void)x;
    if (pv_ink.n == 0 && !pv_built[0]) pv_side = y >= PV_TOP + (H - PV_TOP - PV_BOT) / 2;
}
static void pdfv_ink_side(void) {
    if (pv_ink.n != 1 || pv_built[0] || pv_ink.s[0].n < 1) return;
    const inkstroke_t *st = &pv_ink.s[0];
    int y0 = st->p[0].y, y1 = y0;
    for (int j = 1; j < st->n; j++) { y0 = std::min(y0, (int)st->p[j].y); y1 = std::max(y1, (int)st->p[j].y); }
    y0 -= INK_W; y1 += INK_W;
    int was = pv_side, cover[2];
    for (int side = 0; side < 2; side++) {
        pv_side = side;
        rect_t p = pv_panel_rect();
        cover[side] = std::max(0, std::min(y1, p.y + p.h) - std::max(y0, p.y));
    }
    pv_side = cover[0] < cover[1] ? 0 : cover[1] < cover[0] ? 1 : was;
}

/* ---- reading the character under a press ---- */
static void pv_read_add(const char *ch, int score) {
    if (!pv_hanzi(ch)) return;
    for (int i = 0; i < pv_read.n; i++) if (!strcmp(pv_read.cand[i].ch, ch)) return;
    if (pv_read.n >= HW_CANDS) pv_read.n = HW_CANDS - 1;      /* the last makes room */
    snprintf(pv_read.cand[pv_read.n].ch, sizeof pv_read.cand[0].ch, "%s", ch);
    pv_read.cand[pv_read.n].score = score;
    pv_read.n++;
}
static void pv_ctx_set(const std::vector<std::string> &chars, int at) {
    int n = (int)chars.size(), first = 0;
    if (n > PV_CTX_MAX) first = pv_clampi(at - PV_CTX_MAX / 2, 0, n - PV_CTX_MAX);
    pv_read.ctx.assign(chars.begin() + first, chars.begin() + (n > PV_CTX_MAX ? first + PV_CTX_MAX : n));
    pv_read.at = at >= first && at < first + (int)pv_read.ctx.size() ? at - first : -1;
}

/* The models, opened once: ocr_open drops networks already loaded. Where
 * main put them (HANZI_MODELS_DIR), or, on the desktop run from the
 * repository's out/, the repository's own. */
static int pv_ocr_state = 0;     /* 1 open; 0 not yet, or not there last time */
static int pv_ocr_ready(void) {
    if (pv_ocr_state) return 1;
    const char *d = getenv("HANZI_MODELS_DIR");
    if (d && *d && ocr_open(d)) pv_ocr_state = 1;
#ifdef HANZI_DESKTOP
    for (const char *c : { "../data/models", "data/models" })
        if (!pv_ocr_state && ocr_open(c)) pv_ocr_state = 1;
#endif
    return pv_ocr_state;
}

/* Read the hanzi at page point (px, py) from the picture, at `s` pixels a
 * point: the view already drawn when that is its scale (nothing rendered),
 * else a square of PV_OCR_SIDE around the point rendered for it. Returns
 * what ocr_candidates_at does; the line in *L and the box of the character
 * read, in page coordinates, in pv_read.mark. */
static int pv_ocr_at(float px, float py, float s, ocr_cand_t *oc, ocr_line_t *L) {
    pv_geom_t g;
    float vx = 0, vy = 0;
    memset(L, 0, sizeof *L);
    const uint8_t *grey = NULL;
    int have_g = pdfv_geom(&g);
    if (have_g && fabsf(s - g.scale) < 0.005f && (grey = pdfv_pixels(NULL)) != NULL)
        s = g.scale;             /* the view as drawn: the press placed at its own scale */
    /* else a square rendered to read: never smaller than this, and the
     * press placed in it at the scale it is drawn at */
    if (!grey && s < 0.25f) s = 0.25f;
    if (!have_g || !pdf_page_to_view(pv.page, s, pv.rot, px, py, &vx, &vy)) {
        snprintf(pv_read.why, sizeof pv_read.why, "no such place on the page");
        return -1;
    }
    std::vector<uint8_t> img;
    int w, h, ox, oy;
    if (grey) {
        w = g.sw; h = g.sh; ox = g.sx; oy = g.sy;
    } else {
        w = h = PV_OCR_SIDE;
        ox = (int)lroundf(vx) - w / 2;
        oy = (int)lroundf(vy) - h / 2;
        img.assign((size_t)w * h, WHITE);
        if (!pdf_render(pv.page, s, pv.rot, ox, oy, w, h, img.data(), w)) {
            snprintf(pv_read.why, sizeof pv_read.why, "the page could not be drawn to read: %s", pdf_error());
            return -1;
        }
        grey = img.data();
    }
    int tx = (int)lroundf(vx) - ox, ty = (int)lroundf(vy) - oy;
    msec_t t0 = now_ms();
    pv_ocr_runs++;
    int n = ocr_candidates_at(grey, w, h, w, tx, ty, oc, HW_CANDS, L);
    fprintf(stderr, "pdf: read page %d at %.0f,%.0f pt, %.2f px/pt, %dx%d: %d candidate%s%s%s, "
                    "line \"%s\"%s, %ld ms%s%s\n", pv.page + 1, px, py, s, w, h, n, n == 1 ? "" : "s",
            n > 0 ? " from " : "", n > 0 ? oc[0].ch : "", L->text, L->vertical ? " (a column)" : "",
            (long)(now_ms() - t0), n > 0 ? "" : ": ", n > 0 ? "" : ocr_error());
    if (n > 0) {
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (int k = 0; k < 4; k++) {
            float qx = 0, qy = 0;
            if (!pdf_view_to_page(pv.page, s, pv.rot, ox + L->char_quad[2 * k], oy + L->char_quad[2 * k + 1], &qx, &qy))
                continue;
            x0 = fminf(x0, qx); y0 = fminf(y0, qy); x1 = fmaxf(x1, qx); y1 = fmaxf(y1, qy);
        }
        if (x1 > x0 && y1 > y0) { pv_read.mark = { x0, y0, x1, y1 }; pv_read.marked = 1; }
    }
    return n;
}

/* The line's thickness in the image read: across a line, along a column's width. */
static float pv_line_thick(const ocr_line_t &L) {
    float a = hypotf(L.quad[2] - L.quad[0], L.quad[3] - L.quad[1]);
    float b = hypotf(L.quad[6] - L.quad[0], L.quad[7] - L.quad[1]);
    return fminf(a, b);
}

/* A press on the page with OCR on: the candidates for the character there. */
static void pdfv_page_tap(int x, int y) {
    pv_geom_t g;
    float px = 0, py = 0;
    if (!pdfv_geom(&g) || !pdfv_to_page(x, y, &px, &py)) return;
    pv_read = pv_read_t();
    pv_read.on = 1;
    pv_side = y >= g.vp.y + g.vp.h / 2;          /* the panel away from the press */
    float slack = PV_SLACK_PX / g.scale;
    pv_hold_page(1);
    int scan = pdf_is_scan(pv.page) != 0;        /* an error believes the layer no more than a scan */
    pdf_char tc;
    int have_tc = pdf_char_at(pv.page, px, py, slack, &tc);
    int tc_han = have_tc && ocr_is_hanzi((uint32_t)tc.c);
    if (tc_han && !scan) {
        pv_read_add(tc.utf8, 0);
        pv_read.mark = tc.box;
        pv_read.marked = 1;
        std::vector<pdf_char> line;
        int at = -1;
        std::vector<std::string> chars;
        if (pdf_line_at(pv.page, px, py, slack, &line, &at))
            for (const pdf_char &c : line) chars.push_back(c.utf8);
        else { chars.push_back(tc.utf8); at = 0; }
        pv_ctx_set(chars, at);
        fprintf(stderr, "pdf: press on page %d at %.0f,%.0f pt: %s, from the text layer\n",
                pv.page + 1, px, py, tc.utf8);
        return;
    }
    if (!pv_ocr_ready()) {
        if (tc_han) pv_read_add(tc.utf8, 0);
        snprintf(pv_read.why, sizeof pv_read.why, "nothing to read pictures with: %s", ocr_error());
        fprintf(stderr, "pdf: %s\n", pv_read.why);
        return;
    }
    /* A read takes a moment on the Kobo: say so where the answer will be. */
    snprintf(pv_read.why, sizeof pv_read.why, "reading\342\200\246");
    if (!sleeping) {
        render();
#ifdef HANZI_DESKTOP
        kfocus_draw();
#endif
        surf_present(pv_panel_rect(), RF_PART);
    }
    long_task("reading a character on a PDF page");
    /* The scale to read at: a glyph about PV_OCR_CHAR_PX high, judged from
     * the layer's box where there is one (on a scan it says where, if not
     * what); else the view's, and a thin line found there read again larger
     * -- a page at fit sets body text far too small for the recogniser. */
    float s = g.scale;
    if (have_tc) {
        float hpt = fmaxf(tc.box.x1 - tc.box.x0, tc.box.y1 - tc.box.y0);
        if (hpt > 1) s = fminf(fmaxf(PV_OCR_CHAR_PX / hpt, g.scale * 0.5f), g.scale * 3);
    }
    ocr_cand_t oc[HW_CANDS];
    ocr_line_t L;
    pv_read.why[0] = 0;
    int n = pv_ocr_at(px, py, s, oc, &L);
    if (!have_tc && n >= 0) {
        float th = n > 0 ? pv_line_thick(L) : 0;
        float again = n > 0 ? (th > 1 && th < PV_OCR_THIN ? fminf(PV_OCR_CHAR_PX * 1.2f / th, 3.f) : 0)
                            : 2.f;            /* nothing found: once more at twice the size */
        if (again > 1.2f) {
            ocr_cand_t oc2[HW_CANDS];
            ocr_line_t L2;
            int n2 = pv_ocr_at(px, py, s * again, oc2, &L2);
            if (n2 > 0 || n <= 0) { n = n2; L = L2; memcpy(oc, oc2, sizeof oc); }
        }
    }
    std::string failed = n < 0 ? (pv_read.why[0] && strcmp(pv_read.why, "reading\342\200\246")
                                  ? pv_read.why : ocr_error()) : "";
    pv_read.why[0] = 0;
    for (int i = 0; i < n; i++) pv_read_add(oc[i].ch, (int)lroundf((1.f - oc[i].p) * 1000));
    if (tc_han) pv_read_add(tc.utf8, 1000);      /* the layer's: one more, never first */
    if (n > 0) {
        std::vector<std::string> chars;
        for (int i = 0, m = u8_len(L.text); i < m; i++) {
            char c[8];
            u8_at(L.text, i, c);
            chars.push_back(c);
        }
        pv_ctx_set(chars, L.char_index);
    }
    if (pv_read.n == 0)
        snprintf(pv_read.why, sizeof pv_read.why, "%s",
                 n < 0 ? failed.c_str() : L.text[0] ? "no hanzi read here" : "no text found here");
}

/* ---- a word drawn over the page ---- */
/* A word written in one go, in characters: the strokes grouped by where they
 * are, as assess_writing groups them by the box each falls in -- over a page
 * there are no boxes, so they come from the ink. Along the writing (across
 * when the ink is wider than tall, down when taller): strokes whose extents
 * overlap are one block (二's two strokes; 尔's); neighbouring blocks join
 * while the group stays within PV_CL_JOIN of the writing's thickness (亻 and
 * 尔 are 你; 川's three strokes are one); and a group longer than PV_CL_SPLIT
 * of it -- characters written touching -- is cut in even parts, each stroke
 * going to the part its centroid falls in, assess_writing's rule, unless
 * the parts would overlap by more than PV_CL_OVERLAP of it. Fills
 * grp[stroke] (-1 for an empty one) and returns the number of characters, in
 * the order written along; each keeps its strokes in the order they were
 * drawn, which the recogniser depends on. */
#define PV_CL_JOIN  1.3f
#define PV_CL_SPLIT 1.8f
#define PV_CL_OVERLAP 0.35f
static int ink_clusters(const ink_t *k, int *grp, int maxg) {
    int n = k->n < MAXINK_STROKE ? k->n : MAXINK_STROKE;
    float lo[MAXINK_STROKE], hi[MAXINK_STROKE], mid[MAXINK_STROKE];
    int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -(1 << 30), by1 = -(1 << 30), any = 0;
    for (int i = 0; i < n; i++) {
        grp[i] = -1;
        const inkstroke_t *st = &k->s[i];
        for (int j = 0; j < st->n; j++) {
            bx0 = std::min(bx0, (int)st->p[j].x); bx1 = std::max(bx1, (int)st->p[j].x);
            by0 = std::min(by0, (int)st->p[j].y); by1 = std::max(by1, (int)st->p[j].y);
            any = 1;
        }
    }
    if (!any || maxg < 1) return 0;
    int across = bx1 - bx0 >= by1 - by0;
    float size = (float)(across ? by1 - by0 : bx1 - bx0);
    if (size < 24) size = 24;                    /* a dash of ink is no measure */
    int order[MAXINK_STROKE], m = 0;
    for (int i = 0; i < n; i++) {
        const inkstroke_t *st = &k->s[i];
        if (st->n < 1) continue;
        float a = 1e30f, b = -1e30f, sum = 0;
        for (int j = 0; j < st->n; j++) {
            float v = across ? st->p[j].x : st->p[j].y;
            a = fminf(a, v); b = fmaxf(b, v); sum += v;
        }
        lo[i] = a; hi[i] = b; mid[i] = sum / st->n;
        order[m++] = i;
    }
    for (int a = 1; a < m; a++)                  /* by where each starts along */
        for (int b = a; b > 0 && lo[order[b]] < lo[order[b - 1]]; b--) std::swap(order[b], order[b - 1]);
    /* blocks of overlapping strokes, then groups of neighbouring blocks */
    struct span_t { float a, b; int first, last; };   /* over order[first..last] */
    span_t blk[MAXINK_STROKE], grs[MAXINK_STROKE];
    int nb = 0, ng = 0;
    for (int i = 0; i < m; i++) {
        int s = order[i];
        if (nb && lo[s] <= blk[nb - 1].b) { blk[nb - 1].b = fmaxf(blk[nb - 1].b, hi[s]); blk[nb - 1].last = i; }
        else blk[nb++] = { lo[s], hi[s], i, i };
    }
    for (int i = 0; i < nb; i++) {
        if (ng && fmaxf(grs[ng - 1].b, blk[i].b) - grs[ng - 1].a <= PV_CL_JOIN * size) {
            grs[ng - 1].b = fmaxf(grs[ng - 1].b, blk[i].b);
            grs[ng - 1].last = blk[i].last;
        } else grs[ng++] = blk[i];
    }
    int out = 0;
    for (int gi = 0; gi < ng; gi++) {
        const span_t &gr = grs[gi];
        float len = gr.b - gr.a;
        int parts = len > PV_CL_SPLIT * size ? (int)lroundf(len / size) : 1;
        parts = pv_clampi(parts, 1, gr.last - gr.first + 1);    /* no more parts than strokes */
        int used[MAXINK_STROKE] = { 0 };
        for (int i = gr.first; i <= gr.last; i++) {
            int s = order[i];
            int p = parts > 1 ? pv_clampi((int)((mid[s] - gr.a) * parts / len), 0, parts - 1) : 0;
            grp[s] = p;                          /* the part, numbered below */
            used[p] = 1;
        }
        /* parts that still overlap by much along the writing are one
         * character cut through its middle (二's strokes, both across it):
         * joined again */
        float pa[MAXINK_STROKE], pb[MAXINK_STROKE];
        for (int p = 0; p < parts; p++) { pa[p] = 1e30f; pb[p] = -1e30f; }
        for (int i = gr.first; i <= gr.last; i++) {
            int s = order[i];
            pa[grp[s]] = fminf(pa[grp[s]], lo[s]);
            pb[grp[s]] = fmaxf(pb[grp[s]], hi[s]);
        }
        int into[MAXINK_STROKE], prev = -1;
        for (int p = 0; p < parts; p++) {
            into[p] = p;
            if (!used[p]) continue;
            if (prev >= 0 && pb[prev] - pa[p] > PV_CL_OVERLAP * size) {
                into[p] = into[prev];
                pb[prev] = fmaxf(pb[prev], pb[p]);
                used[p] = 0;
                continue;
            }
            prev = p;
        }
        for (int i = gr.first; i <= gr.last; i++) grp[order[i]] = into[grp[order[i]]];
        int num[MAXINK_STROKE];
        for (int p = 0; p < parts; p++) num[p] = used[p] ? out++ : -1;
        for (int i = gr.first; i <= gr.last; i++) {
            int s = order[i];
            grp[s] = std::min(num[grp[s]], maxg - 1);
        }
    }
    return std::min(out, maxg);
}

/* Each character of the ink recognised on its own (the draw pad's search). */
static void pv_word_recognise(void) {
    static int grp[MAXINK_STROKE];
    static ink_t sub;                            /* 150KB: not on the stack */
    /* the candidates chosen so far, kept where the character still offers
     * them: a stroke more for 好 must not undo the choice made for 你 */
    char was[PV_MAXCL][8];
    for (int c = 0; c < PV_MAXCL; c++)
        snprintf(was[c], sizeof was[c], "%s", c < pvw.n && pvw.chosen[c] && pvw.nc[c] > 0
                                              ? pvw.cand[c][pvw.pick[c]].ch : "");
    pvw.full = 0;
    pvw.n = ink_clusters(&pv_ink, grp, PV_MAXCL);
    for (int c = 0; c < pvw.n; c++) {
        ui_ink_clear(&sub);
        for (int i = 0; i < pv_ink.n && i < MAXINK_STROKE; i++)
            if (grp[i] == c) sub.s[sub.n++] = pv_ink.s[i];
        unsigned char sig[HW_MAX_STROKES * 6];
        int ns = ui_ink_signature(&sub, sig, HW_MAX_STROKES);
        pvw.nc[c] = ns > 0 ? db_hw_search(sig, ns, pvw.cand[c], HW_CANDS) : 0;
        pvw.pick[c] = 0;
        pvw.chosen[c] = 0;
        for (int k = 0; was[c][0] && k < pvw.nc[c]; k++)
            if (!strcmp(pvw.cand[c][k].ch, was[c])) { pvw.pick[c] = k; pvw.chosen[c] = 1; break; }
    }
    /* the candidates shown: the character the last stroke went to */
    int last = pv_ink.n > 0 && pv_ink.n <= MAXINK_STROKE ? grp[pv_ink.n - 1] : -1;
    pvw.sel = last >= 0 ? last : (pvw.n > 0 ? pvw.n - 1 : 0);
    char w[96] = "";
    for (int c = 0; c < pvw.n; c++)
        if (pvw.nc[c] > 0) strncat(w, pvw.cand[c][0].ch, sizeof w - strlen(w) - 1);
    fprintf(stderr, "pdf: %d stroke%s over the page read as %d character%s: %s\n", pv_ink.n,
            pv_ink.n == 1 ? "" : "s", pvw.n, pvw.n == 1 ? "" : "s", w[0] ? w : "(none)");
}

/* What has been accepted and what the ink reads as: the word to look up. */
static std::string pv_word(void) {
    std::string w = pv_built;
    for (int c = 0; c < pvw.n; c++)
        if (pvw.nc[c] > 0) w += pvw.cand[c][pvw.pick[c]].ch;
    return w;
}

/* A candidate chosen for the character on show. The last character's choice
 * accepts the ink's whole word into pv_built and clears the ink, as the draw
 * pad accepts a character: the page is clean for the next one. */
static void pv_word_choose(int i) {
    if (pvw.sel >= pvw.n || i >= pvw.nc[pvw.sel]) return;
    pvw.pick[pvw.sel] = i;
    pvw.chosen[pvw.sel] = 1;
    if (pvw.sel + 1 < pvw.n) { pvw.sel++; return; }
    std::string w = pv_word();
    /* too long to keep: the ink and the choices stay, and the panel says
     * so -- never thrown away with nothing said */
    if (w.size() >= sizeof pv_built) { pvw.full = 1; return; }
    snprintf(pv_built, sizeof pv_built, "%s", w.c_str());
    pv_word_clear();
}

/* A stroke over the page has ended: recognise, and repaint only the stroke
 * and the panel, both over the page as it is kept (no PDF rendering). */
static void pdfv_ink_lift(rect_t dirty) {
    pv_word_recognise();
    pdfv_ink_side();
    if (sleeping) return;
    render();
#ifdef HANZI_DESKTOP
    kfocus_draw();
#endif
    if (pv_ink.n > 0) {
        const inkstroke_t *st = &pv_ink.s[pv_ink.n - 1];
        for (int j = 0; j < st->n; j++)
            dirty = rect_join(dirty, rc(st->p[j].x - INK_W - 2, st->p[j].y - INK_W - 2,
                                        2 * INK_W + 4, 2 * INK_W + 4));
    }
    if (dirty.w > 0) surf_present(dirty, RF_PART);
    rect_t p = pv_panel_rect();
    if (p.w > 0) surf_present(p, RF_PART);
}

/* ---- drawing them ---- */
static void pv_panel_frame(rect_t p) {
    gfx_fill(p, WHITE);
    gfx_fill(rc(p.x, pv_side ? p.y + p.h - 3 : p.y, p.w, 3), 0x60);   /* its edge on the page */
}

static void pv_read_panel(rect_t p) {
    pv_panel_frame(p);
    int y = p.y + PV_PANEL_PAD;
    rect_t cl = rc(W - 24 - 150, y, 150, PV_CTX_H);
    btn_t b = { cl, "close", 0 };
    ui_btn(&b, 0);
    hotreg(cl, H_PV_RCLOSE);
    /* the line, centred on the character pressed */
    int fit = (cl.x - 16 - 24) / PV_CTX_CELL, nctx = (int)pv_read.ctx.size();
    int first = nctx > fit ? pv_clampi(pv_read.at - fit / 2, 0, nctx - fit) : 0;
    for (int i = first, x = 24; i < nctx && i < first + fit; i++, x += PV_CTX_CELL) {
        rect_t c = rc(x, y, PV_CTX_CELL, PV_CTX_H);
        const char *ch = pv_read.ctx[i].c_str();
        int han = pv_hanzi(ch);
        if (i == pv_read.at) gfx_round_rect(c, 10, 4, BLACK, 0xE4);
        int tw = gfx_text_w(52, ch);
        gfx_text(c.x + (c.w - tw) / 2, c.y + c.h / 2 + 19, 52, han ? BLACK : 0x90, ch);
        if (han) hotreg(c, H_PV_CTX + i);
    }
    y += PV_CTX_H + 10;
    if (pv_read.n > 0) cand_grid(24, y, W - 48, pv_read.cand, pv_read.n, 2);
    else if (pv_read.why[0]) gfx_text_fit(44, y + 60, 40, 28, W - 88, GREY, pv_read.why);
}

static void pv_draw_panel(rect_t p) {
    pv_panel_frame(p);
    int y = p.y + PV_PANEL_PAD, x = 24;
    /* the word: what was accepted, then a cell per character in the ink */
    int cells = pvw.n * (88 + 10);
    if (pv_built[0]) {
        int room = W - 48 - cells - 16;
        if (room > 64) {
            gfx_text_clip(x, y + 72, 64, room, BLACK, pv_built);
            x += std::min(room, gfx_text_w(64, pv_built)) + 16;
        }
    }
    int matched = 0;
    for (int c = 0; c < pvw.n; c++, x += 88 + 10) {
        rect_t cell = rc(x, y, 88, PV_WORD_H);
        int sel = c == pvw.sel && pvw.n > 1;
        gfx_round_rect(cell, 10, sel ? 5 : 2, sel ? BLACK : 0xA8, WHITE);
        if (pvw.nc[c] > 0) {
            const char *ch = pvw.cand[c][pvw.pick[c]].ch;
            int tw = gfx_text_w(72, ch);
            gfx_text(cell.x + (cell.w - tw) / 2, cell.y + cell.h / 2 + 26, 72, BLACK, ch);
            matched++;
        }
        if (pvw.n > 1 && !sel) hotreg(cell, H_PV_WCH + c);    /* the one showing is no control */
    }
    y += PV_WORD_H + 10;
    int rows = pv_draw_rows();
    if (pvw.full)
        gfx_text_fit(44, y + 70, 40, 28, W - 88, GREY,
                     "the word is as long as it can be \342\200\224 look it up, or start over");
    else if (pvw.n > 0 && pvw.sel < pvw.n && pvw.nc[pvw.sel] > 0)
        cand_grid(24, y, W - 48, pvw.cand[pvw.sel], pvw.nc[pvw.sel], rows);
    else if (pv_ink.n > 0 && !matched)
        gfx_text(44, y + 70, 40, GREY, "no match \342\200\224 try again");
    y += rows * CAND_BOX + (rows - 1) * CAND_GAP + 12;
    /* the draw pad's row: undo, clear, pen only, and the search -- here the
     * character screen, which back leaves for this page */
    int nb = has_pen ? 4 : 3;
    int bw = (W - 24 * (nb + 1)) / nb;
    x = 24;
    btn_t u = { rc(x, y, bw, 100), "undo", 0 };
    ui_btn(&u, 0);
    hotreg(u.r, H_UNDO);
    x += bw + 24;
    btn_t c = { rc(x, y, bw, 100), pv_built[0] ? "start over" : "clear", 0 };
    ui_btn(&c, 0);
    hotreg(c.r, H_CLEAR);
    x += bw + 24;
    if (has_pen) { pen_button(rc(x, y, bw, 100)); x += bw + 24; }
    std::string w = pv_word();
    char lb[96];
    snprintf(lb, sizeof lb, w.empty() ? "look up" : "look up %s", w.c_str());
    btn_t d = { rc(x, y, bw, 100), lb, 0 };
    if (w.empty()) {
        gfx_round_rect(d.r, 12, 2, 0xC8, WHITE);
        label_in(d.r, 36, 60, 0xB0, lb);
    } else {
        ui_btn(&d, 1);
        hotreg(d.r, H_PV_LOOKUP);
    }
}

/* Over the page as drawn, under the bars: the page greyed for drawing on,
 * the character read marked, the ink. */
static void pdfv_layers(const pv_geom_t &g) {
    rect_t pr = rc(g.dx, g.dy, g.sw, g.sh);
    if (pv.tapmode == PT_DRAW) {
        for (int yy = std::max(pr.y, 0); yy < pr.y + pr.h && yy < gfx_h; yy++) {
            uint8_t *row = gfx_fb + (size_t)yy * gfx_w;
            for (int xx = std::max(pr.x, 0); xx < pr.x + pr.w && xx < gfx_w; xx++)
                row[xx] = (uint8_t)(0x60 + row[xx] * (0xFF - 0x60) / 0xFF);
        }
        ui_ink_draw(&pv_ink, INK_W);
    }
    if (pv.tapmode == PT_OCR && pv_read.on && pv_read.marked) {
        pdf_box vb;
        if (pdf_box_to_view(pv.page, g.scale, pv.rot, pv_read.mark, &vb)) {
            rect_t m = rc(g.dx + (int)vb.x0 - g.sx - 6, g.dy + (int)vb.y0 - g.sy - 6,
                          (int)(vb.x1 - vb.x0) + 12, (int)(vb.y1 - vb.y0) + 12);
            gfx_frame(m, 4, 0x50);
        }
    }
}

/* The panel, last over the page, and the page's own press where it is not. */
static void pdfv_panel(const pv_geom_t &g) {
    if (pv.tapmode == PT_OCR && !pv.choosing) {
        rect_t t = pv_minus_panel(rc(g.dx, g.dy, g.sw, g.sh));
        if (t.h >= 56) hotreg(t, H_PV_PAGE);        /* MIN_TARGET, below */
    }
    rect_t p = pv_panel_rect();
    if (p.w <= 0) return;
    if (pv.tapmode == PT_OCR) pv_read_panel(p);
    else                      pv_draw_panel(p);
}

/* Keep the part within what this page and screen have, and the spot. The
 * part is the zoom's, counted on the screen that zoom is shown on: not the
 * quarter chooser's fit (choosing and putting it away leaves the part as it
 * was), and a flipped half view's steps counted flipped even where the
 * screen has not turned yet (pdfv_open, before render() turns it) or has
 * turned back (choosing). */
static void pdfv_settle(void) {
    pv_geom_t g;
    int choosing = pv.choosing, w = W, h = H;
    pv.choosing = 0;
    if (pdfv_flip_wanted() != (surf_rotation() & 1)) { W = h; H = w; }
    if (pdfv_geom(&g)) pv.part = pv_clampi(pv.part, 0, g.nparts - 1);
    W = w; H = h;
    pv.choosing = choosing;
    if (!pv.open) return;
    pdf_spot_t s = { pv.page, pv.zoom, pv.part, pv.trim, pv.rot, pv.flip };
    db_pdf_set_spot(pv.path.c_str(), &s);       /* a pin's only; unchanged, not written */
}

static int pdfv_nparts(void) {
    pv_geom_t g;
    return pdfv_geom(&g) ? g.nparts : 1;
}

/* prev/next and the page keys: through the quarters or the half's steps,
 * then on to the next page (back to the previous one's last part). */
static int pdfv_can_step(int dir) {
    int inpage = !pv.choosing && pv.zoom != PZ_FIT;
    if (dir > 0) return pv.page + 1 < pv.npages || (inpage && pv.part + 1 < pdfv_nparts());
    return pv.page > 0 || (inpage && pv.part > 0);
}

static void pdfv_step(int dir) {
    if (!pv.choosing && pv.zoom != PZ_FIT) {
        int n = pdfv_nparts();
        if (dir > 0 && pv.part + 1 < n) { pv.part++; return; }
        if (dir < 0 && pv.part > 0)     { pv.part--; return; }
    }
    int np = pv.page + dir;
    if (np < 0 || np >= pv.npages) return;
    pv.page = np;
    pv.part = dir > 0 ? 0 : PV_LAST;
}

static int pdfv_open(const std::string &path, const std::string &name) {
    if (access(path.c_str(), R_OK) != 0) {
        snprintf(toast, sizeof toast, "not found: %s", path.c_str());
        return 0;
    }
    long_task("opening a PDF");
    if (!pdf_open(path.c_str())) {
        snprintf(toast, sizeof toast, "%s: %s", name.c_str(), pdf_error());
        fprintf(stderr, "pdf: %s: %s\n", path.c_str(), pdf_error());
        return 0;
    }
    pdfv_cache_drop();
    pdfv_marks_drop();
    pv = pdfview_t();
    pv.open = 1;
    pv.path = path;
    pv.name = name;
    pv.npages = pdf_pages();
    pv.back = scr;
    pdf_spot_t s;
    if (db_pdf_pinned(path.c_str())) {
        db_pdf_used(path.c_str(), (long)time(NULL));   /* read first time or again: up the list */
        if (db_pdf_spot(path.c_str(), &s)) {
            pv.page = pv_clampi(s.page, 0, pv.npages - 1);
            pv.zoom = pv_clampi(s.zoom, 0, PZ_N - 1);
            pv.part = s.part < 0 ? 0 : s.part;
            pv.trim = pv_clampi(s.trim, 0, PV_TRIM_MAX);
            pv.rot  = ((s.rot / 90) % 4 + 4) % 4 * 90;
            pv.flip = s.flip ? 1 : 0;
        }
    }
    toast[0] = 0;
    scr = SCR_PDFVIEW;
    pdfv_settle();
    fprintf(stderr, "pdf: opened %s, %d pages, at page %d\n", path.c_str(), pv.npages, pv.page + 1);
    return 1;
}

static void pdfv_close(void) {
    pdfv_settle();
    pdf_close();
    pdfv_cache_drop();
    pdfv_marks_drop();
    /* the OCR networks and their key table, tens of MB once a press has
     * loaded them: not kept for a session of cards that can last days. The
     * next press on a page loads them again, and the models are looked for
     * again (a damaged one replaced meanwhile is found). */
    ocr_close();
    pv_ocr_state = 0;
    pv.open = 0;
    pv.tapmode = PT_NONE;
    scr = pv.back;
    pdf_list_build();                /* a pin read just now moves up */
}

/* ---- the comic's scan -------------------------------------------------- *
 * A volume's PDF, when the scans lie in the comic's own folder: the PDF
 * whose name shares the most two-character runs of Chinese with the
 * volume's title ("01 殷纣失政" ~ 殷纣失政.pdf; two at least, and one PDF
 * ahead of the rest). A tie -- 西游记01 ~ 西游记_..._1.pdf, 西游记_..._2.pdf
 * all share the series name -- is broken by the volume's number being the
 * PDF name's last number. None found: no button. */
static std::vector<std::string> cjk_chars(const std::string &s) {
    std::vector<std::string> v;          /* "" between runs */
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xE4 && c <= 0xE9 && i + 2 < s.size()) { v.push_back(s.substr(i, 3)); i += 3; }
        else { if (!v.empty() && !v.back().empty()) v.push_back(""); i++; }
    }
    return v;
}
static int num_of(const std::string &s, int last) {
    int n = -1;
    for (size_t i = 0; i < s.size(); i++) {
        if (!isdigit((unsigned char)s[i])) continue;
        int v = 0;
        while (i < s.size() && isdigit((unsigned char)s[i])) v = v * 10 + (s[i++] - '0');
        if (!last) return v;
        n = v;
    }
    return n;
}
static std::string comic_pdf_pick(const std::string &dir, const std::string &title) {
    std::vector<std::string> tc = cjk_chars(title);
    int tn = num_of(title, 0);
    std::vector<std::pair<int, std::string>> sc;       /* score, path */
    DIR *dp = opendir(dir.c_str());
    if (!dp) return "";
    while (struct dirent *e = readdir(dp)) {
        std::string n = e->d_name;
        if (n.size() < 5 || strcasecmp(n.c_str() + n.size() - 4, ".pdf") != 0) continue;
        std::vector<std::string> nc = cjk_chars(n);
        int score = 0;
        for (size_t i = 0; i + 1 < tc.size(); i++) {
            if (tc[i].empty() || tc[i + 1].empty()) continue;
            for (size_t j = 0; j + 1 < nc.size(); j++)
                if (nc[j] == tc[i] && nc[j + 1] == tc[i + 1]) { score++; break; }
        }
        if (score >= 2) sc.push_back({ score, dir + "/" + n });
    }
    closedir(dp);
    if (sc.empty()) return "";
    int best = 0;
    for (auto &x : sc) best = std::max(best, x.first);
    std::vector<std::string> top;
    for (auto &x : sc) if (x.first == best) top.push_back(x.second);
    if (top.size() == 1) return top[0];
    std::string hit;
    for (auto &p : top)
        if (tn >= 0 && num_of(p.substr(p.rfind('/') + 1), 1) == tn) { if (!hit.empty()) return ""; hit = p; }
    return hit;
}
/* for the volume open, found once */
static std::string comic_pdf_for(void) {
    static int key_id = -1;
    static std::string key_path, found;
    if (!comic_path[0] || !tchap_id) return "";
    if (key_id != tchap_id || key_path != comic_path) {
        key_id = tchap_id; key_path = comic_path;
        size_t sl = key_path.rfind('/');
        found = comic_pdf_pick(sl == std::string::npos ? "." : key_path.substr(0, sl), tchap_title);
    }
    return found;
}
static void comic_pdf_btn(rect_t r) {
    if (comic_pdf_for().empty()) return;
    gfx_round_rect(r, 12, 3, BLACK, WHITE);
    int w = gfx_text_w(34, "PDF");
    gfx_text(r.x + (r.w - w) / 2, r.y + r.h / 2 + 11, 34, BLACK, "PDF");
    hotreg(r, H_TPDF);
}
/* Open it at about the plate: the plate's place in the volume, taken to the
 * same place in the scan (which has a cover and credits the EPUB may not). */
static int comic_pdf_open(void) {
    std::string p = comic_pdf_for();
    if (p.empty() || !ntplates) return 0;
    if (!pdfv_open(p, pdf_name_of(p))) return 0;
    pv.comic = 1;
    pv.page = pv_clampi((int)((2L * cplate + 1) * pv.npages / (2L * ntplates)), 0, pv.npages - 1);
    pv.part = 0;
    fprintf(stderr, "pdf: comic plate %d/%d -> page %d/%d\n", cplate + 1, ntplates, pv.page + 1, pv.npages);
    return 1;
}

/* A tool of the bars: an ordinary button, held in while on (toggle_btn's
 * look), its label at the bar's one size `px`; one that cannot act now is
 * drawn grey and is no control at all. */
static void pv_tool(rect_t r, const char *label, int on, int id, int enabled, int px) {
    if (!enabled)  gfx_round_rect(r, 12, 2, 0xC8, WHITE);
    else if (on)   gfx_round_rect(r, 12, 6, BLACK, 0xC8);
    else           gfx_round_rect(r, 12, 3, BLACK, WHITE);
    int w = gfx_text_w(px, label);
    gfx_text_clip(r.x + (w < r.w - 12 ? (r.w - w) / 2 : 6), r.y + r.h / 2 + px / 3, px, r.w - 12,
                  enabled ? BLACK : 0xB0, label);
    if (enabled) hotreg(r, id);
}

static void pdfv_bars(void) {
    /* over the page: back, the title, where you are; OCR, draw, settings, exit */
    gfx_fill(rc(0, 0, W, PV_TOP), WHITE);
    gfx_fill(rc(0, PV_TOP - 2, W, 2), GREY);
    int by = 12, bh = 60;
    /* from the comic reader, the way back says where it goes */
    rect_t b = rc(16, by, pv.comic ? 176 : 104, bh);
    gfx_frame(b, 3, BLACK);
    label_in(b, 36, 42, BLACK, pv.comic ? "to comic" : "back");
    hotreg(b, H_BACK);
    int right = W - 16;
    rect_t q = rc(right - 104, by, 104, bh);
    gfx_frame(q, 3, BLACK);
    label_in(q, 36, 42, BLACK, "exit");
    hotreg(q, H_QUIT);
    right = q.x - 10;
    int sw = gfx_text_w(36, "settings") + 28;
    rect_t st = rc(right - sw, by, sw, bh);
    gfx_frame(st, 3, BLACK);
    label_in(st, 36, 42, BLACK, "settings");
    hotreg(st, H_HDRSET);
    right = st.x - 18;
    rect_t dr = rc(right - 104, by, 104, bh);
    toggle_btn(dr, "draw", pv.tapmode == PT_DRAW, H_PV_DRAWON);
    rect_t oc = rc(dr.x - 10 - 104, by, 104, bh);
    toggle_btn(oc, "OCR", pv.tapmode == PT_OCR, H_PV_OCR);
    right = oc.x - 18;
    char pg[48];
    snprintf(pg, sizeof pg, "%d/%d", pv.page + 1, pv.npages);
    int pw = gfx_text_w(36, pg);
    gfx_text(right - pw, by + 42, 36, 0x40, pg);
    right -= pw + 20;
    if (right > b.x + b.w + 32) gfx_text_clip(b.x + b.w + 16, by + 42, 36, right - (b.x + b.w + 16), BLACK, pv.name.c_str());

    /* under it: prev, the zoom, next */
    gfx_fill(rc(0, H - PV_BOT, W, PV_BOT), WHITE);
    gfx_fill(rc(0, H - PV_BOT, W, 2), GREY);
    int flip = pv.zoom == PZ_HALF && !pv.choosing && pdfv_can_flip();
    int n = flip ? 9 : 8, gap = 8;
    int w = (W - 32 - (n - 1) * gap) / n, x = 16, y = H - PV_BOT + 14;
    int h = PV_BOT - 26;
    char rl[16];
    if (pv.rot) snprintf(rl, sizeof rl, "rot %d", pv.rot);
    else        snprintf(rl, sizeof rl, "rotate");
    /* one size for every label, the largest the widest fits at with room
     * either side (at w - 20 "quarter" all but touched its frame) */
    int px = 36;
    for (const char *l : { "quarter", "rotate", "trim +", (const char *)rl })
        while (px > 26 && gfx_text_w(px, l) > w - 30) px -= 2;
    pv_tool(rc(x, y, w, h), "prev", 0, H_PREV, pdfv_can_step(-1), px);        x += w + gap;
    pv_tool(rc(x, y, w, h), "fit", pv.zoom == PZ_FIT && !pv.choosing, H_PV_FIT, 1, px); x += w + gap;
    pv_tool(rc(x, y, w, h), "quarter", pv.zoom == PZ_QUAD || pv.choosing, H_PV_QUAD, 1, px); x += w + gap;
    pv_tool(rc(x, y, w, h), "half", pv.zoom == PZ_HALF && !pv.choosing, H_PV_HALF, 1, px); x += w + gap;
    if (flip) { pv_tool(rc(x, y, w, h), "flip", pv.flip, H_PV_FLIP, 1, px); x += w + gap; }
    pv_tool(rc(x, y, w, h), "trim -", 0, H_PV_TRIMOUT, pv.trim > 0, px);    x += w + gap;
    pv_tool(rc(x, y, w, h), "trim +", 0, H_PV_TRIMIN, pv.trim < PV_TRIM_MAX, px); x += w + gap;
    pv_tool(rc(x, y, w, h), rl, pv.rot != 0, H_PV_ROT, 1, px);              x += w + gap;
    pv_tool(rc(x, y, w, h), "next", 0, H_MORE, pdfv_can_step(1), px);
}

static void draw_pdfview(void) {
    pdfv_orient();
    nhot = 0;
    gfx_clear(WHITE);
    pv_geom_t g;
    if (pdfv_geom(&g)) {
        if (pdfv_render(g)) {
            for (int r = 0; r < g.sh; r++) {
                int yy = g.dy + r;
                if (yy < 0 || yy >= gfx_h) continue;
                int x0 = g.dx < 0 ? -g.dx : 0, x1 = g.dx + g.sw > gfx_w ? gfx_w - g.dx : g.sw;
                if (x1 > x0) memcpy(gfx_fb + (size_t)yy * gfx_w + g.dx + x0,
                                    pv_cache.px.data() + (size_t)r * g.sw + x0, (size_t)(x1 - x0));
            }
            /* the page's edge, where there is white around it */
            if (g.dx - 3 >= g.vp.x || g.dy - 3 >= g.vp.y)
                gfx_frame(rc(g.dx - 3, g.dy - 3, g.sw + 6, g.sh + 6), 2, GREY);
        } else {
            gfx_text_fit(40, g.vp.y + 120, 39, 30, W - 80, 0x40, "this page could not be drawn");
            gfx_text_fit(40, g.vp.y + 180, 34, 26, W - 80, 0x70, pv_cache.err.c_str());
        }
        pdfv_layers(g);          /* greyed for ink, the ink, the character read */
        if (pv.choosing) {
            /* the page in four: press the quarter to read */
            int hx = g.sw / 2, hy = g.sh / 2;
            gfx_fill(rc(g.dx + hx - 2, g.dy, 4, g.sh), 0x40);
            gfx_fill(rc(g.dx, g.dy + hy - 2, g.sw, 4), 0x40);
            for (int k = 0; k < 4; k++)
                hotreg(rc(g.dx + (k % 2) * hx, g.dy + (k / 2) * hy, k % 2 ? g.sw - hx : hx,
                          k / 2 ? g.sh - hy : hy), H_PV_QPICK + k);
        }
    } else {
        gfx_text_fit(40, PV_TOP + 120, 39, 30, W - 80, 0x40,
                     pv.open ? "this page has no size" : "no PDF open");
    }
    pdfv_bars();
    if (pdfv_geom(&g)) pdfv_panel(g);   /* the press on the page, and the panel over it */
    if (toast[0]) {              /* on white, not over the page */
        gfx_fill(rc(0, H - PV_BOT - 66, W, 64), WHITE);
        toast_y = H - PV_BOT - 22;
        toastline();
    }
}

static const char *scr_name(int s) {
    switch (s) {
    case SCR_SETTINGS: return "settings";
    case SCR_RESET: return "reset";
    case SCR_WORDS: return "words";
    case SCR_DRAW: return "draw";
    case SCR_STUDY: return "study";
    case SCR_STATS: return "stats";
    case SCR_STROKES: return "strokes";
    case SCR_BOOK: return "book";
    case SCR_TEXTCH: return "chapters"; case SCR_TEXTREAD: return "read";
    case SCR_TEXTWRITE: return "write-text";
    case SCR_TEXTLINE:  return "sentence";
    case SCR_TEXTDONE: return "text-written";
    case SCR_TEXTNOTES: return "translation";
    case SCR_MYNOTE:    return "my notes";
    case SCR_EXPORT:    return "export";
    case SCR_PDFS:      return "pdfs";
    case SCR_PDFVIEW:   return "pdf";
    case SCR_CATORDER:  return "home order";
    case SCR_HOME: return "home";     case SCR_LISTS:    return "lists";
    case SCR_REVIEW: return "review"; case SCR_DICT:     return "dict";
    case SCR_CHAR: return "char";     case SCR_PICK:     return "pick";
    case SCR_NEWLIST: return "name";  case SCR_MENU:     return "menu";
    case SCR_LEARNED: return "studied"; case SCR_MYLISTS: return "mylists";
    case SCR_LISTVIEW: return "list"; case SCR_DONE:     return "done";
    default: return "?";
    }
}

/* Draw the current screen into the buffer, without touching the panel. */
static void render(void) {
    /* One line per screen change: if the app dies, the log names where. */
    static int last = -1, last_prac = -1;
    if (scr != last || practice_active != last_prac) {
        log_cap();
        fprintf(stderr, "scr: %s%s  rss=%ldKB\n",
                practice_active ? "practice/" : "", scr_name(scr), rss_kb());
        last = scr;
        last_prac = practice_active;
    }
    if (!practice_active) toast_follow();
    pdfv_orient();               /* upright, unless it is the flipped PDF page */
    if (practice_active) draw_practice();
    else switch (scr) {
        case SCR_HOME:    draw_home();    break;
        case SCR_LISTS:   draw_lists();   break;
        case SCR_REVIEW:  draw_review();  break;
        case SCR_DICT:    draw_dict();    break;
        case SCR_CHAR:    draw_char();    break;
        case SCR_PICK:    draw_pick();    break;
        case SCR_NEWLIST: draw_newlist(); break;
        case SCR_MENU:    draw_menu();    break;
        case SCR_LEARNED: draw_learned(); break;

        case SCR_MYLISTS: draw_mylists(); break;
        case SCR_LISTVIEW:draw_listview();break;
        case SCR_DONE:    draw_done();    break;
        case SCR_SETTINGS:draw_settings();break;
        case SCR_RESET:   draw_reset();   break;
        case SCR_WORDS:   draw_words();   break;
        case SCR_DRAW:    draw_draw();    break;
        case SCR_STUDY:   draw_study();   break;
        case SCR_STATS:   draw_stats();   break;
        case SCR_STROKES: draw_strokeview(); break;
        case SCR_BOOK:      draw_book();      break;
        case SCR_TEXTCH:    draw_textch();    break;
        case SCR_TEXTREAD:  draw_textread();  break;
        case SCR_TEXTLINE:  draw_textline();  break;
        case SCR_TEXTWRITE: draw_textwrite(); break;
        case SCR_TEXTDONE:  draw_textdone();  break;
        case SCR_TEXTNOTES: draw_textnotes(); break;
        case SCR_MYNOTE:    draw_mynote();    break;
        case SCR_PDFS:      draw_pdfs();      break;
        case SCR_PDFVIEW:   draw_pdfview();   break;
        case SCR_CATORDER:  draw_catorder();  break;
#ifdef HANZI_DESKTOP
        case SCR_EXPORT:    draw_export();    break;
#endif
    }
}

#ifdef HANZI_DESKTOP
static void kfocus_draw(void);      /* the keyboard's ring, below */
#endif
static void redraw(void) {
    WHERE("redrawing the screen");
    msec_t t0 = now_ms();
    if (sleeping) return;          /* the panel is showing the sleep screen */
    render();
#ifdef HANZI_DESKTOP
    kfocus_draw();
#endif
    msec_t t_draw = now_ms();
    /* The non-flashing waveform, always. Every eighth redraw used to be a
     * full GC16 to clear ghosting -- and a GC16 turns the whole panel black
     * before it draws, which is what "the grey outlines flash black" was,
     * on every screen, however the ordinary redraws were done. The only
     * full refreshes left are the sleep screen and the blank on exit. */
    surf_present(rc(0, 0, W, H), RF_PART);
    msec_t t_end = now_ms();
    if (t_end - t0 > 250)          /* only mention redraws that actually drag */
        fprintf(stderr, "slow redraw: %s render=%ldms present=%ldms\n",
                scr_name(scr), (long)(t_draw - t0), (long)(t_end - t_draw));
}

/* Repaint only what the pen just changed. A whole-screen refresh is for
 * moving between screens. A stroke
 * being judged repainted the whole panel, so a right stroke blinked away and
 * came back as the model's, which reads exactly like being told it was
 * wrong.
 *
 * So: the box, and wherever this screen put the note saying why a stroke
 * was refused. */
static void redraw_guided(rect_t box) {
    if (sleeping) return;
    g_note_rect = rc(0, 0, 0, 0);
    render();
    surf_present(rc(0, box.y - 8, W, box.h + 16), RF_PART);
    if (g_note_rect.w > 0) surf_present(g_note_rect, RF_PART);
}

static void card_strokes_load(void) {
    for (int i = 0; i < nstrk; i++) db_strokes_free(&strk[i]);
    nstrk = 0;
    int n = u8_len(card.headword);
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        char ch[8];
        u8_at(card.headword, i, ch);
        db_strokes(ch, &strk[nstrk]);
        nstrk++;
    }
}

static int load_card(void) {
    revealed = 0;
    ui_ink_clear(&ink);
    /* A card answered "again" can come straight back: a guided run kept from
     * last time would show it already written before it was revealed. */
    guided_reset();
    have_card = in_test
        ? db_session_next(&card)
        : db_next_card(list_id, scope_book[0] ? scope_book : NULL,
                       mode, today(), &card);
    nexs = 0;
    if (have_card) {
        card_strokes_load();
        nexs = db_examples(card.key, exs, 3);
        hw_checked = 0;
        for (int i = 0; i < 8; i++) { hw_rank[i] = -1; hw_top[i][0] = 0; }
    }
    if (in_test) db_session_load(&sess);
    if (have_card)
        fprintf(stderr, "card: %s (%s, %d strokes, %d examples) rss=%ldKB\n",
                card.headword, mode == MODE_WRITE ? "write" : "read",
                nstrk ? strk[0].n : 0, nexs, rss_kb());
    return have_card;
}

/* A test is 10 to write, 30 to read. `scored` 0 reviews ahead of schedule
 * without writing to the schedule. */
/* Where a test was started from, to go back to it: the screen, its page and
 * its book. Back used to guess -- "the deck list if there is a book" -- and
 * after a test begun from a book page showed another book's decks under
 * this one's title, where a tick changed that other book. */
static int  test_from_scr = SCR_HOME, test_from_scroll = 0;
static char test_from_book[160] = "";

static void start_session(int scored, int ignore_limit) {
    if (!in_test) {               /* "again" from the results keeps the origin */
        test_from_scr = scr;
        test_from_scroll = scroll;
        snprintf(test_from_book, sizeof test_from_book, "%s", cur_book);
    }
    db_session_start(mode, list_id, scope_book[0] ? scope_book : NULL,
                     list_label, today(),
                     mode == MODE_WRITE ? TEST_WRITE : TEST_READ, scored,
                     ignore_limit);
    db_session_load(&sess);
    in_test = 1;
    scr = sess.total ? SCR_REVIEW : SCR_DONE;
    load_card();
}

static void start_test(void) { start_session(1, 0); }

/* Out of a test, back where it was begun, with that screen's data fresh. */
static void leave_test(void) {
    in_test = 0;
    reload_books();
    snprintf(cur_book, sizeof cur_book, "%s", test_from_book);
    scroll = test_from_scroll;
    switch (test_from_scr) {
    case SCR_LISTS:
        nlists = db_lists(cur_book, lists, MAXLISTS);
        if (scroll >= nlists) scroll = 0;
        scr = SCR_LISTS;
        break;
    case SCR_WORDS:
        scr = SCR_WORDS;
        load_words();
        break;
    case SCR_BOOK:
        /* the book may have gone meanwhile (a reset, a list deleted):
         * decided here, not by draw_book, which can only draw */
        scr = cur_source() ? SCR_BOOK : SCR_HOME;
        break;
    default:
        scr = SCR_HOME;
        break;
    }
}

/* Sentences from the books, with the word itself picked out. Draws as many as
 * fit between `y` and `ybot`, and returns the y it finished at. */
/* Height one sentence needs, including its source line. */
static int ex_height(const char *text) {
    int lines = 1 + (gfx_text_w(EX_PX, text) / (W - 88));
    return lines * EX_LINE + EX_TAIL;
}

/* Space needed before it is worth starting an example block at all. */
static int ex_min_height(void) {
    return nexs ? 44 + ex_height(exs[0].text) : 0;
}

static int draw_examples(int y, int ybot, const char *word) {
    /* The sentences are quoted from the books, so they are not ours to
     * republish -- content.db and the epubs are committed (data/content.db.xz,
     * data/books and the rest) only because the repository is private. A
     * screenshot in the README would publish them just as surely, so those
     * renders are taken with this set. */
    /* Keep the space reserved so the screenshot shows the real geometry --
     * silently reclaiming it would make every other element look bigger than
     * it is on the device. */
    if (getenv("HANZI_NO_EXAMPLES")) {
        if (nexs == 0 || y + ex_min_height() > ybot) return y;
        gfx_fill(rc(24, y, W - 48, 1), FAINT);
        gfx_text(44, y + 52, 36, 0xA0,
                 "(example sentences omitted from published screenshots)");
        return y + ex_min_height();
    }
    /* never leave an orphan rule with nothing under it */
    if (nexs == 0 || y + ex_min_height() > ybot) return y;
    gfx_fill(rc(24, y, W - 48, 1), FAINT);
    y += 44;
    for (int i = 0; i < nexs && i < EX_MAX; i++) {
        if (y + ex_height(exs[i].text) > ybot) break;
        y = ui_text_hl(44, y, W - 88, EX_PX, EX_LINE, exs[i].text, word);
        int bw = gfx_text_w(37, exs[i].book);
        gfx_text(W - 44 - bw, y, 37, 0x90, exs[i].book);
        y += EX_TAIL;
    }
    return y;
}

/* Logs the reason when a press inside the card is not treated as a stroke. */
static int in_writearea_dbg(int x, int y);

static int in_writearea(int x, int y) {
    if (scr == SCR_DRAW) return ui_hit(hw_pad(), x, y);
    if (scr == SCR_MYNOTE) return ui_hit(mynote_pad(), x, y);
    if (scr == SCR_TEXTWRITE) return ui_hit(tw_box(), x, y);
    if (practice_active) {
        int n, py, size;
        prac_geom(&n, &py, &size);
        for (int i = 0; i < n; i++)
            if (ui_hit(cellrect(i, n, py, size), x, y)) return 1;
        return 0;
    }
    if (scr == SCR_PDFVIEW) return pdfv_in_ink(x, y);    /* drawing over the page */
    /* Still live once the answer is showing: seeing where you went wrong and
     * immediately writing it again is the point of having the model there. */
    if (scr != SCR_REVIEW || mode != MODE_WRITE || no_pen || !have_card) return 0;
    int n, cy, size;
    write_geom(&n, &cy, &size);
    for (int i = 0; i < n; i++) if (ui_hit(cellrect(i, n, cy, size), x, y)) return 1;
    return 0;
}

/* The ink a stroke goes into, on the screen it is written on: one choice,
 * for the press and for every move after it. */
static ink_t *ink_target(void) {
    if (scr == SCR_DRAW) return &hw_ink;
    if (practice_active) return &prac_ink;
    if (scr == SCR_PDFVIEW) return &pv_ink;
    return &ink;
}

static int in_writearea_dbg(int x, int y) {
    if (scr != SCR_REVIEW)   { fprintf(stderr, "ink: not on a card\n");       return 0; }
    if (mode != MODE_WRITE)  { fprintf(stderr, "ink: read card\n");           return 0; }
    if (revealed)            { fprintf(stderr, "ink: already revealed\n");    return 0; }
    if (no_pen)              { fprintf(stderr, "ink: no-pen mode\n");         return 0; }
    if (!have_card)          { fprintf(stderr, "ink: no card\n");             return 0; }
    int n, cy, size;
    write_geom(&n, &cy, &size);
    rect_t c = cellrect(0, n, cy, size);
    fprintf(stderr, "ink: %d,%d outside boxes (first box %d,%d %dx%d, n=%d)\n",
            x, y, c.x, c.y, c.w, c.h, n);
    return 0;
}

/* Split the ink by which writing box each stroke falls in, recognise each box
 * and record how the expected character ranked. */
static void assess_writing(void) {
    hw_checked = 0;
    for (int i = 0; i < 8; i++) { hw_rank[i] = -1; hw_top[i][0] = 0; }
    if (no_pen || ink.n == 0 || nstrk == 0) return;

    int n, cy, size;
    write_geom(&n, &cy, &size);

    for (int c = 0; c < n && c < 8; c++) {
        rect_t box = cellrect(c, n, cy, size);
        ink_t sub;
        ui_ink_clear(&sub);
        for (int i = 0; i < ink.n; i++) {
            const inkstroke_t *st = &ink.s[i];
            if (st->n < 1) continue;
            long sx = 0, sy = 0;                  /* centroid decides the box */
            for (int j = 0; j < st->n; j++) { sx += st->p[j].x; sy += st->p[j].y; }
            int cx = (int)(sx / st->n), cy2 = (int)(sy / st->n);
            if (!ui_hit(box, cx, cy2)) continue;
            if (sub.n >= MAXINK_STROKE) break;
            sub.s[sub.n] = *st;
            sub.n++;
        }
        if (sub.n == 0) continue;

        unsigned char sig[HW_MAX_STROKES * 6];
        int ns = ui_ink_signature(&sub, sig, HW_MAX_STROKES);
        if (ns <= 0) continue;
        hwcand_t cand[8];
        int got = db_hw_search(sig, ns, cand, 8);
        if (got <= 0) continue;

        char want[8];
        u8_at(card.headword, c, want);
        for (int k = 0; k < got; k++)
            if (!strcmp(cand[k].ch, want)) { hw_rank[c] = k; break; }
        snprintf(hw_top[c], sizeof hw_top[c], "%s", cand[0].ch);
        hw_checked = 1;
    }
}

static void grade(int g) {
    if (in_test) {
        /* Schedule on the first answer only; a repeat inside the same test is
         * there to make you produce it, not to hit the card twice. Both writes
         * land before the next card is drawn, so quitting here loses nothing.
         * When reviewing ahead, nothing is written to the schedule at all. */
        /* One commit for both. Two let a crash between them schedule the
         * card and leave it unanswered in the test, so on resuming its
         * "first" answer was applied to the schedule a second time. */
        db_begin();
        if (sess.scored && db_session_is_first(card.key)) {
            srs_review(&card.c, g, today());
            db_save_card(&card, mode);
        }
        db_session_mark(card.key, g > 1);
        db_commit();
        if (!load_card()) scr = SCR_DONE;
        return;
    }
    srs_review(&card.c, g, today());
    db_save_card(&card, mode);
    load_card();
}

static void postpone(void) {
    /* Reviewing ahead schedules nothing: "later" there only takes the card
     * out of the session, as a right answer would not move it either. */
    db_begin();                         /* as grade(): one commit */
    if (!(in_test && !sess.scored)) db_postpone(card.key, mode, today(), POSTPONE_DAYS);
    /* out of the test, and neither right nor wrong: it was marked right */
    if (in_test) db_session_put_off(card.key);
    db_commit();
    if (in_test) {
        if (!load_card()) scr = SCR_DONE;
        return;
    }
    load_card();
}

/* Takes a whole word: every character of it gets a box. */
static void open_practice(const char *word) {
    for (int i = 0; i < prac_n; i++) db_strokes_free(&prac_s[i]);
    prac_n = 0;
    /* its characters: a mark inside a phrase (、 of 天时、地利、人和) has no
     * strokes to practise, and took a box of its own */
    prac_word[0] = 0;
    for (int i = 0, m = word ? u8_len(word) : 0; i < m; i++) {
        char ch[8];
        u8_at(word, i, ch);
        if (is_lookup(ch)) strncat(prac_word, ch, sizeof prac_word - strlen(prac_word) - 1);
    }
    int n = u8_len(prac_word);
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        char ch[8];
        u8_at(prac_word, i, ch);
        db_strokes(ch, &prac_s[prac_n]);
        prac_n++;
    }
    ui_ink_clear(&prac_ink);
    prac_reveal = 0;
    practice_active = 1;
    guided_reset();          /* a run left from the card underneath is not this one */
}

static void do_search(void) { nres = db_dict(q, res, MAXRES); }

/* Where `back` returns to from the character screen. It is reached from the
 * dictionary, but also from a card or the study screen, and dumping someone
 * into the dictionary from the middle of a test loses their place. */
static int char_back = SCR_DICT;

/* Pinyin compared as spelled, spaces, apostrophes and case aside: "wàn wù"
 * and "wànwù" are one reading; "jiào" and "jiǎo" are two. */
static void py_key(const char *in, char *out, size_t n) {
    size_t k = 0;
    for (const char *p = in; p && *p && k + 1 < n; p++) {
        if (*p == ' ' || *p == '\'' || *p == '-') continue;
        out[k++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
    }
    out[k] = 0;
}

/* The same, with the tones taken off: bú and bù are one syllable here.
 * translator B and the pinyin editions write tones as they are spoken -- 不 is
 * bú before a fourth tone, 一 yí -- where the dictionary writes the citation
 * tone, so a reading from a text often matches no entry exactly. */
static void py_bare(const char *in, char *out, size_t n) {
    static const struct { unsigned cp; char c; } MARKED[] = {
        {0x101,'a'},{0xe1,'a'},{0x1ce,'a'},{0xe0,'a'}, {0x113,'e'},{0xe9,'e'},{0x11b,'e'},{0xe8,'e'},
        {0x12b,'i'},{0xed,'i'},{0x1d0,'i'},{0xec,'i'}, {0x14d,'o'},{0xf3,'o'},{0x1d2,'o'},{0xf2,'o'},
        {0x16b,'u'},{0xfa,'u'},{0x1d4,'u'},{0xf9,'u'}, {0x1d6,'v'},{0x1d8,'v'},{0x1da,'v'},{0x1dc,'v'},
        {0xfc,'v'},
    };
    size_t k = 0;
    const unsigned char *p = (const unsigned char *)(in ? in : "");
    while (*p && k + 1 < n) {
        unsigned cp = *p;
        int len = 1;
        if (cp >= 0xC0 && cp < 0xE0 && p[1]) { cp = ((cp & 0x1Fu) << 6) | (p[1] & 0x3Fu); len = 2; }
        else if (cp >= 0xE0 && p[1] && p[2]) { cp = ((cp & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); len = 3; }
        p += len;
        if (cp == ' ' || cp == '\'' || cp == '-') continue;
        if (cp < 0x80) { out[k++] = (char)(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp); continue; }
        for (const auto &m : MARKED) if (m.cp == cp) { out[k++] = m.c; break; }
    }
    out[k] = 0;
}

static void open_char_read(const char *word, const char *ctx);
static void open_char(const char *word) { open_char_read(word, NULL); }

/* `ctx` is how the word is read where it was pressed, when that is known:
 * its entry goes first, and is the one "add" puts in a list. */
static void open_char_read(const char *word, const char *ctx) {
    detail_from_book = 0;
    detail_bnote[0] = 0;               /* the caller that has one sets it */
    detail_bnote_on = 0;
    snprintf(detail_word, sizeof detail_word, "%s", word);
    u8_at(word, 0, detail_ch);
    /* Every character of the word, not just the first: opening 声音 and
     * being shown only 声 is not opening 声音. */
    for (int i = 0; i < detail_n; i++) db_strokes_free(&detail_ws[i]);
    detail_n = 0;
    int n = u8_len(detail_word);
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        char ch[8];
        u8_at(detail_word, i, ch);
        db_strokes(ch, &detail_ws[detail_n]);
        detail_n++;
    }
    db_strokes_free(&detail_s);
    db_strokes(detail_ch, &detail_s);
    db_charinfo(detail_ch, &detail_ci);
    /* The word as it is, where the dictionary has it (卡拉OK is one);
     * otherwise without the marks inside it, a phrase the book glosses */
    snprintf(detail_key, sizeof detail_key, "%s", detail_word);
    detail_nents = db_word_entries(detail_key, detail_ents, MAXENTS);
    int marks = 0;
    if (detail_nents == 0) {
        char bare[sizeof detail_key] = "";
        for (int i = 0, m = u8_len(detail_word); i < m; i++) {
            char ch[8];
            u8_at(detail_word, i, ch);
            if (is_lookup(ch)) strncat(bare, ch, sizeof bare - strlen(bare) - 1);
        }
        if (bare[0] && strcmp(bare, detail_word)) {
            marks = 1;
            snprintf(detail_key, sizeof detail_key, "%s", bare);
            detail_nents = db_word_entries(detail_key, detail_ents, MAXENTS);
        }
    }
    db_word_lookup(detail_key, detail_py, (int)sizeof detail_py,
                   detail_df, (int)sizeof detail_df);
    /* add: the word; with marks, the word they leave where it is one, and
     * nothing where it is not (你吃白面馒头，我吃菜团；...) */
    snprintf(detail_add, sizeof detail_add, "%s",
             !marks || detail_nents > 0 ? detail_key : "");
    detail_ctx_first = 0;
    detail_all = 0;
    if (ctx && ctx[0] && detail_nents > 0) {
        /* The entry read here: spelled the same, or failing that the one
         * entry that is the same syllable with the tones off -- only when it
         * is the only one, so 徼 jiào is never taken for 徼 jiǎo. */
        char want[160], have[160];
        int pick = -1;
        py_key(ctx, want, sizeof want);
        for (int i = 0; i < detail_nents && pick < 0; i++) {
            py_key(detail_ents[i].pinyin, have, sizeof have);
            if (!strcmp(want, have)) pick = i;
        }
        if (pick < 0) {
            /* Readings, not rows: 西 comes back as the surname Xī and as
             * xī, both "xi" with the tones off, and that is one reading,
             * not a choice to refuse. Two spellings (jiǎo, jiào) are. */
            char first[160] = "";
            int differ = 0;
            py_bare(ctx, want, sizeof want);
            for (int i = 0; i < detail_nents; i++) {
                py_bare(detail_ents[i].pinyin, have, sizeof have);
                if (strcmp(want, have)) continue;
                char spelt[160];
                py_key(detail_ents[i].pinyin, spelt, sizeof spelt);
                if (pick < 0) { pick = i; snprintf(first, sizeof first, "%s", spelt); }
                else if (strcmp(first, spelt)) differ = 1;
            }
            if (differ) pick = -1;
        }
        if (pick >= 0) {
            dictrow_t e = detail_ents[pick];           /* to the front */
            memmove(&detail_ents[1], &detail_ents[0], sizeof e * (size_t)pick);
            detail_ents[0] = e;
            detail_ctx_first = 1;
        }
    }
    if (detail_nents > 0) {            /* what add saves: the entry shown first */
        snprintf(detail_py, sizeof detail_py, "%s", detail_ents[0].pinyin);
        snprintf(detail_df, sizeof detail_df, "%s", detail_ents[0].defn);
    }
    toast[0] = 0;
    scr = SCR_CHAR;
}

/* A word pressed in a text: the definition its book's word list gives
 * goes first -- the one learned from that chapter's deck -- then the dictionary's readings. */
static void detail_book_first(const char *book, const char *chap) {
    dictrow_t e;
    if (!db_book_entry(book, chap, detail_key, &e)) return;
    if (detail_nents >= MAXENTS) detail_nents = MAXENTS - 1;
    if (detail_nents > 0) memmove(&detail_ents[1], &detail_ents[0], sizeof e * (size_t)detail_nents);
    detail_ents[0] = e;
    detail_nents++;
    detail_ctx_first = 0;
    detail_from_book = 1;
    snprintf(detail_py, sizeof detail_py, "%s", e.pinyin);
    snprintf(detail_df, sizeof detail_df, "%s", e.defn);
}

/* The syllable at position i of a spaced reading ("wàn wù", 1 -> "wù"),
 * when it has exactly `count` syllables; empty otherwise. */
static void syllable_of(const char *py, int i, int count, char *out, size_t n) {
    out[0] = 0;
    char buf[160];
    snprintf(buf, sizeof buf, "%s", py ? py : "");
    int k = 0;
    char *save = NULL;
    for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save), k++)
        if (k == i) snprintf(out, n, "%s", t);
    if (k != count) out[0] = 0;
}

static void kbd_edit(char *buf, size_t cap, int x, int y) {
    int k = ui_keyboard_hit(rc(0, H - KB_HEIGHT, W, KB_HEIGHT), x, y);
    if (k == KEY_CLEAR) buf[0] = 0;
    else if (k == 8) {
        /* Back up a whole character, not a byte. A hanzi is three bytes in
         * UTF-8, so dropping one left a truncated sequence that drew as a
         * pair of empty boxes -- it looked like backspace *added* something. */
        size_t l = strlen(buf);
        while (l > 0) {
            l--;
            if (((unsigned char)buf[l] & 0xC0) != 0x80) break;   /* not a tail */
        }
        buf[l] = 0;
    }
    else if (k) { size_t l = strlen(buf); if (l < cap - 2) { buf[l] = (char)k; buf[l+1] = 0; } }
}

static void tap(int x, int y);

/* Press the centre of whatever hot rect carries `id`, going through the same
 * hit test a finger does rather than calling the action directly -- a button
 * that is not on screen must do nothing, here as well as under a thumb.
 * Returns 0 if no such rect is present. Used by the page keys and the tests. */
/* Is this id on screen? Used to page towards something without pressing
 * anything on the way. */
static int press_dry(int id) {
    for (int i = 0; i < nhot; i++)
        if (hot[i].id == id) return 1;
    return 0;
}

static int press(int id) {
    for (int i = 0; i < nhot; i++)
        if (hot[i].id == id) {
            tap(hot[i].r.x + hot[i].r.w / 2, hot[i].r.y + hot[i].r.h / 2);
            return 1;
        }
    return 0;
}

#ifdef HANZI_DESKTOP
/* ---- the keyboard (the desktop only) -------------------------------------- *
 * Every screen works from the keyboard: Tab / Shift+Tab (or Down / Up) move
 * a ring from control to control in reading order and Enter presses it; Esc
 * goes back; Left / Right / Page Up / Page Down are the Kobo's page keys;
 * typing goes into the dictionary's search and a list's name; 1 2 3 grade a
 * card; p, e and n are pinyin, English and notes on a text; [ and ] step
 * through a sentence's characters, Enter opening one. Writing still wants
 * the mouse (or "picture it, then check"). The Kobo build
 * has none of this -- it has no keyboard -- and the ring is only drawn once
 * a key has been used, and goes when the mouse is. */
static int kfocus_id = -1;          /* the control with the ring, by hot id */
static int kfocus_on = 0;           /* a key has been used since the mouse */
/* The sentence screen's characters are not controls but boxes (tbox), one
 * per character: [ and ] move a ring along them, Enter opens the one it is
 * on, as a finger would. -1: none; kept only for the sentence it was on. */
static int kchar = -1, kchar_line = -1, kchar_page = -1;

/* The screen's controls in reading order: top to bottom, left to right. The
 * on-screen keyboard is typed on instead, and a writing box drawn in. */
static int hot_order(int *out) {
    int n = 0;
    for (int i = 0; i < nhot; i++) {
        if (hot[i].id == H_KEY) continue;
        if (hot[i].r.w * hot[i].r.h > W * H / 3) continue;
        int dup = 0;                 /* a rect registered twice (a child) once */
        for (int j = 0; j < n; j++) if (hot[out[j]].id == hot[i].id) dup = 1;
        if (!dup) out[n++] = i;
    }
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0; b--) {
            rect_t p = hot[out[b - 1]].r, q = hot[out[b]].r;
            int before = q.y + q.h / 2 < p.y ? 1 : (q.y > p.y + p.h / 2 ? 0 : q.x < p.x);
            if (!before) break;
            int t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
        }
    return n;
}

static void kfocus_draw(void) {
    if (scr == SCR_TEXTLINE && kchar >= 0 && kchar < ntbox &&
        kchar_line == tw_line && kchar_page == tline_page) {
        rect_t r = tbox[kchar].r;
        gfx_frame(rc(r.x - 4, r.y - 4, r.w + 8, r.h + 8), 4, BLACK);
    }
    if (!kfocus_on) return;
    for (int i = 0; i < nhot; i++)
        if (hot[i].id == kfocus_id && hot[i].r.w * hot[i].r.h <= W * H / 3) {
            rect_t r = hot[i].r;
            gfx_frame(rc(r.x - 7, r.y - 7, r.w + 14, r.h + 14), 5, BLACK);
            gfx_frame(rc(r.x - 2, r.y - 2, r.w + 4, r.h + 4), 3, WHITE);
            return;
        }
}

static void kfocus_move(int dir) {
    static int order[MAXHOT];
    int n = hot_order(order);
    if (n == 0) return;
    int at = -1;
    for (int k = 0; k < n; k++) if (hot[order[k]].id == kfocus_id) at = k;
    if (!kfocus_on || at < 0) at = dir > 0 ? 0 : n - 1;
    else at = (at + dir + n) % n;
    kfocus_id = hot[order[at]].id;
    kfocus_on = 1;
}

/* Typing into a field: the dictionary's search, a list's name. */
static int type_into(char *buf, size_t cap, int k) {
    size_t l = strlen(buf);
    if (k == 8) {
        while (l > 0) { l--; if (((unsigned char)buf[l] & 0xC0) != 0x80) break; }
        buf[l] = 0;
    } else if (k >= 32 && k < 127) {
        if (l + 2 < cap) { buf[l] = (char)k; buf[l + 1] = 0; }
    } else return 0;
    return 1;
}

static void redraw(void);
static void do_search(void);

/* One key. Returns 1 when it did something (and the screen is redrawn). */
static int app_key(int k) {
    int typing = !practice_active && (scr == SCR_DICT || scr == SCR_NEWLIST || scr == SCR_PDFS);
    if (typing && (k == 8 || (k >= 32 && k < 127))) {
        if (scr == SCR_DICT) { type_into(q, sizeof q, k); do_search(); }
        else if (scr == SCR_PDFS) { type_into(pdf_q, sizeof pdf_q, k); pdf_list_build(); scroll = 0; }
        else type_into(newname, sizeof newname, k);
        redraw();
        return 1;
    }
    if (scr == SCR_TEXTLINE && ntbox > 0 && (k == '[' || k == ']' ||
                                             (k == 13 && kchar >= 0 && kchar_line == tw_line))) {
        if (kchar_line != tw_line || kchar_page != tline_page || kchar >= ntbox) kchar = -1;
        if (k == 13) {
            if (kchar < 0) return 1;                /* the character it was on is gone */
            rect_t r = tbox[kchar].r;
            kchar = -1;
            int was = scr;
            tap(r.x + r.w / 2, r.y + r.h / 2);      /* as a finger would */
            if (scr == was) redraw();               /* a □ is absorbed without one */
            return 1;
        }
        kchar = kchar < 0 ? (k == ']' ? 0 : ntbox - 1)
                          : (kchar + (k == ']' ? 1 : -1) + ntbox) % ntbox;
        kchar_line = tw_line; kchar_page = tline_page;
        kfocus_on = 0;
        redraw();
        return 1;
    }
    switch (k) {
    case K_TAB: case K_DOWN:    kfocus_move(1);  kchar = -1; redraw(); return 1;
    case K_BACKTAB: case K_UP:  kfocus_move(-1); kchar = -1; redraw(); return 1;
    case 13:
        if (kfocus_on && press_dry(kfocus_id)) {
            int id = kfocus_id;
            press(id);
            /* the ring stays on the same control if it is still there */
            if (!press_dry(id)) kfocus_on = 0;
            redraw();
            return 1;
        }
        if (scr == SCR_NEWLIST && press(H_CHECK)) return 1;
        return 0;
    case 27: case 8:
        if (press(H_BACK)) { kfocus_on = 0; return 1; }
        return 0;
    }
    /* shortcuts, where the control is on screen */
    int id = 0;
    if (k == '1') id = H_AGAIN; else if (k == '2') id = H_HARD; else if (k == '3') id = H_GOOD;
    else if (k == ' ') id = press_dry(H_CHECK) ? H_CHECK : 0;
    else if (k == 'p') id = H_TPY; else if (k == 'e') id = press_dry(H_TEN) ? H_TEN : H_TCHAPEN;
    else if (k == 'n') id = H_TNOTES;
    if (id && press(id)) { redraw(); return 1; }
    return 0;
}
#endif

/* ---- two editions of a text ------------------------------------------- */

/* texts[] index of the text in hand, and of its other edition; -1 if none. */
static int text_index(void) {
    for (int i = 0; i < ntexts; i++) if (texts[i].id == text_id) return i;
    return -1;
}
static int other_edition(void) {
    int cur = text_index();
    if (cur < 0) return -1;
    if (is_edition(texts[cur].title)) {
        char base[200];
        edition_base(texts[cur].title, base, sizeof base);
        for (int i = 0; i < ntexts; i++) if (!strcmp(texts[i].title, base)) return i;
        return -1;
    }
    return edition_of(texts[cur].title);
}

/* The header's name for the edition in hand: 1 where you read a text and
 * pressing it switches, 2 where you write one and it only says which you are
 * writing; 0 where there is no other edition. */
static int edition_label(char *out, size_t n) {
    if (practice_active) return 0;
    int writing = scr == SCR_TEXTWRITE || scr == SCR_TEXTDONE;
    if (scr != SCR_TEXTCH && scr != SCR_TEXTREAD && scr != SCR_TEXTLINE
        && scr != SCR_TEXTNOTES && !writing) return 0;
    if (other_edition() < 0) return 0;
    int cur = text_index();
    snprintf(out, n, "%s", is_edition(texts[cur].title) ? "edition"
                          : (texts[cur].author[0] ? texts[cur].author : "original"));
    return writing ? 2 : 1;
}

/* Over to the other edition, at the same place: the same chapter, and the
 * passage the sentence in hand corresponds to -- by the translator's passage
 * numbers, which both editions carry. */
static void switch_edition(void) {
    int other = other_edition();
    if (other < 0) return;
    int was = scr, idx = tchap_idx, from = tchap_id;
    int line = was == SCR_TEXTREAD ? tread_start[tread_np] : tw_line;
    int to_edition = !is_edition(text_title);
    int src = -1;
    if (to_edition) {
        /* the first passage on this line, or on the nearest line before */
        for (int l = line; l >= 0 && src < 0; l--) src = db_note_src(from, l);
    } else {
        src = line;                       /* in his edition, a line is a passage */
    }
    text_open(texts[other].id, texts[other].title);      /* -> the chapter list */
    if (was == SCR_TEXTCH || ntchaps == 0) return;
    tchap_open(idx < ntchaps ? idx : ntchaps - 1);
    int to = 0;
    if (src >= 0) to = to_edition ? src : db_note_line_for_src(tchap_id, src);
    if (to < 0) to = 0;
    if (to >= ntlines) to = ntlines > 0 ? ntlines - 1 : 0;
    if (was == SCR_TEXTREAD) {
        tread_seek(to);
        scr = SCR_TEXTREAD;
        mark_page_read();
    } else {
        tw_load(to);
        tline_page = 0;
        mark_reading(tw_line);
        if (was == SCR_TEXTNOTES) { notes_open(); scr = SCR_TEXTNOTES; }
        else scr = SCR_TEXTLINE;
    }
}

/* Whether a press on no control does anything here. Mostly it does not, and
 * redrawing the whole panel for it is a 2.1M-pixel blit and a refresh with
 * nothing changed. Where it does, it is always putting something away: an
 * armed delete or clear, the notes' page grid or "about here", a picture
 * filling the screen. A state like those added to a screen belongs here. */
static int nothing_matters(void) {
    if (practice_active) return 0;
    switch (scr) {
    case SCR_LISTVIEW:  return delete_armed;
    case SCR_MYNOTE:    return mynote_clear_armed;
    case SCR_TEXTNOTES: return tnote_picking || tnote_about;
    case SCR_TEXTREAD:  return ntplates && comic_full;
    case SCR_PDFS:      return pdf_kb;
    case SCR_PDFVIEW:   return pv.choosing;
    default:            return 0;
    }
}

/* ---- settings, from every screen ---------------------------------------- *
 * The header's button opens settings wherever you are, and back returns to
 * exactly that screen: the page of the book, the card half-way through a
 * test, the ink in the box, the query in the dictionary, the practice box
 * over whatever it was over. Settings has state of its own and touches
 * none of what those screens draw from, so most of it is simply left where
 * it is. What is kept here is what settings can change under a screen: a
 * text's layout (size, punctuation, readings), laid out again from the line
 * that was at the top; the counts (new words a day, a reset), read again;
 * and a test, which a reset ends -- that goes back the way the test's own
 * back would. Opened from the menu, back is the menu, as it always was. */
struct settings_from {
    int scr = SCR_MENU;
    int prac = 0, scroll = 0, in_test = 0;
    int scale = 0, punct = 0, readings = 0;   /* the layout it was drawn at */
    int anchor = -1;          /* a text's line, or character, at the top */
    int reset = 0;            /* progress was reset while there */
};
static settings_from set_from;

static void settings_open(void) {
    if (scr == SCR_SETTINGS || scr == SCR_RESET) return;
    set_from = settings_from();
    set_from.scr = scr;
    set_from.prac = practice_active;
    set_from.scroll = scroll;
    set_from.in_test = in_test;
    set_from.scale = text_scale;
    set_from.punct = show_punct;
    set_from.readings = text_readings;
    /* The top of the page, unless the point kept last time is still on
     * it: then that, or larger and back again would land a page early. */
    static int kept = -1;
    if (!practice_active && scr == SCR_TEXTREAD && ntplates) {
        comic_layout();
        int a, b;
        comic_page_lines(&a, &b);
        int lf = tplates[cplate].line_first;
        if (b > a) set_from.anchor = kept >= lf + a && kept < lf + b ? kept : lf + a;
    } else if (!practice_active && scr == SCR_TEXTREAD) {
        set_from.anchor = tread_start[tread_np];     /* reflowed to start there */
    } else if (!practice_active && scr == SCR_TEXTLINE) {
        set_from.anchor = kept >= tline_top && kept < tline_end ? kept : tline_top;
    }
    kept = set_from.anchor;
    /* an armed delete or clear is disarmed by any other press, this too */
    delete_armed = 0;
    mynote_clear_armed = 0;
    practice_active = 0;
    reset_armed = -2;
    toast[0] = 0;
    scr = SCR_SETTINGS;
}

static void settings_close(void) {
    settings_from f = set_from;
    set_from = settings_from();              /* used once */
    toast[0] = 0;
    reset_armed = -2;
    scr = f.scr;
    if (f.scr == SCR_MENU) return;
    practice_active = f.prac;
    scroll = f.scroll;
    reload_books();                           /* the counts, whatever changed */
    if (f.reset && f.in_test) {
        /* the reset ended the test (db_reset_progress clears the session):
         * out of it to where it was begun, as the test's own back goes */
        practice_active = 0;
        leave_test();
        return;
    }
    int relaid = text_scale != f.scale || show_punct != f.punct
              || text_readings != f.readings;
    switch (f.scr) {
    case SCR_LISTS:
        nlists = db_lists(cur_book, lists, MAXLISTS);
        if (scroll >= nlists) scroll = 0;
        break;
    case SCR_WORDS:    load_words();   break;
    case SCR_LEARNED:  load_learned(); break;
    case SCR_LISTVIEW: load_uents();   break;
    case SCR_TEXTREAD:
        if (!relaid) break;
        if (!ntplates) tread_reflow(f.anchor >= 0 ? f.anchor : tread_start[tread_np]);
        else if (f.anchor >= 0) cseek_line = f.anchor;   /* a caption page */
        break;
    case SCR_TEXTLINE:
        if (relaid) tline_seek = f.anchor;
        break;
    default:
        break;                  /* the notes page keeps its own place */
    }
}

/* What the PDF page shows as it was drawn: a change of any of it leaves
 * the ink and the character read behind (pdfv_marks_drop). */
static std::vector<int> pdfv_view_key(void) {
    return { pv.open, pv.page, pv.zoom, pv.part, pv.trim, pv.rot, pv.flip, pv.choosing, pv.tapmode };
}

/* The panel's controls on the PDF page; 1 if `id` was one of them. A
 * character chosen opens the character screen (add to a list is there),
 * whose back returns to this page as it was. */
static int pdfv_panel_tap(int id) {
    if (pv.tapmode == PT_OCR && pv_read.on) {
        if (id == H_PV_RCLOSE) { pv_read = pv_read_t(); return 1; }
        if (id >= H_CAND && id < H_CAND + HW_CANDS) {
            if (id - H_CAND < pv_read.n) { char_back = SCR_PDFVIEW; open_char(pv_read.cand[id - H_CAND].ch); }
            return 1;
        }
        if (id >= H_PV_CTX && id < H_PV_CTX + PV_CTX_MAX) {
            size_t i = (size_t)(id - H_PV_CTX);
            if (i < pv_read.ctx.size() && pv_hanzi(pv_read.ctx[i].c_str())) {
                char_back = SCR_PDFVIEW;
                open_char(pv_read.ctx[i].c_str());
            }
            return 1;
        }
    }
    if (pv.tapmode == PT_DRAW) {
        if (id == H_UNDO)  { if (pv_ink.n > 0) { pv_ink.n--; pv_word_recognise(); } return 1; }
        if (id == H_CLEAR) { pv_word_clear(); pv_built[0] = 0; return 1; }
        if (id >= H_CAND && id < H_CAND + HW_CANDS) { pv_word_choose(id - H_CAND); return 1; }
        if (id >= H_PV_WCH && id < H_PV_WCH + PV_MAXCL) {
            if (id - H_PV_WCH < pvw.n) { pvw.sel = id - H_PV_WCH; pvw.full = 0; }
            return 1;
        }
        if (id == H_PV_LOOKUP) {
            std::string w = pv_word();
            if (!w.empty()) { char_back = SCR_PDFVIEW; open_char(w.c_str()); }
            return 1;
        }
    }
    return 0;
}

static void tap(int x, int y) {
    int id = hottest(x, y);
    if (id == H_EDSWITCH) { switch_edition(); redraw(); return; }

    if (id == H_QUIT)  { want_quit = 1; return; }
    /* from every screen, the practice box included (settings_open) */
    if (id == H_HDRSET || id == H_SETTINGS) { settings_open(); redraw(); return; }

    /* Reading a text: every character is live, resolved by position rather
     * than by a hot rect, because a chapter has more characters than the hot
     * rect table holds. */
    /* The chapter page's characters too (not a comic's, whose caption lines
     * open as sentences -- but an apart comic's text page is a chapter
     * page, and its picture page registers none). A press opens the word
     * the character is in, not the word starting at it. */
    if ((scr == SCR_TEXTLINE || (scr == SCR_TEXTREAD && (!ntplates || comic_apart))) && id == H_NONE) {
        for (int i = 0; i < ntbox; i++) {
            if (!ui_hit(tbox[i].r, x, y)) continue;
            /* □, a mark: nothing to look up, so the press does nothing,
             * as no reading is set under them. A page for □ showed
             * CC-CEDICT's placeholders, and one for a mark was empty. */
            char c0[8];
            u8_at(tlines[tbox[i].line], tbox[i].idx, c0);
            if (!is_lookup(c0)) return;
            char w[64];
            /* the word the character is in, on both screens */
            int at = word_at(tlines[tbox[i].line], tbox[i].idx, w, sizeof w);
            /* or the word the book notes there, marked; and the book's
             * note on the word, where it has one (bnote_pick) */
            std::string bnote;
            int bn_how = bnote_pick(tbox[i].line, tbox[i].idx,
                                    scr == SCR_TEXTREAD && show_booknotes, w, sizeof w, &at, bnote);
            if (bn_how == 0 && scr == SCR_TEXTREAD && show_booknotes)
                bn_how = bnote_pick(tbox[i].line, tbox[i].idx, 0, w, sizeof w, &at, bnote);
            if (w[0]) {
                /* its reading in this sentence, where the text says */
                char ctx[160] = "";
                int known = 1, n = u8_len(w);
                for (int k = 0; k < n; k++) {
                    char c1[8], sy[40];
                    u8_at(tlines[tbox[i].line], at + k, c1);
                    known &= reading_at(tbox[i].line, at + k, c1, sy, sizeof sy);
                    size_t l = strlen(ctx);
                    snprintf(ctx + l, sizeof ctx - l, "%s%s", k ? " " : "", sy);
                }
                char_back = scr;         /* back to the same page */
                open_char_read(w, known ? ctx : NULL);
                detail_book_first(text_title, tchap_title);
                snprintf(detail_bnote, sizeof detail_bnote, "%s", bnote.c_str());
                detail_bnote_on = bn_how == 2;
                redraw();
            }
            return;
        }
    }
    if (id == H_NONE && !nothing_matters()) return;

    /* A character of the word on screen, wherever it is shown large. */
    if (id >= H_CHARSEL && id < H_CHARSEL + CHARSEL_MAX && !practice_active) {
        const char *word = NULL;
        if (scr == SCR_STUDY && study_abs >= scroll && study_abs < scroll + nwords)
            word = words[study_abs - scroll].headword;
        else if (scr == SCR_REVIEW && have_card)
            word = card.headword;
        if (word) {
            char ch[8];
            if (u8_at(word, id - H_CHARSEL, ch)) {
                const char *wpy = scr == SCR_STUDY ? words[study_abs - scroll].pinyin
                                                   : card.pinyin;
                char sy[40];
                syllable_of(wpy, id - H_CHARSEL, u8_len(word), sy, sizeof sy);
                char_back = scr;          /* come back to the card, not the dict */
                open_char_read(ch, sy);
                redraw();
                return;
            }
        }
    }

    /* The toggle is on every writing screen, so it is handled once here
     * rather than repeated in each screen's block. */
    if (id == H_GUIDED) {
        guided_on = !guided_on;
        db_set_guided(guided_on);
        guided_reset();
        redraw();
        return;
    }
    if (id == H_PENONLY) {
        db_set_pen_only(!pen_only);
        pen_only = db_pen_only();
        redraw();
        return;
    }

    if (practice_active) {
        if (id == H_BACK)       practice_active = 0;
        else if (id == H_CHECK) prac_reveal = !prac_reveal;
        else if (id == H_CLEAR) { ui_ink_clear(&prac_ink); guided_reset(); }
        redraw();
        return;
    }

    switch (scr) {
    case SCR_HOME:
        if (id == H_QUIT) { want_quit = 1; return; }
        else if (id == H_BACK && cur_cat >= 0) { cur_cat = -1; scroll = top_scroll; }
        else if (id == H_MORE) { scroll += page_step;
                                 if (scroll >= nhrows) scroll = nhrows - 1; }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0; }
        else if (id == H_MENU) { toast[0] = 0; scr = SCR_MENU; }
        else if (id == H_PDFS) {
            top_scroll = scroll;
            pdfs_back = SCR_HOME;
            pdfs_open();
        }
        else if (id == H_DICT) { scr = SCR_DICT; q[0] = 0; nres = 0; }
        else if (id == H_SCRATCH) { draw_back = SCR_HOME; ui_ink_clear(&hw_ink);
                                    hw_n = 0; hw_built[0] = 0; scr = SCR_DRAW; }
        else if (id >= H_ITEM && id < H_TICK) {
            int k = id - H_ITEM;
            if (k < nhrows && hrows[k].kind == HR_CAT) { top_scroll = scroll; cur_cat = hrows[k].idx; scroll = 0; }
            else if (k < nhrows && hrows[k].kind == HR_MYLISTS) {
                top_scroll = scroll; scroll = 0;
                mylists_back = SCR_HOME;
                scr = SCR_MYLISTS;
            }
            else if (k < nhrows) {
                if (cur_cat >= 0) cat_scroll = scroll; else top_scroll = scroll;
                snprintf(cur_src_name, sizeof cur_src_name, "%s", srcs[hrows[k].idx].name);
                scr = SCR_BOOK;
            }
        }
        else if (id == H_TCONTINUE) {
            /* Straight back to the page you were on, not to the book's
             * page: this button exists to save those presses. Back comes
             * out through the book's page to this page of home. */
            if (cur_cat >= 0) cat_scroll = scroll; else top_scroll = scroll;
            int lt = db_last_text();
            for (int i = 0; i < ntexts; i++)
                if (texts[i].id == lt) {
                    edition_base(texts[i].title, cur_src_name, sizeof cur_src_name);
                    text_open(texts[i].id, texts[i].title);
                    int last = db_text_last_chapter(lt);
                    for (int k = 0; k < ntchaps; k++)
                        if (tchaps[k].id == last) {
                            tchap_open(k);
                            tread_seek(db_text_read(last));
                            scr = SCR_TEXTREAD;
                            break;
                        }
                    break;
                }
        }
        else if (id == H_WRITE || id == H_READ) {
            mode = (id == H_WRITE) ? MODE_WRITE : MODE_READ;
            list_id = 0;
            scope_book[0] = 0;
            cur_book[0] = 0;        /* else back lands in a stale chapter list */
            snprintf(list_label, sizeof list_label, "all decks");
            start_test();
        }
        else if (id == H_RESUME) {
            mode    = sess.mode;
            list_id = sess.list_id;
            snprintf(scope_book, sizeof scope_book, "%s", sess.book);
            snprintf(list_label, sizeof list_label, "%s", sess.label);
            /* resumed from home, so back is home -- not the last book seen */
            test_from_scr = SCR_HOME; test_from_scroll = 0;
            test_from_book[0] = cur_book[0] = 0;
            in_test = 1;
            scr = SCR_REVIEW;
            if (!load_card()) scr = SCR_DONE;
        }
        else if (id >= H_TICK && id < H_GO_W) {
            /* The ticks are indexed by row, and a row is a book that may have
             * no decks at all; only the ones that do can be ticked. */
            int k = id >= H_TICK_R ? id - H_TICK_R : id - H_TICK;
            tick_mode = id >= H_TICK_R ? MODE_READ : MODE_WRITE;
            if (k < nhrows && hrows[k].kind == HR_CAT) {
                /* a category's tick sets every book in it the same way */
                int c = hrows[k].idx, en[2];
                category_ticks(c, en);
                db_begin();              /* every book in the category, one commit */
                for (int j = 0; j < nsrcs; j++)
                    if (book_category(srcs[j].name) == c && srcs[j].book_i >= 0)
                        db_book_set(books[srcs[j].book_i].name, tick_mode, en[tick_mode] == 1 ? 0 : 1);
                db_commit();
                reload_books();
                build_hrows();
            }
            int i = k < nhrows && hrows[k].kind == HR_BOOK ? hrows[k].idx : nsrcs;
            if (i < nsrcs && srcs[i].book_i >= 0) {
                const char *nm = books[srcs[i].book_i].name;
                db_book_set(nm, tick_mode,
                            books[srcs[i].book_i].enabled[tick_mode] == 1 ? 0 : 1);
                reload_books();
            }
        }
        break;

    case SCR_BOOK: {
        /* back to the page of home the book was opened from */
        if (id == H_BACK) {
            scr = SCR_HOME;
            scroll = cur_cat >= 0 ? cat_scroll : top_scroll;
            build_sources(); build_hrows();
            if (scroll >= nhrows) scroll = 0;       /* never an empty page */
            break;
        }
        const source_t *sv = cur_source();
        if (!sv) { scr = SCR_HOME; break; }
        if (id == H_TREAD && sv->text_i >= 0) {
            text_open(texts[sv->text_i].id, texts[sv->text_i].title);
        }
        else if (id == H_TEDITION && sv->text_i >= 0 && edition_of(sv->name) >= 0) {
            /* Where you were in that edition, or its chapters if nowhere yet */
            int ed = edition_of(sv->name);
            text_open(texts[ed].id, texts[ed].title);
            int last = db_text_last_chapter(text_id);
            for (int k = 0; k < ntchaps; k++)
                if (tchaps[k].id == last) {
                    tchap_open(k);
                    tread_seek(db_text_read(last));
                    scr = SCR_TEXTREAD;
                    break;
                }
        }
        else if (id == H_TCONTINUE && sv->text_i >= 0) {
            text_open(texts[sv->text_i].id, texts[sv->text_i].title);
            int last = db_text_last_chapter(text_id);
            for (int k = 0; k < ntchaps; k++)
                if (tchaps[k].id == last) {
                    tchap_open(k);
                    tread_seek(db_text_read(last));
                    scr = SCR_TEXTREAD;
                    break;
                }
        }
        else if (id == H_DECKS && sv->book_i >= 0) {
            snprintf(cur_book, sizeof cur_book, "%s", books[sv->book_i].name);
            nlists = db_lists(cur_book, lists, MAXLISTS);
            scroll = 0;
            scr = SCR_LISTS;
        }
        else if ((id == H_GO_W || id == H_GO_R) && sv->book_i >= 0) {
            const char *nm = books[sv->book_i].name;
            mode = (id == H_GO_R) ? MODE_READ : MODE_WRITE;
            list_id = 0;
            snprintf(scope_book, sizeof scope_book, "%s", nm);
            snprintf(cur_book,   sizeof cur_book,   "%s", nm);
            snprintf(list_label, sizeof list_label, "%s", nm);
            start_test();
        }
        break;
    }

    case SCR_LISTS:
        if (id == H_BACK) { reload_books();       /* then ask: is the book there */
                            scr = cur_source() ? SCR_BOOK : SCR_HOME; scroll = 0; }
        else if (id == H_MORE) { scroll += page_step; if (scroll >= nlists) scroll = nlists - 1; }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0; }

        else if (id == H_WRITE || id == H_READ) {
            mode = (id == H_WRITE) ? MODE_WRITE : MODE_READ;
            list_id = 0;
            snprintf(scope_book, sizeof scope_book, "%s", cur_book);
            snprintf(list_label, sizeof list_label, "%s", cur_book);
            start_test();
        }
        else if (id >= H_TICK && id < H_GO_W) {
            int i = id >= H_TICK_R ? id - H_TICK_R : id - H_TICK;
            tick_mode = id >= H_TICK_R ? MODE_READ : MODE_WRITE;
            if (i < nlists) {
                db_deck_set(lists[i].book, lists[i].chapter, tick_mode,
                            !lists[i].enabled[tick_mode]);
                nlists = db_lists(cur_book, lists, MAXLISTS);
            }
        }
        else if (id >= H_GO_W && id < H_KEY) {
            int i = id >= H_GO_R ? id - H_GO_R : id - H_GO_W;
            if (i < nlists) {
                mode = id >= H_GO_R ? MODE_READ : MODE_WRITE;
                list_id = lists[i].id;
                scope_book[0] = 0;
                snprintf(list_label, sizeof list_label, "%s", lists[i].chapter);
                start_test();
            }
        }
        else if (id >= H_ITEM && id < H_TICK) {
            int i = id - H_ITEM;
            if (i < nlists) {                 /* the name opens its words */
                words_list = lists[i].id;
                snprintf(words_label, sizeof words_label, "%s", lists[i].chapter);
                list_id = lists[i].id;
                scope_book[0] = 0;
                snprintf(list_label, sizeof list_label, "%s", lists[i].chapter);
                scroll = 0;
                load_words();
                scr = SCR_WORDS;
            }
        }
        break;

    case SCR_REVIEW:
        /* Quitting a test is safe and resumable -- nothing to save here,
         * because each answer was written as it happened. */
        if (id == H_BACK) leave_test();
        else if (id == H_CHECK)    { revealed = 1; assess_writing(); }
        else if (id == H_POSTPONE) postpone();
        else if (id == H_CLEAR)    { ui_ink_clear(&ink); hw_checked = 0;
                                     guided_reset(); }
        else if (id >= H_STROKEVIEW && id < H_STROKEVIEW + 8 && have_card) {
            char ch[8];
            if (u8_at(card.headword, id - H_STROKEVIEW, ch)) {
                sv_back = SCR_REVIEW;
                open_strokeview(ch);
            }
        }
        else if (id == H_AGAIN)    grade(1);
        else if (id == H_HARD)     grade(2);
        else if (id == H_TREVEAL)  card_outline = !card_outline;
        else if (id == H_GOOD)     grade(3);
        else if (id == H_PRACTICE && have_card) open_practice(card.headword);
        break;

    case SCR_MENU:
        if (id == H_BACK) { scr = SCR_HOME; scroll = 0;
                            reload_books(); }
        else if (id == H_LEARNED) { scroll = 0; load_learned(); scr = SCR_LEARNED; }
        else if (id == H_DICT)    { scr = SCR_DICT; q[0] = 0; nres = 0; }
        else if (id == H_KOBO) {
            int nw = 0, nh = 0;
            const char *kp = getenv("HANZI_KOBO_DB");
            long_task("importing Kobo lookups");
            int n = db_import_kobo(kp ? kp : "/mnt/onboard/.kobo/KoboReader.sqlite",
                                   &nw, &nh);
            if (n < 0) snprintf(toast, sizeof toast,
                                "no KoboReader.sqlite here (that path only exists on the device)");
            else snprintf(toast, sizeof toast,
                          "imported %d lookups, %d highlights", nw, nh);
            reload_books();
        }
        else if (id == H_MYLISTS) { scroll = 0; mylists_back = SCR_MENU; scr = SCR_MYLISTS; }
        else if (id == H_CATORDER) scr = SCR_CATORDER;
        else if (id == H_PDFS)    { pdfs_back = SCR_MENU; pdfs_open(); }
        else if (id == H_PLECO) {
            int nf = 0, nl = 0;
            long_task("importing Pleco decks");
            int n = db_import_pleco_dir(pleco_dir, &nf, &nl);
            if (n < 0) snprintf(toast, sizeof toast, "cannot read %s", pleco_dir);
            else if (nf == 0) snprintf(toast, sizeof toast,
                                       "no .txt files in %s", pleco_dir);
            else snprintf(toast, sizeof toast,
                          "imported %d words in %d lists from %d file%s",
                          n, nl, nf, nf == 1 ? "" : "s");
            reload_books();
        }
        break;

    case SCR_DONE:
        if (id == H_AGAIN_TEST) start_test();
        else if (id == H_AHEAD)   start_session(0, 0);
        else if (id == H_MORENEW) start_session(1, 1);
        else if (id == H_BACK) {
            db_session_clear();
            leave_test();
        }
        break;

    case SCR_CATORDER:
        cat_order_load();
        if (id == H_BACK) scr = SCR_MENU;
        else if (id >= H_GO_W && id < H_GO_W + NCATEGORY) cat_order_move(id - H_GO_W, id - H_GO_W - 1);
        else if (id >= H_GO_R && id < H_GO_R + NCATEGORY) cat_order_move(id - H_GO_R, id - H_GO_R + 1);
        break;

    case SCR_MYLISTS:
        if (id == H_BACK && mylists_back == SCR_HOME) {
            /* to the page of home it was opened from; a list may have
             * gained its first word meanwhile, and the row is the book's */
            scr = SCR_HOME; scroll = top_scroll;
            reload_books(); build_hrows();
            if (scroll >= nhrows) scroll = 0;
        }
        else if (id == H_BACK) { scr = SCR_MENU; scroll = 0; }
        else if (id == H_MORE) { scroll += page_step; if (scroll >= nulists) scroll = nulists - 1; }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0; }
        else if (id == H_EXPORT) {
            int nf = 0;
            long_task("exporting lists");
            int n = db_export_pleco(export_dir, &nf);
            if (n < 0) snprintf(toast, sizeof toast, "cannot write to %s", export_dir);
            else snprintf(toast, sizeof toast, "wrote %d words to %d file%s in %s",
                          n, nf, nf == 1 ? "" : "s", export_dir);
        }
        else if (id >= H_TICK && id < H_GO_W) {
            int i = id >= H_TICK_R ? id - H_TICK_R : id - H_TICK;
            tick_mode = id >= H_TICK_R ? MODE_READ : MODE_WRITE;
            if (i < nulists) db_deck_set("My lists", ulists[i].chapter, tick_mode,
                                         !ulists[i].enabled[tick_mode]);
        }
        else if (id >= H_ITEM && id < H_TICK) {
            int i = id - H_ITEM;
            if (i < nulists) {
                cur_ulist = ulists[i].id;
                snprintf(cur_uname, sizeof cur_uname, "%s", ulists[i].chapter);
                scroll = 0;
                load_uents();
                scr = SCR_LISTVIEW;
            }
        }
        break;

    case SCR_LISTVIEW:
        if (id != H_DELETE) delete_armed = 0;     /* anything else disarms it */
        if (id == H_BACK) { scr = SCR_MYLISTS; scroll = 0; toast[0] = 0; }
        else if (id == H_MORE) { scroll += page_step; load_uents(); }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0;
                                 load_uents(); }
        else if (id == H_RENAME) {
            naming_rename = 1;
            snprintf(newname, sizeof newname, "%s", cur_uname);
            scr = SCR_NEWLIST;
        }
        else if (id == H_DELETE && !delete_armed) {
            /* A whole list, one tap from "rename": asked twice, as reset is. */
            delete_armed = 1;
            snprintf(toast, sizeof toast, "press delete again to delete %s and its words",
                     cur_uname);
        }
        else if (id == H_DELETE) {
            delete_armed = 0;
            db_user_list_delete(cur_ulist);
            snprintf(toast, sizeof toast, "deleted %s", cur_uname);
            reload_books();
            scroll = 0;
            scr = SCR_MYLISTS;
        }
        else if (id >= H_ITEM && id < H_TICK) {
            int i = id - H_ITEM;
            if (i < nuents) {
                snprintf(toast, sizeof toast, "%s %s",
                         db_user_entry_delete(cur_ulist, uents[i].simp) == 0 ? "removed"
                                                                            : "could not remove",
                         uents[i].simp);
                load_uents();
            }
        }
        break;

    case SCR_SETTINGS: {
        int w = db_new_limit(MODE_WRITE), r = db_new_limit(MODE_READ);
        if (id == H_BACK)        settings_close();
        else if (id == H_WMINUS) db_set_new_limit(MODE_WRITE, w - (w > 10 ? 5 : 1));
        else if (id == H_WPLUS)  db_set_new_limit(MODE_WRITE, w + (w >= 10 ? 5 : 1));
        else if (id == H_RMINUS) db_set_new_limit(MODE_READ,  r - (r > 10 ? 5 : 1));
        else if (id == H_RPLUS)  db_set_new_limit(MODE_READ,  r + (r >= 10 ? 5 : 1));
        else if (id == H_PUNCT) { show_punct = !show_punct;
                                  db_set_show_punct(show_punct); }
        else if (id == H_READINGS) { text_readings = !text_readings;
                                     db_set_text_readings(text_readings); }
        else if (id >= H_CSIZE0 && id <= H_CSIZE2) { comic_size = id - H_CSIZE0; db_set_comic_size(comic_size); }
        else if (id == H_CSPLIT) { comic_split = !comic_split; db_set_comic_split(comic_split); }
        else if (id == H_TXMINUS || id == H_TXPLUS) {
            text_scale += (id == H_TXPLUS) ? 15 : -15;
            if (text_scale < TEXT_SCALE_MIN) text_scale = TEXT_SCALE_MIN;
            if (text_scale > TEXT_SCALE_MAX) text_scale = TEXT_SCALE_MAX;
            db_set_text_scale(text_scale);
        }
        else if (id == H_LMINUS || id == H_LPLUS) {
            int lv = surf_light_get();
            if (lv >= 0) {
                int step = lv >= 20 ? 10 : 2;
                lv += (id == H_LPLUS) ? step : -step;
                if (lv < 0)   lv = 0;
                if (lv > 100) lv = 100;
                surf_light_set(lv);
                db_set_light(lv);
            }
        }
        else if (id == H_RESET) { reset_armed = -2; reset_scroll = 0; toast[0] = 0; scr = SCR_RESET; }
        else if (id >= H_WIN0 && id <= H_WIN3) {
            int m = id - H_WIN0;
            int rc2 = surf_set_window_mode(m);
            if (rc2 >= 0) db_set_window_mode(m);
            if (rc2 == 1) snprintf(toast, sizeof toast,
                                   "sized, not placed: this display does not let a window be moved");
        }
        break;
    }

    case SCR_WORDS:
        if (id == H_BACK) { scr = SCR_LISTS; scroll = 0; }
        else if (id == H_MORE) { scroll += page_step; load_words(); }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0;
                                 load_words(); }
        else if (id == H_STATS) scr = SCR_STATS;
        else if (id == H_STUDY) study_open(scroll);
        else if (id >= H_ITEM && id < H_ITEM + MAXWORDS)
            study_open(scroll + (id - H_ITEM));   /* the word you pressed */
        else if (id == H_GO_W || id == H_GO_R) {
            mode = (id == H_GO_R) ? MODE_READ : MODE_WRITE;
            start_test();
        }
        break;

    case SCR_STATS:
        /* the list again, from the database: settings (a reset) can have
         * been opened from here */
        if (id == H_BACK) { scr = SCR_WORDS; load_words(); }
        break;

#ifdef HANZI_DESKTOP
    case SCR_EXPORT:
        if (id == H_BACK) scr = SCR_TEXTCH;
        else if (id == H_EX_EPUB || id == H_EX_PDF) { ex_fmt = id == H_EX_PDF; toast[0] = 0; }
        else if (id >= H_EX_M0 && id <= H_EX_M2) { ex_mode = id - H_EX_M0; toast[0] = 0; }
        else if (id == H_EX_BOOK || id == H_EX_CHAP) { ex_scope = id == H_EX_CHAP; toast[0] = 0; }
        else if (id >= H_ENSRC && id < H_ENSRC + MAXNSRC) { if (id - H_ENSRC < n_esrc) ex_src = id - H_ENSRC; toast[0] = 0; }
        else if (id == H_EX_GO) export_go();
        break;
#endif

    case SCR_TEXTCH:
#ifdef HANZI_DESKTOP
        if (id == H_EXPORTBOOK) { export_open(); break; }
#endif
        if (id == H_BACK) { scr = cur_source() ? SCR_BOOK : SCR_HOME; scroll = 0; }
        else if (id == H_MORE) { scroll += page_step; if (scroll >= ntchaps) scroll = ntchaps - 1; }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0; }
        else if (id >= H_ITEM && id < H_ITEM + MAXTCHAPS) {
            tchap_open(id - H_ITEM);
            tread_seek(db_text_read(tchap_id));
            mark_page_read();
            scr = SCR_TEXTREAD;
        }
        else if (id == H_TWRITE) {
            if (!tchap_id) {
                int last = db_text_last_chapter(text_id), at = scroll;
                for (int k = 0; k < ntchaps; k++)
                    if (tchaps[k].id == last) { at = k; break; }
                tchap_open(at);
            }
            tw_load(db_text_pos(tchap_id));
            scr = SCR_TEXTLINE;
        }
        break;

    case SCR_TEXTREAD:
        if (ntplates && id == H_BACK && comic_full) { comic_full = 0; break; }
        if (ntplates && id != H_BACK && !(id >= H_TLINE && id < H_TLINE + MAXTLINES)) {
            /* a 连环画: a plate, and its caption's page, at a time */
            if (id == H_TPIC) { comic_full = !comic_full; break; }
            if (id == H_TPDF) { comic_pdf_open(); break; }
            if (id >= H_MYNOTE && id < H_MYNOTE + MAXTLINES) { mynote_open(id - H_MYNOTE, SCR_TEXTREAD); break; }
            /* an apart comic's text page has the chapter page's toggles */
            if (id >= H_BNOTE && id < H_BNOTE + MAXTLINES) { bnote_open_line(id - H_BNOTE); break; }
            if (id == H_BNOTESHOW) {
                show_booknotes = !show_booknotes;
                db_set_show_book_notes(show_booknotes);
                break;
            }
            if (id == H_TSENT) { tread_pick = !tread_pick; break; }
            if (id == H_MYNOTESHOW) {         /* the signs only: the layout is the same */
                show_mynotes = !show_mynotes;
                db_set_show_my_notes(show_mynotes);
                break;
            }
            comic_full = 0;
            if (id == H_TNEXT) {
                if (csub + 1 < cpages) csub++;
                else if (cplate + 1 < ntplates) { cplate++; csub = 0; }
                else if (tchap_idx + 1 < ntchaps) tchap_open(tchap_idx + 1);
                mark_page_read();
            } else if (id == H_TPREV) {
                if (csub > 0) csub--;
                else if (cplate > 0) { cplate--; csub = -1; }
                else if (tchap_idx > 0) {
                    tchap_open(tchap_idx - 1);
                    cplate = ntplates - 1; csub = -1;
                }
                mark_page_read();
            } else if (id == H_TPY) {
                /* Reading the caption is why pinyin was pressed: on a caption
                 * page, stay on the line at its top, wherever the new layout
                 * puts it. On the picture's page, stay on the picture (pinyin made the caption too tall to stay
                 * under it, and the picture went); with split, the lines that
                 * still fit stay under it. */
                comic_layout();
                int a, b;
                comic_page_lines(&a, &b);
                int on_pic = csub == 0 && !comic_apart;
                if (b > a && !on_pic) cseek_line = tplates[cplate].line_first + a;
                tread_py = !tread_py;
                if (b <= a || on_pic) csub = 0;
            }
            break;
        }
        if (id == H_BACK) { scr = SCR_TEXTCH; tread_pick = 0; }
        else if (id == H_TNEXT) {
            const int MAXP = (int)(sizeof tread_start / sizeof *tread_start);
            int first = tread_start[tread_np];
            int step  = tread_shown > 0 ? tread_shown : 1;   /* never stall */
            if (first + step < ntlines) {
                if (tread_np + 1 < MAXP) {
                    tread_start[++tread_np] = first + step;
                } else {
                    /* The stack of page starts is full. Drop the oldest and
                     * keep going: losing how to page back to the top of the
                     * chapter is a nuisance, skipping the rest of it is the
                     * bug this screen has already had twice. */
                    memmove(tread_start, tread_start + 1,
                            sizeof tread_start - sizeof *tread_start);
                    tread_start[MAXP - 1] = first + step;
                }
            } else if (tchap_idx + 1 < ntchaps) {
                /* On into the next chapter at its first page, as a page
                 * turned; opening it at its saved place put a chapter read
                 * once to its end on its last page. tchap_open starts it
                 * at the top. */
                tchap_open(tchap_idx + 1);
            }
            mark_page_read();
        }
        else if (id == H_TPREV) {
            if (tread_np > 0) tread_np--;
            else if (tchap_idx > 0) {
                /* back into the chapter before at its last page, whatever
                 * was saved there: a chapter never read opened on line 0 */
                tchap_open(tchap_idx - 1);
                tread_seek(ntlines - 1);
            }
            mark_page_read();
        }
        else if (id >= H_MYNOTE && id < H_MYNOTE + MAXTLINES) {
            mynote_open(id - H_MYNOTE, SCR_TEXTREAD);
        }
        else if (id >= H_BNOTE && id < H_BNOTE + MAXTLINES) bnote_open_line(id - H_BNOTE);
        else if (id == H_TCHAPEN && chapter_has_translation()) {
            /* the English's page of the chapter, about where the page's
             * first line is; back is this page, as from a margin mark */
            notes_from_read = 1; en_from_read = 1;
            notes_from_chap = tchap_id;
            tline_to_notes = 0;
            tw_load(tread_start[tread_np]);
            en_page_open(1, 0);
        }
        /* None moves a line: the page is laid out with the syllables'
         * room and the marks' margins whether they are shown or not, so the
         * stack of page starts stays good (tread_walk). */
        else if (id == H_TPY) tread_py = !tread_py;
        else if (id == H_MYNOTESHOW) { show_mynotes = !show_mynotes; db_set_show_my_notes(show_mynotes); }
        else if (id == H_BNOTESHOW) { show_booknotes = !show_booknotes; db_set_show_book_notes(show_booknotes); }
        else if (id == H_TSENT) tread_pick = !tread_pick;
        else if (id >= H_TLINE && id < H_TLINE + MAXTLINES) {
            tread_pick = 0;              /* one line, then words again */
            tline_to_notes = 0;
            tw_load(id - H_TLINE);
            mark_reading(tw_line);
            scr = SCR_TEXTLINE;
        }
        break;

    case SCR_TEXTLINE:
        /* opened from the notes page's sentence: back there, and its notes
         * button is that page too */
        if (tline_to_notes && (id == H_BACK || id == H_TNOTES)) { notes_return(); break; }
        if (id == H_BACK) {
            if (ntplates) comic_seek_line(tw_line);
            else tread_seek(tw_line);
            scr = SCR_TEXTREAD;
        }
        else if (id == H_TPY)   tread_py = !tread_py;    /* the same rows either way */
        /* A sentence too long for one page is finished before moving on,
         * the same way a phrase is finished before the next sentence. */
        else if (id == H_TNEXT) {
            if (tline_page + 1 < tline_pages) tline_page++;
            else if (tw_line + 1 < ntlines) { tw_load(tw_line + 1); tline_page = 0; }
            else if (tchap_idx + 1 < ntchaps) {
                /* The chapter is read. Carrying on is what you meant by
                 * pressing next at the end of it. */
                tchap_open(tchap_idx + 1);
                tw_load(0);             /* its first line, not its saved place */
                tline_page = 0;
            }
            mark_reading(tw_line);
        }
        else if (id == H_TPREV) {
            if (tline_page > 0) tline_page--;
            else if (tw_line > 0) { tw_load(tw_line - 1); tline_page = 0; }
            else if (tchap_idx > 0) {
                tchap_open(tchap_idx - 1);
                tw_load(ntlines - 1);
                tline_page = 0;
            }
            mark_reading(tw_line);
        }
        else if (id == H_TWRITE) { db_set_text_pos(tchap_id, tw_line); scr = SCR_TEXTWRITE; }
        else if (id == H_TNOTES) { notes_from_read = en_from_read = 0; notes_open(); scr = SCR_TEXTNOTES; }
        /* the chapter's translation, about where this line is: "English"
         * where that is all the source has, or the toggle over the English */
        else if (id == H_TCHAPEN) en_page_open(1, 0);
        else if (id == H_TCHAP)   en_page_open(1, 1);
        else if (id == H_ENMORE)  en_page_open(0, 0);     /* all of this line's */
        else if (id >= H_ENSRC && id < H_ENSRC + n_esrc) en_source_pick(id - H_ENSRC);
        /* the English takes rows from the sentence: the page holding the
         * character at the top of this one */
        else if (id == H_TEN)    { tread_en = !tread_en; tline_seek = tline_top; }
        else if (id == H_MYNOTES) mynote_open(tw_line, SCR_TEXTLINE);
        break;

    case SCR_MYNOTE:
        if (id == H_BACK || id == H_CHECK) { scr = mynote_back; mynote_has_for = -1; }
        else if (id == H_UNDO && lnote.n > 0) {
            lnote.n--;
            lnote.npt = lnote.start[lnote.n];
            mynote_full = 0;
            lnote_save();
        }
        else if (id == H_CLEAR && lnote.n > 0) {
            if (!mynote_clear_armed) { mynote_clear_armed = 1; break; }
            lnote.n = lnote.npt = 0;
            mynote_full = 0;
            lnote_save();
        }
        if (id != H_CLEAR) mynote_clear_armed = 0;
        else if (lnote.n == 0) mynote_clear_armed = 0;
        break;

    case SCR_TEXTNOTES:
        if (!tnote_en && id >= H_NOTESRC && id < H_NOTESRC + n_nsrc) { note_source_pick(id - H_NOTESRC); break; }
        if (tnote_en && id >= H_ENSRC && id < H_ENSRC + n_esrc) { en_source_pick(id - H_ENSRC); break; }
        if (id == H_TCHAP) { notes_chapter_toggle(); break; }
        if (tnote_picking) {
            if (id == H_TPGPREV || id == H_TPGNEXT) {
                /* a whole block on or back, blocks starting at multiples of
                 * its size -- stepping by the cells shown drifted from the
                 * short last block */
                int per = tnote_grid_per > 0 ? tnote_grid_per : 100;
                int blk = tnote_grid_first / per + (id == H_TPGNEXT ? 1 : -1);
                int maxblk = (tnote_pages - 1) / per;
                if (blk < 0) blk = 0;
                if (blk > maxblk) blk = maxblk;
                tnote_grid_first = blk * per;
                break;
            }
            if (id >= H_ITEM && id < H_ITEM + tnote_pages) {
                tnote_page = id - H_ITEM;
                tnote_about = 0;
            }
            tnote_picking = 0;         /* a page, or back: the grid goes */
            break;
        }
        if (id == H_TPAGES) { tnote_picking = 1; tnote_grid_first = -1; break; }
        tnote_about = 0;
        if (id == H_NOTESLINE) {
            /* the sentence over the notes: open as a sentence, back here */
            notes_keep_place();
            tline_to_notes = 1;
            tline_notes_chap = tchap_id; tline_notes_line = tw_line;
            tline_page = 0; tline_seek = -1;
            scr = SCR_TEXTLINE;
        }
        else if (id == H_BACK && notes_from_read && (!tnote_en || en_from_read)) {
            /* opened from the chapter page's margin, or its English: back
             * to that page -- the same one, unless next / prev went past it */
            notes_from_read = 0; en_from_read = 0;
            tnote_chap = tnote_chap_want = 0; tnote_en = tnote_en_from_line = 0;
            /* the page as laid out now: the size or the marks may have
             * changed while the notes were open, and tread_shown is the
             * last page drawn's, in the old layout */
            if (ntplates) comic_seek_line(tw_line);   /* an apart comic's text page */
            else if (tchap_id != notes_from_chap) tread_seek(tw_line);
            else {
                tread_relayout();
                int first = tread_start[tread_np], n = tread_walk(first, 0);
                if (tw_line < first || tw_line >= first + (n > 0 ? n : 1)) tread_seek(tw_line);
            }
            scr = SCR_TEXTREAD;
        }
        else if (id == H_BACK) {
            tline_page = 0; scr = SCR_TEXTLINE;
            tnote_chap = tnote_chap_want = 0; tnote_en = tnote_en_from_line = 0;
        }
        else if (tnote_chap && id == H_TNEXT) {
            if (tnote_page + 1 < tnote_pages) tnote_page++;
        }
        else if (tnote_chap && id == H_TPREV) {
            if (tnote_page > 0) tnote_page--;
        }
        else if (id == H_TNEXT) {
            if (tnote_page + 1 < tnote_pages) tnote_page++;
            else {
                /* The next sentence's notes, across a chapter's end too;
                 * sentences with none are passed over (notes_step). */
                int c, l, found = notes_step(1, &c, &l);
                if (found) {
                    if (c != tchap_idx) tchap_open(c);
                    tw_load(l);
                    tnotes_load(); tnote_page = 0; mark_reading(tw_line);
                }
            }
        }
        else if (id == H_TPREV) {
            if (tnote_page > 0) tnote_page--;
            else {
                int c, l, found = notes_step(-1, &c, &l);     /* as for next */
                if (found) {
                    if (c != tchap_idx) tchap_open(c);
                    tw_load(l);
                    tnotes_load(); tnote_page = 1 << 20; mark_reading(tw_line);
                }
            }
        }
        break;

    case SCR_TEXTWRITE:
        if (id == H_BACK) {
            tw_settle();
            scr = SCR_TEXTLINE;
        }
        else if (id == H_TDONE) {
            /* Done is not "back": it shows the sentence you wrote, and asks
             * what next. What is in the box is judged first. */
            tw_settle();
            scr = SCR_TEXTDONE;
        }
        else if (id == H_CLEAR) {
            ui_ink_clear(&ink);
            tw_end_shown = 0;
            /* The verdict belonged to what was in the box. */
            if (tw_i < MAXTCH) { tw_rank[tw_i] = TW_UNCHECKED;
                                 tw_guess[tw_i][0] = 0; tw_ink[tw_i].n = 0;
                                 tw_magic[tw_i] = 0; }
            guided_reset();
        }
        else if (id == H_TREVEAL) { tw_reveal = (tw_reveal + 1) % 3; tw_auto_help = 0; }
        else if (id == H_VERDICTCH && tw_said >= 0) {
            char c3[8];
            tw_char_at(tw_said, c3);
            if (c3[0]) {
                /* where it stands in the line, for its reading there */
                char sy[40] = "";
                int n = u8_len(tlines[tw_line]), g = 0;
                for (int k = 0; k < n; k++) {
                    char c1[8];
                    u8_at(tlines[tw_line], k, c1);
                    if (!is_han(c1)) continue;
                    if (g++ == tw_said) {
                        if (!reading_at(tw_line, k, c1, sy, sizeof sy)) sy[0] = 0;
                        break;
                    }
                }
                char_back = SCR_TEXTWRITE;
                open_char_read(c3, sy);
            }
        }
        else if (id >= H_TWCELL && id < H_TWCELL + MAXTCH) {
            /* a box of the strip: its character, read as the line has it */
            int k = id - H_TWCELL;
            if (tw_line < ntlines && k < tw_npos) {
                char c4[8], sy[40] = "";
                u8_at(tlines[tw_line], tw_pos[k], c4);
                if (!reading_at(tw_line, tw_pos[k], c4, sy, sizeof sy)) sy[0] = 0;
                char_back = SCR_TEXTWRITE;
                open_char_read(c4, sy);
            }
        }
        else if (id == H_STROKEVIEW) {
            char c2[8];
            if (tw_line < ntlines && tw_i < tw_npos) {
                u8_at(tlines[tw_line], tw_pos[tw_i], c2);
                sv_back = SCR_TEXTWRITE;
                open_strokeview(c2);
            }
        }
        else if (id == H_TNEXT) {
            /* One press: judge what is in the box, keep it, move on -- to
             * the next character, the next phrase, and past the last one to
             * the sentence you wrote.
             *
             * Unless it came out wrong. Then it stays: the outline and the
             * stroke order come up, the box is emptied to write it again,
             * and the attempt stays in the strip above. Pressing next again
             * judges the rewrite, or with the box left empty moves on --
             * never a trap. */
            int wrote = tw_i < tw_npos && ink.n > 0 && !tw_magic[tw_i];
            tw_settle();
            if (wrote && tw_rank[tw_i] == TW_WRONG) {
                if (!tw_auto_help) { tw_help_was = tw_reveal; tw_auto_help = 1; }
                tw_reveal = 2;
                ui_ink_clear(&ink);
                guided_reset();
                break;
            }
            if (tw_auto_help) { tw_reveal = tw_help_was; tw_auto_help = 0; }
            if (tw_i + 1 < tw_npos) {
                tw_i++;
                ui_ink_clear(&ink);
                guided_reset();
            } else if (tw_seg + 1 < tw_nseg) {
                tw_seg++;
                tw_seg_load();
            } else if (tw_said == tw_base + tw_i
                       && (tw_magic[tw_i] || tw_rank[tw_i] >= 0)
                       && !(tw_end_shown && ink.n == tw_end_n)) {
                tw_end_shown = 1;            /* its verdict first; next again goes on */
                tw_end_n = ink.n;
            } else {
                scr = SCR_TEXTDONE;
            }
        }
        else if (id == H_TPREV) {
            /* Back a character, across phrases too. What was written is
             * kept; the box starts empty rather than restoring strokes at
             * the wrong resolution. */
            tw_settle();
            ui_ink_clear(&ink);
            guided_reset();
            tw_end_shown = 0;
            if (tw_auto_help) { tw_reveal = tw_help_was; tw_auto_help = 0; }
            if (tw_i > 0) tw_i--;
            else if (tw_seg > 0) { tw_seg--; tw_seg_load(); tw_i = tw_npos > 0 ? tw_npos - 1 : 0; }
        }
        break;

    case SCR_TEXTDONE:
        if (id == H_BACK) scr = SCR_TEXTLINE;
        else if (id == H_TAGAIN) {
            tw_load(tw_line);
            scr = SCR_TEXTWRITE;
        }
        else if (id == H_TNEXTLINE) {
            /* Writing on moves the writing place only: the reading place is
             * where you were reading, and writing one sentence used to pull
             * it back to here (and to 0 in the next chapter). */
            db_set_text_pos(tchap_id, tw_line);
            if (tw_line + 1 < ntlines) {
                tw_begin(tw_line + 1);
                scr = SCR_TEXTWRITE;
            } else if (tchap_idx + 1 < ntchaps) {
                tchap_open(tchap_idx + 1);
                tw_begin(0);
                scr = SCR_TEXTWRITE;
            } else {
                scr = SCR_TEXTLINE;
            }
        }
        break;

    case SCR_STROKES:
        if (id == H_BACK) scr = sv_back;
        else if (id == H_PRACTICE) open_practice(sv_ch);
        break;

    case SCR_STUDY:
        /* reloaded: a reset from settings, opened here or from a character
         * or practice over this, leaves words[] stale */
        if (id == H_BACK) { scr = SCR_WORDS; load_words(); }
        else if (id == H_STUDY_NEXT) study_open(study_abs + 1);
        else if (id == H_STUDY_PREV) study_open(study_abs - 1);
        else if (id == H_PRACTICE) {
            if (study_abs >= scroll && study_abs < scroll + nwords)
                open_practice(words[study_abs - scroll].headword);
        }
        break;

    case SCR_RESET:
        if (id == H_BACK) { reset_armed = -2; scr = SCR_SETTINGS; }
        else if (id == H_MORE) { reset_armed = -2; reset_scroll += reset_step;
                                 if (reset_scroll >= nbooks) reset_scroll = nbooks - 1; }
        else if (id == H_PREV) { reset_armed = -2; reset_scroll -= reset_step;
                                 if (reset_scroll < 0) reset_scroll = 0; }
        else if (id == H_RESET_ALL) {
            if (reset_armed == -1) {
                long_task("resetting all progress");
                int n = db_reset_progress(NULL, -1);
                snprintf(toast, sizeof toast, "forgot %d card%s", n, n == 1 ? "" : "s");
                reset_armed = -2;
                in_test = 0;
                set_from.reset = 1;
                reload_books();
            } else reset_armed = -1;
        }
        else if (id >= H_ITEM && id < H_TICK) {
            int i = id - H_ITEM;
            if (i < nbooks) {
                if (reset_armed == i) {
                    long_task("resetting a book");
                    int n = db_reset_progress(books[i].name, -1);
                    snprintf(toast, sizeof toast, "forgot %d card%s in %s",
                             n, n == 1 ? "" : "s", books[i].name);
                    reset_armed = -2;
                    in_test = 0;
                    set_from.reset = 1;
                    reload_books();
                } else reset_armed = i;
            }
        }
        break;

    case SCR_LEARNED:
        /* scroll is shared by five screens; leaving it set here left the home
         * screen scrolled past its last row and therefore blank */
        if (id == H_BACK) { scr = SCR_MENU; scroll = 0; }
        else if (id == H_FILTER) {
            rev_filter = rev_filter == -1 ? MODE_WRITE
                       : rev_filter == MODE_WRITE ? MODE_READ : -1;
            scroll = 0;
            load_learned();
        }
        else if (id == H_MORE) { scroll += page_step; load_learned(); }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0;
                                 load_learned(); }
        break;

    case SCR_PDFS:
        if (id == H_NONE)            pdf_kb = 0;      /* put the keyboard away */
        else if (id == H_BACK && pdfs_back == SCR_HOME) {
            /* to the page of home it was opened from */
            pdf_kb = 0; scr = SCR_HOME;
            scroll = top_scroll;
        }
        else if (id == H_BACK)       { pdf_kb = 0; scr = SCR_MENU; }
        else if (id == H_PDFFIELD)   pdf_kb = !pdf_kb;
        else if (id == H_KEY)        { kbd_edit(pdf_q, sizeof pdf_q, x, y); pdf_list_build(); scroll = 0; }
        else if (id == H_PDFDRAW)    { draw_back = SCR_PDFS; ui_ink_clear(&hw_ink); hw_n = 0;
                                       hw_built[0] = 0; pdf_kb = 0; scr = SCR_DRAW; }
        else if (id == H_PDFRESCAN) {
            pdf_rescan();
            pdf_list_build();
            scroll = 0;
            snprintf(toast, sizeof toast, "found %zu PDF%s", pdf_found.size(),
                     pdf_found.size() == 1 ? "" : "s");
        }
        else if (id == H_MORE) { scroll += page_step;
                                 if (scroll >= (int)pdf_rows.size()) scroll = (int)pdf_rows.size() - 1;
                                 if (scroll < 0) scroll = 0; }
        else if (id == H_PREV) { scroll -= page_step; if (scroll < 0) scroll = 0; }
        else if (id >= H_ITEM && id < H_ITEM + PDF_MAXROWS) {
            size_t i = (size_t)(scroll + id - H_ITEM);
            if (i < pdf_rows.size()) { pdf_kb = 0; pdfv_open(pdf_rows[i].path, pdf_rows[i].name); }
        }
        else if (id >= H_PDFPIN && id < H_PDFPIN + PDF_MAXROWS) {
            size_t i = (size_t)(scroll + id - H_PDFPIN);
            if (i < pdf_rows.size()) {
                pdfrow_t r = pdf_rows[i];
                db_pdf_pin(r.path.c_str(), !r.pinned, (long)time(NULL));
                snprintf(toast, sizeof toast, "%s %s", r.pinned ? "unpinned" : "pinned", r.name.c_str());
                pdf_list_build();
                if (scroll >= (int)pdf_rows.size()) scroll = 0;
            }
        }
        break;

    case SCR_PDFVIEW: {
        std::vector<int> view = pdfv_view_key();
        if (pdfv_panel_tap(id))      break;             /* what was read, or drawn */
        if (id == H_NONE)            pv.choosing = 0;   /* the four quarters put away */
        else if (id == H_BACK)       { pdfv_close(); break; }
        else if (id == H_MORE)       pdfv_step(1);
        else if (id == H_PREV)       pdfv_step(-1);
        else if (id == H_PV_FIT)     { pv.zoom = PZ_FIT; pv.part = 0; pv.choosing = 0; }
        else if (id == H_PV_QUAD)    pv.choosing = !pv.choosing;
        else if (id >= H_PV_QPICK && id < H_PV_QPICK + 4) {
            pv.zoom = PZ_QUAD; pv.part = id - H_PV_QPICK; pv.choosing = 0;
        }
        else if (id == H_PV_HALF)    { pv.zoom = pv.zoom == PZ_HALF && !pv.choosing ? PZ_FIT : PZ_HALF;
                                       pv.part = 0; pv.choosing = 0; }
        else if (id == H_PV_FLIP)    { pv.flip = !pv.flip; pv.part = 0; }
        else if (id == H_PV_TRIMIN)  { if (pv.trim < PV_TRIM_MAX) pv.trim++; }
        else if (id == H_PV_TRIMOUT) { if (pv.trim > 0) pv.trim--; }
        else if (id == H_PV_ROT)     { pv.rot = (pv.rot + 90) % 360; pv.part = 0; }
        else if (id == H_PV_OCR)     pv.tapmode = pv.tapmode == PT_OCR ? PT_NONE : PT_OCR;
        else if (id == H_PV_DRAWON)  pv.tapmode = pv.tapmode == PT_DRAW ? PT_NONE : PT_DRAW;
        else if (id == H_PV_PAGE)    pdfv_page_tap(x, y);
        pdfv_settle();                /* the part kept in range, and a pin's spot */
        if (pdfv_view_key() != view) pdfv_marks_drop();
        break;
    }

    case SCR_DICT:
        if (id == H_BACK) scr = SCR_HOME;
        else if (id == H_DRAW) { draw_back = SCR_DICT;
                                 ui_ink_clear(&hw_ink); hw_n = 0; hw_built[0] = 0;
                                 scr = SCR_DRAW; }
        else if (id == H_KEY) { kbd_edit(q, sizeof q, x, y); do_search(); }
        else if (id >= H_ITEM && id < H_TICK) {
            int i = id - H_ITEM;
            if (i < nres) { char_back = SCR_DICT; open_char_read(res[i].simp, res[i].pinyin); }
        }
        break;

    case SCR_DRAW:
        if (id == H_BACK)       scr = draw_back;
        else if (id == H_CLEAR) { ui_ink_clear(&hw_ink); hw_n = 0; hw_built[0] = 0; }
        else if (id == H_UNDO)  { if (hw_ink.n > 0) hw_ink.n--; hw_recognise(); }
        else if (id == H_CHECK) {
            if (hw_built[0] && draw_back == SCR_PDFS) {
                /* hanzi for the PDF search, added to what is typed there */
                size_t l = strlen(pdf_q);
                if (l + strlen(hw_built) < sizeof pdf_q) {
                    snprintf(pdf_q + l, sizeof pdf_q - l, "%s", hw_built);
                    pdf_list_build();
                    scroll = 0;
                }
                hw_built[0] = 0;
                scr = SCR_PDFS;
            } else if (hw_built[0]) {       /* search what has been built */
                snprintf(q, sizeof q, "%s", hw_built);
                do_search();
                scr = SCR_DICT;
            } else {
                scr = draw_back;            /* nothing drawn: just the way out */
            }
        }
        else if (id >= H_CAND && id < H_CAND + HW_CANDS) {
            int i = id - H_CAND;
            if (i < hw_n) {
                /* Append, so a word is drawn one character at a time. */
                size_t l = strlen(hw_built);
                snprintf(hw_built + l, sizeof hw_built - l, "%s", hw_cand[i].ch);
                ui_ink_clear(&hw_ink);
                hw_n = 0;                   /* the search runs once, on search */
            }
        }
        break;

    case SCR_CHAR:
        if (id >= H_CHARCELL && id < H_CHARCELL + 8)   /* a word's character: as its large one */
            id = H_CHARSEL + (id - H_CHARCELL);
        if (id >= H_STROKEVIEW && id < H_STROKEVIEW + 8) {
            char ch[8];
            if (u8_at(detail_word, id - H_STROKEVIEW, ch)) {
                sv_back = SCR_CHAR;
                open_strokeview(ch);
            }
            break;
        }
        if (id >= H_CHARSEL && id < H_CHARSEL + CHARSEL_MAX) {
            char ch[8];
            /* A character of a compound, opened on its own. */
            if (u8_len(detail_word) > 1 && u8_at(detail_word, id - H_CHARSEL, ch)
                && is_lookup(ch)) {
                /* its syllable in the reading of the word looked up, which
                 * has none for the marks in a phrase */
                int j = 0;
                for (int k = 0; k < id - H_CHARSEL; k++) {
                    char c[8];
                    u8_at(detail_word, k, c);
                    j += is_lookup(c);
                }
                char sy[40];
                syllable_of(detail_py, j, u8_len(detail_key), sy, sizeof sy);
                open_char_read(ch, sy);
            }
            break;
        }
        if (id == H_MOREREAD) { detail_all = 1; break; }
        if (id == H_WNOTE || id == H_WDICT) { detail_bnote_on = id == H_WNOTE; break; }
        if (id == H_BACK)          scr = char_back;
        else if (id == H_PRACTICE) open_practice(detail_word);
        else if (id == H_ADD && detail_add[0]) {
            nulists = db_user_lists(ulists, MAXLISTS);
            pick_scroll = 0;
            scr = SCR_PICK;
        }
        break;

    case SCR_PICK:
        if (id == H_BACK) scr = SCR_CHAR;
        else if (id == H_MORE) { pick_scroll += pick_step;
                                 if (pick_scroll >= nulists) pick_scroll = nulists - 1; }
        else if (id == H_PREV) { pick_scroll -= pick_step; if (pick_scroll < 0) pick_scroll = 0; }
        else if (id == H_NEWLIST) { newname[0] = 0; scr = SCR_NEWLIST; }
        else if (id >= H_ITEM && id < H_TICK) {
            int i = id - H_ITEM;
            if (i < nulists) {
                db_user_add(ulists[i].id, detail_add, detail_py, detail_df);
                db_deck_set("My lists", ulists[i].chapter, MODE_WRITE, 1);
                db_deck_set("My lists", ulists[i].chapter, MODE_READ,  1);
                snprintf(toast, sizeof toast, "added %s to %s",
                         detail_add, ulists[i].chapter);
                reload_books();
                scr = SCR_CHAR;
            }
        }
        break;

    case SCR_NEWLIST:
        if (id == H_BACK) { scr = naming_rename ? SCR_LISTVIEW : SCR_PICK;
                            naming_rename = 0; }
        else if (id == H_KEY) kbd_edit(newname, sizeof newname, x, y);
        else if (id == H_CHECK && newname[0] && naming_rename) {
            /* A name already taken is refused by the database; it used to
             * say "renamed" anyway and leave the old name in place. */
            if (db_user_list_rename(cur_ulist, newname) != 0) {
                snprintf(toast, sizeof toast, "a list called %s already exists", newname);
            } else {
                snprintf(cur_uname, sizeof cur_uname, "%s", newname);
                snprintf(toast, sizeof toast, "renamed to %s", newname);
                naming_rename = 0;
                reload_books();
                scr = SCR_LISTVIEW;
            }
        }
        else if (id == H_CHECK && newname[0]) {
            int lid = db_user_list_create(newname);
            if (lid >= 0) {
                db_user_add(USER_LIST_BASE + lid, detail_add, detail_py, detail_df);
                db_deck_set("My lists", newname, MODE_WRITE, 1);
                db_deck_set("My lists", newname, MODE_READ,  1);
                snprintf(toast, sizeof toast, "added %s to %s", detail_add, newname);
                reload_books();
            }
            scr = SCR_CHAR;
        }
        break;
    }
    redraw();
}

/* ---- public ------------------------------------------------------------ */

/* Everything both the real app and the headless harness need set up before
 * anything draws. They each used to do it themselves, and the shot path was
 * missing the settings, so every render was at the default text size
 * whatever the setting said -- the sort of difference nobody sees in a
 * screenshot because the screenshot is the thing that is wrong. */
static void app_setup(int nopen) {
    W = gfx_w; H = gfx_h;
    no_pen = nopen;
    /* Set here, not only in app_run: the renders at the Clara's size drew
     * a "pen only" toggle it has no stylus for, and the harness pressed it. */
    has_pen = !nopen;
    if (!pleco_dir[0]) {
        const char *d = getenv("HANZI_PLECO_DIR");
        snprintf(pleco_dir, sizeof pleco_dir, "%s", d ? d : "import");
    }
    if (!export_dir[0]) {
        const char *d = getenv("HANZI_EXPORT_DIR");
        snprintf(export_dir, sizeof export_dir, "%s", d ? d : "export");
    }
    memset(&detail_s, 0, sizeof detail_s);
    memset(&prac_s, 0, sizeof prac_s);
    nbooks = db_books(books, MAXBOOKS);
    ntexts = db_texts(texts, MAXTEXTS);
    build_sources();
    show_punct = db_show_punct();
    text_readings = db_text_readings();
    text_scale = db_text_scale();
    comic_size = db_comic_size();
    comic_split = db_comic_split();
    guided_on  = db_guided();
    pen_only   = db_pen_only();
    show_mynotes = db_show_my_notes();
    show_booknotes = db_show_book_notes();
}

/* Time the device spent suspended since the last call, in ms. CLOCK_MONOTONIC
 * stops while the device sleeps and CLOCK_BOOTTIME does not, so the gap
 * between them grows by exactly the time asleep. The loop's old test -- a
 * long gap on the monotonic clock -- could never see a suspend, and fired
 * instead on anything that merely took a while. */
static msec_t slept_since_last(void) {
    struct timespec m, b;
    clock_gettime(CLOCK_MONOTONIC, &m);
    if (clock_gettime(CLOCK_BOOTTIME, &b) != 0) return 0;
    msec_t off = (msec_t)(b.tv_sec - m.tv_sec) * 1000 + (b.tv_nsec - m.tv_nsec) / 1000000;
    static msec_t last = -1;
    msec_t d = last < 0 ? 0 : off - last;
    last = off;
    return d > 0 ? d : 0;
}

/* Put down with the app open, the device would otherwise stay awake, and lit,
 * until the battery ran out: Nickel is not running to send it to sleep. Ten
 * minutes without a touch, a pen or a key, and it sleeps as if the power
 * button had been pressed. HANZI_AUTOSLEEP_MIN overrides; 0 turns it off. */
static msec_t autosleep_ms(void) {
    static msec_t v = -1;
    if (v >= 0) return v;
    const char *e = getenv("HANZI_AUTOSLEEP_MIN");
#ifdef HANZI_DESKTOP
    double min = e ? atof(e) : 0.0;        /* a desktop has no power button to wake it */
#else
    double min = e ? atof(e) : 10.0;       /* fractions allowed, for testing */
#endif
    v = min > 0 ? (msec_t)(min * 60000.0) : 0;
    return v;
}

/* The smallest rectangle holding both; an empty one (w 0) adds nothing. */
static rect_t rect_join(rect_t a, rect_t b) {
    if (a.w <= 0) return b;
    if (b.w <= 0) return a;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    rect_t r;
    r.x = a.x < b.x ? a.x : b.x;
    r.y = a.y < b.y ? a.y : b.y;
    r.w = x1 - r.x;
    r.h = y1 - r.y;
    return r;
}

void app_run(int nopen, int window_given) {
    app_setup(nopen);
#ifdef HANZI_DESKTOP
    /* the window as settings left it, unless a --window flag chose one for
     * this run -- `--window window` included, which is the way back to an
     * ordinary window when the saved mode misbehaves on some display */
    if (!window_given && db_window_mode() > WIN_WINDOW)
        surf_set_window_mode(db_window_mode());
#else
    (void)window_given;
#endif
    fprintf(stderr, "db: %d books, %d texts\n", nbooks, ntexts);
    redraw();

    ev_t e;
    int drawing = 0, strokes_seen = 0;
    int drawing_pen = 0;      /* whether the stroke under way is the pen's */
    msec_t drawing_ms = 0;    /* when that stroke last moved */
    rect_t ink_dirty = rc(0, 0, 0, 0);
    msec_t last_ink_ms = 0;
    has_pen  = !nopen && surf_has_pen();
    fprintf(stderr, "input: has_pen=%d pen_only=%d\n", has_pen, pen_only);
    msec_t last_tick = now_ms();
    msec_t woke_ms   = 0;
    msec_t last_input = now_ms();     /* for sleeping when left alone */
    slept_since_last();               /* start the suspend clock */
    signal(SIGALRM, watchdog_fired);
    int  light_save = -1;   /* frontlight level to put back on waking */
    int  light_held = -1;   /* a lock's saved level, carried into the sleep after it */
    int  lock_on_charger = 0;   /* locked, not asleep, because it was charging */
    msec_t charger_ms = 0;      /* when the lock last asked about the charger */
    for (;;) {
        alarm(0);                       /* round again: the loop is alive */
        if (want_quit) { fprintf(stderr, "app: quit requested\n"); break; }
        WHERE("waiting for an event");
        alarm(20);

        /* The device slept behind our back -- our own suspend already puts
         * the display back, so this is for anything else that suspends it.
         * The framebuffer may have been reconfigured while it was away, so
         * re-establish it and redraw rather than paint into a stale mapping.
         *
         * No timed release of the lock. There was one: two minutes after
         * locking, it turned the reading light back on by itself -- on a
         * device put down for the night. Any touch or key releases the lock,
         * and that is the whole of the way out it needs. */
        msec_t tick = now_ms();
        msec_t away = slept_since_last();
        if (away > 1000 && !sleeping) {
            fprintf(stderr, "wake: asleep %ld ms, reinitialising display\n", (long)away);
            if (surf_reinit()) { /* geometry may differ */ }
            redraw();
        }
        last_tick = tick;

        /* Waiting for input used to wake the CPU every 40 ms -- 25 times a
         * second with nothing on the screen changing, which keeps it from
         * ever idling. Nothing here runs on a timer, so the wait is as long
         * as the watchdog allows. While the pen is down it stays short, as
         * it was. */
        int wait = drawing ? 40 : 5000;
        if (!sleeping && autosleep_ms() > 0) {
            msec_t left = autosleep_ms() - (tick - last_input);
            if (left < wait) wait = left > 0 ? (int)left : 0;
        }
        if (!surf_poll(&e, wait)) {
            /* The pen held still mid-stroke: what it drew since the last
             * refresh waits for a move that is not coming. Show it now --
             * that is what the short poll while drawing is for. */
            if (drawing && ink_dirty.w > 0 && now_ms() - last_ink_ms >= INK_MS) {
                surf_present(ink_dirty, RF_FAST);
                ink_dirty = rc(0, 0, 0, 0);
                last_ink_ms = now_ms();
            }
            /* Locked because it was on the charger, and now it is not: sleep
             * properly, once. Otherwise a device unplugged and put away sat
             * awake, polling every five seconds, until someone touched it.
             * Only on that change, so a refusal for any other reason is not
             * retried over and over. */
            if (sleeping) {
                /* About once per wait (5 s): surf_poll also comes back empty
                 * for input that decodes to nothing -- a pen hovering over
                 * the locked panel, a hundred times a second -- and each
                 * check reads sysfs and logs a line. */
                if (now_ms() - charger_ms < 4000) continue;
                charger_ms = now_ms();
                int c = surf_charging();
                int unplugged = lock_on_charger && !c;
                lock_on_charger = c;
                if (unplugged) {
                    fprintf(stderr, "power: unplugged while locked; going to sleep\n");
                    /* the lock blanked the light: keep its level, not a 0 read now */
                    light_held = light_save;
                    sleeping = 0;
                    e.type = EV_POWER; e.x = e.y = 0; e.pen = 0; e.key = 0;
                    woke_ms = 0;
                    goto power;
                }
                continue;
            }
            if (!sleeping && autosleep_ms() > 0
                && now_ms() - last_input >= autosleep_ms()) {
                fprintf(stderr, "power: idle %ld s, going to sleep\n",
                        (long)(autosleep_ms() / 1000));
                last_input = now_ms();
                e.type = EV_POWER; e.x = e.y = 0; e.pen = 0; e.key = 0;
                woke_ms = 0;                  /* not a wake press */
                /* falls through to the power button's path */
            } else {
                continue;
            }
        } else {
            last_input = now_ms();
        }
        if (e.type == EV_QUIT) break;
#ifdef HANZI_DESKTOP
        /* a key or a page key in the middle of a mouse stroke: the stroke
         * ends where it is (your page keeps it) before the screen changes */
        if (drawing && (e.type == EV_KEY || e.type == EV_PAGE)) {
            if (scr == SCR_MYNOTE) lnote_save();
            drawing = 0;
            ink_dirty = rc(0, 0, 0, 0);
        }
        /* Esc goes back, and the window's close or Ctrl+Q quits; asleep, a
         * key wakes it, below, like any input */
        if (e.type == EV_KEY && !sleeping) { app_key(e.key); continue; }
        if (e.type == EV_DOWN && kfocus_on) {       /* the mouse: no ring */
            kfocus_on = 0;
            /* a stroke does not redraw by itself, and nor does a press on
             * nothing (tap) */
            if (in_writearea(e.x, e.y) || hottest(e.x, e.y) == H_NONE) redraw();
        }
#else
        if (e.type == EV_KEY && e.key == 27) break;
#endif

        /* Power button, which outside this app blanks the panel and suspends.
         * It should mean the same thing inside it.
         *
         * surf_suspend() blocks in the write to /sys/power/state and returns
         * when the button wakes the device again, so the wake path is simply
         * the code after it. If the kernel refuses to suspend we stay on the
         * sleep screen anyway: that is still a lock, which is most of what it
         * is wanted for, and a second press releases it.
         *
         * The press that wakes the device is also delivered as an input event
         * once we are running again. Without the guard below that event puts
         * the device straight back to sleep. */
        /* Locked and something happened: come back, whatever it was.
         *
         * This used to wake on the power button alone. Suspend is refused on
         * this device -- every log line says EPERM -- so the fallback is a
         * lock with the light off, and when that press was not delivered the
         * app sat alive and invisible with every other event dropped. That
         * is a device you have to hold the power button to escape. Any event
         * at all releases it now: a touch, a page key, the pen. */
        if (sleeping && e.type != EV_POWER) {
            fprintf(stderr, "power: woken by input (type %d)\n", e.type);
            sleeping = 0;
            woke_ms  = now_ms();
            surf_reinit();
            surf_light_set(light_save);
            redraw();
            last_tick = now_ms();
            continue;
        }

    power:
        if (e.type == EV_POWER) {
            if (now_ms() - woke_ms < 1200) continue;   /* the wake press itself */
            if (sleeping) {                 /* only reached if suspend refused */
                sleeping = 0;
                woke_ms  = now_ms();
                surf_reinit();
                surf_light_set(light_save);
                redraw();
                last_tick = now_ms();
                continue;
            }
            fprintf(stderr, "power: sleeping\n");
            sleeping = 1;
            draw_sleeping();
            surf_present(rc(0, 0, W, H), RF_FULL);
            /* Blank the reading light before suspending. Suspending the SoC
             * does not touch it, so without this the device sleeps with the
             * light on and is flat by morning. Restored on the way back. */
            /* Remember the level to come back to: exactly what it was, off
             * included (light_wake_level). Every way back -- a touch, the
             * power button on a refused suspend, a real wake -- restores
             * this one number. It is not written to the stored setting:
             * that is the level last chosen in settings, the guess for a
             * light that cannot be read. */
            light_save = light_wake_level(surf_light_get(), light_held, db_light());
            light_held = -1;
            fprintf(stderr, "light: saving %d before sleep\n", light_save);
            surf_light_set(0);
            /* surf_suspend() blocks until the button wakes the device, which
             * on a machine where suspend works is hours -- it turns the
             * watchdog off for that one write itself. Getting there (the
             * panel's settle wait, retries, dmesg) and back (the display,
             * the light, the first redraw) are where a hang would leave a
             * frozen device, so they are covered: generously on the way down,
             * and as usual from the moment it returns. */
            alarm(90);
            WHERE("going to sleep");
            int slept = surf_suspend();
            alarm(20);
            WHERE("waking");
            slept_since_last();   /* this path puts the display back itself */
            if (slept != 0) {
                /* 1: slept and woke. -1: power pressed on the way down, so it never
                 * slept -- either way, the device is wanted awake now. */
                fprintf(stderr, slept > 0 ? "power: woke\n"
                                          : "power: sleep cancelled by the power button\n");
                sleeping = 0;
                woke_ms  = now_ms();
                surf_reinit();
                /* The driver needs a moment after a resume before it will
                 * take a new level. */
                { struct timespec w = { 0, 200 * 1000 * 1000 };
                  nanosleep(&w, NULL); }
                surf_light_set(light_save);
                redraw();
                last_tick = now_ms();
                fprintf(stderr, "power: back on screen %ld ms after waking\n",
                        (long)(last_tick - woke_ms));
                surf_flush_input();
                surf_wake_log();
                log_cap();
                last_tick = now_ms();
                /* The press that woke it, and any made while the screen was
                 * still coming back, are not a request to sleep again: count
                 * the guard from when the page is actually up, not from
                 * before the half second it takes to get there. */
                woke_ms = last_tick;
            } else {
                /* Refused, most often because the device is plugged in --
                 * suspending an MTK Kobo over USB hangs it, so the backend
                 * declines. The light is off and input is ignored, which is
                 * still a lock; a second press brings it back. */
                fprintf(stderr, "power: not suspended -- locked with the light"
                                " off; any touch or key releases it\n");
                lock_on_charger = surf_charging();
                charger_ms = now_ms();
            }
            continue;
        }
        /* Asleep, everything else is a pocket touch on a panel that is still
         * physically responsive. Drop it. */
        if (sleeping) continue;

        /* The Libra's page-turn buttons. They mean "the next thing" on every
         * screen that has an order to it: the next word while studying, the
         * next page of a list, the next card once the current one is graded.
         * Reaching for them is the whole reason they are under your thumb. */
        if (e.type == EV_PAGE) {
            int fwd = e.key > 0;
            /* press() goes through tap(), which redraws for itself; the two
             * branches that change state directly redraw here instead. One
             * refresh per press either way -- a spare one costs a quarter of
             * a second on this panel and is plainly visible. */
            if (practice_active) {
                if (fwd) prac_reveal = !prac_reveal;
                redraw();
            } else switch (scr) {
            case SCR_REVIEW:
                if (!revealed) {
                    snprintf(keynote, sizeof keynote, "%s",
                             fwd ? "key: check" : "key: later");
                    press(fwd ? H_CHECK : H_POSTPONE);
                } else {
                    /* Turned over, the keys are the two answers you actually
                     * give: it went well, or it did not. Putting a card off
                     * is a decision you make before looking, not after. */
                    snprintf(keynote, sizeof keynote, "%s",
                             fwd ? "key: good" : "key: again");
                    press(fwd ? H_GOOD : H_AGAIN);
                }
                break;
            case SCR_STUDY:
                study_open(study_abs + (fwd ? 1 : -1));
                redraw();
                break;
            case SCR_TEXTREAD:
            case SCR_TEXTLINE:
            case SCR_TEXTWRITE:
            case SCR_TEXTNOTES:
                /* Reading a book is where a page key is most expected. */
                /* the picture filling the screen has only itself to press:
                 * a page key leaves it and turns the page */
                if (scr == SCR_TEXTREAD && ntplates && comic_full) {
                    comic_full = 0;
                    render();
                }
                /* the page-number grid pages by its blocks */
                if (scr == SCR_TEXTNOTES && tnote_picking)
                    press(fwd ? H_TPGNEXT : H_TPGPREV);
                else
                    press(fwd ? H_TNEXT : H_TPREV);
                break;
            case SCR_TEXTDONE:
                press(fwd ? H_TNEXTLINE : H_BACK);
                break;
            case SCR_CHAR:
                /* a dictionary entry has nothing to page: either key goes
                 * back to where it was opened from, the book's page above all */
                press(H_BACK);
                break;
            default:
                press(fwd ? H_MORE : H_PREV);   /* page the list, if it pages */
                break;
            }
            continue;
        }

        if (e.type == EV_DOWN) {
            WHERE("pen down");
            keynote[0] = 0;        /* a finger has its own feedback */
            /* Palm rejection: with a stylus available, a finger inside the
             * writing box is a resting hand, not a stroke. Buttons elsewhere
             * stay finger-tappable. */
            int inbox = in_writearea(e.x, e.y);
            /* A resting palm does not only land in the box -- it lands on
             * whatever is next to it. While the pen is in use on a card being
             * written, ignore finger contact anywhere on that screen, buttons
             * included, or the palm presses them. */
            int writing_screen = (scr == SCR_REVIEW && mode == MODE_WRITE)
                              || scr == SCR_DRAW || scr == SCR_TEXTWRITE
                              || scr == SCR_MYNOTE || practice_active
                              || (scr == SCR_PDFVIEW && pv.tapmode == PT_DRAW);
            /* "pen only" means a finger never writes. It used to mean a
             * finger could not write for a second and a half after the pen
             * touched the screen -- so in practice it nearly always could --
             * and the text screen was left out entirely. A finger still
             * presses buttons, except while the pen is actually in use,
             * when a finger beside the box is the writing hand's palm. */
            if (!e.pen && pen_only && has_pen && writing_screen) {
                if (inbox || surf_pen_seen()) {
                    fprintf(stderr, "ink: finger at %d,%d ignored (pen only)\n",
                            e.x, e.y);
                    /* and the pen's stroke, if one is under way, goes on:
                     * clearing `drawing` here cut it off where the palm
                     * landed, and it was never judged */
                    continue;
                }
            }
            /* A second contact while one is writing does not start another
             * stroke on top of it -- unless that stroke has gone quiet, in
             * case its lift was lost: fingers must never be locked out. */
            if (drawing && e.pen != drawing_pen && now_ms() - drawing_ms < 1500) continue;
            if (inbox) {
                drawing = 1;
                drawing_pen = e.pen;
                drawing_ms = now_ms();
                strokes_seen = 0;
                if (scr == SCR_MYNOTE) {                       /* your own page */
                    /* writing again takes back a clear asked for once */
                    if (mynote_clear_armed) { mynote_clear_armed = 0; mynote_redraw = 1; }
                    lnote_down(e.x, e.y);
                }
                else {
                    if (scr == SCR_PDFVIEW) pdfv_ink_begin(e.x, e.y);   /* the panel's side */
                    ink_t *k = ink_target();
                    if (!ui_ink_down(k, e.x, e.y)) {
                        /* The box is full. Not a stroke, or its moves are
                         * drawn onto the end of the last one. */
                        fprintf(stderr, "ink: %d strokes, the box takes no more\n", k->n);
                        drawing = 0;
                    } else {
                        /* The point at once: a tap is a dot, and has no
                         * segment for a move to draw. */
                        rect_t d = rc(0, 0, 0, 0);
                        ui_ink_draw_last_seg(k, INK_W, &d);
                        ink_dirty = rect_join(ink_dirty, d);
                    }
                }
            } else {
                drawing = 0;
                if (scr == SCR_REVIEW && mode == MODE_WRITE && !revealed)
                    in_writearea_dbg(e.x, e.y);   /* says why it was not a stroke */
                WHERE("handling a tap");
                tap(e.x, e.y);
                if (want_quit) break;
            }
        } else if (e.type == EV_MOVE && drawing) {
            if (!e.pen && pen_only && has_pen) continue;
            if (e.pen != drawing_pen) continue;      /* another contact's */
            drawing_ms = now_ms();
            /* The stylus reports far faster than the panel can refresh. Draw
             * every sample into the buffer, but coalesce the screen updates
             * into one refresh per INK_MS over the union of what changed --
             * asking for a refresh per sample saturates the EPDC queue and
             * the app stops responding. */
            rect_t d = rc(0,0,0,0);
            if (scr == SCR_MYNOTE) {
                lnote_move(e.x, e.y, &d);
                strokes_seen++;
            } else {
                ink_t *k = ink_target();
                ui_ink_move(k, e.x, e.y);
                strokes_seen++;
                ui_ink_draw_last_seg(k, INK_W, &d);
            }
            if (d.w > 0) {
                ink_dirty = rect_join(ink_dirty, d);
                msec_t t = now_ms();
                if (t - last_ink_ms >= INK_MS) {
                    surf_present(ink_dirty, RF_FAST);
                    ink_dirty = rc(0, 0, 0, 0);
                    last_ink_ms = t;
                }
            }
        } else if (e.type == EV_UP) {
            /* The palm lifting is not the pen's stroke ending. */
            if (drawing && e.pen != drawing_pen) continue;
            if (drawing)
                fprintf(stderr, "ink: stroke ended, %d points\n", strokes_seen);
            if (drawing && scr == SCR_MYNOTE) {  /* your page: kept on every lift */
                drawing = 0;
                /* a tap is a dot: one point, never drawn as a segment */
                if (lnote.n > 0 && lnote.start[lnote.n] - lnote.start[lnote.n - 1] == 1) {
                    rect_t pad = mynote_pad();
                    int a = lnote.start[lnote.n - 1];
                    int x = pad.x + lnote.x[a], y = pad.y + lnote.y[a];
                    gfx_line(x, y, x, y, INK_W, INK);
                    rect_t d = rc(x - INK_W - 2, y - INK_W - 2, 2 * INK_W + 4, 2 * INK_W + 4);
                    surf_present(d, RF_FAST);
                }
                if (ink_dirty.w > 0) { surf_present(ink_dirty, RF_FAST); ink_dirty = rc(0, 0, 0, 0); }
                WHERE("saving your page");
                lnote_save();
                /* the first stroke brings undo and clear; a full page, a
                 * failed save or a clear taken back say so */
                if (lnote.n == 1 || mynote_full || mynote_redraw) { mynote_redraw = 0; redraw(); }
                continue;
            }
            if (drawing && scr == SCR_DRAW) {   /* recognise on lift */
                WHERE("recognising handwriting on pen-up");
                drawing = 0;
                ink_dirty = rc(0, 0, 0, 0);
                hw_recognise();
                redraw();
                continue;
            }
            /* Over a PDF page: recognise, and repaint the stroke and the
             * panel over the page as kept -- never the guided branch below,
             * nor the card's boxes the fallback repaints. */
            if (drawing && scr == SCR_PDFVIEW && !practice_active) {
                WHERE("recognising handwriting over a PDF page");
                drawing = 0;
                rect_t d = ink_dirty;
                ink_dirty = rc(0, 0, 0, 0);
                pdfv_ink_lift(d);
                last_ink_ms = now_ms();
                continue;
            }
            /* Guided: judge the stroke that just finished, and if it was
             * wrong take it back off the page before anything is repainted. */
            if (drawing && guided_on) {
                ink_t *k = practice_active ? &prac_ink : &ink;
                if (k->n > 0 && k->s[k->n - 1].n > 0) {
                    char gch[8];
                    rect_t gbox;
                    if (writing_target(k->s[k->n - 1].p[0].x,
                                       k->s[k->n - 1].p[0].y,
                                       gch, sizeof gch, &gbox)) {
                        WHERE("guided: loading the model");
                        guided_begin(gch, gbox);
                        WHERE("guided: judging the stroke");
                        guided_try(k, gbox);
                        guided_stroke_done();
                        WHERE("guided: redrawing");
                        drawing = 0;
                        ink_dirty = rc(0, 0, 0, 0);
                        /* The box, and the line under it that says why a
                         * stroke was refused. Nothing else changed. */
                        redraw_guided(gbox);
                        continue;
                    }
                }
            }

            drawing = 0;
            if (ink_dirty.w > 0) {
                /* Repaint the whole writing box on pen-up with the ordinary
                 * waveform. Intermediate updates are fast but fussy; the
                 * finished stroke must be visible whatever the panel made of
                 * them. */
                /* Whichever screen's boxes the ink actually went into. The
                 * text screen's box is its own: repainting the write card's
                 * boxes there left the tail of a stroke -- drawn after the
                 * last fast refresh -- off the panel until something else
                 * redrew it. */
                if (scr == SCR_TEXTWRITE && !practice_active) {
                    surf_present(tw_box(), RF_PART);
                } else {
                    int n, cy, size;
                    if (practice_active) prac_geom(&n, &cy, &size);
                    else                 write_geom(&n, &cy, &size);
                    rect_t a = cellrect(0, n, cy, size);
                    rect_t b = cellrect(n - 1, n, cy, size);
                    surf_present(rc(a.x, a.y, b.x + b.w - a.x, a.h), RF_PART);
                }
                ink_dirty = rc(0, 0, 0, 0);
                last_ink_ms = now_ms();
            }
        }
    }
}

/* ---- headless screenshots ---------------------------------------------- */

/* Smallest touch target we are willing to ship. The panel is about 300dpi,
 * so 56px is a shade under 5mm -- already at the low end of what a fingertip
 * hits reliably, and the reason to keep a floor at all. */
#define MIN_TARGET 56
/* The chapter page's margin marks (H_BNOTE, H_MYNOTE): as wide as the margin
 * (44px on the right and on a comic's page, TREAD_LX on a chapter page's
 * left) and no wider, so they never take a press meant for the character
 * beside them, and the whole line tall. */
#define MARGIN_TARGET 44

/* Every screenshot doubles as an audit: anything tappable that came out too
 * small to hit is reported here rather than found on the device. */
static int check_targets(const char *path) {
    int bad = 0;
    if (hot_full) {
        fprintf(stderr, "  FAIL: %s ran out of hot rects (MAXHOT %d); "
                        "something drawn is not pressable\n", path, MAXHOT);
        hot_full = 0;
        bad++;
    }
    if (nhot > (MAXHOT * 3) / 4)
        fprintf(stderr, "  note: %s uses %d of %d hot rects\n", path, nhot, MAXHOT);
    for (int i = 0; i < nhot; i++) {
        if (hot[i].r.w >= MIN_TARGET && hot[i].r.h >= MIN_TARGET) continue;
        if (((hot[i].id >= H_BNOTE && hot[i].id < H_BNOTE + MAXTLINES)
             || (hot[i].id >= H_MYNOTE && hot[i].id < H_MYNOTE + MAXTLINES))
            && hot[i].r.w >= MARGIN_TARGET && hot[i].r.h >= MIN_TARGET) continue;
        fprintf(stderr, "  FAIL: %s has a %dx%d target (id %d) at %d,%d\n",
                path, hot[i].r.w, hot[i].r.h, hot[i].id, hot[i].r.x, hot[i].r.y);
        bad++;
    }
    return bad;
}

/* Every control on a screen must do something.
 *
 * Four settings rows once had their handlers written inside the practice
 * overlay's branch -- the one place those rows are never drawn -- so
 * pressing them changed nothing and nothing said so. The screen still drew,
 * the hot rect was still there, every check passed.
 *
 * So: draw the screen, press each of its controls in turn, and draw it
 * again. A control that leaves the screen on the same page with not one
 * pixel different did nothing at all. Anything that legitimately has no
 * visible effect is named in `quiet` below, with the reason. */
static unsigned fb_hash(void) {
    unsigned h = 2166136261u;
    for (long i = 0; i < (long)W * H; i++) { h ^= gfx_fb[i]; h *= 16777619u; }
    return h;
}

static int quiet_control(int id) {
#ifdef HANZI_DESKTOP
    /* by id, not by screen: an earlier press (back) can have left it */
    if ((id >= H_EX_EPUB && id <= H_EX_CHAP) || (scr == SCR_EXPORT && export_selected(id)))
        return export_selected(id);
#endif
    /* the window mode already chosen, and the PDF page already at fit */
    if (scr == SCR_SETTINGS && id >= H_WIN0 && id <= H_WIN3 && id - H_WIN0 == db_window_mode())
        return 1;
    if (scr == SCR_SETTINGS && id >= H_CSIZE0 && id <= H_CSIZE2 && id - H_CSIZE0 == comic_size)
        return 1;                                  /* the comic size already chosen */
    if (scr == SCR_PDFVIEW && id == H_PV_FIT && pv.zoom == PZ_FIT && !pv.choosing) return 1;
    switch (id) {
    case H_QUIT:                    /* sets a flag; the app exits after */
    case H_KEY:                     /* a keyboard: the key is the position */
        return 1;
    default:
        /* the source already shown: a switch's own position (the word
         * page's book note | dictionary too) */
        return (n_nsrc > 1 && id == H_NOTESRC + cur_nsrc) || (n_esrc > 1 && id == H_ENSRC + cur_esrc)
            || (scr == SCR_CHAR && id == (detail_bnote_on ? H_WNOTE : H_WDICT));
    }
}

/* States from which every control on a writing screen has something to do:
 * a stroke in the box for clear to take away, a character behind for prev
 * to go back to. */
static void a_stroke(ink_t *k, rect_t b) {
    ui_ink_clear(k);
    ui_ink_down(k, b.x + b.w / 4, b.y + b.h / 2);
    for (int i = 1; i <= 8; i++) ui_ink_move(k, b.x + b.w / 4 + i * b.w / 16, b.y + b.h / 2);
}
static void setup_textwrite(void) {
    tw_load(0);
    if (tw_npos > 1) tw_i = 1;
    practice_active = 0;
    scr = SCR_TEXTWRITE;
    a_stroke(&ink, tw_box());
}
static void setup_textdone(void) {
    tw_load(0);
    practice_active = 0;
    scr = SCR_TEXTDONE;
}
static void setup_practice(void) {
    scr = SCR_HOME;
    open_practice("\345\245\275");                /* 好 */
    int n, py, sz;
    prac_geom(&n, &py, &sz);
    a_stroke(&prac_ink, cellrect(0, n, py, sz));
}

static int dead_controls(const char *what, void (*draw)(void),
                         void (*setup)(void) = NULL) {
    struct { rect_t r; int id; } snap[MAXHOT];
    if (setup) setup();
    draw();
    int n = nhot;
    if (n > MAXHOT) n = MAXHOT;
    memcpy(snap, hot, sizeof(*snap) * (size_t)n);

    int save_scr = scr, bad = 0;
    for (int i = 0; i < n; i++) {
        int id = snap[i].id;
        if (quiet_control(id)) continue;
        scr = save_scr;
        if (setup) setup();
        draw();
        unsigned before = fb_hash();
        int x = snap[i].r.x + snap[i].r.w / 2;
        int y = snap[i].r.y + snap[i].r.h / 2;
        /* A control an earlier press has taken off the page -- prev, once
         * the page is back at the first sentence -- is not there to press,
         * which is different from pressing it and nothing happening. */
        if (hottest(x, y) != id) continue;
        int prac = practice_active;
        if (id == H_HDRSET || id == H_SETTINGS) {
            /* a toast left by an earlier press goes with any change of
             * screen (toast_follow), settings included */
            toast[0] = 0;
            draw();
            before = fb_hash();
        }
        press(hottest(x, y));
        if (id == H_HDRSET || id == H_SETTINGS) {
            /* settings, and back to this very screen as it was left */
            int ok = scr == SCR_SETTINGS && press(H_BACK)
                  && scr == save_scr && practice_active == prac;
            if (ok) { draw(); ok = fb_hash() == before; }
            if (!ok) {
                fprintf(stderr, "  FAIL: %s: settings and back does not return to it"
                                " as it was (now on %s)\n", what, scr_name(scr));
                bad++;
            }
            continue;
        }
        if (scr != save_scr) continue;          /* it went somewhere */
        draw();
        if (fb_hash() == before) {
            fprintf(stderr, "  FAIL: %s: the control at %d,%d (id %d) "
                            "does nothing\n", what, x, y, id);
            bad++;
        }
    }
    scr = save_scr;
    draw();
    return bad;
}

/* Settings from the screen on show, and back: it must come back as it was
 * left, to the pixel -- the page, the card, the ink, the query, the practice
 * box over it. With `via_reset`, on from settings to the reset page and back
 * first (resetting nothing). Returns 1 on a failure, which it names. */
static int settings_trip(const char *what, int via_reset = 0) {
    int was = scr, prac = practice_active;
    toast[0] = 0;                       /* a toast goes with its screen anyway */
    render();
    unsigned before = fb_hash();
    if (!press(H_HDRSET) || scr != SCR_SETTINGS || practice_active) {
        fprintf(stderr, "  FAIL: %s: no settings in the header, or it did not open\n", what);
        return 1;
    }
    if (via_reset && (!press(H_RESET) || scr != SCR_RESET || press_dry(H_HDRSET)
                      || !press(H_BACK) || scr != SCR_SETTINGS)) {
        fprintf(stderr, "  FAIL: %s: settings, reset, back is not settings again\n", what);
        return 1;
    }
    if (press_dry(H_HDRSET)) {
        fprintf(stderr, "  FAIL: settings shows its own button\n");
        return 1;
    }
    press(H_BACK);
    if (scr != was || practice_active != prac) {
        fprintf(stderr, "  FAIL: %s: back from settings went to %s%s\n", what,
                practice_active ? "practice over " : "", scr_name(scr));
        return 1;
    }
    render();
    if (fb_hash() != before) {
        fprintf(stderr, "  FAIL: %s: back from settings is not the screen as it was left\n", what);
        return 1;
    }
    fprintf(stderr, "  settings and back%s: %s as it was\n", via_reset ? " (via reset)" : "", what);
    return 0;
}

static void shot(const char *path) {
    check_targets(path);
    FILE *f = fopen(path, "wb");
    /* Silence here meant the whole pass could write nothing and still look
     * like it had run. */
    if (!f) { fprintf(stderr, "  FAIL: cannot write %s\n", path); return; }
    fprintf(f, "P5\n%d %d\n255\n", W, H);
    fwrite(gfx_fb, 1, (size_t)W * H, f);
    fclose(f);
    fprintf(stderr, "  %s\n", path);
}

/* The PDFs the harness reads: tests/data's, copied under names that
 * exercise the list -- a Chinese name, folders, a hidden folder that must be
 * skipped, a file that is not a PDF, and enough of them to page -- into a
 * folder of their own (HANZI_PDF_DIRS), so no render depends on the PDFs
 * the machine happens to have. Every file and folder made is listed, and
 * pdf_fixture_drop() removes exactly those. "" when tests/data is missing. */
static std::string pdf_fix_dir;
static std::vector<std::string> pdf_fix_made;     /* in the order made */
static int pdf_fix_have = 0;     /* the PDFs are there (else pdf_fix_dir is an empty folder) */
static int pdf_fix_skip = 0;     /* no tests/ here at all: not a checkout (the AppImage) */
#define PDF_FIX_COUNT 17                          /* the PDFs it should find */
#define PDF_FIX_TEXT "\350\256\272\350\257\255 \345\255\246\350\200\214"   /* 论语 学而 */

static int pdf_fix_copy(const std::string &from, const std::string &to) {
    FILE *a = fopen(from.c_str(), "rb");
    if (!a) return 0;
    FILE *b = fopen(to.c_str(), "wb");
    if (!b) { fclose(a); return 0; }
    char buf[8192];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n) ok = 0;
    fclose(a);
    if (fclose(b) != 0) ok = 0;
    pdf_fix_made.push_back(to);
    return ok;
}

static std::string pdf_fixture(void) {
    if (!pdf_fix_dir.empty()) return pdf_fix_have ? pdf_fix_dir : "";
    std::string src;
    for (const char *c : { "../tests/data", "tests/data" })
        if (access((std::string(c) + "/text.pdf").c_str(), R_OK) == 0) { src = c; break; }
    const char *tmp = getenv("TMPDIR");
    std::string t = std::string(tmp && *tmp ? tmp : "/tmp") + "/hanzi-pdfs-XXXXXX";
    std::vector<char> b(t.begin(), t.end());
    b.push_back(0);
    if (!mkdtemp(b.data())) return "";
    std::string d = b.data();
    pdf_fix_made.push_back(d);
    /* With no fixture to copy -- run outside a checkout, as the AppImage's
     * --selftest and --shot are -- the PDF screens' own checks are skipped
     * (pdf_fix_skip), and every scan the harness makes, the menu's sweep
     * included, reads this empty folder: never the machine's ~/Documents. */
    pdf_fix_dir = d;
    setenv("HANZI_PDF_DIRS", d.c_str(), 1);
    if (src.empty()) {
        pdf_fix_skip = access("../tests", F_OK) != 0 && access("tests", F_OK) != 0;
        return "";
    }
    pdf_fix_have = 1;
    for (const char *sub : { "/books", "/books/deeper", "/books/deeper/still", "/series", "/.hidden" }) {
        if (mkdir((d + sub).c_str(), 0755) != 0) return "";
        pdf_fix_made.push_back(d + sub);
    }
    int ok = pdf_fix_copy(src + "/text.pdf", d + "/" PDF_FIX_TEXT ".pdf")
          && pdf_fix_copy(src + "/scan.pdf", d + "/books/scan page.pdf")
          && pdf_fix_copy(src + "/text.pdf", d + "/books/deeper/still/Text Copy.PDF")
          && pdf_fix_copy(src + "/text.pdf", d + "/.hidden/never shown.pdf")
          && pdf_fix_copy(src + "/make_test_pdfs.py", d + "/books/not a pdf.py");
    for (int i = 1; ok && i <= 14; i++) {
        char n[64];
        snprintf(n, sizeof n, "/series/vol %02d.pdf", i);
        ok = pdf_fix_copy(src + "/text.pdf", d + n);
    }
    if (!ok) fprintf(stderr, "  FAIL: the PDF fixture could not be written in %s\n", d.c_str());
    return d;
}

static void pdf_fixture_drop(void) {
    for (size_t i = pdf_fix_made.size(); i-- > 0; ) {
        struct stat st;
        if (stat(pdf_fix_made[i].c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) rmdir(pdf_fix_made[i].c_str());
        else unlink(pdf_fix_made[i].c_str());
    }
    pdf_fix_made.clear();
    pdf_fix_dir.clear();
    pdf_fix_have = pdf_fix_skip = 0;
    unsetenv("HANZI_PDF_DIRS");
    pdf_scanned = 0;
    pdf_found.clear();
    pdf_rows.clear();
}

/* The row of the list that holds a name, or -1. */
static int pdf_row_named(const char *name) {
    for (size_t i = 0; i < pdf_rows.size(); i++)
        if (pdf_rows[i].name == name) return (int)i;
    return -1;
}

/* A page with every bar control live: the text PDF's second page, a quarter
 * of it, the margins trimmed one step. */
static void setup_pdfview(void) {
    if (!pv.open) {
        scr = SCR_PDFS;
        pdfv_open(pdf_fix_dir + "/" PDF_FIX_TEXT ".pdf", PDF_FIX_TEXT);
    }
    pv.page = 1; pv.zoom = PZ_QUAD; pv.part = 1; pv.trim = 1; pv.rot = 0; pv.flip = 0;
    pv.choosing = 0; pv.tapmode = PT_NONE;
    scr = SCR_PDFVIEW;
    toast[0] = 0;
}
static void setup_pdfs(void) {
    pdf_kb = 0;
    pdf_q[0] = 0;
    pdf_list_build();
    scroll = 0;
    toast[0] = 0;
    scr = SCR_PDFS;
}

/* The page's dark pixels inside the page's rect: a page that drew. */
static long pdf_dark(void) {
    rect_t r = pdfv_page_rect();
    long n = 0;
    for (int yy = r.y; yy < r.y + r.h && yy < H; yy++)
        for (int xx = r.x; xx < r.x + r.w && xx < W; xx++)
            if (gfx_fb[(size_t)yy * gfx_w + xx] < 0x80) n++;
    return n;
}

/* Everything the PDF screens claim, pressed the way a finger would; with
 * `dir`, the renders too. Returns the failures. */
static int pdf_screens_check(const char *dir) {
    int bad = 0;
    char p[512];
#define PDF_FAIL(...) do { fprintf(stderr, "  FAIL: pdf: " __VA_ARGS__); fprintf(stderr, "\n"); bad++; } while (0)
#define PDF_SHOT(name) do { if (dir) { render(); snprintf(p, sizeof p, "%s/%s.pgm", dir, name); shot(p); } } while (0)
    std::string fx = pdf_fixture();
    if (fx.empty() && pdf_fix_skip) {
        fprintf(stderr, "  note: no tests/data here (not a checkout): the PDF screens are not checked\n");
        return bad;
    }
    if (fx.empty()) { PDF_FAIL("no tests/data/text.pdf to make the PDF fixture from"); return bad; }
    pdf_scanned = 0;
    pdf_q[0] = 0;
    for (const std::string &pin : db_pdf_pins()) db_pdf_pin(pin.c_str(), 0, 0);

    scr = SCR_MENU; scroll = 0; practice_active = 0; toast[0] = 0;
    render();
    if (!press(H_PDFS) || scr != SCR_PDFS) { PDF_FAIL("the menu's PDFs row does not open them"); return bad; }
    if (pdf_found.size() != PDF_FIX_COUNT)
        PDF_FAIL("found %zu PDFs, not %d (hidden folders skipped, .PDF counted)", pdf_found.size(), PDF_FIX_COUNT);
    for (const pdf_file &f : pdf_found)
        if (f.path.find("/.hidden/") != std::string::npos) PDF_FAIL("a hidden folder was searched");
    PDF_SHOT("50-pdfs");
    if (dir) settings_trip("the PDFs");
    if (press(H_MORE)) {
        if (scroll <= 0) PDF_FAIL("next does not page the list");
        PDF_SHOT("50b-pdfs-next");
        press(H_PREV);
    } else PDF_FAIL("%d PDFs and no next page", PDF_FIX_COUNT);

    /* typed: the keyboard comes up from the field, and the case is ignored */
    if (!press(H_PDFFIELD) || !pdf_kb) PDF_FAIL("the search field brings no keyboard");
#ifdef HANZI_DESKTOP
    app_key('V'); app_key('o'); app_key('L');
#else
    snprintf(pdf_q, sizeof pdf_q, "VoL"); pdf_list_build();
#endif
    if (pdf_rows.size() != 14) PDF_FAIL("\"VoL\" holds %zu rows, not 14", pdf_rows.size());
    PDF_SHOT("50c-pdfs-typed");
    pdf_q[0] = 0; pdf_list_build();
    /* a press beside everything puts the keyboard away */
    render();
    tap(W / 2, 250);
    if (pdf_kb) PDF_FAIL("a press on nothing leaves the keyboard up");

    /* drawn: hanzi from the draw pad into the search */
    render();
    if (!press(H_PDFDRAW) || scr != SCR_DRAW) PDF_FAIL("draw does not open the draw pad");
    else {
        snprintf(hw_built, sizeof hw_built, "\350\256\272");          /* 论 */
        render();
        press(H_CHECK);
        if (scr != SCR_PDFS || strcmp(pdf_q, "\350\256\272") || pdf_rows.size() != 1)
            PDF_FAIL("a drawn 论 did not search the PDFs (on %s, \"%s\", %zu rows)",
                     scr_name(scr), pdf_q, pdf_rows.size());
        PDF_SHOT("50d-pdfs-drawn");
    }

    /* pinned, from the search (a Chinese name sorts last): then first in the list */
    render();
    int k = pdf_row_named(PDF_FIX_TEXT);
    if (k < 0 || k >= page_step) { PDF_FAIL("the Chinese name is not found by 论"); return bad; }
    press(H_PDFPIN + k);
    pdf_q[0] = 0; pdf_list_build(); scroll = 0;
    if (pdf_rows.empty() || !pdf_rows[0].pinned || pdf_rows[0].name != PDF_FIX_TEXT || pdf_npinned != 1)
        PDF_FAIL("a pin does not put the PDF first");
    PDF_SHOT("51-pdfs-pinned");

    /* a page, at fit */
    render();
    if (!press(H_ITEM + 0) || scr != SCR_PDFVIEW || !pv.open || pv.npages != 3) {
        PDF_FAIL("the pinned row does not open its 3 pages (%s)", pdf_error());
        return bad;
    }
    PDF_SHOT("52-pdf-fit");
    render();
    if (pdf_dark() < 200) PDF_FAIL("the page drew nothing (%ld dark pixels)", pdf_dark());
    {   /* the hooks for pressing on the page */
        rect_t where = rc(0, 0, 0, 0), pr = pdfv_page_rect();
        const uint8_t *px = pdfv_pixels(&where);
        float qx = -1, qy = -1, pw = 0, ph = 0;
        pdf_page_size(pv.page, &pw, &ph);
        int in = pdfv_to_page(pr.x + pr.w / 2, pr.y + pr.h / 2, &qx, &qy);
        if (!px || where.x != pr.x || where.y != pr.y || where.w != pr.w || where.h != pr.h
            || !in || qx < pw * 0.4f || qx > pw * 0.6f || qy < ph * 0.4f || qy > ph * 0.6f
            || pdfv_to_page(pr.x - 5, pr.y - 5, &qx, &qy))
            PDF_FAIL("the page's hooks disagree with what is drawn");
    }
    int r0 = pv_renders;
    render(); render();
    if (pv_renders != r0) PDF_FAIL("a redraw rendered the page again (%d times)", pv_renders - r0);
    if (dir) settings_trip("a PDF page");
    if (pv_renders != r0) PDF_FAIL("settings and back rendered the page again");

    press(H_MORE);
    if (pv.page != 1) PDF_FAIL("next is not page 2");
    PDF_SHOT("52b-pdf-page2");

    /* quarters: chosen on the page, then stepped through and on */
    press(H_PV_QUAD);
    if (!pv.choosing) PDF_FAIL("quarter does not offer the four");
    PDF_SHOT("53-pdf-choose");
    press(H_PV_QPICK + 1);
    if (pv.zoom != PZ_QUAD || pv.part != 1 || pv.choosing) PDF_FAIL("the top right quarter was not chosen");
    PDF_SHOT("53b-pdf-quarter");
    press(H_MORE); press(H_MORE);
    if (pv.page != 1 || pv.part != 3) PDF_FAIL("next does not step the quarters (%d/%d)", pv.page, pv.part);
    press(H_MORE);
    if (pv.page != 2 || pv.part != 0) PDF_FAIL("next from the last quarter is not the next page");
    press(H_PREV);
    if (pv.page != 1 || pv.part != 3) PDF_FAIL("prev from a page's first quarter is not the last before");
    press(H_PV_QUAD); press(H_PV_QUAD);          /* the four offered, and put away */
    if (pv.zoom != PZ_QUAD || pv.part != 3 || pv.choosing)
        PDF_FAIL("the four put away again lose the quarter (part %d)", pv.part);

    /* a half, stepped */
    press(H_PV_HALF);
    if (pv.zoom != PZ_HALF || pv.part != 0) PDF_FAIL("half is not the first half");
    PDF_SHOT("54-pdf-half");
    if (pdfv_nparts() < 2) PDF_FAIL("the half view has %d steps", pdfv_nparts());
    press(H_MORE);
    if (pv.part != 1 || pv.page != 1) PDF_FAIL("next is not the second half");
    PDF_SHOT("54b-pdf-half2");

    /* margins, then the page turned */
    r0 = pv_renders;
    press(H_PV_TRIMIN); press(H_PV_TRIMIN);
    if (pv.trim != 2 || pv_renders == r0) PDF_FAIL("trim does not trim");
    PDF_SHOT("55-pdf-trim");
    press(H_PV_ROT);
    if (pv.rot != 90) PDF_FAIL("rotate is not a quarter turn");
    PDF_SHOT("56-pdf-rotated");
    press(H_PV_ROT); press(H_PV_ROT); press(H_PV_ROT);
    if (pv.rot != 0) PDF_FAIL("four turns are not upright");

    /* the Kobo's flip, offered here for the render */
    pv_force_flip = 1;
    render();
    int ow = W, oh = H;
    if (!press(H_PV_FLIP) || !pv.flip || W != oh || H != ow) PDF_FAIL("flip does not turn the screen");
    PDF_SHOT("57-pdf-flip");
    if (dir) settings_trip("the flipped half");
    if (W != oh) PDF_FAIL("back from settings is not flipped again");

    /* kept for a pin: leave, come back, the same spot -- turned (four turns
     * above came back to 0, which a rotation lost would match too), and on
     * a later step of the flipped half where there is one (the screen is
     * upright again while it reopens) */
    press(H_PV_ROT);
    if (pv.rot != 90 || !pv.flip) PDF_FAIL("a turn in the flipped half is not kept turned and flipped");
    render();
    if (pdfv_nparts() > 1) press(H_MORE);
    pdfview_t was = pv;
    press(H_BACK);
    if (scr != SCR_PDFS || W != ow || H != oh) PDF_FAIL("back from the flipped page is not the list, upright");
    render();
    press(H_ITEM + 0);
    if (scr != SCR_PDFVIEW || pv.page != was.page || pv.zoom != was.zoom || pv.part != was.part
        || pv.trim != was.trim || pv.rot != was.rot || pv.flip != was.flip)
        PDF_FAIL("a pinned PDF does not open where it was left (page %d zoom %d part %d trim %d"
                 " rot %d flip %d; left at %d %d %d %d %d %d)", pv.page, pv.zoom, pv.part, pv.trim,
                 pv.rot, pv.flip, was.page, was.zoom, was.part, was.trim, was.rot, was.flip);
    render();
    if (pv.part != was.part) PDF_FAIL("the flipped half's step was lost turning the screen (%d, left at %d)",
                                      pv.part, was.part);
    press(H_PV_ROT); press(H_PV_ROT); press(H_PV_ROT);   /* upright again, for what follows */
    press(H_PV_HALF);                  /* out of the half: upright */
    pv_force_flip = 0;
    if (W != ow) PDF_FAIL("leaving the half does not turn the screen back");
    press(H_BACK);

    /* not pinned: forgotten */
    render();
    k = pdf_row_named("scan page");
    if (k < 0 || k >= page_step) PDF_FAIL("the scan is not on the first page");
    else {
        press(H_ITEM + k);
        if (scr != SCR_PDFVIEW || pv.npages != 1) PDF_FAIL("the scan does not open");
        PDF_SHOT("58-pdf-scan");
        press(H_PV_HALF);
        press(H_BACK);
        render();
        press(H_ITEM + k);
        if (pv.zoom != PZ_FIT) PDF_FAIL("a PDF not pinned remembered its zoom");
        press(H_BACK);
    }

    /* the comic reader's PDF: found by the volume's title in the comic's
     * folder, opened at the plate's place, paged freely, and back to the plate */
    {
        char o_path[sizeof comic_path], o_title[sizeof tchap_title];
        memcpy(o_path, comic_path, sizeof o_path); memcpy(o_title, tchap_title, sizeof o_title);
        int o_id = tchap_id, o_n = ntplates, o_pl = cplate, o_sub = csub;
        plate_t o_t[6];
        memcpy(o_t, tplates, sizeof o_t);
        snprintf(comic_path, sizeof comic_path, "%s/fake series.epub", fx.c_str());
        snprintf(tchap_title, sizeof tchap_title, "01 \350\256\272\350\257\255\345\255\246\350\200\214");   /* 论语学而 */
        tchap_id = 987654321; ntplates = 6; cplate = 4; csub = 0;
        memset(tplates, 0, sizeof o_t);
        scr = SCR_TEXTREAD;
        render();
        if (!press_dry(H_TPDF)) PDF_FAIL("the comic page has no PDF button though the PDF is in its folder");
        else if (!press(H_TPDF) || scr != SCR_PDFVIEW || !pv.open || !pv.comic || pv.npages != 3
                 || pv.page != 2)
            PDF_FAIL("the comic's PDF button does not open page 3 of 3 for plate 5 of 6 (page %d)", pv.page + 1);
        else {
            render();
            PDF_SHOT("59-pdf-from-comic");
            press(H_PREV); press(H_PREV);
            if (pv.page != 0) PDF_FAIL("the PDF from the comic cannot be paged");
            press(H_BACK);
            if (scr != SCR_TEXTREAD || pv.open || cplate != 4) PDF_FAIL("back to the comic does not return to its plate");
        }
        scr = SCR_PDFS; render();
        if (!press(H_ITEM + 0) || pv.comic) PDF_FAIL("a PDF opened from the list is offered a way back to a comic");
        if (pv.open) pdfv_close();
        snprintf(tchap_title, sizeof tchap_title, "01 abc");
        tchap_id = 987654322;
        scr = SCR_TEXTREAD; render();
        if (press_dry(H_TPDF)) PDF_FAIL("the comic page offers a PDF none matches");
        memcpy(comic_path, o_path, sizeof comic_path); memcpy(tchap_title, o_title, sizeof tchap_title);
        tchap_id = o_id; ntplates = o_n; cplate = o_pl; csub = o_sub;
        memcpy(tplates, o_t, sizeof o_t);
    }

    /* every control does something */
    bad += dead_controls("the PDFs", draw_pdfs, setup_pdfs);
    bad += dead_controls("a PDF page", draw_pdfview, setup_pdfview);
    if (pv.open) pdfv_close();
    pv_force_flip = 0;
    for (const std::string &pin : db_pdf_pins()) db_pdf_pin(pin.c_str(), 0, 0);
    pdf_kb = 0; pdf_q[0] = 0; scroll = 0; toast[0] = 0;
    scr = SCR_HOME;
    render();
    if (W != ow || H != oh) PDF_FAIL("the screen was left turned");
#undef PDF_FAIL
#undef PDF_SHOT
    fprintf(stderr, "  PDFs: %d failure%s\n", bad, bad == 1 ? "" : "s");
    return bad;
}

/* ---- pressing and drawing on a PDF page, for the harness ---- */
/* Where a character of the page's text layer is on screen; 0 if it is not. */
static int pv_layer_char_at(const char *ch, int *sx, int *sy) {
    pv_geom_t g;
    float pw = 0, ph = 0;
    if (!pdfv_geom(&g) || !pdf_page_size(pv.page, &pw, &ph)) return 0;
    std::vector<pdf_char> all;
    pdf_text_in(pv.page, pdf_box{ 0, 0, pw, ph }, &all);
    for (const pdf_char &c : all) {
        if (strcmp(c.utf8, ch)) continue;
        float vx = 0, vy = 0;
        if (!pdf_page_to_view(pv.page, g.scale, pv.rot, (c.box.x0 + c.box.x1) / 2,
                              (c.box.y0 + c.box.y1) / 2, &vx, &vy)) return 0;
        *sx = g.dx + (int)lroundf(vx) - g.sx;
        *sy = g.dy + (int)lroundf(vy) - g.sy;
        return 1;
    }
    return 0;
}
/* A point of the page, in points from its top left, on screen. */
static int pv_page_pt_at(float px, float py, int *sx, int *sy) {
    pv_geom_t g;
    float vx = 0, vy = 0;
    if (!pdfv_geom(&g) || !pdf_page_to_view(pv.page, g.scale, pv.rot, px, py, &vx, &vy)) return 0;
    *sx = g.dx + (int)lroundf(vx) - g.sx;
    *sy = g.dy + (int)lroundf(vy) - g.sy;
    return 1;
}
/* A character written over the page from its own medians, into `box`, a
 * stroke at a time as the pen would (pdfv_ink_begin before the first). */
static int pv_write(const char *ch, rect_t box) {
    strokes_t hs;
    if (db_strokes(ch, &hs) <= 0) return 0;
    float sc = (float)box.w / 1024.0f;
    for (int i = 0; i < hs.n; i++)
        for (int j = 0; j < hs.nmed[i]; j++) {
            int x = (int)(box.x + hs.med[i][j * 2] * sc);
            int y = (int)(box.y + hs.med[i][j * 2 + 1] * sc);
            if (j == 0) { pdfv_ink_begin(x, y); ui_ink_down(&pv_ink, x, y); }
            else        ui_ink_move(&pv_ink, x, y);
        }
    db_strokes_free(&hs);
    return 1;
}
/* The ink's characters as read, first candidates, as one string. */
static std::string pv_read_word(void) {
    std::string w;
    for (int c = 0; c < pvw.n; c++) w += pvw.nc[c] > 0 ? pvw.cand[c][0].ch : "?";
    return w;
}
static int pv_has_cand(const char *ch, int within) {
    for (int i = 0; i < pv_read.n && i < within; i++) if (!strcmp(pv_read.cand[i].ch, ch)) return 1;
    return 0;
}
/* The text PDF's first page at fit, with `mode` on. */
static void pv_harness_page(const char *file, const char *name, int mode) {
    std::string path = pdf_fix_dir + "/" + file;
    if (!pv.open || pv.path != path) { scr = SCR_PDFS; pdfv_open(path, name); }
    pv.page = 0; pv.zoom = PZ_FIT; pv.part = 0; pv.trim = 0; pv.rot = 0; pv.flip = 0;
    pv.choosing = 0; pv.tapmode = mode;
    pdfv_marks_drop();
    scr = SCR_PDFVIEW;
    toast[0] = 0;
}
#define PV_TEXT_FILE PDF_FIX_TEXT ".pdf"
/* 而 of 学而时习之, pressed: read from the text layer. */
static void setup_pdf_read(void) {
    pv_harness_page(PV_TEXT_FILE, PDF_FIX_TEXT, PT_OCR);
    int x = 0, y = 0;
    if (pv_layer_char_at("\350\200\214", &x, &y)) pdfv_page_tap(x, y);
}
/* 你好 written over the page in one go. */
static void setup_pdf_draw(void) {
    pv_harness_page(PV_TEXT_FILE, PDF_FIX_TEXT, PT_DRAW);
    rect_t a = pdfv_ink_area();
    int side = std::min(300, (a.w - 120) / 2);
    pv_write("\344\275\240", rc(a.x + 60, a.y + 40, side, side));
    pv_write("\345\245\275", rc(a.x + 60 + side + 30, a.y + 40, side, side));
    pv_word_recognise();
}

/* Tap OCR and drawing over a PDF page, as a finger and a pen would: a
 * character of the text page pressed (the text layer's, nothing read from
 * the picture), characters of the scan pressed across and down a column
 * (read from the picture, never the garbage layer's letters), a candidate
 * and a character of the line opening the character screen, whose back is
 * the page as it was; then a word written over the page in one go, cut into
 * its characters and read, chosen, looked up. Returns the failures; with
 * `dir`, the renders too. */
static int pdf_read_check(const char *dir) {
    int bad = 0;
    char p[512];
#define PR_FAIL(...) do { fprintf(stderr, "  FAIL: pdf read: " __VA_ARGS__); fprintf(stderr, "\n"); bad++; } while (0)
#define PR_SHOT(name) do { if (dir) { render(); snprintf(p, sizeof p, "%s/%s.pgm", dir, name); shot(p); } } while (0)
    if (pdf_fixture().empty() && pdf_fix_skip) {
        fprintf(stderr, "  note: no tests/data here (not a checkout): reading PDFs is not checked\n");
        return bad;
    }
    if (pdf_fixture().empty()) { PR_FAIL("no tests/data/text.pdf to make the PDF fixture from"); return bad; }
    for (const std::string &pin : db_pdf_pins()) db_pdf_pin(pin.c_str(), 0, 0);
    practice_active = 0;
    int ow = W, oh = H;

    /* the OCR toggle; a press on the page then reads 而 from the layer */
    pv_harness_page(PV_TEXT_FILE, PDF_FIX_TEXT, PT_NONE);
    if (!pv.open) { PR_FAIL("the text PDF does not open (%s)", pdf_error()); return bad; }
    render();
    if (!press(H_PV_OCR) || pv.tapmode != PT_OCR) PR_FAIL("OCR does not turn on");
    int x = 0, y = 0, runs = pv_ocr_runs;
    if (!pv_layer_char_at("\350\200\214", &x, &y)) PR_FAIL("no \350\200\214 in the text page's layer");
    render();
    if (hottest(x, y) != H_PV_PAGE) PR_FAIL("a press on the page with OCR on is not the page's");
    tap(x, y);
    if (!pv_read.on || pv_read.n < 1 || strcmp(pv_read.cand[0].ch, "\350\200\214"))
        PR_FAIL("\350\200\214 pressed offers %s first (%d candidates)", pv_read.n ? pv_read.cand[0].ch : "nothing", pv_read.n);
    if (pv_ocr_runs != runs) PR_FAIL("a believed text layer was read from the picture too");
    std::string line;
    for (const std::string &c : pv_read.ctx) line += c;
    if (line != "\345\255\246\350\200\214\346\227\266\344\271\240\344\271\213" || pv_read.at != 1)
        PR_FAIL("the line is \"%s\" at %d, not \345\255\246\350\200\214\346\227\266\344\271\240\344\271\213 at 1", line.c_str(), pv_read.at);
    if (pv_side != 0) PR_FAIL("a press at the top put the panel over it");
    {   /* the panel is not the page: a press on its blank is not a new read */
        rect_t pn = pv_panel_rect();
        if (pn.w <= 0 || hottest(W - 30, pn.y + pn.h - 20) == H_PV_PAGE) PR_FAIL("the panel's blank reads the page under it");
    }
    PR_SHOT("59-pdf-read-text");
    /* a candidate, and a character of the line: the character screen, and back to the page as it was */
    render();
    unsigned before = fb_hash();
    int r0 = pv_renders;
    if (!press(H_CAND + 0) || scr != SCR_CHAR || strcmp(detail_word, "\350\200\214"))
        PR_FAIL("the candidate does not open \350\200\214 (on %s, \"%s\")", scr_name(scr), detail_word);
    if (!press(H_BACK) || scr != SCR_PDFVIEW) PR_FAIL("back from the character is not the PDF page");
    render();
    if (fb_hash() != before) PR_FAIL("back from the character is not the page as it was left");
    if (pv_renders != r0) PR_FAIL("the character screen and back rendered the page again");
    if (!press(H_PV_CTX + 3) || scr != SCR_CHAR || strcmp(detail_word, "\344\271\240"))
        PR_FAIL("the line's 4th character does not open \344\271\240 (\"%s\")", detail_word);
    press(H_BACK);
    if (dir) bad += settings_trip("a character read on a PDF page");
    press(H_PV_RCLOSE);
    if (pv_read.on) PR_FAIL("close leaves the panel up");
    tap(x, y);
    press(H_MORE);
    if (pv_read.on) PR_FAIL("a page turned keeps the character read on the page before");
    bad += dead_controls("a character read on a PDF page", draw_pdfview, setup_pdf_read);

    /* A press maps through every view, not only the page upright at fit
     * (where the part shown starts at the view's corner and nothing is
     * turned): a turned, trimmed quarter, a trimmed half, the
     * page upside down and turned the other way -- the layer's 而 in all of
     * them, and the scan's 地 read from the picture where its text can be
     * read at all: upright, or turned a quarter right (a column, which the
     * recogniser turns back). Upside down, or a quarter left, the picture's
     * text is upside down to it, and reads as nothing. */
    {
        struct { int zoom, trim, rot; } views[] = {
            { PZ_QUAD, 1, 90 }, { PZ_HALF, 2, 0 }, { PZ_HALF, 1, 180 }, { PZ_FIT, 1, 270 } };
        struct { const char *file, *name, *ch; float px, py; } pts[] = {
            { PV_TEXT_FILE, PDF_FIX_TEXT, "\350\200\214", -1, -1 },                  /* 而, by the layer */
            { "books/scan page.pdf", "scan page", "\345\234\260", 132, 104 },        /* 地, by the picture */
        };
        int ocr = pv_ocr_ready();
        for (const auto &pt : pts)
            for (const auto &v : views) {
                if (pt.px >= 0 && (!ocr || v.rot >= 180)) continue;   /* no models: said below */
                pv_harness_page(pt.file, pt.name, PT_OCR);
                if (!pv.open) { PR_FAIL("%s does not open", pt.file); break; }
                pv.zoom = v.zoom; pv.trim = v.trim; pv.rot = v.rot;
                int found = 0;
                for (int part = 0; part < pdfv_nparts() && !found; part++) {
                    pv.part = part;
                    pdfv_marks_drop();
                    render();
                    int on = pt.px < 0 ? pv_layer_char_at(pt.ch, &x, &y) : pv_page_pt_at(pt.px, pt.py, &x, &y);
                    if (!on || hottest(x, y) != H_PV_PAGE) continue;
                    found = 1;
                    tap(x, y);
                    if (!pv_has_cand(pt.ch, pt.px < 0 ? 1 : 5) || !pv_read.marked)
                        PR_FAIL("%s pressed in zoom %d part %d trim %d rot %d offers %s first (%d candidates)%s",
                                pt.ch, v.zoom, part, v.trim, v.rot, pv_read.n ? pv_read.cand[0].ch : "nothing",
                                pv_read.n, pv_read.marked ? "" : ", unmarked");
                }
                if (!found) PR_FAIL("%s is on no part of zoom %d trim %d rot %d", pt.ch, v.zoom, v.trim, v.rot);
            }
    }

    /* the scan: its layer is ABCD over 天地玄黄, so 地 comes from the picture */
    pv_harness_page("books/scan page.pdf", "scan page", PT_OCR);
    if (!pv.open) PR_FAIL("the scan does not open");
    else if (pdf_is_scan(0) != 1) PR_FAIL("the scan is not seen as one");
    else if (!pv_ocr_ready()) PR_FAIL("no OCR models to read the scan with: %s", ocr_error());
    else {
        struct { const char *ch, *line; float px, py; const char *what; } taps[] = {
            { "\345\234\260", "\345\244\251\345\234\260\347\216\204\351\273\204", 132, 104, "59b-pdf-read-scan" },    /* 地 of 天地玄黄 */
            { "\345\256\231", "\345\256\207\345\256\231\346\264\252\350\215\222", 360, 284, "59c-pdf-read-column" },  /* 宙, down 宇宙洪荒 */
        };
        for (const auto &t : taps) {
            runs = pv_ocr_runs;
            render();
            if (!pv_page_pt_at(t.px, t.py, &x, &y)) { PR_FAIL("no %s on screen", t.ch); continue; }
            tap(x, y);
            line.clear();
            for (const std::string &c : pv_read.ctx) line += c;
            fprintf(stderr, "  pressed %s on the scan: %d candidates, first %s, line \"%s\", %d read%s\n", t.ch,
                    pv_read.n, pv_read.n ? pv_read.cand[0].ch : "-", line.c_str(), pv_ocr_runs - runs,
                    pv_ocr_runs - runs == 1 ? "" : "s");
            if (pv_ocr_runs == runs) PR_FAIL("a scan was not read from the picture");
            if (!pv_has_cand(t.ch, 5)) PR_FAIL("%s pressed on the scan is not in the first five", t.ch);
            for (int i = 0; i < pv_read.n; i++)
                if (!pv_hanzi(pv_read.cand[i].ch)) PR_FAIL("%s offered: not a hanzi", pv_read.cand[i].ch);
            /* the line it was read in (the fixture's JPEG is poor: a
             * neighbour may be misread, the character pressed may not) */
            if (line.find(t.ch) == std::string::npos || u8_len(line.c_str()) != u8_len(t.line))
                PR_FAIL("the line read is \"%s\", not %s", line.c_str(), t.line);
            if (!pv_read.marked) PR_FAIL("where %s was read is not marked", t.ch);
            PR_SHOT(t.what);
        }
        /* a press on bare paper says so, offering nothing */
        press(H_PV_RCLOSE);
        render();
        if (pv_page_pt_at(150, 420, &x, &y)) {
            tap(x, y);
            if (pv_read.n != 0 || !pv_read.why[0]) PR_FAIL("bare paper offered %d candidates", pv_read.n);
        }
    }

    /* drawing over the page: 你好 in one go, two characters, read */
    pv_harness_page(PV_TEXT_FILE, PDF_FIX_TEXT, PT_NONE);
    render();
    if (!press(H_PV_DRAWON) || pv.tapmode != PT_DRAW) PR_FAIL("draw does not turn on");
    rect_t a = pdfv_ink_area();
    if (!in_writearea(a.x + a.w / 2, a.y + a.h / 2) || ink_target() != &pv_ink)
        PR_FAIL("a stroke over the page with draw on is not ink for it");
    r0 = pv_renders;
    setup_pdf_draw();
    pdfv_ink_lift(rc(0, 0, 0, 0));
    if (pv_renders != r0) PR_FAIL("a stroke rendered the PDF again (%d)", pv_renders - r0);
    if (pvw.n != 2 || pv_read_word() != "\344\275\240\345\245\275")
        PR_FAIL("\344\275\240\345\245\275 written in one go read as %d characters, \"%s\"", pvw.n, pv_read_word().c_str());
    if (pv_side != 0) PR_FAIL("ink at the top put the panel over it");
    {
        rect_t pn = pv_panel_rect();
        if (pn.w <= 0 || in_writearea(W / 2, pn.y + pn.h / 2)) PR_FAIL("the panel takes ink");
    }
    PR_SHOT("59d-pdf-draw-word");
    if (dir) bad += settings_trip("a word written over a PDF page");
    /* one character each, however their strokes stand apart: 二, 川, 明 */
    for (const char *one : { "\344\272\214", "\345\267\235", "\346\230\216", "\345\245\275" }) {
        pv_word_clear();
        pv_write(one, rc(a.x + 200, a.y + 60, 360, 360));
        pv_word_recognise();
        if (pvw.n != 1) PR_FAIL("%s written alone read as %d characters (%s)", one, pvw.n, pv_read_word().c_str());
        else fprintf(stderr, "  wrote %s over the page: read %s\n", one, pv_read_word().c_str());
    }
    /* three characters, written down a column: 你好吗 */
    pv_word_clear();
    for (int i = 0; i < 3; i++) {
        const char *ch[] = { "\344\275\240", "\345\245\275", "\345\220\227" };
        pv_write(ch[i], rc(a.x + 300, a.y + 30 + i * 300, 260, 260));
    }
    pv_word_recognise();
    if (pvw.n != 3) PR_FAIL("\344\275\240\345\245\275\345\220\227 down a column read as %d characters (%s)", pvw.n, pv_read_word().c_str());
    /* chosen: the word accepted, the page clean; looked up; back is the page with it */
    setup_pdf_draw();
    render();
    if (!press(H_CAND + 0) || strcmp(pv_built, "\344\275\240\345\245\275") || pv_ink.n != 0)
        PR_FAIL("choosing does not accept the word (\"%s\", %d strokes left)", pv_built, pv_ink.n);
    PR_SHOT("59e-pdf-draw-built");
    if (!press(H_PV_LOOKUP) || scr != SCR_CHAR || strcmp(detail_word, "\344\275\240\345\245\275"))
        PR_FAIL("look up does not open \344\275\240\345\245\275 (on %s, \"%s\")", scr_name(scr), detail_word);
    if (!press(H_BACK) || scr != SCR_PDFVIEW || strcmp(pv_built, "\344\275\240\345\245\275"))
        PR_FAIL("back from the word is not the page with it");
    press(H_MORE);
    if (pv_built[0] || pv_ink.n) PR_FAIL("a page turned keeps the ink of the page before");
    {   /* pen only is pressed among them, and is a setting: put back */
        int was_pen_only = pen_only;
        bad += dead_controls("a word written over a PDF page", draw_pdfview, setup_pdf_draw);
        if (pen_only != was_pen_only) { db_set_pen_only(was_pen_only); pen_only = db_pen_only(); }
    }

    if (pv.open) pdfv_close();
    pdfv_marks_drop();
    scr = SCR_HOME;
    toast[0] = 0;
    render();
    if (W != ow || H != oh) PR_FAIL("the screen was left turned");
#undef PR_FAIL
#undef PR_SHOT
    fprintf(stderr, "  PDF reading and drawing: %d failure%s\n", bad, bad == 1 ? "" : "s");
    return bad;
}

/* Drive a whole test the way a person would, including quitting halfway and
 * coming back, and assert the invariants. Used by tools/check.sh. */
int app_selftest(void) {
    app_setup(0);           /* the Libra with its stylus: pen only is pressed too */
    pdf_fixture();          /* the menu's PDFs row, pressed below, scans it, not ~ */
    int bad = 0;

    for (int i = 0; i < nbooks; i++)
        if (!strcmp(books[i].name, "HSK 3.1")) {
            nlists = db_lists(books[i].name, lists, MAXLISTS);
            for (int k = 0; k < nlists; k++) {
                db_deck_set(lists[k].book, lists[k].chapter, MODE_WRITE, 1);
                db_deck_set(lists[k].book, lists[k].chapter, MODE_READ,  1);
            }
            break;
        }

    for (int m = 0; m < 2; m++) {
        mode = m; list_id = 0; scope_book[0] = 0;
        snprintf(list_label, sizeof list_label, "all decks");
        /* On a fresh profile nothing is due, so a test is as big as the
         * daily new-card allowance permits, never more than the test size. */
        int size  = m == MODE_WRITE ? TEST_WRITE : TEST_READ;
        int allow = db_new_allowance(m, today());
        int want  = size < allow ? size : allow;
        start_test();
        db_session_load(&sess);
        printf("    %s test: %d cards (size %d, allowance %d, want %d)\n",
               m == MODE_WRITE ? "write" : "read", sess.total, size, allow, want);
        if (sess.total != want) { printf("    FAIL: wrong test size\n"); bad = 1; }

        /* answer the first three wrong, then quit mid-test */
        int answered = 0, wrong_keys = 0;
        char wrong[3][96];
        while (have_card && answered < 5) {
            if (answered < 3) {
                snprintf(wrong[wrong_keys++], 96, "%s", card.key);
                grade(1);
            } else {
                grade(3);
            }
            answered++;
        }
        db_session_load(&sess);
        printf("    answered %d, then quit: %d left on disk\n",
               answered, sess.pending + sess.repeat);
        if (sess.repeat != 3) { printf("    FAIL: wrong answers not held for redo\n"); bad = 1; }

        in_test = 0;                       /* simulate leaving the app */
        memset(&card, 0, sizeof card);
        have_card = 0;

        if (!db_session_load(&sess) || sess.pending + sess.repeat == 0) {
            printf("    FAIL: test did not survive the quit\n"); bad = 1;
        }
        in_test = 1;                       /* resume */
        if (!load_card()) { printf("    FAIL: nothing to resume\n"); bad = 1; }

        /* finish: everything right, which must include the three wrong ones */
        int seen = 0, redone = 0;
        while (have_card && seen < want * 3) {
            for (int k = 0; k < wrong_keys; k++)
                if (!strcmp(card.key, wrong[k])) redone++;
            grade(3);
            seen++;
        }
        db_session_load(&sess);
        printf("    finished after %d more answers; wrong ones re-shown %d times\n",
               seen, redone);
        if (redone < 3) { printf("    FAIL: a wrong card never came back\n"); bad = 1; }
        if (sess.pending + sess.repeat != 0) { printf("    FAIL: test not complete\n"); bad = 1; }
        db_session_clear();
    }

    /* The per-row start buttons must actually start something. H_GO_W sat
     * inside the tick range, so the tick branch swallowed every press. */
    {
        scr = SCR_HOME; scroll = 0; in_test = 0;
        nbooks = db_books(books, MAXBOOKS);
        if (nbooks) {
            snprintf(cur_book, sizeof cur_book, "%s", books[0].name);
            nlists = db_lists(cur_book, lists, MAXLISTS);
            scr = SCR_LISTS;
            /* Start from the opposite mode, so the assertion is not vacuous. */
            mode = MODE_WRITE;
            list_id = -1;
            draw_lists();
            if (!press(H_GO_R + 0)) { printf("    FAIL: no 读 start on a chapter row\n"); bad = 1; }
            printf("    chapter 读 start: screen %s, mode %s, list %d (wanted %d)\n",
                   scr_name(scr), mode == MODE_READ ? "read" : "write",
                   list_id, lists[0].id);
            if (scr != SCR_REVIEW && scr != SCR_DONE) {
                printf("    FAIL: the start button did nothing\n"); bad = 1;
            }
            if (mode != MODE_READ)      { printf("    FAIL: wrong mode\n"); bad = 1; }
            if (list_id != lists[0].id) { printf("    FAIL: wrong chapter scoped\n"); bad = 1; }
            if (scope_book[0])          { printf("    FAIL: book scope not cleared\n"); bad = 1; }
            in_test = 0; db_session_clear(); scr = SCR_HOME;
        }
    }

    /* Two drawings must build a two-character word. */
    {
        scr = SCR_DRAW; ui_ink_clear(&hw_ink); hw_n = 0; hw_built[0] = 0;
        const char *want[2] = { "\344\275\240", "\345\245\275" };   /* 你好 */
        for (int c = 0; c < 2; c++) {
            strokes_t hs;
            if (db_strokes(want[c], &hs) <= 0) continue;
            rect_t pad = hw_pad();
            float sc = (float)pad.w / 1024.0f;
            ui_ink_clear(&hw_ink);
            for (int i = 0; i < hs.n; i++)
                for (int j = 0; j < hs.nmed[i]; j++) {
                    int x = (int)(pad.x + hs.med[i][j*2]     * sc);
                    int y = (int)(pad.y + hs.med[i][j*2 + 1] * sc);
                    if (j == 0) ui_ink_down(&hw_ink, x, y);
                    else        ui_ink_move(&hw_ink, x, y);
                }
            db_strokes_free(&hs);
            hw_recognise();
            draw_draw();
            press(H_CAND + 0);              /* accept the top candidate */
        }
        printf("    drew two characters, built \"%s\"\n", hw_built);
        if (strcmp(hw_built, "\344\275\240\345\245\275") != 0) {
            printf("    FAIL: expected \344\275\240\345\245\275\n"); bad = 1;
        }
        hw_built[0] = 0; ui_ink_clear(&hw_ink); hw_n = 0; scr = SCR_HOME;
    }

    /* Every candidate slot must accept, not trip some other action. H_CAND
     * once overlapped H_LOCK, so the fourth candidate locked the screen. */
    {
        scr = SCR_DRAW;
        strokes_t hs;
        int slots = 0, ok = 0;
        if (db_strokes("\346\260\264", &hs) > 0) {
            rect_t pad = hw_pad();
            float sc = (float)pad.w / 1024.0f;
            for (int k = 0; k < HW_CANDS; k++) {
                hw_built[0] = 0; want_quit = 0; scr = SCR_DRAW;
                ui_ink_clear(&hw_ink);
                for (int i = 0; i < hs.n; i++)          /* retrace every time */
                    for (int j = 0; j < hs.nmed[i]; j++) {
                        int x = (int)(pad.x + hs.med[i][j*2]     * sc);
                        int y = (int)(pad.y + hs.med[i][j*2 + 1] * sc);
                        if (j == 0) ui_ink_down(&hw_ink, x, y);
                        else        ui_ink_move(&hw_ink, x, y);
                    }
                hw_recognise();
                if (k >= hw_n) break;
                slots++;
                draw_draw();
                press(H_CAND + k);
                if (want_quit || hw_built[0] == 0) {
                    printf("    FAIL: candidate %d did not accept (quit=%d)\n",
                           k, want_quit);
                    bad = 1; want_quit = 0;
                    break;
                }
                ok++;
            }
            db_strokes_free(&hs);
        }
        printf("    %d of %d candidate slots accept\n", ok, slots);
        hw_built[0] = 0; ui_ink_clear(&hw_ink); hw_n = 0; scr = SCR_HOME;
    }

    /* exit must be reachable from a screen that is not home */
    {
        scr = SCR_DICT; q[0] = 0; nres = 0;
        draw_dict();
        want_quit = 0;
        if (!press(H_QUIT)) { printf("    FAIL: no exit on the dictionary\n"); bad = 1; }
        if (!want_quit) { printf("    FAIL: exit did not fire off-home\n"); bad = 1; }
        else printf("    exit works from a screen that is not home\n");
        want_quit = 0;
        scr = SCR_HOME;
    }




    /* Reading a book must survive closing it. This is the whole point of the
     * feature, and every part of it is easy to half-do: recording a position
     * that is never read back, reading one back but landing on the wrong
     * page, or landing right but forgetting which chapter it was. */
    if (ntexts > 0) {
        text_open(texts[0].id, texts[0].title);
        if (ntchaps > 2) {
            tchap_open(2);
            int want_chap = tchap_id;
            scr = SCR_TEXTREAD;
            tread_seek(0);
            draw_textread();
            int pages = 0;
            while (pages < 4 && tread_start[tread_np] + tread_shown < ntlines) {
                press(H_TNEXT);
                draw_textread();
                pages++;
            }
            int want_line = tread_start[tread_np];

            /* Leave the book entirely, the way quitting does. */
            scr = SCR_HOME;
            text_id = 0; tchap_id = 0; ntlines = 0;
            tread_np = 0; tread_start[0] = 0;

            if (db_text_last_chapter(texts[0].id) != want_chap) {
                printf("    FAIL: the book forgot which chapter it was in\n");
                bad = 1;
            }
            text_open(texts[0].id, texts[0].title);
            int last = db_text_last_chapter(text_id);
            int found = 0;
            for (int k = 0; k < ntchaps; k++)
                if (tchaps[k].id == last) {
                    tchap_open(k);
                    tread_seek(db_text_read(last));
                    found = 1;
                    break;
                }
            scr = SCR_TEXTREAD;
            draw_textread();
            if (!found) { printf("    FAIL: the saved chapter is gone\n"); bad = 1; }
            else if (tread_start[tread_np] != want_line) {
                printf("    FAIL: came back to line %d, left at %d\n",
                       tread_start[tread_np], want_line);
                bad = 1;
            } else {
                printf("    a book reopens where it was left: %s line %d\n",
                       tchap_title, want_line + 1);
            }
        }
        /* A position saved against a chapter that has since got shorter --
         * a rebuilt database, a book re-extracted -- must land on a real
         * page, not push the stack to its limit. */
        if (ntchaps > 0) {
            tchap_open(0);
            scr = SCR_TEXTREAD;
            tread_seek(ntlines - 1);          /* the real last page */
            int want_np = tread_np;
            tread_seek(ntlines + 10000);      /* and a position past the end */
            draw_textread();
            if (tread_np != want_np) {
                printf("    FAIL: a stale position lands on page %d, the last"
                       " page is %d\n", tread_np + 1, want_np + 1);
                bad = 1;
            } else if (tread_start[tread_np] >= ntlines || tread_shown < 1) {
                printf("    FAIL: a stale position lands on nothing"
                       " (page starts at %d of %d lines)\n",
                       tread_start[tread_np], ntlines);
                bad = 1;
            } else {
                printf("    a stale reading position lands on the last page"
                       " (%d of %d)\n", tread_np + 1, want_np + 1);
            }
        }
        db_set_text_last_chapter(texts[0].id, 0);
        scr = SCR_HOME;
    }

    /* A book page in the back stack must still be that book when you come
     * back to it, and a toast must not follow you there. srcs[] holds
     * indices into books[]: creating your first own list puts "My lists"
     * among the books, and the page kept an index that now meant another
     * book (reload_books). The way a person gets there: 道德经's page, read,
     * press a character, add, a new list, and back out again. */
    {
        static const char *DDJ = "\351\201\223\345\276\267\347\273\217";     /* 道德经 */
        scr = SCR_HOME; cur_cat = -1; scroll = 0; in_test = 0;
        practice_active = 0; toast[0] = 0;
        reload_books();
        int had_mine = 0, bi_was = -1;
        for (int i = 0; i < nbooks; i++) {
            if (!strcmp(books[i].name, "My lists")) had_mine = 1;
            if (!strcmp(books[i].name, DDJ)) bi_was = i;
        }
        snprintf(cur_src_name, sizeof cur_src_name, "%s", DDJ);
        const source_t *sv = cur_source();
        if (!sv || sv->text_i < 0 || sv->book_i < 0) {
            printf("    FAIL: no %s with a text and decks for the book page\n", DDJ);
            bad = 1;
        } else {
            scr = SCR_BOOK; render();
            int ok = press(H_TREAD) && scr == SCR_TEXTCH;
            ok = ok && press(H_ITEM + 0) && scr == SCR_TEXTREAD;
            ok = ok && press(H_TSENT) && press(H_TLINE + tread_start[tread_np]) && scr == SCR_TEXTLINE;
            int tapped = 0;
            for (int k = 0; ok && k < ntbox && !tapped; k++) {
                char c0[8];
                u8_at(tlines[tbox[k].line], tbox[k].idx, c0);
                if (!is_lookup(c0)) continue;
                tap(tbox[k].r.x + tbox[k].r.w / 2, tbox[k].r.y + tbox[k].r.h / 2);
                tapped = 1;
            }
            ok = ok && tapped && scr == SCR_CHAR;
            ok = ok && press(H_ADD) && scr == SCR_PICK;
            ok = ok && press(H_NEWLIST) && scr == SCR_NEWLIST;
            snprintf(newname, sizeof newname, "selftest list");
            ok = ok && press(H_CHECK) && scr == SCR_CHAR;
            int said = ok && toast[0];          /* "added 道 to selftest list" */
            for (int k = 0; ok && k < 4; k++) ok = press(H_BACK);   /* to the book */
            if (!ok || scr != SCR_BOOK) {
                printf("    FAIL: book page -> a new list -> back broke, on %s\n", scr_name(scr));
                bad = 1;
            } else {
                sv = cur_source();
                int bi = sv ? sv->book_i : -1;
                const char *shows = bi >= 0 ? books[bi].name : "(none)";
                int bi_now = -1;
                for (int i = 0; i < nbooks; i++) if (!strcmp(books[i].name, DDJ)) bi_now = i;
                printf("    %s's page after a new list: %s's decks (books[%d] -> [%d]%s)\n",
                       DDJ, shows, bi_was, bi_now, had_mine ? ", My lists was there" : "");
                if (strcmp(shows, DDJ)) {
                    printf("    FAIL: the book page came back as another book's\n");
                    bad = 1;
                }
                if (!said) { printf("    FAIL: adding a word said nothing\n"); bad = 1; }
                else if (toast[0]) {
                    printf("    FAIL: \"%s\" followed onto the book page\n", toast);
                    bad = 1;
                }
                if (!press(H_GO_W) || strcmp(scope_book, DDJ)) {
                    printf("    FAIL: its write button tested \"%s\"\n", scope_book);
                    bad = 1;
                }
                press(H_BACK);                   /* out of the test, where it began */
                sv = cur_source();
                if (scr != SCR_BOOK || !sv || sv->book_i < 0 || strcmp(books[sv->book_i].name, DDJ)) {
                    printf("    FAIL: out of the test, not back on %s's page\n", DDJ);
                    bad = 1;
                }
            }
            in_test = 0; db_session_clear();
            nulists = db_user_lists(ulists, MAXLISTS);
            for (int i = 0; i < nulists; i++)
                if (!strcmp(ulists[i].chapter, "selftest list")) db_user_list_delete(ulists[i].id);
            reload_books();
        }
        /* a test begun on a page whose book has gone by its end goes home:
         * leave_test decides, rather than draw_book drawing a blank page */
        in_test = 1; test_from_scr = SCR_BOOK; test_from_scroll = 0; test_from_book[0] = 0;
        snprintf(cur_src_name, sizeof cur_src_name, "no such book");
        leave_test();
        if (scr != SCR_HOME) { printf("    FAIL: out of a test to a book that is gone\n"); bad = 1; }
        in_test = 0; cur_src_name[0] = 0; toast[0] = 0; scr = SCR_HOME;
    }

    /* Across a chapter's end next lands on the next chapter's first page,
     * and prev on the last page of the one before, whatever was saved in
     * them: a chapter read to its end once came up on its last page, and a
     * chapter never read on its first. The sentence screen's next too. */
    {
        int tested = 0;
        for (int t = 0; t < ntexts && !tested; t++) {
            text_open(texts[t].id, texts[t].title);
            if (ntchaps < 2) continue;
            scr = SCR_TEXTREAD;
            tchap_open(1);
            if (ntplates) continue;
            tread_seek(ntlines - 1);
            int last1 = ntlines - 1, pages1 = tread_np;
            tchap_open(0);
            tread_seek(ntlines - 1);
            int last_np0 = tread_np;
            if (ntplates || pages1 < 1 || last_np0 < 1) continue;   /* a page or less */
            tested = 1;
            db_set_text_read(tchaps[1].id, last1);       /* read to its end */
            db_set_text_read(tchaps[0].id, 0);           /* and at its top */
            render();                                    /* the first's last page */
            press(H_TNEXT);
            int c1 = tchap_idx, np1 = tread_np, at1 = tread_start[tread_np];
            press(H_TPREV);
            int c0 = tchap_idx, np0 = tread_np;
            db_set_text_read(tchaps[1].id, last1);
            tw_load(ntlines - 1); tline_page = 0;
            scr = SCR_TEXTLINE; render();
            for (int k = 0; k < 20 && tchap_idx == 0; k++) if (!press(H_TNEXT)) break;
            printf("    %s: next from the end of %s opens line %d of the next,"
                   " prev page %d of %d, next sentence line %d\n", texts[t].title,
                   tchaps[0].title, at1 + 1, np0 + 1, last_np0 + 1, tw_line + 1);
            if (c1 != 1 || np1 != 0 || at1 != 0) {
                printf("    FAIL: next went to chapter %d, page %d\n", c1 + 1, np1 + 1);
                bad = 1;
            }
            if (c0 != 0 || np0 != last_np0) {
                printf("    FAIL: prev went to chapter %d page %d, not its last\n", c0 + 1, np0 + 1);
                bad = 1;
            }
            if (tchap_idx != 1 || tw_line != 0) {
                printf("    FAIL: the sentence after the last went to chapter %d line %d\n",
                       tchap_idx + 1, tw_line + 1);
                bad = 1;
            }
            db_set_text_last_chapter(texts[t].id, 0);
        }
        if (!tested) { printf("    FAIL: no text with two chapters of more than a page\n"); bad = 1; }
        scr = SCR_HOME;
    }

    /* A one-line chapter read shows as read. Its place is line 0, which
     * "> 0" took for never opened, and mark_reading never wrote it. */
    {
        int found = 0;
        for (int t = 0; t < ntexts && !found; t++) {
            text_open(texts[t].id, texts[t].title);
            for (int k = 0; k < ntchaps && !found; k++) {
                if (tchaps[k].n_line != 1) continue;
                found = 1;
                char was[96], now[96];
                chapter_label(&tchaps[k], was, sizeof was);
                tchap_open(k);
                scr = SCR_TEXTREAD; tread_seek(0); render();
                mark_page_read();
                chapter_label(&tchaps[k], now, sizeof now);
                printf("    a one-line chapter (%s %s) read: \"%s\" -> \"%s\"\n",
                       texts[t].title, tchaps[k].title, was, now);
                if (!strstr(now, "read")) { printf("    FAIL: it does not show as read\n"); bad = 1; }
            }
            if (found) db_set_text_last_chapter(texts[t].id, 0);
        }
        if (!found) printf("    (no one-line chapter to check)\n");
        scr = SCR_HOME;
    }

    /* A tap is a dot. It was kept in the ink and never drawn -- a segment
     * needs two points -- and guided writing refused every one, so a 点
     * written as a tap could not be written at all. 主's first stroke. */
    {
        int save_guided = guided_on;
        scr = SCR_HOME; guided_on = 0;
        open_practice("\344\270\273");                   /* 主 */
        int n, py, sz;
        prac_geom(&n, &py, &sz);
        rect_t box = cellrect(0, n, py, sz);
        const strokes_t *m0 = &prac_s[0];
        if (m0->n < 2 || m0->nmed[0] < 2 || m0->nmed[m0->n - 1] < 2) {
            printf("    FAIL: no model for the dot\n");
            bad = 1;
        } else {
            /* the middle of a stroke, on the screen */
            auto mid = [&](int s, int *x, int *y) {
                int e = (m0->nmed[s] - 1) * 2;
                *x = box.x + (m0->med[s][0] + m0->med[s][e]) / 2 * box.w / 1024;
                *y = box.y + (m0->med[s][1] + m0->med[s][e + 1]) / 2 * box.h / 1024;
            };
            auto dark = [&](int x, int y) {
                int c = 0;
                for (int yy = y - INK_W; yy <= y + INK_W; yy++)
                    for (int xx = x - INK_W; xx <= x + INK_W; xx++)
                        if (xx >= 0 && yy >= 0 && xx < W && yy < H && gfx_fb[yy * W + xx] <= 0x40) c++;
                return c;
            };
            int x, y;
            mid(0, &x, &y);
            render();
            int before = dark(x, y);
            ui_ink_down(&prac_ink, x, y);
            rect_t d = rc(0, 0, 0, 0);
            ui_ink_draw_last_seg(&prac_ink, INK_W, &d);  /* what pen-down draws */
            int at_once = d.w > 0 && dark(x, y) > before;
            render();                                     /* and a whole redraw */
            int after = dark(x, y);
            /* guided: the same tap, judged the way the pen-up judges it */
            guided_on = 1; guided_reset(); ui_ink_clear(&prac_ink);
            ui_ink_down(&prac_ink, x, y);
            char gch[8] = "";
            rect_t gbox;
            int took = 0;
            if (writing_target(x, y, gch, sizeof gch, &gbox)) {
                guided_begin(gch, gbox);
                took = guided_try(&prac_ink, gbox) && g_next == 1;
            }
            /* and one where no dot goes, the middle of the long 横 at the
             * bottom, is not taken for the next stroke */
            int lx, ly, wrongly = 0;
            mid(m0->n - 1, &lx, &ly);
            if (took) {
                ui_ink_down(&prac_ink, lx, ly);
                wrongly = guided_try(&prac_ink, gbox) || g_next != 1;
            }
            printf("    a tap on %s's dot: %d dark pixels there -> %d; guided %s it\n",
                   "\344\270\273", before, after, took ? "takes" : "refuses");
            if (!at_once) { printf("    FAIL: a dot is not shown as the pen goes down\n"); bad = 1; }
            if (after <= before) { printf("    FAIL: a dot is not drawn\n"); bad = 1; }
            if (!took) { printf("    FAIL: guided refuses a dot where the dot goes (%s)\n", g_msg); bad = 1; }
            if (wrongly) { printf("    FAIL: guided takes a tap on the 横 for stroke 2\n"); bad = 1; }
        }
        guided_on = save_guided; guided_reset();
        ui_ink_clear(&prac_ink); practice_active = 0;
        /* and the 65th stroke is refused, not drawn onto the end of the 64th */
        int took = 0;
        for (int i = 0; i <= MAXINK_STROKE; i++) took += ui_ink_down(&prac_ink, 100 + i, 100);
        if (took != MAXINK_STROKE || prac_ink.n != MAXINK_STROKE) {
            printf("    FAIL: the ink took %d strokes of %d\n", took, MAXINK_STROKE + 1);
            bad = 1;
        }
        ui_ink_clear(&prac_ink);
        scr = SCR_HOME;
    }

    /* A press on nothing redraws nothing, except where it puts something
     * away: there, it disarms delete as it always did. */
    {
        scr = SCR_HOME; scroll = 0; cur_cat = -1; render();
        int nx = -1, ny = -1;
        for (int y = 100; y < H && nx < 0; y += 16)
            for (int x = 8; x < W && nx < 0; x += 32)
                if (hottest(x, y) == H_NONE) { nx = x; ny = y; }
        uint8_t mark = (uint8_t)(gfx_fb[0] ^ 0x55);
        gfx_fb[0] = mark;                                /* a redraw paints it over */
        tap(nx, ny);
        if (nx < 0 || scr != SCR_HOME || gfx_fb[0] != mark) {
            printf("    FAIL: a press on nothing on home redrew it\n");
            bad = 1;
        }
        scr = SCR_LISTVIEW; cur_ulist = -1; scroll = 0; nuents = uent_total = 0;
        delete_armed = 1;
        render();
        nx = -1;
        for (int y = 100; y < H && nx < 0; y += 16)
            for (int x = 8; x < W && nx < 0; x += 32)
                if (hottest(x, y) == H_NONE) { nx = x; ny = y; }
        if (nx >= 0) tap(nx, ny);
        if (delete_armed) { printf("    FAIL: a press on nothing left delete armed\n"); bad = 1; }
        delete_armed = 0; toast[0] = 0; scr = SCR_HOME;
    }

    /* Guided writing along the path a pen actually takes. Everything here
     * used to call guided_try() directly with a box it worked out itself,
     * which skipped writing_target() -- the part that decides, from where
     * the stroke started, which character is being written and in which box.
     * So the tests passed while the feature did nothing on the device. */
    if (ntexts > 0) {
        int save_guided = guided_on;
        guided_on = 1;
        text_open(texts[0].id, texts[0].title);
        tchap_open(0);
        tw_load(0);
        scr = SCR_TEXTWRITE;
        draw_textwrite();

        char want[8];
        u8_at(tlines[tw_line], tw_pos[tw_i], want);
        strokes_t md;
        if (db_strokes(want, &md) <= 0) {
            printf("    FAIL: no model for %s\n", want);
            bad = 1;
        } else {
            rect_t box = tw_box();
            float sc = (float)box.w / 1024.0f;
            int accepted = 0;
            ui_ink_clear(&ink);
            for (int i2 = 0; i2 < md.n; i2++) {
                /* Strokes accumulate, the way a hand makes them -- clearing
                 * between them is not what a pen does, and it hid the fact
                 * that accepted strokes were being thrown away. */
                for (int j = 0; j < md.nmed[i2]; j++) {
                    int x = (int)(box.x + md.med[i2][j*2]     * sc);
                    int y = (int)(box.y + md.med[i2][j*2 + 1] * sc);
                    if (j == 0) ui_ink_down(&ink, x, y);
                    else        ui_ink_move(&ink, x, y);
                }
                /* exactly what the pen-up handler does */
                char gch[8];
                rect_t gbox;
                if (!writing_target(ink.s[ink.n - 1].p[0].x,
                                    ink.s[ink.n - 1].p[0].y,
                                    gch, sizeof gch, &gbox)) {
                    printf("    FAIL: writing_target found no character under"
                           " a stroke inside the box\n");
                    bad = 1;
                    break;
                }
                if (strcmp(gch, want)) {
                    printf("    FAIL: writing_target says %s, the box wants"
                           " %s\n", gch, want);
                    bad = 1;
                    break;
                }
                guided_begin(gch, gbox);
                if (guided_try(&ink, gbox)) accepted++;
                guided_stroke_done();
            }
            int nstk = g_model.n;
            db_strokes_free(&md);
            printf("    guided along the pen's own path: %d of %d strokes of"
                   " %s accepted\n", accepted, nstk, want);
            if (accepted < nstk) {
                printf("    FAIL: a character traced from its own medians was"
                       " not accepted stroke by stroke\n");
                bad = 1;
            }
            /* Every stroke on the page came from the model, so the pen's
             * ink is gone by design. What must survive is the verdict: a
             * character written stroke by stroke is written, and the strip
             * shows the character rather than an empty box. */
            if (ink.n != 0) {
                printf("    FAIL: %d of the pen's own strokes left on the"
                       " page\n", ink.n);
                bad = 1;
            }
            if (!tw_magic[tw_i]) {
                printf("    FAIL: finishing a character against the model did"
                       " not mark it written\n");
                bad = 1;
            } else if (tw_rank[tw_i] < 0) {
                printf("    FAIL: a character written against the model is"
                       " not counted right\n");
                bad = 1;
            } else {
                printf("    and it counts as written, from the model's own"
                       " strokes\n");
            }
        }
        guided_on = save_guided;
        guided_reset();
        ui_ink_clear(&ink);
        scr = SCR_HOME;
    }

    /* Every control on every screen that can be set up from nothing must do
     * something when pressed. See dead_controls(). */
    {
        struct { const char *what; int screen; void (*draw)(void); } pages[] = {
            { "home",     SCR_HOME,     draw_home     },
            { "settings", SCR_SETTINGS, draw_settings },
            { "the menu", SCR_MENU,     draw_menu     },
        };
        for (int i = 0; i < (int)(sizeof pages / sizeof *pages); i++) {
            scr = pages[i].screen;
            scroll = 0;
            bad += dead_controls(pages[i].what, pages[i].draw);
        }
        /* and the book page, which needs a book chosen first */
        scr = SCR_HOME; scroll = 0; draw_home();
        build_sources();
        for (int i = 0; i < nsrcs; i++) {
            if (srcs[i].text_i < 0 || srcs[i].book_i < 0) continue;
            snprintf(cur_src_name, sizeof cur_src_name, "%s", srcs[i].name);
            scr = SCR_BOOK;
            bad += dead_controls("a book page", draw_book);
            break;
        }
        cur_src_name[0] = 0;

        /* The PDF screens, from a fixture of their own */
        bad += pdf_screens_check(NULL);
        bad += pdf_read_check(NULL);        /* tap OCR, and drawing over a page */
        pdf_fixture_drop();

        /* The writing screens, where the controls changed most. */
        if (ntexts > 0) {
            text_open(texts[0].id, texts[0].title);
            tchap_open(0);
            bad += dead_controls("writing a sentence", draw_textwrite, setup_textwrite);
            bad += dead_controls("the sentence written", draw_textdone, setup_textdone);
            tw_load(0);
        }
        /* render(), not draw_practice(): back closes the overlay, and only
         * drawing what is actually showing can see it go. */
        bad += dead_controls("try writing it", render, setup_practice);
        practice_active = 0;
        guided_on = 0; db_set_guided(0); guided_reset();
        pen_only = 1;  db_set_pen_only(1);
        scr = SCR_HOME;
        /* Pressing every control starts tests and ticks decks along the way.
         * Put it all back, or the assertions after this one are measuring
         * whatever the sweep happened to leave behind. */
        db_session_clear();
        db_reset_progress(NULL, -1);
        nbooks = db_books(books, MAXBOOKS);
        build_sources();
        in_test = 0;
        printf("    every control on home, settings, the menu, a book and the"
               " writing screens does something, and settings from each comes"
               " back to it as it was\n");
    }

    /* Every control on the settings screen must actually change something.
     * Four of them did not: their handlers had been written inside the
     * practice overlay's block, which is the one place they are never
     * drawn, so pressing them on the settings screen did nothing at all and
     * nothing said so. Press each one and read the value back. */
    {
        scr = SCR_SETTINGS;
        struct { int id; const char *what; int (*get)(void); } t[] = {
            { H_PUNCT,   "punctuation in texts", db_show_punct },
            { H_TXPLUS,  "text size",            db_text_scale },
            { H_WPLUS,   "write new limit",      NULL          },
            { H_RPLUS,   "read new limit",       NULL          },
        };
        for (int i = 0; i < (int)(sizeof t / sizeof *t); i++) {
            draw_settings();
            int before = t[i].get ? t[i].get()
                                  : db_new_limit(t[i].id == H_WPLUS ? MODE_WRITE
                                                                    : MODE_READ);
            if (!press(t[i].id)) {
                printf("    FAIL: no control for %s\n", t[i].what); bad = 1;
                continue;
            }
            int after = t[i].get ? t[i].get()
                                 : db_new_limit(t[i].id == H_WPLUS ? MODE_WRITE
                                                                   : MODE_READ);
            if (after == before) {
                printf("    FAIL: %s does nothing when pressed\n", t[i].what);
                bad = 1;
            }
        }
        /* and back to where they were */
        db_set_show_punct(1); show_punct = 1;
        db_set_guided(0);     guided_on = 0;
        db_set_text_scale(100); text_scale = 100;
        db_set_new_limit(MODE_WRITE, 5);
        db_set_new_limit(MODE_READ, 15);
        printf("    every settings control changes its setting\n");
        scr = SCR_HOME;
    }

    /* A tick box must toggle, not open the row behind it. hottest() returns
     * the last registered rect, so a child has to be registered after its
     * parent -- and a refactor once reversed that, making every tick a tap. */
    {
        scr = SCR_HOME; scroll = 0; practice_active = 0;
        cur_cat = -1;                       /* the top of home, whatever was open */
        nbooks = db_books(books, MAXBOOKS);
        draw_home();
        /* A row is a book now, and the first one may be a text with no decks
         * of its own; the ticks belong to the decks, so find a row that has
         * some rather than assuming row 0 does. */
        /* rows are home's rows now (books and categories), not srcs */
        int ti = -1;
        for (int k = 0; k < nhrows; k++)
            if (hrows[k].kind == HR_BOOK && srcs[hrows[k].idx].book_i >= 0) { ti = k; break; }
        int bx = ti >= 0 ? srcs[hrows[ti].idx].book_i : 0;
        if (ti < 0) { printf("    FAIL: no row on the home screen has decks\n"); bad = 1; }
        int before = books[bx].enabled[MODE_WRITE];
        if (!press(H_TICK + ti)) { printf("    FAIL: no 写 tick box on the home row\n"); bad = 1; }
        nbooks = db_books(books, MAXBOOKS); build_sources();
        int after = books[bx].enabled[MODE_WRITE];
        printf("    写 tick on %s: %d -> %d, screen %s\n",
               books[bx].name, before, after, scr_name(scr));
        if (scr != SCR_HOME) { printf("    FAIL: ticking opened the row\n"); bad = 1; }
        if (after == before) { printf("    FAIL: tick did not change\n"); bad = 1; }

        draw_home();
        int rbefore = books[bx].enabled[MODE_READ];
        press(H_TICK_R + ti);
        nbooks = db_books(books, MAXBOOKS); build_sources();
        if (scr != SCR_HOME) { printf("    FAIL: 读 tick opened the row\n"); bad = 1; }
        if (books[bx].enabled[MODE_READ] == rbefore) { printf("    FAIL: 读 tick did not change\n"); bad = 1; }
        if (books[bx].enabled[MODE_WRITE] != after) { printf("    FAIL: 读 tick changed 写\n"); bad = 1; }

        /* A category's tick sets every book in it; a second press clears them. */
        {
            int ck = -1;
            for (int k = 0; k < nhrows; k++) if (hrows[k].kind == HR_CAT) { ck = k; break; }
            if (ck >= 0) {
                int c = hrows[ck].idx, en0[2], en1[2], en2[2];
                category_ticks(c, en0);
                /* the row may be on a later page (a resume row, the Clara) */
                scroll = 0; draw_home();
                while (ck >= scroll + page_step && press(H_MORE)) draw_home();
                if (!press(H_TICK + ck)) { printf("    FAIL: no tick box on the category row\n"); bad = 1; }
                category_ticks(c, en1);
                draw_home();
                press(H_TICK + ck);
                category_ticks(c, en2);
                scroll = 0;
                printf("    写 tick on category %s: %d -> %d -> %d\n", CATEGORY[c], en0[0], en1[0], en2[0]);
                if (en1[0] == en0[0] && en0[0] != 2) { printf("    FAIL: category tick did not change\n"); bad = 1; }
                if (en1[0] == 2) { printf("    FAIL: category tick left its books mixed\n"); bad = 1; }
                if (scr != SCR_HOME || cur_cat != -1) { printf("    FAIL: category tick opened it\n"); bad = 1; }
                draw_home();
            }
        }

        /* and the row itself must still open the book when tapped away from
         * the boxes, and the book page must lead on to both of its halves */
        draw_home();
        press(H_ITEM + ti);
        if (scr != SCR_BOOK) { printf("    FAIL: tapping the row no longer opens the book\n"); bad = 1; }
        else {
            draw_book();
            if (!press(H_DECKS) || scr != SCR_LISTS) {
                printf("    FAIL: the book page does not reach its decks\n"); bad = 1; }
            if (srcs[hrows[ti].idx].text_i >= 0) {
                scr = SCR_BOOK; draw_book();
                if (!press(H_TREAD) || scr != SCR_TEXTCH) {
                    printf("    FAIL: the book page does not reach its text\n"); bad = 1; }
                else if (!press(H_BACK) || scr != SCR_BOOK) {
                    printf("    FAIL: back from the chapters leaves the book\n"); bad = 1; }
            }
        }
        scr = SCR_HOME; cur_src_name[0] = 0;
        db_reset_progress(NULL, -1);
    }

    /* A repeat at the end of a test must not touch the schedule again: the
     * first answer is what counts, the repeat is only to make you produce it. */
    {
        mode = MODE_WRITE; list_id = 0; scope_book[0] = 0;
        snprintf(list_label, sizeof list_label, "all decks");
        db_reset_progress(NULL, -1);
        start_test();
        if (have_card) {
            char key[96];
            snprintf(key, sizeof key, "%s", card.key);
            grade(1);                         /* wrong: schedules it */
            cardrow_t after_first;
            db_card_by_key(key, mode, &after_first);
            long due1 = after_first.c.due;
            int  reps1 = after_first.c.reps, lapses1 = after_first.c.lapses;

            /* work to the repeat of that same card and answer it correctly */
            int guard = 0;
            while (have_card && strcmp(card.key, key) != 0 && guard++ < 100) grade(3);
            if (have_card && strcmp(card.key, key) == 0) {
                grade(3);
                cardrow_t after_repeat;
                db_card_by_key(key, mode, &after_repeat);
                printf("    repeat of a failed card: due %ld -> %ld, reps %d -> %d\n",
                       due1, after_repeat.c.due, reps1, after_repeat.c.reps);
                if (after_repeat.c.due != due1 || after_repeat.c.reps != reps1
                    || after_repeat.c.lapses != lapses1) {
                    printf("    FAIL: the repeat changed the schedule\n");
                    bad = 1;
                }
            } else {
                printf("    FAIL: the failed card never came back\n");
                bad = 1;
            }
        }
        db_session_clear();
        db_reset_progress(NULL, -1);
    }

    /* Reviewing ahead must not move a single due date. */
    {
        mode = MODE_WRITE; list_id = 0; scope_book[0] = 0;
        int ahead = db_ahead_available(0, NULL, mode, today());
        printf("    %d cards available to review ahead\n", ahead);
        if (ahead > 0) {
            char keys[TEST_WRITE][96];
            long dues[TEST_WRITE];
            int  nk = 0;
            start_session(0, 0);
            db_session_load(&sess);
            if (!sess.scored) printf("    session is marked not-scored\n");
            else { printf("    FAIL: review-ahead session is scored\n"); bad = 1; }
            while (have_card && nk < TEST_WRITE) {
                snprintf(keys[nk], 96, "%s", card.key);
                dues[nk] = card.c.due;
                nk++;
                grade(1);                    /* deliberately answer wrong */
            }
            int moved = 0;
            for (int i = 0; i < nk; i++) {
                cardrow_t chk;
                if (db_card_by_key(keys[i], mode, &chk) && chk.c.due != dues[i]) moved++;
            }
            printf("    answered %d wrong ahead of time; due dates moved: %d\n", nk, moved);
            if (moved) { printf("    FAIL: reviewing ahead changed the schedule\n"); bad = 1; }
            db_session_clear();
        }
    }
    /* "later" in a test is neither right nor wrong. It was marked right; and
     * with every redo ending right, the done screen's total - correct was 0
     * and said "all correct first time" whatever had happened. */
    {
        mode = MODE_READ; list_id = 0; scope_book[0] = 0;
        snprintf(list_label, sizeof list_label, "all decks");
        db_reset_progress(NULL, -1);
        start_test();
        if (have_card && sess.total >= 3) {
            postpone();                           /* one for later */
            grade(1);                             /* one wrong */
            for (int guard = 0; have_card && guard < 200; guard++) grade(3);
            db_session_load(&sess);
            printf("    a test with one put off and one wrong: %d cards, %d right,"
                   " %d for later, %d needed a second go\n",
                   sess.total, sess.correct, sess.later, sess.wrong);
            if (sess.later != 1 || sess.correct != sess.total - 1) {
                printf("    FAIL: a card put off counts as right\n"); bad = 1; }
            if (sess.wrong != 1) {
                printf("    FAIL: a wrong answer is forgotten once it is redone\n"); bad = 1; }
            if (scr != SCR_DONE) { printf("    FAIL: the test did not finish\n"); bad = 1; }
        } else {
            printf("    FAIL: no test to put a card off in\n"); bad = 1;
        }
        in_test = 0;
        db_session_clear();
        db_reset_progress(NULL, -1);
        scr = SCR_HOME;
    }
    /* Every text in exactly one of home's categories -- the Kobo's collections
     * -- and every title a category names a text: the lists are exact titles,
     * so a book the build adds, or one renamed, would otherwise be a loose
     * row at the top or a category quietly a book short. The top of home is
     * the shortcuts, then the categories; the 青少年版 are only in theirs. */
    {
        int loose = 0, twice = 0, ghost = 0, per[NCATEGORY] = {};
        for (int k = 0; k < ntexts; k++) {
            if (is_edition(texts[k].title)) continue;   /* filed with its book */
            int in = 0;
            for (int c = 0; c < NCATEGORY; c++)
                for (int j = 0; j < CAT_MAX && CAT_BOOKS[c][j]; j++)
                    if (!strcmp(texts[k].title, CAT_BOOKS[c][j])) { in++; per[c]++; }
            if (in == 0) { printf("    FAIL: %s is in no category\n", texts[k].title); loose++; }
            if (in > 1)  { printf("    FAIL: %s is in %d categories\n", texts[k].title, in); twice++; }
        }
        for (int c = 0; c < NCATEGORY; c++)
            for (int j = 0; j < CAT_MAX && CAT_BOOKS[c][j]; j++) {
                int found = 0;
                for (int k = 0; k < ntexts; k++) found |= !strcmp(texts[k].title, CAT_BOOKS[c][j]);
                if (!found) { printf("    FAIL: %s names %s, which is no text\n",
                                     CATEGORY[c], CAT_BOOKS[c][j]); ghost++; }
            }
        char line[400] = "";
        for (int c = 0; c < NCATEGORY; c++) {
            size_t l = strlen(line);
            snprintf(line + l, sizeof line - l, "%s%s %d", c ? ", " : "", CATEGORY[c], per[c]);
        }
        printf("    categories: %s\n", line);
        if (loose || twice || ghost) bad = 1;

        scr = SCR_HOME; cur_cat = -1; scroll = 0;
        nbooks = db_books(books, MAXBOOKS);
        build_sources(); build_hrows();
        int cats = 0, want_cats = 0;
        for (int k = 0; k < nhrows; k++) cats += hrows[k].kind == HR_CAT;
        for (int c = 0; c < NCATEGORY; c++) want_cats += per[c] > 0;
        if (cats != want_cats) { printf("    FAIL: home shows %d categories, %d hold a text\n", cats, want_cats); bad = 1; }
        for (int k = 0; k + 1 < nhrows && hrows[k].kind == HR_BOOK; k++) {
            int r = shortcut_rank(srcs[hrows[k].idx].name);
            if (r < 0) break;
            if (r != k) { printf("    FAIL: shortcut %d on home is %s\n", k + 1, srcs[hrows[k].idx].name); bad = 1; }
        }
    }

    /* Settings in the middle of a test, from the card: back is the same card
     * of the same test, nothing answered. A reset from there ends the test
     * (db_reset_progress clears the session), so back from settings leaves
     * it the way the test's own back does, to where it was begun -- not to
     * a card of a test that no longer exists. */
    {
        mode = MODE_READ; list_id = 0; scope_book[0] = 0;
        snprintf(list_label, sizeof list_label, "all decks");
        db_reset_progress(NULL, -1);
        scr = SCR_HOME; scroll = 0; practice_active = 0;
        start_test();
        if (!have_card || scr != SCR_REVIEW) {
            printf("    FAIL: no test to open settings in\n"); bad = 1;
        } else {
            char key[96];
            snprintf(key, sizeof key, "%s", card.key);
            int pending = sess.pending;
            render();
            if (!press(H_HDRSET) || scr != SCR_SETTINGS || !press(H_BACK)
                || scr != SCR_REVIEW || !in_test || strcmp(card.key, key)) {
                printf("    FAIL: settings mid-test did not come back to the card\n"); bad = 1;
            }
            db_session_load(&sess);
            if (sess.pending != pending) {
                printf("    FAIL: settings mid-test answered a card (%d -> %d left)\n",
                       pending, sess.pending);
                bad = 1;
            }
            render();
            press(H_HDRSET);
            int ok = scr == SCR_SETTINGS && press(H_RESET) && scr == SCR_RESET
                  && press(H_RESET_ALL) && press(H_RESET_ALL)     /* asked, then confirmed */
                  && press(H_BACK) && scr == SCR_SETTINGS && press(H_BACK);
            printf("    settings mid-test, and back: the same card; reset from there,"
                   " back, back: %s (in a test: %d)\n", scr_name(scr), in_test);
            if (!ok || scr != SCR_HOME || in_test || db_session_load(&sess)) {
                printf("    FAIL: a reset from a test did not leave the test for home\n");
                bad = 1;
            }
        }
        in_test = 0;
        db_session_clear();
        db_reset_progress(NULL, -1);
        scr = SCR_HOME;
    }

    /* A reset from settings over a deck's word, and back to the deck's word
     * list: the list shows what the reset left, not the cached words[] --
     * the study screen's back once went to the list without reloading it. */
    {
        nbooks = db_books(books, MAXBOOKS);
        nlists = nbooks ? db_lists(books[0].name, lists, MAXLISTS) : 0;
        if (!nlists) { printf("    FAIL: no deck to reset a word of\n"); bad = 1; }
        else {
            words_list = lists[0].id;
            snprintf(words_label, sizeof words_label, "%s", lists[0].chapter);
            scroll = 0;
            load_words();
            static cardrow_t cr;
            static wordrow_t fresh;
            db_card_by_key(words[0].headword, MODE_READ, &cr);
            snprintf(cr.key, sizeof cr.key, "%s", words[0].headword);
            srs_review(&cr.c, 3, today());
            db_save_card(&cr, MODE_READ);
            load_words();
            int before = words[0].st[MODE_READ];
            study_open(0); render();
            int ok = press(H_HDRSET) && scr == SCR_SETTINGS && press(H_RESET) && scr == SCR_RESET
                  && press(H_RESET_ALL) && press(H_RESET_ALL)     /* asked, then confirmed */
                  && press(H_BACK) && scr == SCR_SETTINGS && press(H_BACK) && scr == SCR_STUDY
                  && press(H_BACK) && scr == SCR_WORDS;
            db_list_words(words_list, 0, 1, today(), &fresh);
            printf("    a word's reset from settings over its study screen, back to the list:"
                   " %s, read state %d -> %d (database %d)\n", scr_name(scr), before,
                   words[0].st[MODE_READ], fresh.st[MODE_READ]);
            if (!ok || !before || words[0].st[MODE_READ] != fresh.st[MODE_READ]) {
                printf("    FAIL: the word list kept the state from before the reset\n");
                bad = 1;
            }
        }
        db_reset_progress(NULL, -1);
        scroll = 0;
        scr = SCR_HOME;
    }
    pdf_fixture_drop();     /* dropped above already, unless something returned early */
    printf(bad ? "    -> FAILED\n" : "    -> test sessions behave\n");
    /* the exit status: a count of failures could be a multiple of 256,
     * which exits 0 and passes */
    return bad ? 1 : 0;
}

/* Every main screen, drawn headless to PGM files in DIR at this device's size, with
 * the real hit-testing: shot() fails anything too small to press, press()
 * drives the screens as a tap would. */
void app_shot(int nopen, const char *dir) {
    app_setup(nopen);
    pdf_fixture();          /* any PDF scan reads the fixture, never the machine's */
    int bad = 0;
    auto save = [&](const char *name) {
        render();
        char p[512];
        snprintf(p, sizeof p, "%s/%s.pgm", dir, name);
        shot(p);
    };
    auto must = [&](int ok, const char *what) {
        if (!ok) { fprintf(stderr, "  FAIL: %s\n", what); bad++; }
    };
    auto home = [&]() {
        scr = SCR_HOME; cur_cat = -1; scroll = 0; in_test = 0; toast[0] = 0;
        reload_books();
    };

    for (int i = 0; i < nbooks; i++)
        if (!strcmp(books[i].name, "HSK 3.1")) {
            nlists = db_lists(books[i].name, lists, MAXLISTS);
            db_deck_set(lists[0].book, lists[0].chapter, MODE_WRITE, 1);
            db_deck_set(lists[0].book, lists[0].chapter, MODE_READ,  1);
        }
    home();
    save("home");
    bad += settings_trip("home");

    /* cards */
    for (int m = 0; m < 2; m++) {
        mode = m; list_id = 0; scope_book[0] = 0; in_test = 0; revealed = 0;
        snprintf(list_label, sizeof list_label, "all decks");
        scr = SCR_REVIEW;
        must(load_card(), "no card to show");
        save(m == MODE_WRITE ? "write-card" : "read-card");
        revealed = 1;
        save(m == MODE_WRITE ? "write-checked" : "read-checked");
    }

    /* a book: its page, a chapter, a sentence, writing it */
    home();
    snprintf(cur_src_name, sizeof cur_src_name, "道德经");
    scr = SCR_BOOK;
    save("book");
    must(press(H_TREAD) && scr == SCR_TEXTCH, "a book does not open its chapters");
    save("chapters");
    must(press(H_ITEM + 0) && scr == SCR_TEXTREAD, "a chapter does not open");
    save("text-chapter");
    must(press(H_TSENT) && press(H_TLINE + 0) && scr == SCR_TEXTLINE, "a sentence does not open");
    save("text-sentence");
    tw_load(0);
    scr = SCR_TEXTWRITE;
    save("text-write");

    /* dictionary, a word, a character, drawing one */
    home();
    scr = SCR_DICT; q[0] = 0; nres = 0;
    save("dictionary");
    setup_practice();
    save("practice");
    practice_active = 0;
    home();
    scr = SCR_DRAW; ui_ink_clear(&hw_ink); hw_n = 0; hw_built[0] = 0;
    strokes_t hs;
    if (db_strokes("好", &hs) > 0) {
        rect_t pad = hw_pad();
        float sc = (float)pad.w / 1024.0f;
        for (int i = 0; i < hs.n; i++)
            for (int j = 0; j < hs.nmed[i]; j++) {
                int x = (int)(pad.x + hs.med[i][j*2] * sc), y = (int)(pad.y + hs.med[i][j*2 + 1] * sc);
                if (j == 0) ui_ink_down(&hw_ink, x, y); else ui_ink_move(&hw_ink, x, y);
            }
        db_strokes_free(&hs);
        hw_recognise();
    }
    save("draw");
    must(hw_n > 0, "a drawn character offers nothing");

    home();
    scr = SCR_SETTINGS;
    save("settings");

    if (!pdf_fix_skip) { bad += pdf_screens_check(dir); bad += pdf_read_check(dir); }
    fprintf(stderr, "  %d press-path failures\n", bad);
    pdf_fixture_drop();
}
