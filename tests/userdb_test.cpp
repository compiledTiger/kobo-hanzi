/* What user.db must keep. A handwritten page whose sentence moved away is
 * hidden, not gone: writing or clearing the page the line now shows used to
 * overwrite or delete it, as both were keyed on the line alone. And a list
 * exported to Pleco comes out as long as it went in: the export read through
 * dictrow_t's fixed fields and clipped what the import had kept whole.
 *
 *   userdb_test content.db fresh-user.db scratch-dir */
#include "../src/core/db.h"
#include "../src/vendor/sqlite3.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

/* rows in user.db's line_ink, hidden ones included, which db.h cannot see */
static int ink_rows(const char *user, int chap) {
    sqlite3 *u = NULL;
    int n = -1;
    if (sqlite3_open_v2(user, &u, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        sqlite3_stmt *q = NULL;
        if (sqlite3_prepare_v2(u, "SELECT COUNT(*) FROM line_ink WHERE chapter_id=?1",
                               -1, &q, NULL) == SQLITE_OK) {
            sqlite3_bind_int(q, 1, chap);
            if (sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0);
        }
        sqlite3_finalize(q);
    }
    sqlite3_close(u);
    return n;
}

static int shows(int chap, const char *want) {
    char buf[64];
    int w = 0, h = 0, n = db_line_ink_get(chap, 0, buf, (int)sizeof buf, &w, &h);
    if (!want) return n == 0;
    return n == (int)strlen(want) && !memcmp(buf, want, (size_t)n);
}

int main(int argc, char **argv) {
    if (argc < 4) { printf("    FAIL: usage\n"); return 1; }
    /* a user.db from before pages were keyed on their sentence */
    unlink(argv[2]);
    {
        sqlite3 *u = NULL;
        sqlite3_open(argv[2], &u);
        sqlite3_exec(u, "CREATE TABLE line_ink (chapter_id INTEGER NOT NULL,"
                        " line INTEGER NOT NULL, w INTEGER NOT NULL, h INTEGER NOT NULL,"
                        " ink BLOB NOT NULL, PRIMARY KEY (chapter_id, line));",
                     NULL, NULL, NULL);
        sqlite3_close(u);
    }
    /* a chapter's first line, from content.db */
    int chap = -1;
    std::string line0;
    {
        sqlite3 *c = NULL;
        sqlite3_stmt *q = NULL;
        if (sqlite3_open_v2(argv[1], &c, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK
            && sqlite3_prepare_v2(c, "SELECT chapter_id, text FROM text_lines WHERE ord=0"
                                     " ORDER BY chapter_id LIMIT 1", -1, &q, NULL) == SQLITE_OK
            && sqlite3_step(q) == SQLITE_ROW) {
            chap = sqlite3_column_int(q, 0);
            line0 = (const char *)sqlite3_column_text(q, 1);
        }
        sqlite3_finalize(q);
        sqlite3_close(c);
    }
    if (chap < 0) { printf("    FAIL: content.db has no text lines\n"); return 1; }
    {
        sqlite3 *u = NULL;
        sqlite3_open(argv[2], &u);
        char sql[160];
        snprintf(sql, sizeof sql, "INSERT INTO line_ink VALUES (%d, 0, 10, 10, 'legacy')", chap);
        sqlite3_exec(u, sql, NULL, NULL, NULL);
        sqlite3_close(u);
    }
    if (db_open(argv[1], argv[2]) != 0) { printf("    FAIL: no db\n"); return 1; }

    int ok = 1;
    struct { const char *what; int got; } c[8];
    int nc = 0;
    c[nc++] = { "an old page shows on its line after the migration", shows(chap, "legacy") };
    /* and a page written there before the sentence moved, hidden now */
    {
        sqlite3 *u = NULL;
        sqlite3_open(argv[2], &u);
        char sql[200];
        snprintf(sql, sizeof sql, "INSERT INTO line_ink VALUES"
                 " (%d, 0, 10, 10, 'moved', 'not the sentence there')", chap);
        if (sqlite3_exec(u, sql, NULL, NULL, NULL) != SQLITE_OK) ok = 0;
        sqlite3_close(u);
    }
    c[nc++] = { "both kept, the line's own shown", ok && shows(chap, "legacy") };
    db_line_ink_put(chap, 0, line0.c_str(), "mine", 4, 10, 10);
    c[nc++] = { "writing replaces the page shown", shows(chap, "mine") };
    db_line_ink_put(chap, 0, line0.c_str(), "again", 5, 10, 10);
    c[nc++] = { "and again", shows(chap, "again") };
    c[nc++] = { "but not the hidden page", ink_rows(argv[2], chap) == 2 };
    db_line_ink_put(chap, 0, line0.c_str(), NULL, 0, 0, 0);
    c[nc++] = { "clearing deletes the page shown", shows(chap, NULL) };
    c[nc++] = { "but not the hidden one", ink_rows(argv[2], chap) == 1 };
    for (int i = 0; i < nc; i++) {
        if (!c[i].got) { printf("    FAIL: %s\n", c[i].what); ok = 0; }
    }

    /* a long entry, through an import and back out: longer than dictrow_t's
     * pinyin and defn, shorter than the import's 2048-byte line */
    std::string defn, py;
    for (int i = 0; i < 115; i++) defn += "a long gloss ";
    for (int i = 0; i < 40; i++) py += "zhang1 ";
    while (!py.empty() && py.back() == ' ') py.pop_back();
    while (!defn.empty() && defn.back() == ' ') defn.pop_back();
    std::string in = std::string(argv[3]) + "/in", out = std::string(argv[3]) + "/out";
    mkdir(argv[3], 0755);
    mkdir(in.c_str(), 0755);
    mkdir(out.c_str(), 0755);
    std::string entry = "\351\225\277\t" + py + "\t" + defn;       /* 长 */
    {
        FILE *f = fopen((in + "/long.txt").c_str(), "w");
        if (!f) { printf("    FAIL: cannot write the import\n"); return 1; }
        fprintf(f, "//long\n%s\n", entry.c_str());
        fclose(f);
    }
    int nf = 0, nl = 0;
    db_import_pleco_dir(in.c_str(), &nf, &nl);
    db_export_pleco(out.c_str(), &nf);
    std::string got;
    {
        FILE *f = fopen((out + "/long.txt").c_str(), "r");
        if (f) {
            char b[4096];
            while (fgets(b, sizeof b, f)) got += b;
            fclose(f);
        }
    }
    if (got != "//long\n" + entry + "\n") {
        printf("    FAIL: the export is %zu bytes, the import %zu\n",
               got.size(), entry.size() + 8);
        ok = 0;
    }
    /* PDFs: pins by path (long, with spaces and hanzi), the last read
     * first; a spot kept for a pin only, unchanged writes skipped, and
     * forgotten with the pin. */
    {
        std::string a = std::string(200, 'd') + "/\344\270\211\345\233\275 vol 1.pdf";   /* 三国 */
        std::string b = "/mnt/onboard/books/b.pdf";
        struct { const char *what; int got; } p[16];
        int np = 0;
        p[np++] = { "nothing pinned at first", db_pdf_pins().empty() && !db_pdf_pinned(a.c_str()) };
        pdf_spot_t s0 = { 4, 1, 2, 3, 90, 0 }, got = { 0, 0, 0, 0, 0, 0 };
        p[np++] = { "no spot kept for a PDF not pinned",
                    !db_pdf_set_spot(a.c_str(), &s0) && !db_pdf_spot(a.c_str(), &got) };
        db_pdf_pin(a.c_str(), 1, 100);
        db_pdf_pin(b.c_str(), 1, 200);
        db_pdf_pin(b.c_str(), 1, 300);          /* twice is once */
        std::vector<std::string> pins = db_pdf_pins();
        p[np++] = { "two pins, the newer first",
                    pins.size() == 2 && pins[0] == b && pins[1] == a };
        db_pdf_used(a.c_str(), 400);
        pins = db_pdf_pins();
        p[np++] = { "the one read last comes first", pins.size() == 2 && pins[0] == a };
        p[np++] = { "a spot is kept for a pin", db_pdf_set_spot(a.c_str(), &s0) == 1 };
        p[np++] = { "and read back whole",
                    db_pdf_spot(a.c_str(), &got) && got.page == 4 && got.zoom == 1
                    && got.part == 2 && got.trim == 3 && got.rot == 90 && got.flip == 0 };
        pdf_spot_t s1 = s0;
        s1.page = 5; s1.flip = 1;
        db_pdf_set_spot(a.c_str(), &s1);
        p[np++] = { "a page turned moves it",
                    db_pdf_spot(a.c_str(), &got) && got.page == 5 && got.flip == 1 };
        p[np++] = { "the same spot again is no change", db_pdf_set_spot(a.c_str(), &s1) == 1 };
        p[np++] = { "the other pin has none", !db_pdf_spot(b.c_str(), &got) };
        db_pdf_pin(a.c_str(), 0, 500);
        p[np++] = { "unpinned: no pin and no spot",
                    !db_pdf_pinned(a.c_str()) && !db_pdf_spot(a.c_str(), &got)
                    && db_pdf_pins().size() == 1 };
        db_pdf_pin(b.c_str(), 0, 600);
        p[np++] = { "all unpinned", db_pdf_pins().empty() };
        for (int i = 0; i < np; i++)
            if (!p[i].got) { printf("    FAIL: pdf: %s\n", p[i].what); ok = 0; }
        if (ok) printf("    -> PDF pins and reading spots: %d checks\n", np);
    }
    /* Headwords of your own longer than a row carries. db_user_add takes
     * none of 96 bytes or more (a card's key); one already there from
     * before is left out of a test rather than holding it open forever,
     * and is removed by the cut name the list view shows. */
    {
        struct { const char *what; int got; } k[12];
        int nk = 0;
        std::string h31, h40;
        for (int i = 0; i < 31; i++) h31 += "\345\245\275";          /* 好 x31: 93 bytes */
        for (int i = 0; i < 40; i++) h40 += "\351\225\277";          /* 长 x40: 120 */
        int lid = db_user_list_create("long keys");
        k[nk++] = { "a list for them", lid >= 0 };
        k[nk++] = { "31 hanzi are taken", db_user_add(lid, h31.c_str(), "hao", "good") == 0 };
        k[nk++] = { "40 hanzi are not", db_user_add(lid, h40.c_str(), "chang", "long") == -1 };
        dictrow_t rows[4];
        int n = db_user_entries(lid, 0, 4, rows);
        k[nk++] = { "31 hanzi listed whole", n == 1 && h31 == rows[0].simp };
        k[nk++] = { "and removed by it", n == 1 && db_user_entry_delete(lid, rows[0].simp) == 0
                                         && db_user_entry_count(lid) == 0 };
        k[nk++] = { "removing what is not there says so", db_user_entry_delete(lid, h31.c_str()) == -1 };
        /* one from before the limit, written straight into user.db */
        {
            sqlite3 *u = NULL;
            sqlite3_open(argv[2], &u);
            sqlite3_stmt *q = NULL;
            sqlite3_prepare_v2(u, "INSERT INTO user_entries (list_id,ordinal,headword,pinyin,defn,writable)"
                                  " VALUES (?1,0,?2,'chang','long',1)", -1, &q, NULL);
            sqlite3_bind_int(q, 1, lid);
            sqlite3_bind_text(q, 2, h40.c_str(), -1, SQLITE_TRANSIENT);
            k[nk++] = { "an old long one in place", sqlite3_step(q) == SQLITE_DONE };
            sqlite3_finalize(q);
            sqlite3_close(u);
        }
        db_user_add(lid, "\345\245\275", "hao3", "good");            /* 好 */
        /* a test of the list: the short card, and the long one left out */
        db_session_start(MODE_READ, USER_LIST_BASE + lid, NULL, "long keys", 20000, 10, 1, 1);
        cardrow_t r;
        int cards = 0, shown_long = 0;
        while (cards < 10 && db_session_next(&r)) {
            cards++;
            if (strlen(r.key) > 3) shown_long = 1;
            db_session_mark(r.key, 1);
        }
        k[nk++] = { "a test with it ends, the long one left out", cards == 1 && !shown_long };
        db_session_clear();
        n = db_user_entries(lid, 0, 4, rows);
        int at = -1;
        for (int i = 0; i < n; i++) if (strlen(rows[i].simp) > 3) at = i;
        k[nk++] = { "the old one is removed by its cut name",
                    at >= 0 && db_user_entry_delete(lid, rows[at].simp) == 0
                    && db_user_entry_count(lid) == 1 };
        for (int i = 0; i < nk; i++)
            if (!k[i].got) { printf("    FAIL: long keys: %s\n", k[i].what); ok = 0; }
        if (ok) printf("    -> headwords longer than a card holds: %d checks\n", nk);
    }
    /* the desktop's window mode */
    {
        int was = db_window_mode();
        db_set_window_mode(2);
        int now = db_window_mode();
        db_set_window_mode(was < 0 ? 0 : was);
        if (was != -1 || now != 2) { printf("    FAIL: window mode %d then %d\n", was, now); ok = 0; }
    }
    if (ok) printf("    -> pages keep to their sentence, and a list exports as it came in\n");
    return ok ? 0 : 1;
}
