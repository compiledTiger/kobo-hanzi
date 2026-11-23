#include "db.h"
#include "../vendor/sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#include <utility>
#include <memory>
#include <string>
#include <vector>

/* Owns a prepared statement.
 *
 * The prepare/step/finalize dance below appears about sixty times, and the
 * finalize is what releases the statement's read lock -- miss one and later
 * writes fail with SQLITE_BUSY on a device with no way to see the error.
 * Tying it to scope means a `return` added later cannot skip it.
 *
 * The implicit conversion back to a raw statement is deliberate: every
 * existing bind and column call keeps working unchanged, which kept the
 * conversion of the call sites mechanical and reviewable. */
/* The same argument as Stmt, for the two other kinds of handle here. */
struct FileCloser { void operator()(FILE *f) const { if (f) fclose(f); } };
struct DirCloser  { void operator()(DIR *d) const  { if (d) closedir(d); } };
using FilePtr = std::unique_ptr<FILE, FileCloser>;
using DirPtr  = std::unique_ptr<DIR, DirCloser>;

class Stmt {
public:
    Stmt() = default;
    ~Stmt() { sqlite3_finalize(st_); }
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;

    /* Re-preparing finalises whatever was held, so reusing one Stmt for a
     * second query in the same function is safe. */
    bool prepare(sqlite3 *db, const char *sql) {
        sqlite3_finalize(st_);
        st_ = nullptr;
        return sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) == SQLITE_OK;
    }
    bool step() { return sqlite3_step(st_) == SQLITE_ROW; }
    explicit operator bool() const { return st_ != nullptr; }
    operator sqlite3_stmt *() const { return st_; }

private:
    sqlite3_stmt *st_ = nullptr;
};

static sqlite3 *D = NULL;
/* the source of notes the queries read, and whether content.db has the
 * source column at all (-1: not looked yet); see db_set_note_source */
static char s_note_src[16] = "";
static int  s_have_source = -1;
/* whether content.db's notes tables have app_only (-1: not looked yet) */
static int  s_have_app_only = -1;
static char s_err[256] = {0};
const char *db_err(void) { return s_err; }

static const char *USER_SCHEMA =
  "CREATE TABLE IF NOT EXISTS srs ("
  "  key TEXT NOT NULL, mode INTEGER NOT NULL,"
  "  stability REAL, difficulty REAL, reps INTEGER, lapses INTEGER,"
  "  state INTEGER, due INTEGER, last INTEGER,"
  "  introduced INTEGER NOT NULL DEFAULT 0,"
  "  PRIMARY KEY (key, mode)) WITHOUT ROWID;"
  "CREATE INDEX IF NOT EXISTS srs_due ON srs(mode, due);"
  "CREATE TABLE IF NOT EXISTS user_lists ("
  "  id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
  "CREATE TABLE IF NOT EXISTS user_entries ("
  "  id INTEGER PRIMARY KEY, list_id INTEGER NOT NULL, ordinal INTEGER NOT NULL,"
  "  headword TEXT NOT NULL, pinyin TEXT DEFAULT '', defn TEXT DEFAULT '',"
  "  writable INTEGER NOT NULL DEFAULT 0,"
  "  UNIQUE(list_id, headword));"
  "CREATE INDEX IF NOT EXISTS ue_list ON user_entries(list_id, ordinal);"
  "CREATE TABLE IF NOT EXISTS deck_new ("
  "  key TEXT NOT NULL, mode INTEGER NOT NULL,"
  "  enabled INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (key, mode));"
  /* One test at a time. Written to on every answer so a quit loses nothing. */
  "CREATE TABLE IF NOT EXISTS settings (k TEXT PRIMARY KEY, v INTEGER NOT NULL);"
  /* Your own handwritten page on a sentence: packed ink, and the size of
   * the pad it was written on. */
  "CREATE TABLE IF NOT EXISTS line_ink ("
  "  chapter_id INTEGER NOT NULL, line INTEGER NOT NULL,"
  "  w INTEGER NOT NULL, h INTEGER NOT NULL, ink BLOB NOT NULL,"
  "  line_text TEXT,"                  /* the sentence it was written on */
  /* keyed on it too: a page whose sentence moved away keeps its row while
   * the sentence now on that line gets a page of its own */
  "  PRIMARY KEY (chapter_id, line, line_text));"
  /* How far through a text chapter you had written it out. */
  "CREATE TABLE IF NOT EXISTS text_pos ("
  "  chapter_id INTEGER PRIMARY KEY, line INTEGER NOT NULL);"
  /* And where you were reading it, which moves independently: you write a
   * chapter you have already read, and you read on past what you have
   * written. text_last is the chapter to come back to in each book. */
  "CREATE TABLE IF NOT EXISTS text_read ("
  "  chapter_id INTEGER PRIMARY KEY, line INTEGER NOT NULL);"
  "CREATE TABLE IF NOT EXISTS text_last ("
  "  text_id INTEGER PRIMARY KEY, chapter_id INTEGER NOT NULL);"
  "CREATE TABLE IF NOT EXISTS session ("
  "  id INTEGER PRIMARY KEY CHECK (id = 1), mode INTEGER NOT NULL,"
  "  list_id INTEGER NOT NULL, book TEXT, label TEXT, started INTEGER,"
  "  scored INTEGER NOT NULL DEFAULT 1);"
  "CREATE TABLE IF NOT EXISTS session_item ("
  "  key TEXT PRIMARY KEY, ord INTEGER NOT NULL,"
  /* state: 0 not yet answered, 1 right, 2 wrong and waiting to come back,
   * 3 put off ("later"). answered: 0 not yet, 1 answered, 2 answered wrong
   * at least once. 3 and 2 came later, in the same columns, so a test in
   * progress from before reads the same and an older app skips them. */
  "  state INTEGER NOT NULL DEFAULT 0, answered INTEGER NOT NULL DEFAULT 0);"
  /* Re-importing the same folder used to pile up duplicate empty lists.
   * Fold any that already exist into the lowest id, then make it impossible. */
  /* OR IGNORE: the same word can exist in both duplicates, and UNIQUE(list_id,
   * headword) would otherwise abort the whole schema script and leave the
   * database unopenable on every subsequent launch. */
  "UPDATE OR IGNORE user_entries SET list_id = (SELECT MIN(u2.id) FROM user_lists u2"
  "   WHERE u2.name = (SELECT u1.name FROM user_lists u1 WHERE u1.id = list_id))"
  " WHERE EXISTS (SELECT 1 FROM user_lists u1 WHERE u1.id = list_id);"
  /* whatever OR IGNORE skipped is a duplicate of a row that did move */
  "DELETE FROM user_entries WHERE list_id NOT IN (SELECT id FROM user_lists)"
  "   OR list_id IN (SELECT id FROM user_lists WHERE id NOT IN"
  "                  (SELECT MIN(id) FROM user_lists GROUP BY name));"
  "DELETE FROM user_lists WHERE id NOT IN"
  "  (SELECT MIN(id) FROM user_lists GROUP BY name);"
  "CREATE UNIQUE INDEX IF NOT EXISTS user_lists_name ON user_lists(name);"
  /* day a card was first scheduled, for the daily new-card allowance */
  "CREATE INDEX IF NOT EXISTS srs_intro ON srs(mode, introduced);"
  /* PDFs pinned on the PDF screen, by absolute path (the Kobo and the
   * desktop keep separate user.db files), with when each was last read so
   * the one in hand comes first; and a pinned PDF's reading spot: the page
   * and the view it was left at (db_pdf_spot). */
  "CREATE TABLE IF NOT EXISTS pdf_pin ("
  "  path TEXT PRIMARY KEY, added INTEGER NOT NULL, used INTEGER NOT NULL DEFAULT 0);"
  "CREATE TABLE IF NOT EXISTS pdf_spot ("
  "  path TEXT PRIMARY KEY, page INTEGER NOT NULL, zoom INTEGER NOT NULL,"
  "  part INTEGER NOT NULL, trim INTEGER NOT NULL, rot INTEGER NOT NULL,"
  "  flip INTEGER NOT NULL DEFAULT 0);"
  ;

/* One view over both deck sources, so every query below is written once.
 * Your own lists get ids above USER_LIST_BASE to keep them distinguishable. */
static const char *ALL_VIEW =
  "CREATE TEMP VIEW av AS"
  "  SELECT l.id AS list_id, l.book AS book, l.chapter AS chapter,"
  "         e.ordinal AS ordinal, e.headword AS headword, e.pinyin AS pinyin,"
  "         e.defn AS defn, e.key AS key, e.writable AS writable"
  "    FROM c.entries e JOIN c.lists l ON l.id = e.list_id"
  "  UNION ALL"
  "  SELECT 100000 + ul.id, 'My lists', ul.name,"
  "         ue.ordinal, ue.headword, ue.pinyin, ue.defn, ue.headword, ue.writable"
  "    FROM user_entries ue JOIN user_lists ul ON ul.id = ue.list_id;"
  /* The same lists without their words: one row per list rather than one
   * per word. Asking which decks are ticked, or which books have which
   * chapters, of `av` meant walking every word to find the lists -- 39,000
   * words and 400 lists then, the home screen's slowest query at 80 ms on
   * a desktop and several times that on the Kobo; 240,000 words and 2,900
   * lists now. Lists with no words are left out, as `av` leaves them out. */
  "CREATE TEMP VIEW al AS"
  "  SELECT l.id AS list_id, l.book AS book, l.chapter AS chapter"
  "    FROM c.lists l WHERE EXISTS (SELECT 1 FROM c.entries e WHERE e.list_id = l.id)"
  "  UNION ALL"
  "  SELECT 100000 + ul.id, 'My lists', ul.name FROM user_lists ul"
  "   WHERE EXISTS (SELECT 1 FROM user_entries ue WHERE ue.list_id = ul.id);";

/* A path as a read-only SQLite URI. ATTACH of a plain path opens the file
 * read-write, and SQLite may then write to it -- rolling back a journal it
 * finds, checkpointing a WAL on close -- which for content.db, and above all
 * for Nickel's own KoboReader.sqlite, is not ours to do. The three
 * characters that mean something in a URI are escaped. */
static std::string ro_uri(const char *path) {
    std::string u = "file:";
    for (const char *p = path; *p; p++) {
        if (*p == '%')      u += "%25";
        else if (*p == '?') u += "%3f";
        else if (*p == '#') u += "%23";
        else                u += *p;
    }
    return u + "?mode=ro";
}

static char s_content_path[512] = "";
const char *db_content_path(void) { return s_content_path; }

/* Your places in the books (text_pos, text_read, text_last, line_ink, the
 * comic_place: settings) and a test in progress (session) hold content.db
 * ids, which a rebuild may hand out differently. So user.db remembers what
 * each id it holds meant -- a chapter by its book's title and its place in
 * the book, a book by its title, a deck by book and chapter (id_keys) -- and
 * on opening, an id whose meaning content.db now gives another number is
 * moved to it. Two passes (negated, then back) so ids that swap never meet
 * in a primary key; OR REPLACE lets a moved place win over a row left from a
 * chapter that no longer exists. Any failure leaves user.db as it was. */
void db_apply_deck_renames(void);
static void remap_ids(void) {
    static const char *SQL =
        "BEGIN;"
        "CREATE TABLE IF NOT EXISTS id_keys (kind TEXT NOT NULL, id INTEGER NOT NULL,"
        "  title TEXT NOT NULL, ord INTEGER NOT NULL DEFAULT 0, chapter TEXT,"
        "  PRIMARY KEY (kind, id));"
        "DROP TABLE IF EXISTS id_remap;"
        "CREATE TABLE id_remap (kind TEXT NOT NULL, old INTEGER NOT NULL, new INTEGER NOT NULL,"
        "  PRIMARY KEY (kind, old));"
        /* a chapter a new edition no longer has: where its place goes, and how far in */
        "INSERT OR IGNORE INTO id_remap SELECT 'c', m.old, m.new FROM c.chapter_moves m"
        "  JOIN id_keys k ON k.kind='c' AND k.id=m.old AND k.title=m.title;"
        /* a chapter whose id is still its book's stays, whatever its place in the book now */
        "INSERT OR IGNORE INTO id_remap SELECT 'c', k.id, ch.id FROM id_keys k"
        "  JOIN c.texts t ON t.title=k.title JOIN c.text_chapters ch ON ch.text_id=t.id AND ch.ord=k.ord"
        "  WHERE k.kind='c' AND ch.id<>k.id AND NOT EXISTS"
        "  (SELECT 1 FROM c.text_chapters x WHERE x.id=k.id AND x.text_id=t.id);"
        "INSERT OR IGNORE INTO id_remap SELECT 't', k.id, t.id FROM id_keys k"
        "  JOIN c.texts t ON t.title=k.title WHERE k.kind='t' AND t.id<>k.id;"
        "INSERT OR IGNORE INTO id_remap SELECT 'l', k.id, l.id FROM id_keys k"
        "  JOIN c.lists l ON l.book=k.title AND l.chapter=k.chapter WHERE k.kind='l' AND l.id<>k.id;"
        "UPDATE text_pos SET chapter_id=-(SELECT new FROM id_remap WHERE kind='c' AND old=chapter_id),"
        "  line=line+IFNULL((SELECT line_add FROM c.chapter_moves m WHERE m.old=text_pos.chapter_id),0)"
        "  WHERE chapter_id IN (SELECT old FROM id_remap WHERE kind='c');"
        "UPDATE OR REPLACE text_pos SET chapter_id=-chapter_id WHERE chapter_id<0;"
        "UPDATE text_read SET chapter_id=-(SELECT new FROM id_remap WHERE kind='c' AND old=chapter_id),"
        "  line=line+IFNULL((SELECT line_add FROM c.chapter_moves m WHERE m.old=text_read.chapter_id),0)"
        "  WHERE chapter_id IN (SELECT old FROM id_remap WHERE kind='c');"
        "UPDATE OR REPLACE text_read SET chapter_id=-chapter_id WHERE chapter_id<0;"
        "UPDATE line_ink SET chapter_id=-(SELECT new FROM id_remap WHERE kind='c' AND old=chapter_id),"
        "  line=line+IFNULL((SELECT line_add FROM c.chapter_moves m WHERE m.old=line_ink.chapter_id),0)"
        "  WHERE chapter_id IN (SELECT old FROM id_remap WHERE kind='c');"
        "UPDATE OR REPLACE line_ink SET chapter_id=-chapter_id WHERE chapter_id<0;"
        "UPDATE text_last SET chapter_id=(SELECT new FROM id_remap WHERE kind='c' AND old=chapter_id)"
        "  WHERE chapter_id IN (SELECT old FROM id_remap WHERE kind='c');"
        "UPDATE text_last SET text_id=-(SELECT new FROM id_remap WHERE kind='t' AND old=text_id)"
        "  WHERE text_id IN (SELECT old FROM id_remap WHERE kind='t');"
        "UPDATE OR REPLACE text_last SET text_id=-text_id WHERE text_id<0;"
        "UPDATE settings SET k='comic_place_moved:'||(SELECT new FROM id_remap WHERE kind='c'"
        "    AND old=CAST(substr(k,13) AS INTEGER))"
        "  WHERE k LIKE 'comic_place:%' AND CAST(substr(k,13) AS INTEGER) IN"
        "    (SELECT old FROM id_remap WHERE kind='c');"
        "UPDATE OR REPLACE settings SET k='comic_place:'||substr(k,19) WHERE k LIKE 'comic_place_moved:%';"
        "UPDATE session SET list_id=(SELECT new FROM id_remap WHERE kind='l' AND old=list_id)"
        "  WHERE list_id IN (SELECT old FROM id_remap WHERE kind='l');"
        /* an id moved from means nothing now: it may be another book's chapter next */
        "DELETE FROM id_keys WHERE kind='c' AND id IN (SELECT old FROM id_remap WHERE kind='c');"
        "DROP TABLE id_remap;"
        /* and what the ids held now mean, for next time */
        "INSERT OR REPLACE INTO id_keys SELECT 'c', ch.id, t.title, ch.ord, NULL"
        "  FROM c.text_chapters ch JOIN c.texts t ON t.id=ch.text_id WHERE ch.id IN"
        "  (SELECT chapter_id FROM text_pos UNION SELECT chapter_id FROM text_read"
        "   UNION SELECT chapter_id FROM text_last UNION SELECT chapter_id FROM line_ink"
        "   UNION SELECT CAST(substr(k,13) AS INTEGER) FROM settings WHERE k LIKE 'comic_place:%');"
        "INSERT OR REPLACE INTO id_keys SELECT 't', t.id, t.title, 0, NULL FROM c.texts t"
        "  WHERE t.id IN (SELECT text_id FROM text_last);"
        "INSERT OR REPLACE INTO id_keys SELECT 'l', l.id, l.book, 0, l.chapter FROM c.lists l"
        "  WHERE l.id IN (SELECT list_id FROM session);"
        "COMMIT;";
    if (sqlite3_exec(D, SQL, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "db: id remap: %s\n", sqlite3_errmsg(D));
        sqlite3_exec(D, "ROLLBACK", NULL, NULL, NULL);
    }
    db_apply_deck_renames();
}

/* A deck's tick is kept by its name: a chapter a new edition retitled takes its
 * ticks along (build_db.py deck_renames). A tick already under the new name
 * stays as it is; a database without the table changes nothing. */
void db_apply_deck_renames(void) {
    int have = 0;
    {
        Stmt has;
        if (has.prepare(D, "SELECT 1 FROM c.sqlite_master WHERE type='table' AND name='deck_renames'"))
            have = has.step();
    }
    if (!have) return;
    if (sqlite3_exec(D,
            "BEGIN;"
            "UPDATE OR IGNORE deck_new SET key=(SELECT new FROM c.deck_renames r WHERE r.old=deck_new.key)"
            "  WHERE key IN (SELECT old FROM c.deck_renames);"
            "DELETE FROM deck_new WHERE key IN (SELECT old FROM c.deck_renames);"
            "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "db: deck renames: %s\n", sqlite3_errmsg(D));
        sqlite3_exec(D, "ROLLBACK", NULL, NULL, NULL);
    }
}

int db_open(const char *content_path, const char *user_path) {
    snprintf(s_content_path, sizeof s_content_path, "%s", content_path);
    /* user.db is main so it can be created and written; content.db rides
     * along read-only as `c`. Table names do not collide, so queries can
     * reference either unqualified. */
    if (sqlite3_open_v2(user_path, &D,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI,
                        NULL) != SQLITE_OK) {
        snprintf(s_err, sizeof s_err, "cannot open %s: %s", user_path, sqlite3_errmsg(D));
        return -1;
    }
    /* ATTACH on a read-write connection happily creates a missing file, which
     * turns a mistyped path into a silent empty app. Check first. */
    if (access(content_path, R_OK) != 0) {
        snprintf(s_err, sizeof s_err, "cannot read %s", content_path);
        return -1;
    }
    Stmt st;
    s_have_source = -1;              /* a new content.db: look again */
    s_have_app_only = -1;
    if (!st.prepare(D, "ATTACH DATABASE ?1 AS c")) {
        snprintf(s_err, sizeof s_err, "attach prepare: %s", sqlite3_errmsg(D));
        return -1;
    }
    std::string curi = ro_uri(content_path);
    sqlite3_bind_text(st, 1, curi.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        snprintf(s_err, sizeof s_err, "attach %s: %s", content_path, sqlite3_errmsg(D));
        return -1;
    }
    char *e = NULL;
    /* Older user.db files predate `introduced`; add it before the schema runs,
     * because the schema indexes it. Fails harmlessly when the table does not
     * exist yet or the column is already there. */
    if (sqlite3_exec(D, "ALTER TABLE srs ADD COLUMN introduced INTEGER NOT NULL DEFAULT 0",
                     NULL, NULL, NULL) == SQLITE_OK)
        /* Cards already studied were introduced some day before this: not
         * 0, which means "never", or the first review of each would be
         * stamped today and count against today's new cards. */
        sqlite3_exec(D, "UPDATE srs SET introduced=-1 WHERE introduced=0 AND state>0",
                     NULL, NULL, NULL);
    sqlite3_exec(D, "ALTER TABLE session ADD COLUMN scored INTEGER NOT NULL DEFAULT 1",
                 NULL, NULL, NULL);
    /* deck_new was once keyed on the deck alone. Split it per mode, applying
     * the old choice to both so nothing a user picked is lost. */
    {
        Stmt q;
        int has_mode = 0, has_table = 0;
        if (q.prepare(D, "PRAGMA table_info(deck_new)")) {
            while (sqlite3_step(q) == SQLITE_ROW) {
                has_table = 1;
                const unsigned char *col = sqlite3_column_text(q, 1);
                if (col && strcmp((const char *)col, "mode") == 0) has_mode = 1;
            }
        }
        if (has_table && !has_mode) {
            if (sqlite3_exec(D,
                "BEGIN;"
                "CREATE TABLE deck_new_v2 (key TEXT NOT NULL, mode INTEGER NOT NULL,"
                "  enabled INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (key, mode));"
                "INSERT OR IGNORE INTO deck_new_v2 SELECT key, 0, enabled FROM deck_new;"
                "INSERT OR IGNORE INTO deck_new_v2 SELECT key, 1, enabled FROM deck_new;"
                "DROP TABLE deck_new;"
                "ALTER TABLE deck_new_v2 RENAME TO deck_new;"
                "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
                /* exec stops at the failing statement, with the transaction
                 * still open: everything after would never be committed */
                sqlite3_exec(D, "ROLLBACK", NULL, NULL, NULL);
        }
    }
    if (sqlite3_exec(D, USER_SCHEMA, NULL, NULL, &e) != SQLITE_OK) {
        snprintf(s_err, sizeof s_err, "user schema: %s", e ? e : "?");
        sqlite3_free(e);
        return -1;
    }
    /* a user.db from before pages kept their sentence: the column, empty
     * (NULL reads as "still its line"); an error means it is already there */
    sqlite3_exec(D, "ALTER TABLE line_ink ADD COLUMN line_text TEXT", NULL, NULL, NULL);
    /* and from before they were keyed on it: the same rows, under the new key */
    {
        Stmt q;
        int text_in_key = 0;
        if (q.prepare(D, "PRAGMA table_info(line_ink)")) {
            while (sqlite3_step(q) == SQLITE_ROW) {
                const unsigned char *col = sqlite3_column_text(q, 1);
                if (col && strcmp((const char *)col, "line_text") == 0
                    && sqlite3_column_int(q, 5) > 0) text_in_key = 1;
            }
        }
        if (!text_in_key && sqlite3_exec(D,
                "BEGIN;"
                "CREATE TABLE line_ink_v2 ("
                "  chapter_id INTEGER NOT NULL, line INTEGER NOT NULL,"
                "  w INTEGER NOT NULL, h INTEGER NOT NULL, ink BLOB NOT NULL,"
                "  line_text TEXT, PRIMARY KEY (chapter_id, line, line_text));"
                "INSERT INTO line_ink_v2 SELECT chapter_id, line, w, h, ink, line_text"
                "  FROM line_ink;"
                "DROP TABLE line_ink;"
                "ALTER TABLE line_ink_v2 RENAME TO line_ink;"
                "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
            sqlite3_exec(D, "ROLLBACK", NULL, NULL, NULL);       /* as deck_new's, above */
    }
    remap_ids();
    /* These MUST run before the temp view is created: changing temp_store
     * destroys every existing temporary object, which would silently delete
     * `av` and make every query return nothing.
     *
     * The device has far less RAM than a laptop, so keep SQLite's footprint
     * small: no mmap of the 230 MB content file, a modest page cache, and
     * durable writes so an abrupt power-off cannot lose a review. */
    sqlite3_exec(D,
        "PRAGMA temp_store=MEMORY;"
        "PRAGMA journal_mode=TRUNCATE;"
        "PRAGMA synchronous=FULL;"
        "PRAGMA mmap_size=0;"
        "PRAGMA cache_size=-2000;",         /* ~2 MB */
        NULL, NULL, NULL);

    if (sqlite3_exec(D, ALL_VIEW, NULL, NULL, &e) != SQLITE_OK) {
        snprintf(s_err, sizeof s_err, "view: %s", e ? e : "?");
        sqlite3_free(e);
        return -1;
    }
    /* Cheap assertion: if the view is not queryable, nothing else will work
     * and the app would otherwise just look empty. */
    Stmt chk;
    int books_seen = 0;
    if (chk.prepare(D, "SELECT COUNT(DISTINCT book) FROM av")) {
        if (sqlite3_step(chk) == SQLITE_ROW) books_seen = sqlite3_column_int(chk, 0);
    }
    if (books_seen == 0) {
        snprintf(s_err, sizeof s_err,
                 "content has no decks (view broken or content.db empty)");
        return -1;
    }
    fprintf(stderr, "db: %d books available\n", books_seen);
    return 0;
}

/* ---- deck ticks -------------------------------------------------------- */

static void deckkey(char *out, size_t n, const char *book, const char *chapter) {
    snprintf(out, n, "%s\t%s", book, chapter ? chapter : "");
}

int db_deck_enabled(const char *book, const char *chapter, int mode) {
    char k[340];
    deckkey(k, sizeof k, book, chapter);
    Stmt st;
    if (!st.prepare(D, "SELECT enabled FROM deck_new WHERE key=?1 AND mode=?2")) return 0;
    sqlite3_bind_text(st, 1, k, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, mode);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

void db_deck_set(const char *book, const char *chapter, int mode, int on) {
    char k[340];
    deckkey(k, sizeof k, book, chapter);
    Stmt st;
    if (!st.prepare(D, "INSERT INTO deck_new (key,mode,enabled) VALUES (?1,?3,?2)"
        " ON CONFLICT(key,mode) DO UPDATE SET enabled=?2")) return;
    sqlite3_bind_text(st, 1, k, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, on ? 1 : 0);
    sqlite3_bind_int (st, 3, mode);
    sqlite3_step(st);
}

/* A transaction that may hold others: only the outermost begins and
 * commits, so a category's tick is one commit however many books it sets. */
static int tx_depth = 0;
/* A COMMIT that fails can leave its transaction open, and then every write
 * after it waits in there for a commit that may never come: quitting throws
 * them all away. Roll back what could not be committed, and say so. */
static void commit_now(void) {
    if (sqlite3_exec(D, "COMMIT", NULL, NULL, NULL) == SQLITE_OK) return;
    fprintf(stderr, "db: commit failed: %s\n", sqlite3_errmsg(D));
    if (!sqlite3_get_autocommit(D)) {
        sqlite3_exec(D, "ROLLBACK", NULL, NULL, NULL);
        fprintf(stderr, "db: rolled back\n");
    }
}
void db_begin(void)  { if (tx_depth++ == 0) sqlite3_exec(D, "BEGIN", NULL, NULL, NULL); }
void db_commit(void) { if (tx_depth > 0 && --tx_depth == 0) commit_now(); }

void db_book_set(const char *book, int mode, int on) {
    /* One transaction, and the chapters from `al` (a row a deck) not `av` (a
     * row a word). Each tick committed on its own was four fsyncs, and the
     * 左传's 249 decks were a thousand of them for one press. */
    Stmt st;
    if (!st.prepare(D, "SELECT DISTINCT chapter FROM al WHERE book=?1")) return;
    sqlite3_bind_text(st, 1, book, -1, SQLITE_TRANSIENT);
    db_begin();
    while (sqlite3_step(st) == SQLITE_ROW)
        db_deck_set(book, (const char *)sqlite3_column_text(st, 0), mode, on);
    st.prepare(D, "SELECT 1");                 /* done reading before the commit */
    db_commit();
}

/* 0 = none ticked, 1 = all ticked, 2 = some */
int db_book_enabled(const char *book, int mode) {
    Stmt st;
    if (!st.prepare(D, "SELECT DISTINCT chapter FROM al WHERE book=?1")) return 0;
    sqlite3_bind_text(st, 1, book, -1, SQLITE_TRANSIENT);
    int total = 0, on = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        total++;
        if (db_deck_enabled(book, (const char *)sqlite3_column_text(st, 0), mode)) on++;
    }
    if (total == 0 || on == 0) return 0;
    return on == total ? 1 : 2;
}

void db_close(void) { if (D) sqlite3_close(D); D = NULL; }

/* Copy text into a fixed buffer, backing up to a character boundary rather
 * than slicing a multi-byte sequence in half. A pathological headword from an
 * import used to land here half-decoded. */
/* A headword of your own is a card's key, and is carried in fixed rows:
 * cardrow_t's key (every write of a card's progress is keyed on it) and the
 * list view's dictrow_t. One cut to fit them matches nothing it was cut
 * from -- a test whose card can never be marked answered, an entry that
 * cannot be removed -- so db_user_add takes none longer than they hold. */
static const size_t HW_MAX = sizeof(((cardrow_t *)0)->key);   /* bytes, with its NUL */
#define HW_MAX_SQL "96"
static_assert(sizeof(((cardrow_t *)0)->key) == 96, "HW_MAX_SQL is cardrow_t's key");
static_assert(sizeof(((cardrow_t *)0)->headword) >= HW_MAX && sizeof(((dictrow_t *)0)->simp) >= HW_MAX
              && sizeof(((revrow_t *)0)->headword) >= HW_MAX && sizeof(((wordrow_t *)0)->headword) >= HW_MAX,
              "a row that carries a headword holds every one db_user_add takes");

static void cpy(char *dst, size_t n, const unsigned char *src) {
    if (!src || n == 0) { if (n) dst[0] = 0; return; }
    size_t len = strlen((const char *)src);
    if (len < n) { memcpy(dst, src, len + 1); return; }
    len = n - 1;
    while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;  /* continuation */
    memcpy(dst, src, len);
    dst[len] = 0;
}

int db_books(book_t *out, int max) {
    /* One query, not one per chapter. This used to call db_book_enabled per
     * book, which itself ran a query per chapter -- 318 queries to draw the
     * home screen, on every redraw. */
    static const char *SQL =
      "SELECT a.book, COUNT(DISTINCT a.chapter),"
      "       COUNT(DISTINCT CASE WHEN w.enabled=1 THEN a.chapter END),"
      "       COUNT(DISTINCT CASE WHEN r.enabled=1 THEN a.chapter END)"
      "  FROM al a"
      "  LEFT JOIN deck_new w ON w.key = a.book||char(9)||a.chapter AND w.mode=0"
      "  LEFT JOIN deck_new r ON r.key = a.book||char(9)||a.chapter AND r.mode=1"
      " GROUP BY a.book ORDER BY a.book";
    Stmt st;
    int n = 0;
    if (!st.prepare(D, SQL)) return 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].name, sizeof out[n].name, sqlite3_column_text(st, 0));
        int total = sqlite3_column_int(st, 1);
        out[n].n_lists = total;
        for (int m = 0; m < 2; m++) {
            int on = sqlite3_column_int(st, 2 + m);
            out[n].enabled[m] = (on == 0) ? 0 : (on == total ? 1 : 2);
        }
        n++;
    }
    return n;
}

int db_lists(const char *book, list_t *out, int max) {
    /* Tick state comes back with the row rather than a query each. */
    static const char *SQL =
      "SELECT a.list_id, a.book, a.chapter, COUNT(*),"
      "       IFNULL(MAX(w.enabled),0), IFNULL(MAX(r.enabled),0)"
      "  FROM av a"
      "  LEFT JOIN deck_new w ON w.key = a.book||char(9)||a.chapter AND w.mode=0"
      "  LEFT JOIN deck_new r ON r.key = a.book||char(9)||a.chapter AND r.mode=1"
      " WHERE a.book=?1"
      /* In the book's own order -- the order the build wrote its lists in
       * -- not by title: titles sort by character code, which put 德经
       * before 道经 and 第七十七章 before 第七十三章, and scattered the
       * 左传's years. Your own lists, which have no order, alphabetically. */
      " GROUP BY a.list_id, a.chapter"
      " ORDER BY CASE WHEN a.list_id >= 100000 THEN a.chapter END, a.list_id";
    Stmt st;
    int n = 0;
    if (!st.prepare(D, SQL)) return 0;
    sqlite3_bind_text(st, 1, book, -1, SQLITE_TRANSIENT);
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        out[n].id = sqlite3_column_int(st, 0);
        cpy(out[n].book,    160, sqlite3_column_text(st, 1));
        cpy(out[n].chapter, 160, sqlite3_column_text(st, 2));
        out[n].n          = sqlite3_column_int(st, 3);
        out[n].enabled[0] = sqlite3_column_int(st, 4);
        out[n].enabled[1] = sqlite3_column_int(st, 5);
        n++;
    }
    return n;
}

/* Reviews: introduced (state>0) and come due.
 * New: never introduced, or postponed with the postponement expired.
 * With list_id 0 the new pool is limited to ticked decks. */
/* From the cards, not the words: there are a few dozen cards and 61,000
 * deck words, and through `av` SQLite built the whole view to count them --
 * 110 ms a mode on a desktop, and home asks for both. Straight to the two
 * tables it uses the word-key index and takes a fraction of a millisecond.
 * The same count: a card is due if it is in some deck the scope allows. */
static const char *SQL_DUE =
  "SELECT COUNT(*) FROM srs s"
  " WHERE s.mode=?1 AND s.state>0 AND s.due<=?3 AND ("
  "   EXISTS (SELECT 1 FROM c.entries e JOIN c.lists l ON l.id=e.list_id"
  "            WHERE e.key=s.key AND (?2=0 OR e.list_id=?2)"
  "              AND (?4 IS NULL OR l.book=?4) AND (?1=1 OR e.writable=1))"
  "   OR EXISTS (SELECT 1 FROM user_entries ue JOIN user_lists ul ON ul.id=ue.list_id"
  "            WHERE ue.headword=s.key AND (?2=0 OR 100000+ul.id=?2)"
  "              AND (?4 IS NULL OR ?4='My lists') AND (?1=1 OR ue.writable=1)))";

/* The ticks are per mode, as they are where the cards are dealt
 * (SQL_NEXT_NEW, PICK_NEW). Without d.mode here a deck ticked for reading
 * counted as new writing cards too, and the home screen offered cards the
 * test then could not find. */
static const char *SQL_FRESH =
  /* The decks in scope first -- the ticked ones, or the one asked about --
   * then only their words, by the deck index. Through `av` it walked all
   * 61,000 deck words twice a redraw of home, ticked or not: 40 ms on a
   * desktop. The same count (checked against the old query over random
   * ticks, both modes, every book and deck), 0.5-2 ms.
   * It stops at ?5, the day's allowance, which is all home shows: counted
   * to the end, 文言 ticked took 40 ms a mode and everything 150, now 2-3 ms
   * (the same counts over 400 random ticks, scopes and allowances). UNION
   * ALL, and a DISTINCT that can stop early: a UNION gathers every word
   * before it counts one. srs is keyed (key, mode), so the join adds no rows. */
  "WITH li AS MATERIALIZED (SELECT l.id AS id FROM c.lists l"
  "    WHERE (?2=0 OR l.id=?2) AND (?4 IS NULL OR l.book=?4)"
  "      AND (?2<>0 OR EXISTS (SELECT 1 FROM deck_new d"
  "            WHERE d.key=l.book||char(9)||l.chapter AND d.mode=?1 AND d.enabled=1))),"
  " k AS ("
  "  SELECT e.key AS key FROM li CROSS JOIN c.entries e ON e.list_id=li.id"
  "   WHERE (?1=1 OR e.writable=1)"
  "  UNION ALL"
  "  SELECT ue.headword FROM user_lists ul CROSS JOIN user_entries ue ON ue.list_id=ul.id"
  "   WHERE (?2=0 OR 100000+ul.id=?2) AND (?4 IS NULL OR ?4='My lists')"
  "     AND (?1=1 OR ue.writable=1)"
  "     AND (?2<>0 OR EXISTS (SELECT 1 FROM deck_new d"
  "            WHERE d.key='My lists'||char(9)||ul.name AND d.mode=?1 AND d.enabled=1)))"
  " SELECT COUNT(*) FROM (SELECT DISTINCT k.key FROM k"
  "   LEFT JOIN srs s ON s.key=k.key AND s.mode=?1"
  "  WHERE s.key IS NULL OR (s.state=0 AND s.due<=?3) LIMIT ?5)";

/* cap: bound as ?5 where the query stops counting (SQL_FRESH); 0 for none */
static int count1(const char *sql, int mode, int list_id, long today,
                  const char *book, int cap) {
    Stmt st;
    int v = 0;
    if (!st.prepare(D, sql)) return 0;
    sqlite3_bind_int  (st, 1, mode);
    sqlite3_bind_int  (st, 2, list_id);
    sqlite3_bind_int64(st, 3, today);
    int rc4 = book ? sqlite3_bind_text(st, 4, book, -1, SQLITE_TRANSIENT)
                   : sqlite3_bind_null(st, 4);
    if (rc4 != SQLITE_OK)
        fprintf(stderr, "db: book scope not bound (%d) -- query is missing ?4\n", rc4);
    if (cap > 0) {
        int rc5 = sqlite3_bind_int(st, 5, cap);
        if (rc5 != SQLITE_OK)
            fprintf(stderr, "db: cap not bound (%d) -- query is missing ?5\n", rc5);
    }
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

void db_counts(int list_id, const char *book, int mode, long today,
               int *due, int *fresh) {
    if (due)   *due   = count1(SQL_DUE,   mode, list_id, today, book, 0);
    if (fresh) {
        /* what you can still start today -- and nothing to count if none,
         * and no further than that */
        int allow = db_new_allowance(mode, today);
        int pool  = allow > 0 ? count1(SQL_FRESH, mode, list_id, today, book, allow) : 0;
        *fresh = pool < allow ? pool : allow;
    }
}

/* Counts for many lists at once. */
void db_list_counts(list_t *rows, int n, int mode, long today) {
    for (int i = 0; i < n; i++) { rows[i].due = 0; rows[i].fresh = 0; }
    if (n <= 0) return;
    static const char *SQL =
      "SELECT a.list_id,"
      "       COUNT(DISTINCT CASE WHEN s.state>0 AND s.due<=?3 THEN a.key END),"
      "       COUNT(DISTINCT CASE WHEN s.key IS NULL"
      "                            OR (s.state=0 AND s.due<=?3) THEN a.key END)"
      "  FROM av a LEFT JOIN srs s ON s.key=a.key AND s.mode=?1"
      " WHERE a.book=?2 AND (?1=1 OR a.writable=1)"
      " GROUP BY a.list_id";
    Stmt st;
    if (!st.prepare(D, SQL)) return;
    sqlite3_bind_int  (st, 1, mode);
    sqlite3_bind_text (st, 2, rows[0].book, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, today);
    int allow = db_new_allowance(mode, today);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        for (int i = 0; i < n; i++)
            if (rows[i].id == id) {
                rows[i].due   = sqlite3_column_int(st, 1);
                int pool      = sqlite3_column_int(st, 2);
                rows[i].fresh = pool < allow ? pool : allow;
                break;
            }
    }
}

/* Reviews first; new cards only once nothing is due. */
int db_next_card(int list_id, const char *book, int mode, long today,
                 cardrow_t *o) {
    static const char *SQL_NEXT_DUE =
      "SELECT a.key, a.headword, a.pinyin, a.defn, a.writable,"
      "       s.stability, s.difficulty, s.reps, s.lapses, s.state, s.due, s.last"
      "  FROM av a JOIN srs s ON s.key=a.key AND s.mode=?1"
      " WHERE (?2=0 OR a.list_id=?2) AND (?4 IS NULL OR a.book=?4)"
      "   AND (?1=1 OR a.writable=1) AND s.state>0 AND s.due<=?3"
      "   AND length(CAST(a.key AS BLOB)) < " HW_MAX_SQL
      " GROUP BY a.key ORDER BY s.due ASC, a.list_id ASC, a.ordinal ASC LIMIT 1";
    static const char *SQL_NEXT_NEW =
      "SELECT a.key, a.headword, a.pinyin, a.defn, a.writable"
      "  FROM av a LEFT JOIN srs s ON s.key=a.key AND s.mode=?1"
      " WHERE (?2=0 OR a.list_id=?2) AND (?4 IS NULL OR a.book=?4)"
      "   AND (?1=1 OR a.writable=1)"
      "   AND (s.key IS NULL OR (s.state=0 AND s.due<=?3))"
      "   AND (?2<>0 OR a.list_id IN (SELECT al.list_id FROM al JOIN deck_new d"
      "           ON d.key = al.book || char(9) || al.chapter"
      "          AND d.mode=?1 AND d.enabled=1))"
      "   AND length(CAST(a.key AS BLOB)) < " HW_MAX_SQL
      " GROUP BY a.key ORDER BY RANDOM() LIMIT 1";

    Stmt st;
    memset(o, 0, sizeof *o);
    srs_init_card(&o->c);

    if (st.prepare(D, SQL_NEXT_DUE)) {
        sqlite3_bind_int  (st, 1, mode);
        sqlite3_bind_int  (st, 2, list_id);
        sqlite3_bind_int64(st, 3, today);
        int rc4 = book ? sqlite3_bind_text(st, 4, book, -1, SQLITE_TRANSIENT)
                       : sqlite3_bind_null(st, 4);
        if (rc4 != SQLITE_OK)
            fprintf(stderr, "db: book scope not bound (%d) -- query is missing ?4\n", rc4);
        if (sqlite3_step(st) == SQLITE_ROW) {
            cpy(o->key,      sizeof o->key,      sqlite3_column_text(st, 0));
            cpy(o->headword, sizeof o->headword, sqlite3_column_text(st, 1));
            cpy(o->pinyin,   sizeof o->pinyin,   sqlite3_column_text(st, 2));
            cpy(o->defn,     sizeof o->defn,     sqlite3_column_text(st, 3));
            o->writable      = sqlite3_column_int(st, 4);
            o->c.stability   = sqlite3_column_double(st, 5);
            o->c.difficulty  = sqlite3_column_double(st, 6);
            o->c.reps        = sqlite3_column_int(st, 7);
            o->c.lapses      = sqlite3_column_int(st, 8);
            o->c.state       = sqlite3_column_int(st, 9);
            o->c.due         = (long)sqlite3_column_int64(st, 10);
            o->c.last        = (long)sqlite3_column_int64(st, 11);
            o->is_new = 0;
            return 1;
        }
    }

    if (st.prepare(D, SQL_NEXT_NEW)) {
        sqlite3_bind_int  (st, 1, mode);
        sqlite3_bind_int  (st, 2, list_id);
        sqlite3_bind_int64(st, 3, today);
        int rc4 = book ? sqlite3_bind_text(st, 4, book, -1, SQLITE_TRANSIENT)
                       : sqlite3_bind_null(st, 4);
        if (rc4 != SQLITE_OK)
            fprintf(stderr, "db: book scope not bound (%d) -- query is missing ?4\n", rc4);
        if (sqlite3_step(st) == SQLITE_ROW) {
            cpy(o->key,      sizeof o->key,      sqlite3_column_text(st, 0));
            cpy(o->headword, sizeof o->headword, sqlite3_column_text(st, 1));
            cpy(o->pinyin,   sizeof o->pinyin,   sqlite3_column_text(st, 2));
            cpy(o->defn,     sizeof o->defn,     sqlite3_column_text(st, 3));
            o->writable = sqlite3_column_int(st, 4);
            o->is_new = 1;
            return 1;
        }
    }
    return 0;
}

void db_save_card(const cardrow_t *r, int mode) {
    static const char *SQL =
      "INSERT INTO srs (key,mode,stability,difficulty,reps,lapses,state,due,last,introduced)"
      " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?9)"
      " ON CONFLICT(key,mode) DO UPDATE SET"
      "  stability=?3, difficulty=?4, reps=?5, lapses=?6, state=?7, due=?8, last=?9,"
      /* stamp the introduction day once and never move it */
      "  introduced=CASE WHEN introduced=0 THEN ?9 ELSE introduced END";
    Stmt st;
    if (!st.prepare(D, SQL)) return;
    sqlite3_bind_text  (st, 1, r->key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (st, 2, mode);
    sqlite3_bind_double(st, 3, r->c.stability);
    sqlite3_bind_double(st, 4, r->c.difficulty);
    sqlite3_bind_int   (st, 5, r->c.reps);
    sqlite3_bind_int   (st, 6, r->c.lapses);
    sqlite3_bind_int   (st, 7, r->c.state);
    sqlite3_bind_int64 (st, 8, r->c.due);
    sqlite3_bind_int64 (st, 9, r->c.last);
    sqlite3_step(st);
}

/* Not now: hold the card out of both queues for `days`, leaving state alone.
 * A new card stays new; a review card is simply delayed. */
void db_postpone(const char *key, int mode, long today, int days) {
    static const char *SQL =
      "INSERT INTO srs (key,mode,stability,difficulty,reps,lapses,state,due,last)"
      " VALUES (?1,?2,0,0,0,0,0,?3,0)"
      /* Later means later: a card already learnt keeps a due date further
       * off than the postponement -- "later" in a review-ahead session
       * pulled one due in two months to three days. */
      " ON CONFLICT(key,mode) DO UPDATE SET"
      "   due = CASE WHEN srs.state>0 AND srs.due > excluded.due THEN srs.due"
      "              ELSE excluded.due END";
    Stmt st;
    if (!st.prepare(D, SQL)) return;
    sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (st, 2, mode);
    sqlite3_bind_int64(st, 3, today + days);
    sqlite3_step(st);
}

int db_strokes(const char *ch, strokes_t *s) {
    memset(s, 0, sizeof *s);
    Stmt st;
    if (!st.prepare(D, "SELECT idx, outline, median FROM strokes WHERE ch=?1 ORDER BY idx")) return 0;
    sqlite3_bind_text(st, 1, ch, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW && s->n < MAXSTROKE) {
        int i = s->n;
        const void *ob = sqlite3_column_blob(st, 1);
        int on = sqlite3_column_bytes(st, 1);
        const void *mb = sqlite3_column_blob(st, 2);
        int mn = sqlite3_column_bytes(st, 2);
        int16_t *ov = (int16_t *)malloc((size_t)on > 0 ? (size_t)on : 1);
        int16_t *mv = (int16_t *)malloc((size_t)mn > 0 ? (size_t)mn : 1);
        if (!ov || !mv) {           /* render fewer strokes rather than crash */
            free(ov); free(mv);
            break;
        }
        if (on > 0) memcpy(ov, ob, (size_t)on);
        if (mn > 0) memcpy(mv, mb, (size_t)mn);
        s->out[i] = ov; s->nout[i] = on / 4;
        s->med[i] = mv; s->nmed[i] = mn / 4;
        s->n++;
    }
    return s->n;
}

void db_strokes_free(strokes_t *s) {
    for (int i = 0; i < s->n; i++) { free(s->out[i]); free(s->med[i]); }
    memset(s, 0, sizeof *s);
}

int db_charinfo(const char *ch, charinfo_t *ci) {
    memset(ci, 0, sizeof *ci);
    Stmt st;
    if (!st.prepare(D, "SELECT pinyin, defn, radical, decomp, n_stroke FROM chars WHERE ch=?1")) return 0;
    sqlite3_bind_text(st, 1, ch, -1, SQLITE_TRANSIENT);
    int got = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        cpy(ci->pinyin,  sizeof ci->pinyin,  sqlite3_column_text(st, 0));
        cpy(ci->defn,    sizeof ci->defn,    sqlite3_column_text(st, 1));
        cpy(ci->radical, sizeof ci->radical, sqlite3_column_text(st, 2));
        cpy(ci->decomp,  sizeof ci->decomp,  sqlite3_column_text(st, 3));
        ci->n_stroke = sqlite3_column_int(st, 4);
        got = 1;
    }
    return got;
}

/* Search dispatch:
 *   contains hanzi   -> headword prefix on simp/trad
 *   contains a digit -> tone-numbered pinyin prefix   (hao3, ni3hao3)
 *   otherwise        -> toneless pinyin prefix, then English in the glosses
 * Ties break on deck frequency, so common words come first. */
int db_dict(const char *q, dictrow_t *out, int max) {
    if (!q || !*q) return 0;
    int is_han = 0, has_digit = 0, has_latin = 0;
    for (const unsigned char *p = (const unsigned char *)q; *p; p++) {
        if (*p >= 0x80) is_han = 1;
        if (*p >= '0' && *p <= '9') has_digit = 1;
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) has_latin = 1;
    }

    /* Characters and pinyin in one query -- 你hao, ni好, 道ke道 -- when you
     * know part of a word by sight and the rest by sound. It used to be
     * treated as characters only, so the pinyin half matched nothing. Two
     * patterns, in the order typed: the characters where they were written
     * with the pinyin's place left open, and the reading with the
     * characters' place left open. Tone numbers are honoured (py_num). */
    if (is_han && has_latin) {
        char hz[256] = "", pyp[256] = "";
        size_t hk = 0, pk = 0;
        int in_latin = 0;
        for (const unsigned char *p = (const unsigned char *)q; *p; ) {
            if (*p == ' ' || *p == '\'') { p++; continue; }
            if (*p >= 0x80) {
                int l = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
                for (int i = 0; i < l && p[i] && hk + 1 < sizeof hz; i++) hz[hk++] = (char)p[i];
                if (pk == 0 || pyp[pk - 1] != '%') { if (pk + 1 < sizeof pyp) pyp[pk++] = '%'; }
                for (int i = 0; i < l && *p; i++) p++;
                in_latin = 0;
            } else {
                if (!in_latin && (hk == 0 || hz[hk - 1] != '%') && hk + 1 < sizeof hz)
                    hz[hk++] = '%';
                char c = (char)*p;
                if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                if (pk + 1 < sizeof pyp) pyp[pk++] = c;
                in_latin = 1;
                p++;
            }
        }
        if (hk + 1 < sizeof hz && (hk == 0 || hz[hk - 1] != '%')) hz[hk++] = '%';
        if (pk + 1 < sizeof pyp && (pk == 0 || pyp[pk - 1] != '%')) pyp[pk++] = '%';
        hz[hk] = 0; pyp[pk] = 0;

        Stmt st;
        if (!st.prepare(D, "SELECT simp,trad,py_disp,defn FROM words"
                           " WHERE (simp LIKE ?1 OR trad LIKE ?1)"
                           "   AND (py_plain LIKE ?3 OR py_num LIKE ?3)"
                           " ORDER BY demote ASC, LENGTH(simp), freq DESC, simp"
                           " LIMIT ?2")) return 0;
        sqlite3_bind_text(st, 1, hz, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st, 2, max);
        sqlite3_bind_text(st, 3, pyp, -1, SQLITE_TRANSIENT);
        int n = 0;
        while (n < max && sqlite3_step(st) == SQLITE_ROW) {
            cpy(out[n].simp,   sizeof out[n].simp,   sqlite3_column_text(st, 0));
            cpy(out[n].trad,   sizeof out[n].trad,   sqlite3_column_text(st, 1));
            cpy(out[n].pinyin, sizeof out[n].pinyin, sqlite3_column_text(st, 2));
            cpy(out[n].defn,   sizeof out[n].defn,   sqlite3_column_text(st, 3));
            n++;
        }
        return n;
    }

    const char *SQL;
    /* Prefixes as ranges, not LIKE: LIKE ignores case, so it cannot use the
     * indexes (all of them binary), and every keystroke read the whole
     * 125,000-word table. The same rows (checked); ?1 is the typed text. */
    if (is_han) {
        SQL = "SELECT simp,trad,py_disp,defn FROM words"
              " WHERE (simp >= ?1 AND simp < ?1||char(1114111))"
              "    OR (trad >= ?1 AND trad < ?1||char(1114111))"
              " ORDER BY demote ASC, LENGTH(simp), freq DESC, simp LIMIT ?2";
    } else if (has_digit) {
        SQL = "SELECT simp,trad,py_disp,defn FROM words"
              " WHERE py_num >= ?1 AND py_num < ?1||char(1114111)"
              " ORDER BY (py_num=?1) DESC, demote ASC, LENGTH(simp),"
              "          freq DESC, LENGTH(py_num), simp, trad, py_disp LIMIT ?2";
    } else {
        SQL = "SELECT simp,trad,py_disp,defn FROM words"
              " WHERE py_plain LIKE ?1 OR defn LIKE ?3"
              " ORDER BY (py_plain=?4) DESC, (py_plain LIKE ?1) DESC,"
              "          demote ASC, LENGTH(simp), freq DESC, LENGTH(py_plain)"
              " LIMIT ?2";
    }

    /* normalise: lowercase, strip spaces and apostrophes so "ni3 hao3",
     * "ni3hao3" and "Ni3Hao3" all hit the same stored form */
    char norm[128];
    size_t k = 0;
    for (const char *p = q; *p && k + 1 < sizeof norm; p++) {
        if (*p == ' ' || *p == '\'') continue;
        norm[k++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
    }
    norm[k] = 0;

    char pref[160], mid[160];
    snprintf(pref, sizeof pref, "%s%%", norm);
    snprintf(mid,  sizeof mid,  "%% %s %%", norm);

    Stmt st;
    if (!st.prepare(D, SQL)) return 0;
    int words = !is_han && !has_digit;        /* the only LIKE form left */
    sqlite3_bind_text(st, 1, words ? pref : norm, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, max);
    if (words) {
        sqlite3_bind_text(st, 3, mid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, norm, -1, SQLITE_TRANSIENT);
    }
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].simp,   sizeof out[n].simp,   sqlite3_column_text(st, 0));
        cpy(out[n].trad,   sizeof out[n].trad,   sqlite3_column_text(st, 1));
        cpy(out[n].pinyin, sizeof out[n].pinyin, sqlite3_column_text(st, 2));
        cpy(out[n].defn,   sizeof out[n].defn,   sqlite3_column_text(st, 3));
        n++;
    }
    return n;
}

/* Sum of absolute differences between two signatures of the same length. */
static int hw_dist(const unsigned char *a, const unsigned char *b, int n) {
    int d = 0;
    for (int i = 0; i < n * 6; i++) d += abs((int)a[i] - (int)b[i]);
    return d;
}

int db_hw_signature_of(const char *ch, unsigned char *sig, int max_strokes) {
    Stmt st;
    if (!st.prepare(D, "SELECT n, sig FROM c.hwsig WHERE ch=?1")) return 0;
    sqlite3_bind_text(st, 1, ch, -1, SQLITE_TRANSIENT);
    int n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        n = sqlite3_column_int(st, 0);
        auto *b = static_cast<const unsigned char *>(sqlite3_column_blob(st, 1));
        int bytes = sqlite3_column_bytes(st, 1);
        if (n > max_strokes) n = max_strokes;
        if (b && bytes >= n * 6) memcpy(sig, b, (size_t)n * 6);
        else n = 0;
    }
    return n;
}

int db_hw_search(const unsigned char *sig, int n, hwcand_t *out, int max) {
    if (n < 1 || n > HW_MAX_STROKES) return 0;
    /* Stroke count is the strongest filter; allow one either side because a
     * stroke is easy to join or split by accident. */
    static const char *SQL =
      "SELECT ch, n, sig FROM c.hwsig WHERE n BETWEEN ?1 AND ?2";
    Stmt st;
    if (!st.prepare(D, SQL)) return 0;
    sqlite3_bind_int(st, 1, n - 1);
    sqlite3_bind_int(st, 2, n + 1);

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        int cn = sqlite3_column_int(st, 1);
        auto *cs = static_cast<const unsigned char *>(sqlite3_column_blob(st, 2));
        int bytes = sqlite3_column_bytes(st, 2);
        if (!cs || bytes < cn * 6) continue;

        int cmp = cn < n ? cn : n;
        int d = hw_dist(sig, cs, cmp) / (cmp * 6);
        d += abs(cn - n) * 24;            /* a wrong stroke count is a real cost */

        if (count < max) {
            cpy(out[count].ch, sizeof out[count].ch, sqlite3_column_text(st, 0));
            out[count].score = d;
            count++;
        } else {
            int worst = 0;
            for (int i = 1; i < max; i++) if (out[i].score > out[worst].score) worst = i;
            if (d < out[worst].score) {
                cpy(out[worst].ch, sizeof out[worst].ch, sqlite3_column_text(st, 0));
                out[worst].score = d;
            }
        }
    }

    for (int i = 1; i < count; i++) {       /* insertion sort, best first */
        hwcand_t k = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].score > k.score) { out[j + 1] = out[j]; j--; }
        out[j + 1] = k;
    }
    return count;
}

void db_user_list_totals(int *n_lists, int *n_words) {
    if (n_lists) *n_lists = 0;
    if (n_words) *n_words = 0;
    Stmt st;
    if (!st.prepare(D,
        "SELECT COUNT(*), COALESCE(SUM(n),0) FROM"
        " (SELECT (SELECT COUNT(*) FROM user_entries e WHERE e.list_id=l.id) n"
        "    FROM user_lists l)")) return;
    if (sqlite3_step(st) == SQLITE_ROW) {
        if (n_lists) *n_lists = sqlite3_column_int(st, 0);
        if (n_words) *n_words = sqlite3_column_int(st, 1);
    }
}

int db_user_lists(list_t *out, int max) {
    Stmt st;
    int n = 0;
    /* The ticks with the rows, as db_lists does -- two queries a list on
     * every redraw of "my lists" was two hundred with a Pleco import. */
    if (!st.prepare(D, "SELECT ul.id, ul.name, (SELECT COUNT(*) FROM user_entries ue"
        "                        WHERE ue.list_id=ul.id),"
        "       IFNULL(w.enabled,0), IFNULL(r.enabled,0)"
        "  FROM user_lists ul"
        "  LEFT JOIN deck_new w ON w.key='My lists'||char(9)||ul.name AND w.mode=0"
        "  LEFT JOIN deck_new r ON r.key='My lists'||char(9)||ul.name AND r.mode=1"
        " ORDER BY ul.name")) return 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        out[n].id = USER_LIST_BASE + sqlite3_column_int(st, 0);
        snprintf(out[n].book, sizeof out[n].book, "My lists");
        cpy(out[n].chapter, sizeof out[n].chapter, sqlite3_column_text(st, 1));
        out[n].n = sqlite3_column_int(st, 2);
        out[n].enabled[0] = sqlite3_column_int(st, 3) ? 1 : 0;
        out[n].enabled[1] = sqlite3_column_int(st, 4) ? 1 : 0;
        n++;
    }
    return n;
}

int db_user_list_create(const char *name) {
    Stmt st;
    if (!st.prepare(D, "INSERT INTO user_lists (name) VALUES (?1)"
        " ON CONFLICT DO NOTHING")) return -1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    /* return the id whether we just made it or it already existed */
    if (!st.prepare(D, "SELECT id FROM user_lists WHERE name=?1")) return -1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    int id = -1;
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int(st, 0);
    return id;
}

/* Length of the UTF-8 sequence at p, or 1 if it is cut short or malformed.
 * Imported text -- a Pleco file, Kobo's lookups -- is not ours to trust, and
 * a lead byte at the very end would otherwise step past the terminator. */
static int u8seq(const unsigned char *p) {
    int len = (*p < 0x80) ? 1 : ((*p & 0xE0) == 0xC0) ? 2
            : ((*p & 0xF0) == 0xE0) ? 3 : ((*p & 0xF8) == 0xF0) ? 4 : 1;
    for (int i = 1; i < len; i++)
        if ((p[i] & 0xC0) != 0x80) return 1;
    return len;
}

/* every han character in hw must have stroke data for a write card to exist */
static int is_writable(const char *hw) {
    Stmt st;
    if (!st.prepare(D, "SELECT 1 FROM c.strokes WHERE ch=?1 LIMIT 1")) return 0;
    int ok = 1, any = 0;
    for (const unsigned char *p = (const unsigned char *)hw; *p; ) {
        int len = u8seq(p);
        if (len >= 3) {                       /* CJK lives in the 3-byte range */
            char ch[5];
            memcpy(ch, p, (size_t)len);
            ch[len] = 0;
            any = 1;
            sqlite3_reset(st);
            sqlite3_bind_text(st, 1, ch, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) != SQLITE_ROW) { ok = 0; break; }
        }
        p += len;
    }
    return any && ok;
}

int db_word_lookup(const char *hw, char *py, int pyn, char *df, int dfn) {
    Stmt st;
    if (py) py[0] = 0;
    if (df) df[0] = 0;
    if (!st.prepare(D, "SELECT py_disp, defn FROM c.words WHERE simp=?1 OR trad=?1"
        " ORDER BY demote ASC, freq DESC LIMIT 1")) return 0;
    sqlite3_bind_text(st, 1, hw, -1, SQLITE_TRANSIENT);
    int got = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        if (py) cpy(py, (size_t)pyn, sqlite3_column_text(st, 0));
        if (df) cpy(df, (size_t)dfn, sqlite3_column_text(st, 1));
        got = 1;
    }
    return got;
}

int db_user_add(int list_id, const char *hw, const char *py, const char *df) {
    if (list_id >= USER_LIST_BASE) list_id -= USER_LIST_BASE;
    if (strlen(hw) >= HW_MAX) {
        fprintf(stderr, "db: not added: \"%.30s...\" is %zu bytes, a card holds %zu\n",
                hw, strlen(hw), HW_MAX - 1);
        return -1;
    }
    char apy[160] = "", adf[512] = "";
    if (!py || !*py || !df || !*df) db_word_lookup(hw, apy, sizeof apy, adf, sizeof adf);
    Stmt st;
    if (!st.prepare(D, "INSERT INTO user_entries (list_id,ordinal,headword,pinyin,defn,writable)"
        " VALUES (?1,(SELECT IFNULL(MAX(ordinal),-1)+1 FROM user_entries WHERE list_id=?1),"
        "         ?2,?3,?4,?5)"
        " ON CONFLICT(list_id,headword) DO NOTHING")) return -1;
    sqlite3_bind_int (st, 1, list_id);
    sqlite3_bind_text(st, 2, hw, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, (py && *py) ? py : apy, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, (df && *df) ? df : adf, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 5, is_writable(hw));
    int ok = sqlite3_step(st) == SQLITE_DONE;
    int inserted = ok && sqlite3_changes(D) > 0;   /* ON CONFLICT DO NOTHING */
    return inserted ? 0 : -1;
}

int db_examples(const char *key, example_t *out, int max) {
    Stmt st;
    if (!st.prepare(D, "SELECT book, text FROM c.examples WHERE key=?1 ORDER BY ord LIMIT ?2")) return 0;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, max);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].book, sizeof out[n].book, sqlite3_column_text(st, 0));
        cpy(out[n].text, sizeof out[n].text, sqlite3_column_text(st, 1));
        n++;
    }
    return n;
}

/* ---- daily new-card allowance ------------------------------------------ */

static const char *limit_key(int mode) {
    return mode == MODE_WRITE ? "new_per_day_write" : "new_per_day_read";
}

int db_list_word_count(int list_id) {
    Stmt st;
    if (!st.prepare(D, "SELECT COUNT(*) FROM av WHERE list_id=?1")) return 0;
    sqlite3_bind_int(st, 1, list_id);
    int n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    return n;
}

int db_list_words(int list_id, int offset, int limit, long today, wordrow_t *out) {
    static const char *SQL =
      "SELECT a.headword, a.pinyin, a.defn,"
      "       IFNULL(w.state,0), IFNULL(w.due,0),"
      "       IFNULL(r.state,0), IFNULL(r.due,0)"
      "  FROM av a"
      "  LEFT JOIN srs w ON w.key=a.key AND w.mode=0"
      "  LEFT JOIN srs r ON r.key=a.key AND r.mode=1"
      " WHERE a.list_id=?1"
      " ORDER BY a.ordinal LIMIT ?2 OFFSET ?3";
    Stmt st;
    if (!st.prepare(D, SQL)) return 0;
    sqlite3_bind_int(st, 1, list_id);
    sqlite3_bind_int(st, 2, limit);
    sqlite3_bind_int(st, 3, offset);
    int n = 0;
    while (n < limit && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].headword, sizeof out[n].headword, sqlite3_column_text(st, 0));
        cpy(out[n].pinyin,   sizeof out[n].pinyin,   sqlite3_column_text(st, 1));
        cpy(out[n].defn,     sizeof out[n].defn,     sqlite3_column_text(st, 2));
        for (int m = 0; m < 2; m++) {
            int state = sqlite3_column_int(st, 3 + m * 2);
            long due  = (long)sqlite3_column_int64(st, 4 + m * 2);
            out[n].due[m] = due;
            out[n].st[m]  = state == 0 ? 0 : (due <= today ? 2 : 1);
        }
        n++;
    }
    return n;
}

int db_reset_count(const char *book, int mode) {
    static const char *SEL =
      "SELECT COUNT(*) FROM srs WHERE (?1<0 OR mode=?1)"
      "  AND (?2 IS NULL OR key IN (SELECT key FROM av WHERE book=?2))";
    Stmt st;
    int n = 0;
    if (st.prepare(D, SEL)) {
        sqlite3_bind_int(st, 1, mode);
        if (book) sqlite3_bind_text(st, 2, book, -1, SQLITE_TRANSIENT);
        else      sqlite3_bind_null(st, 2);
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    }
    return n;
}

int db_reset_progress(const char *book, int mode) {
    Stmt st;
    int n = 0;
    /* Count first, so the confirmation can say what it is about to destroy. */
    static const char *SEL =
      "SELECT COUNT(*) FROM srs WHERE (?1<0 OR mode=?1)"
      "  AND (?2 IS NULL OR key IN (SELECT key FROM av WHERE book=?2))";
    if (st.prepare(D, SEL)) {
        sqlite3_bind_int(st, 1, mode);
        if (book) sqlite3_bind_text(st, 2, book, -1, SQLITE_TRANSIENT);
        else      sqlite3_bind_null(st, 2);
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    }
    static const char *DEL =
      "DELETE FROM srs WHERE (?1<0 OR mode=?1)"
      "  AND (?2 IS NULL OR key IN (SELECT key FROM av WHERE book=?2))";
    if (st.prepare(D, DEL)) {
        sqlite3_bind_int(st, 1, mode);
        if (book) sqlite3_bind_text(st, 2, book, -1, SQLITE_TRANSIENT);
        else      sqlite3_bind_null(st, 2);
        sqlite3_step(st);
    }
    db_session_clear();          /* a test over reset cards is meaningless */
    return n;
}

/* Every setting is one integer under one key in `settings`, and each pair of
 * accessors was writing the same eight lines out again with the key spelled
 * into the SQL. One place, so adding a setting is a default and a name. */
static int setting_get(const char *k, int dflt) {
    Stmt st;
    if (!st.prepare(D, "SELECT v FROM settings WHERE k=?1")) return dflt;
    sqlite3_bind_text(st, 1, k, -1, SQLITE_TRANSIENT);
    int v = dflt;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

static void setting_set(const char *k, int v) {
    Stmt st;
    if (!st.prepare(D, "INSERT INTO settings (k,v) VALUES (?1,?2)"
                       " ON CONFLICT(k) DO UPDATE SET v=?2")) return;
    sqlite3_bind_text(st, 1, k, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, v);
    sqlite3_step(st);
}

int db_home_cat_pos(const char *name) {
    char k[160];
    snprintf(k, sizeof k, "home_cat:%s", name);
    return setting_get(k, -1);
}
void db_set_home_cat_pos(const char *name, int pos) {
    char k[160];
    snprintf(k, sizeof k, "home_cat:%s", name);
    setting_set(k, pos);
}

int db_pen_only(void)        { return setting_get("pen_only", 1); }
void db_set_pen_only(int on) { setting_set("pen_only", on ? 1 : 0); }

int db_guided(void)          { return setting_get("guided", 0); }
void db_set_guided(int on)   { setting_set("guided", on ? 1 : 0); }

/* Shown unless it has been turned off. */
int db_show_punct(void)         { return setting_get("text_punct", 1); }
void db_set_show_punct(int on)  { setting_set("text_punct", on ? 1 : 0); }

int db_text_readings(void)          { return setting_get("text_readings", 1); }
void db_set_text_readings(int on)   { setting_set("text_readings", on ? 1 : 0); }

/* A comic volume's exact place: the saved line alone cannot say "plate 73's
 * picture" (its caption's page holds the same line), "plate 2" when its
 * caption is line 0 (the cover's line), or which of four caption-less
 * plates. Kept beside text_read, in settings, so user.db needs no new
 * table; -1 when there is none. */
int db_comic_place(int chapter_id) {
    char k[40];
    snprintf(k, sizeof k, "comic_place:%d", chapter_id);
    return setting_get(k, -1);
}

void db_set_comic_place(int chapter_id, int v) {
    char k[40];
    snprintf(k, sizeof k, "comic_place:%d", chapter_id);
    if (setting_get(k, -1) != v) setting_set(k, v);
}

int db_comic_size(void)           { int v = setting_get("comic_size", 0); return v < 0 || v > 2 ? 0 : v; }
void db_set_comic_size(int v)      { setting_set("comic_size", v < 0 || v > 2 ? 0 : v); }
/* a caption splits under its picture unless turned off */
int db_comic_split(void)           { return setting_get("comic_split", 1); }
void db_set_comic_split(int on)    { setting_set("comic_split", on ? 1 : 0); }
int db_show_my_notes(void)         { return setting_get("show_my_notes", 0); }
void db_set_show_my_notes(int on)  { setting_set("show_my_notes", on ? 1 : 0); }
int db_show_book_notes(void)        { return setting_get("show_book_notes", 0); }
void db_set_show_book_notes(int on) { setting_set("show_book_notes", on ? 1 : 0); }

/* A page is kept with the sentence it was written on, and shown only while
 * that sentence is still the chapter's line there: were content.db rebuilt
 * with a chapter's lines moved, a page must not turn up on another sentence
 * -- it stays in user.db, untouched, until its sentence is back. */
#define INK_ON_ITS_LINE \
    " AND (line_text IS NULL OR line_text = (SELECT text FROM c.text_lines" \
    "      WHERE chapter_id=line_ink.chapter_id AND ord=line_ink.line))"

int db_line_ink_get(int chapter_id, int line, void *buf, int cap, int *w, int *h) {
    Stmt st;
    if (!st.prepare(D, "SELECT w, h, ink FROM line_ink WHERE chapter_id=?1 AND line=?2"
                       INK_ON_ITS_LINE)) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    if (sqlite3_step(st) != SQLITE_ROW) return 0;
    int n = sqlite3_column_bytes(st, 2);
    if (n > cap) n = 0;                 /* bigger than the page holds: not ours */
    if (n > 0) memcpy(buf, sqlite3_column_blob(st, 2), (size_t)n);
    *w = sqlite3_column_int(st, 0);
    *h = sqlite3_column_int(st, 1);
    return n;
}

/* Replaces or deletes only the page the line shows, as get finds it: one
 * hidden there, its sentence gone, is left alone. One savepoint, so a write
 * that fails keeps the page it would have replaced. */
int db_line_ink_put(int chapter_id, int line, const char *text, const void *buf, int len,
                    int w, int h) {
    sqlite3_exec(D, "SAVEPOINT ink", NULL, NULL, NULL);
    int ok = 0;
    {
        Stmt st;
        if (st.prepare(D, "DELETE FROM line_ink WHERE chapter_id=?1 AND line=?2"
                          INK_ON_ITS_LINE)) {
            sqlite3_bind_int(st, 1, chapter_id);
            sqlite3_bind_int(st, 2, line);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
        if (ok && len > 0) {
            ok = 0;
            if (st.prepare(D, "INSERT OR REPLACE INTO line_ink"
                              " (chapter_id, line, w, h, ink, line_text)"
                              " VALUES (?1,?2,?3,?4,?5,?6)")) {
                sqlite3_bind_int(st, 1, chapter_id);
                sqlite3_bind_int(st, 2, line);
                sqlite3_bind_int(st, 3, w);
                sqlite3_bind_int(st, 4, h);
                sqlite3_bind_blob(st, 5, buf, len, SQLITE_TRANSIENT);
                if (text) sqlite3_bind_text(st, 6, text, -1, SQLITE_TRANSIENT);
                else sqlite3_bind_null(st, 6);
                ok = sqlite3_step(st) == SQLITE_DONE;
            }
        }
        if (!ok) {
            snprintf(s_err, sizeof s_err, "line_ink: %s", sqlite3_errmsg(D));
            fprintf(stderr, "%s\n", s_err);
        }
    }
    if (!ok) sqlite3_exec(D, "ROLLBACK TO ink", NULL, NULL, NULL);
    /* Outside a transaction RELEASE is the commit, and the one that can
     * fail (a full card, an I/O error): then the page was not kept, and
     * the writer must not be told it was. As commit_now. */
    if (sqlite3_exec(D, "RELEASE ink", NULL, NULL, NULL) != SQLITE_OK) {
        snprintf(s_err, sizeof s_err, "line_ink: not saved: %s", sqlite3_errmsg(D));
        fprintf(stderr, "%s\n", s_err);
        if (tx_depth == 0 && !sqlite3_get_autocommit(D)) {
            sqlite3_exec(D, "ROLLBACK", NULL, NULL, NULL);
            fprintf(stderr, "db: rolled back\n");
        }
        ok = 0;
    }
    return ok;
}

int db_line_ink_lines(int chapter_id, unsigned char *has, int n) {
    memset(has, 0, (size_t)n);
    Stmt st;
    if (!st.prepare(D, "SELECT line FROM line_ink WHERE chapter_id=?1" INK_ON_ITS_LINE)) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    int c = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        int l = sqlite3_column_int(st, 0);
        if (l >= 0 && l < n) { has[l] = 1; c++; }
    }
    return c;
}

int db_text_scale(void) {
    int v = setting_get("text_scale", 100);
    if (v < TEXT_SCALE_MIN) v = TEXT_SCALE_MIN;
    if (v > TEXT_SCALE_MAX) v = TEXT_SCALE_MAX;
    return v;
}

void db_set_text_scale(int pct) {
    if (pct < TEXT_SCALE_MIN) pct = TEXT_SCALE_MIN;
    if (pct > TEXT_SCALE_MAX) pct = TEXT_SCALE_MAX;
    setting_set("text_scale", pct);
}

int db_light(void) {
    /* -1 means never set: on a device with no frontlight there is nothing
     * to restore, and 0 would be a level. */
    return setting_get("light", -1);
}

void db_set_light(int level) { setting_set("light", level); }

/* The desktop's window: WIN_* of surf.h, -1 when never chosen. */
int  db_window_mode(void)        { return setting_get("window_mode", -1); }
void db_set_window_mode(int m)   { if (setting_get("window_mode", -1) != m) setting_set("window_mode", m); }

/* ---- PDFs: pins and reading spots ---------------------------------------- */

std::vector<std::string> db_pdf_pins(void) {
    std::vector<std::string> out;
    Stmt st;
    if (!st.prepare(D, "SELECT path FROM pdf_pin ORDER BY used DESC, added DESC, path")) return out;
    while (st.step()) {
        const unsigned char *t = sqlite3_column_text(st, 0);
        if (t) out.push_back((const char *)t);
    }
    return out;
}

int db_pdf_pinned(const char *path) {
    Stmt st;
    if (!path || !st.prepare(D, "SELECT 1 FROM pdf_pin WHERE path=?1")) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    return st.step() ? 1 : 0;
}

void db_pdf_pin(const char *path, int on, long now) {
    if (!path || !*path) return;
    Stmt st;
    if (on) {
        if (db_pdf_pinned(path)) return;
        if (!st.prepare(D, "INSERT INTO pdf_pin (path, added, used) VALUES (?1, ?2, ?2)")) return;
        sqlite3_bind_text (st, 1, path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, (sqlite3_int64)now);
        sqlite3_step(st);
    } else {
        /* unpinned, it forgets its place too: a spot is kept for pins only */
        if (!st.prepare(D, "DELETE FROM pdf_pin WHERE path=?1")) return;
        sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        if (!st.prepare(D, "DELETE FROM pdf_spot WHERE path=?1")) return;
        sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
    }
}

void db_pdf_used(const char *path, long now) {
    Stmt st;
    if (!path || !st.prepare(D, "UPDATE pdf_pin SET used=?2 WHERE path=?1 AND used<>?2")) return;
    sqlite3_bind_text (st, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)now);
    sqlite3_step(st);
}

int db_pdf_spot(const char *path, pdf_spot_t *out) {
    Stmt st;
    if (!path || !out || !st.prepare(D, "SELECT page, zoom, part, trim, rot, flip"
                                        " FROM pdf_spot WHERE path=?1")) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    if (!st.step()) return 0;
    out->page = sqlite3_column_int(st, 0);
    out->zoom = sqlite3_column_int(st, 1);
    out->part = sqlite3_column_int(st, 2);
    out->trim = sqlite3_column_int(st, 3);
    out->rot  = sqlite3_column_int(st, 4);
    out->flip = sqlite3_column_int(st, 5);
    return 1;
}

/* Only for a pinned PDF, and read before written: it runs on every page
 * turn, and a write is an fsync (synchronous=FULL) where a read is not. */
int db_pdf_set_spot(const char *path, const pdf_spot_t *s) {
    if (!path || !s || !db_pdf_pinned(path)) return 0;
    pdf_spot_t was;
    if (db_pdf_spot(path, &was) && was.page == s->page && was.zoom == s->zoom
        && was.part == s->part && was.trim == s->trim && was.rot == s->rot
        && was.flip == s->flip) return 1;
    Stmt st;
    if (!st.prepare(D, "INSERT INTO pdf_spot (path, page, zoom, part, trim, rot, flip)"
                       " VALUES (?1,?2,?3,?4,?5,?6,?7) ON CONFLICT(path) DO UPDATE SET"
                       " page=?2, zoom=?3, part=?4, trim=?5, rot=?6, flip=?7")) return 0;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, s->page);
    sqlite3_bind_int (st, 3, s->zoom);
    sqlite3_bind_int (st, 4, s->part);
    sqlite3_bind_int (st, 5, s->trim);
    sqlite3_bind_int (st, 6, s->rot);
    sqlite3_bind_int (st, 7, s->flip);
    return sqlite3_step(st) == SQLITE_DONE ? 1 : 0;
}

int db_new_limit(int mode) {
    return setting_get(limit_key(mode),
                       mode == MODE_WRITE ? NEW_PER_DAY_WRITE_DEFAULT
                                          : NEW_PER_DAY_READ_DEFAULT);
}

void db_set_new_limit(int mode, int n) {
    if (n < 0)   n = 0;
    if (n > 200) n = 200;
    setting_set(limit_key(mode), n);
}

int db_new_today(int mode, long today) {
    Stmt st;
    if (!st.prepare(D, "SELECT COUNT(*) FROM srs WHERE mode=?1 AND introduced=?2")) return 0;
    sqlite3_bind_int  (st, 1, mode);
    sqlite3_bind_int64(st, 2, today);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

/* ---- tests ------------------------------------------------------------- */

void db_session_clear(void) {
    sqlite3_exec(D, "DELETE FROM session_item; DELETE FROM session;",
                 NULL, NULL, NULL);
}

/* The entries in a test's scope of the cards learnt in its mode, `av`'s
 * columns, for PICK_DUE and PICK_AHEAD: one row a deck entry, found from
 * srs (a row a card studied) by the key index rather than by building every
 * deck entry there is. The scope is SQL_DUE's. */
#define PICK_ENTRIES \
  "SELECT e.key AS key, e.list_id AS lid, e.ordinal AS o" \
  "  FROM srs s0 JOIN c.entries e ON e.key=s0.key JOIN c.lists l ON l.id=e.list_id" \
  " WHERE s0.mode=?1 AND s0.state>0 AND (?2=0 OR e.list_id=?2)" \
  "   AND (?4 IS NULL OR l.book=?4) AND (?1=1 OR e.writable=1)" \
  " UNION ALL" \
  " SELECT ue.headword, 100000+ue.list_id, ue.ordinal" \
  "  FROM srs s0 JOIN user_entries ue ON ue.headword=s0.key" \
  "  JOIN user_lists ul ON ul.id=ue.list_id" \
  " WHERE s0.mode=?1 AND s0.state>0 AND (?2=0 OR 100000+ul.id=?2)" \
  "   AND (?4 IS NULL OR ?4='My lists') AND (?1=1 OR ue.writable=1)"

void db_session_start(int mode, int list_id, const char *book,
                      const char *label, long today, int size, int scored,
                      int ignore_limit) {
    db_session_clear();

    Stmt st;
    if (!st.prepare(D, "INSERT INTO session (id,mode,list_id,book,label,started,scored)"
        " VALUES (1,?1,?2,?3,?4,?5,?6)")) return;
    sqlite3_bind_int(st, 6, scored ? 1 : 0);
    sqlite3_bind_int (st, 1, mode);
    sqlite3_bind_int (st, 2, list_id);
    if (book && *book) sqlite3_bind_text(st, 3, book, -1, SQLITE_TRANSIENT);
    else               sqlite3_bind_null(st, 3);
    sqlite3_bind_text(st, 4, label ? label : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, today);
    sqlite3_step(st);

    /* Due cards first -- what is due is due, and is never capped. From the
     * cards, as SQL_DUE: through `av` it built an automatic index over all
     * 240,000 deck entries whenever anything was due, 0.75 s for all decks
     * on a desktop -- seconds of a frozen panel on the Kobo, on the most
     * common press there is. Each card's entries are found by the key
     * index; a tie in due date goes to its first deck and place, as before
     * (then whichever entry the grouping kept, now the least). */
    static const char *PICK_DUE =
      "SELECT s.key FROM srs s"
      " JOIN (" PICK_ENTRIES ") m ON m.key=s.key"
      " WHERE s.mode=?1 AND s.state>0 AND s.due<=?3"
      " GROUP BY s.key"
      " ORDER BY s.due ASC, MIN(m.lid) ASC, MIN(m.o) ASC"
      " LIMIT ?5";
    /* Then new ones, limited both by what is left in the test and by how many
     * new cards today's allowance still permits. */
    static const char *PICK_NEW =
      "SELECT a.key FROM av a LEFT JOIN srs s ON s.key=a.key AND s.mode=?1"
      " WHERE (?2=0 OR a.list_id=?2) AND (?4 IS NULL OR a.book=?4)"
      "   AND (?1=1 OR a.writable=1)"
      "   AND (s.key IS NULL OR (s.state=0 AND s.due<=?3))"
      "   AND (?2<>0 OR a.list_id IN (SELECT al.list_id FROM al JOIN deck_new d"
      "           ON d.key = al.book || char(9) || al.chapter"
      "          AND d.mode=?1 AND d.enabled=1))"
      " GROUP BY a.key"
      /* Random, not deck order: going straight down a chapter teaches the
       * sequence as much as the words. */
      " ORDER BY RANDOM()"
      " LIMIT ?5";

    Stmt ins;
    ins.prepare(D, "INSERT OR IGNORE INTO session_item (key,ord) VALUES (?1,?2)");
    int n = 0;
    /* Reviewing ahead: cards already learnt, soonest-due first (from the
     * cards too, as PICK_DUE). */
    static const char *PICK_AHEAD =
      "SELECT s.key FROM srs s"
      " JOIN (" PICK_ENTRIES ") m ON m.key=s.key"
      " WHERE s.mode=?1 AND s.state>0 AND s.due>?3"
      " GROUP BY s.key ORDER BY s.due ASC LIMIT ?5";
    sqlite3_exec(D, "BEGIN", NULL, NULL, NULL);
    if (!scored) {
        if (st.prepare(D, PICK_AHEAD)) {
            sqlite3_bind_int  (st, 1, mode);
            sqlite3_bind_int  (st, 2, list_id);
            sqlite3_bind_int64(st, 3, today);
            if (book && *book) sqlite3_bind_text(st, 4, book, -1, SQLITE_TRANSIENT);
            else               sqlite3_bind_null(st, 4);
            sqlite3_bind_int  (st, 5, size);
            while (sqlite3_step(st) == SQLITE_ROW && ins) {
                sqlite3_reset(ins);
                sqlite3_bind_text(ins, 1, (const char *)sqlite3_column_text(st, 0), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (ins, 2, n++);
                sqlite3_step(ins);
            }
        }
        commit_now();
        return;
    }
    for (int pass = 0; pass < 2 && ins; pass++) {
        int room = size - n;
        if (room <= 0) break;
        if (pass == 1 && !ignore_limit) {
            int allow = db_new_allowance(mode, today);
            if (allow < room) room = allow;
            if (room <= 0) break;
        }
        if (!st.prepare(D, pass == 0 ? PICK_DUE : PICK_NEW))
            break;
        sqlite3_bind_int  (st, 1, mode);
        sqlite3_bind_int  (st, 2, list_id);
        sqlite3_bind_int64(st, 3, today);
        if (book && *book) sqlite3_bind_text(st, 4, book, -1, SQLITE_TRANSIENT);
        else               sqlite3_bind_null(st, 4);
        sqlite3_bind_int  (st, 5, room);
        while (sqlite3_step(st) == SQLITE_ROW) {
            sqlite3_reset(ins);
            sqlite3_bind_text(ins, 1, (const char *)sqlite3_column_text(st, 0), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (ins, 2, n++);
            sqlite3_step(ins);
        }
    }
    commit_now();
}

int db_session_load(session_t *o) {
    memset(o, 0, sizeof *o);
    Stmt st;
    if (!st.prepare(D, "SELECT mode,list_id,IFNULL(book,''),IFNULL(label,''),"
        "       IFNULL(scored,1) FROM session WHERE id=1")) return 0;
    int got = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        o->mode    = sqlite3_column_int(st, 0);
        o->list_id = sqlite3_column_int(st, 1);
        cpy(o->book,  sizeof o->book,  sqlite3_column_text(st, 2));
        cpy(o->label, sizeof o->label, sqlite3_column_text(st, 3));
        o->scored = sqlite3_column_int(st, 4);
        got = 1;
    }
    if (!got) return 0;

    /* wrong: answered=2 -- or state=2, for a test begun before that was kept */
    if (st.prepare(D, "SELECT COUNT(*), SUM(state=1), SUM(state=0), SUM(state=2),"
                      " SUM(state=3), SUM(answered=2 OR state=2) FROM session_item")) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            o->total   = sqlite3_column_int(st, 0);
            o->correct = sqlite3_column_int(st, 1);
            o->pending = sqlite3_column_int(st, 2);
            o->repeat  = sqlite3_column_int(st, 3);
            o->later   = sqlite3_column_int(st, 4);
            o->wrong   = sqlite3_column_int(st, 5);
        }
    }
    return o->total > 0;
}

/* Unanswered cards first, in order; then whatever came back wrong. */
int db_new_available(int list_id, const char *book, int mode, long today) {
    static const char *SQL =
      "SELECT COUNT(*) FROM (SELECT a.key FROM av a"
      "  LEFT JOIN srs s ON s.key=a.key AND s.mode=?1"
      " WHERE (?2=0 OR a.list_id=?2) AND (?4 IS NULL OR a.book=?4)"
      "   AND (?1=1 OR a.writable=1)"
      "   AND (s.key IS NULL OR (s.state=0 AND s.due<=?3))"
      "   AND (?2<>0 OR a.list_id IN (SELECT al.list_id FROM al JOIN deck_new d"
      "           ON d.key = al.book || char(9) || al.chapter"
      "          AND d.mode=?1 AND d.enabled=1))"
      " GROUP BY a.key)";
    Stmt st;
    if (!st.prepare(D, SQL)) return 0;
    sqlite3_bind_int  (st, 1, mode);
    sqlite3_bind_int  (st, 2, list_id);
    sqlite3_bind_int64(st, 3, today);
    if (book && *book) sqlite3_bind_text(st, 4, book, -1, SQLITE_TRANSIENT);
    else               sqlite3_bind_null(st, 4);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

/* One list or one whole book: the same counting over a different scope, so
 * `book` selects which. A word in two chapters of a book is one word here --
 * that is what DISTINCT key is for -- so a book's total is not the sum of
 * its lists' totals. */
static void stats_scope(int list_id, const char *book, long today,
                        liststats_t *out) {
    memset(out, 0, sizeof *out);
    const char *WHERE = book ? "book=?1" : "list_id=?1";

    char sql[512];
    snprintf(sql, sizeof sql,
        "SELECT COUNT(*), SUM(writable) FROM (SELECT DISTINCT key, writable"
        "  FROM av WHERE %s)", WHERE);
    Stmt st;
    if (!st.prepare(D, sql)) return;
    if (book) sqlite3_bind_text(st, 1, book, -1, SQLITE_TRANSIENT);
    else      sqlite3_bind_int (st, 1, list_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        out->total    = sqlite3_column_int(st, 0);
        out->writable = sqlite3_column_int(st, 1);
    }

    snprintf(sql, sizeof sql,
      "SELECT"
      "  SUM(CASE WHEN s.key IS NULL OR s.state=0 THEN 1 ELSE 0 END),"
      /* Learning and known by how well it is held, not by state: the
       * scheduler writes state 2 for any card seen, so "learning" was
       * always 0 and one wrong answer counted a card as known. Known is
       * the usual mature line, an interval of three weeks. */
      "  SUM(CASE WHEN s.state>0 AND s.stability<21 THEN 1 ELSE 0 END),"
      "  SUM(CASE WHEN s.state>0 AND s.stability>=21 THEN 1 ELSE 0 END),"
      "  SUM(CASE WHEN s.state>0 AND s.due<=?3 THEN 1 ELSE 0 END)"
      "  FROM (SELECT DISTINCT key, writable FROM av WHERE %s) a"
      "  LEFT JOIN srs s ON s.key=a.key AND s.mode=?2"
      " WHERE (?2=1 OR a.writable=1)", WHERE);
    for (int m = 0; m < 2; m++) {
        Stmt q;
        if (!q.prepare(D, sql)) return;
        if (book) sqlite3_bind_text(q, 1, book, -1, SQLITE_TRANSIENT);
        else      sqlite3_bind_int (q, 1, list_id);
        sqlite3_bind_int  (q, 2, m);
        sqlite3_bind_int64(q, 3, today);
        if (sqlite3_step(q) == SQLITE_ROW) {
            out->unseen[m]   = sqlite3_column_int(q, 0);
            out->learning[m] = sqlite3_column_int(q, 1);
            out->known[m]    = sqlite3_column_int(q, 2);
            out->due[m]      = sqlite3_column_int(q, 3);
        }
    }
}

void db_list_stats(int list_id, long today, liststats_t *out) {
    stats_scope(list_id, NULL, today, out);
}

void db_book_stats(const char *book, long today, liststats_t *out) {
    stats_scope(0, book, today, out);
}

int db_texts(textbook_t *out, int max) {
    Stmt st;
    if (!st.prepare(D, "SELECT id,title,author,n_chap,n_line FROM c.texts"
                       " ORDER BY title")) return 0;
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        out[n].id = sqlite3_column_int(st, 0);
        cpy(out[n].title,  sizeof out[n].title,  sqlite3_column_text(st, 1));
        cpy(out[n].author, sizeof out[n].author, sqlite3_column_text(st, 2));
        out[n].n_chap = sqlite3_column_int(st, 3);
        out[n].n_line = sqlite3_column_int(st, 4);
        n++;
    }
    return n;
}

int db_text_chapters(int text_id, textchap_t *out, int max) {
    Stmt st;
    if (!st.prepare(D, "SELECT id,title,n_line FROM c.text_chapters"
                       " WHERE text_id=?1 ORDER BY ord")) return 0;
    sqlite3_bind_int(st, 1, text_id);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        out[n].id = sqlite3_column_int(st, 0);
        cpy(out[n].title, sizeof out[n].title, sqlite3_column_text(st, 1));
        out[n].n_line = sqlite3_column_int(st, 2);
        n++;
    }
    return n;
}

int db_text_lines(int chapter_id, char (*out)[TEXT_LINE_MAX], int max) {
    Stmt st;
    if (!st.prepare(D, "SELECT text FROM c.text_lines WHERE chapter_id=?1"
                       " ORDER BY ord")) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n], TEXT_LINE_MAX, sqlite3_column_text(st, 0));
        n++;
    }
    return n;
}

int db_text_pos(int chapter_id) {
    Stmt st;
    if (!st.prepare(D, "SELECT line FROM text_pos WHERE chapter_id=?1")) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

int db_text_pos_saved(int chapter_id) {
    Stmt st;
    if (!st.prepare(D, "SELECT line FROM text_pos WHERE chapter_id=?1")) return -1;
    sqlite3_bind_int(st, 1, chapter_id);
    return sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : -1;
}

void db_set_text_pos(int chapter_id, int line) {
    Stmt st;
    if (!st.prepare(D, "INSERT INTO text_pos (chapter_id,line) VALUES (?1,?2)"
                       " ON CONFLICT(chapter_id) DO UPDATE SET line=?2")) return;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    sqlite3_step(st);
}

int db_text_chapter(int chapter_id, textchap_t *out) {
    Stmt st;
    if (!st.prepare(D, "SELECT id,title,n_line FROM c.text_chapters"
                       " WHERE id=?1")) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    if (sqlite3_step(st) != SQLITE_ROW) return 0;
    out->id = sqlite3_column_int(st, 0);
    cpy(out->title, sizeof out->title, sqlite3_column_text(st, 1));
    out->n_line = sqlite3_column_int(st, 2);
    return 1;
}

/* The notes tables exist only in a content.db built with the translations,
 * so every query here simply finds nothing when they are missing.
 *
 * A book can have more than one source of notes (several translators). The queries named _of read the source they are given --
 * the English and the notes each have their own -- and the rest the one
 * set by db_set_note_source(); '' is the book's own, the only one most
 * books have. A content.db from before sources has no source column: every note
 * is then the book's own. */
static int have_source(void) {
    if (s_have_source < 0) {
        Stmt st;
        s_have_source = st.prepare(D, "SELECT source FROM c.text_notes LIMIT 1") ? 1 : 0;
    }
    return s_have_source;
}

void db_set_note_source(const char *src) {
    snprintf(s_note_src, sizeof s_note_src, "%s", src ? src : "");
}

/* Material the app shows and no exported EPUB carries is kept in a column of its own, app_only
 * (no source fills it now), which only
 * these two queries read: shown after the row's notes, a line on. A
 * content.db from before it has no such column. */
static int have_app_only(void) {
    if (s_have_app_only < 0) {
        Stmt st;
        s_have_app_only = st.prepare(D, "SELECT (SELECT app_only FROM c.text_notes LIMIT 1),"
                                        " (SELECT app_only FROM c.text_chapter_notes LIMIT 1)") ? 1 : 0;
    }
    return s_have_app_only;
}
/* a notes column with app_only after it, where there is one */
#define WITH_APP_ONLY(col) "CASE WHEN IFNULL(app_only,'')='' THEN " col \
                           " ELSE IFNULL(" col ",'')||char(10)||app_only END"

/* the notes tables' condition for the chosen source, bound as ?9 */
#define SRC_COND " AND source=?9"
static const char *src_cond(void) { return have_source() ? SRC_COND : ""; }

int db_note_sources(int text_id, notesrc_t *out, int max) {
    Stmt st;
    if (!st.prepare(D, "SELECT source, label FROM c.note_sources WHERE text_id=?1"
                       " ORDER BY ord LIMIT ?2")) return 0;
    sqlite3_bind_int(st, 1, text_id);
    sqlite3_bind_int(st, 2, max);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].key,   sizeof out[n].key,   sqlite3_column_text(st, 0));
        cpy(out[n].label, sizeof out[n].label, sqlite3_column_text(st, 1));
        n++;
    }
    return n;
}

/* The source chosen for a book is remembered by its key, not by its place
 * in the switch: a source added in between would turn the remembered place
 * into another translator's. A setting is one
 * integer, so the key goes in as a number -- FNV-1a of "src:" and the key,
 * 31 bits -- under note_src:<text id> for the notes page's switch and
 * en_src:<text id> for the English's: two choices, kept apart, so one
 * translator's English can be read with another's notes. A key no longer in the
 * switch matches nothing, and the book opens on its first source. */
static int src_code(const char *key) {
    unsigned h = 2166136261u;
    for (const char *p = "src:"; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    for (const char *p = key ? key : ""; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    return (int)(h & 0x7fffffff);
}

static int source_choice(const char *prefix, int text_id, const notesrc_t *srcs, int n) {
    if (n <= 0) return 0;
    char k[40];
    snprintf(k, sizeof k, "%s:%d", prefix, text_id);
    int code = setting_get(k, -1);
    if (code < 0) return 0;                       /* never chosen: the first */
    for (int i = 0; i < n; i++)
        if (src_code(srcs[i].key) == code) return i;
    return 0;                                     /* a source no longer there */
}

static void set_source_choice(const char *prefix, int text_id, const char *key) {
    char k[40];
    snprintf(k, sizeof k, "%s:%d", prefix, text_id);
    int c = src_code(key);
    if (setting_get(k, -1) != c) setting_set(k, c);
}

int db_note_source_choice(int text_id, const notesrc_t *srcs, int n) {
    return source_choice("note_src", text_id, srcs, n);
}
void db_set_note_source_choice(int text_id, const char *key) {
    set_source_choice("note_src", text_id, key);
}
int db_en_source_choice(int text_id, const notesrc_t *srcs, int n) {
    return source_choice("en_src", text_id, srcs, n);
}
void db_set_en_source_choice(int text_id, const char *key) {
    set_source_choice("en_src", text_id, key);
}

void db_setting_put(const char *k, int v) {
    if (v >= 0) { setting_set(k, v); return; }
    Stmt st;
    if (!st.prepare(D, "DELETE FROM settings WHERE k=?1")) return;
    sqlite3_bind_text(st, 1, k, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
}

int db_setting_peek(const char *k) { return setting_get(k, -1); }

/* Which of a source's rows a query reads (db.h): all, those with English,
 * those with notes. A row's notes are its commentary and its word by word,
 * and what the app alone shows after them (app_only). */
static const char *what_cond(int what) {
    if (what == NOTES_EN) return " AND IFNULL(english,'')<>''";
    if (what == NOTES_NOTED)
        return have_app_only() ? " AND (IFNULL(notes,'')<>'' OR IFNULL(gloss,'')<>''"
                                 " OR IFNULL(app_only,'')<>'')"
                               : " AND (IFNULL(notes,'')<>'' OR IFNULL(gloss,'')<>'')";
    return "";
}
static void bind_key(Stmt &st, const char *src) {
    if (have_source()) sqlite3_bind_text(st, 9, src ? src : "", -1, SQLITE_TRANSIENT);
}

int db_text_notes_of(int chapter_id, int line, const char *src, int what, textnote_t *out, int max) {
    Stmt st;
    char q[640];
    snprintf(q, sizeof q, "SELECT english, zh, pinyin, gloss, %s FROM c.text_notes"
                          " WHERE chapter_id=?1 AND line_ord=?2%s%s ORDER BY ord LIMIT ?3",
             have_app_only() ? WITH_APP_ONLY("notes") : "notes", src_cond(), what_cond(what));
    if (!st.prepare(D, q)) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    sqlite3_bind_int(st, 3, max);
    bind_key(st, src);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].english, sizeof out[n].english, sqlite3_column_text(st, 0));
        cpy(out[n].zh,      sizeof out[n].zh,      sqlite3_column_text(st, 1));
        cpy(out[n].pinyin,  sizeof out[n].pinyin,  sqlite3_column_text(st, 2));
        cpy(out[n].gloss,   sizeof out[n].gloss,   sqlite3_column_text(st, 3));
        cpy(out[n].notes,   sizeof out[n].notes,   sqlite3_column_text(st, 4));
        n++;
    }
    return n;
}

int db_text_has_of(int chapter_id, int line, const char *src, int what) {
    Stmt st;
    char q[320];
    snprintf(q, sizeof q, "SELECT 1 FROM c.text_notes WHERE chapter_id=?1 AND line_ord=?2%s%s"
                          " LIMIT 1", src_cond(), what_cond(what));
    if (!st.prepare(D, q)) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    bind_key(st, src);
    return sqlite3_step(st) == SQLITE_ROW;
}

int db_chapter_noted(int chapter_id, const char *src, noted_row_fn fn, void *ctx) {
    Stmt st;
    char q[640];
    snprintf(q, sizeof q, "SELECT line_ord, IFNULL(gloss,''), IFNULL(notes,'')<>''%s"
                          " FROM c.text_notes WHERE chapter_id=?1%s%s ORDER BY line_ord, ord",
             have_app_only() ? " OR IFNULL(app_only,'')<>''" : "", src_cond(), what_cond(NOTES_NOTED));
    if (!st.prepare(D, q)) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    bind_key(st, src);
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *g = sqlite3_column_text(st, 1);
        fn(sqlite3_column_int(st, 0), g ? (const char *)g : "", sqlite3_column_int(st, 2), ctx);
        n++;
    }
    return n;
}

/* A pointer row is empty but for src_ord. The book's own source ('') has
 * them too -- the classics split by sentence (classics_split.py) -- where
 * other rows' src_ord means something else (translator B's passage) or nothing,
 * and a novel's word notes are gloss alone: all of it must be empty. */
int db_text_en_cover(int chapter_id, int line, const char *src) {
    if (!src || !have_source()) return -1;
    Stmt st;
    if (!st.prepare(D, "SELECT src_ord FROM c.text_notes WHERE chapter_id=?1 AND line_ord=?2"
                       " AND source=?9 AND src_ord IS NOT NULL AND IFNULL(english,'')=''"
                       " AND IFNULL(notes,'')='' AND IFNULL(gloss,'')='' AND IFNULL(zh,'')=''"
                       " ORDER BY ord LIMIT 1")) return -1;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    bind_key(st, src);
    if (sqlite3_step(st) != SQLITE_ROW || sqlite3_column_type(st, 0) == SQLITE_NULL) return -1;
    int a = sqlite3_column_int(st, 0);
    return a == line ? -1 : a;
}

/* One query per source when a book opens: its chapters by the text's index,
 * then the source's rows of each by the notes' (chapter, source) index. */
int db_source_gives(int text_id, const char *src, int what) {
    Stmt st;
    char q[400];
    snprintf(q, sizeof q, "SELECT 1 FROM c.text_chapters ch JOIN c.text_notes n"
                          " ON n.chapter_id=ch.id WHERE ch.text_id=?1%s%s LIMIT 1",
             have_source() ? " AND n.source=?9" : "", what_cond(what));
    if (!st.prepare(D, q)) return 0;
    sqlite3_bind_int(st, 1, text_id);
    bind_key(st, src);
    return sqlite3_step(st) == SQLITE_ROW;
}

int db_source_has_chapter_pages(int text_id, const char *src) {
    Stmt st;
    char q[320];
    snprintf(q, sizeof q, "SELECT 1 FROM c.text_chapters ch JOIN c.text_chapter_notes n"
                          " ON n.chapter_id=ch.id WHERE ch.text_id=?1%s LIMIT 1",
             have_source() ? " AND n.source=?9" : "");
    if (!st.prepare(D, q)) return 0;
    sqlite3_bind_int(st, 1, text_id);
    bind_key(st, src);
    return sqlite3_step(st) == SQLITE_ROW;
}

/* A chapter at a time from the last, by the notes' (chapter, source)
 * index and the pages' key: a novel's 120 chapters at most, once a book. */
int db_source_last_chapter(int text_id, const char *src) {
    if (!have_source()) return -1;
    Stmt st;
    if (!st.prepare(D, "SELECT MAX(ch.ord) FROM c.text_chapters ch WHERE ch.text_id=?1 AND"
                       " (EXISTS (SELECT 1 FROM c.text_notes n WHERE n.chapter_id=ch.id AND n.source=?9)"
                       " OR EXISTS (SELECT 1 FROM c.text_chapter_notes p WHERE p.chapter_id=ch.id"
                       " AND p.source=?9))")) return -1;
    sqlite3_bind_int(st, 1, text_id);
    bind_key(st, src);
    if (sqlite3_step(st) != SQLITE_ROW || sqlite3_column_type(st, 0) == SQLITE_NULL) return -1;
    return sqlite3_column_int(st, 0);
}

int db_text_notes(int chapter_id, int line, textnote_t *out, int max) {
    return db_text_notes_of(chapter_id, line, s_note_src, NOTES_ALL, out, max);
}

int db_text_has_notes(int chapter_id, int line) {
    return db_text_has_of(chapter_id, line, s_note_src, NOTES_ALL);
}

int db_text_plates(int chapter_id, plate_t *out, int max) {
    Stmt st;
    /* a content.db from before the comics has no such table: no plates */
    if (!st.prepare(D, "SELECT line_first, n_lines, image FROM c.text_plates"
                       " WHERE chapter_id=?1 ORDER BY ord")) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        out[n].line_first = sqlite3_column_int(st, 0);
        out[n].n_lines    = sqlite3_column_int(st, 1);
        cpy(out[n].image, sizeof out[n].image, sqlite3_column_text(st, 2));
        n++;
    }
    return n;
}

void db_text_epub(int text_id, char *out, int n) {
    if (n > 0) out[0] = 0;
    Stmt st;
    if (!st.prepare(D, "SELECT file FROM c.text_epub WHERE text_id=?1")) return;
    sqlite3_bind_int(st, 1, text_id);
    if (sqlite3_step(st) == SQLITE_ROW) cpy(out, (size_t)n, sqlite3_column_text(st, 0));
}

int db_text_epub_apart(int text_id) {
    Stmt st;
    /* a content.db from before the column: every series is captions */
    if (!st.prepare(D, "SELECT apart FROM c.text_epub WHERE text_id=?1")) return 0;
    sqlite3_bind_int(st, 1, text_id);
    return sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0);
}

int db_text_has_english(int chapter_id, int line) {
    return db_text_has_of(chapter_id, line, s_note_src, NOTES_EN);
}

/* Whether any source of the text has, in some chapter, both the chapter's
 * own note and notes on its sentences -- where the notes page's "chapter"
 * toggle can appear -- so the switch row keeps the toggle's room throughout
 * the book, and one type size. */
int db_text_has_chapter_toggle(int text_id) {
    Stmt st;
    if (!st.prepare(D, "SELECT 1 FROM c.text_chapter_notes cn JOIN c.text_chapters ch"
                       " ON ch.id=cn.chapter_id WHERE ch.text_id=?1 AND EXISTS"
                       " (SELECT 1 FROM c.text_notes n WHERE n.chapter_id=cn.chapter_id"
                       " AND n.source=cn.source) LIMIT 1")) return 0;
    sqlite3_bind_int(st, 1, text_id);
    return sqlite3_step(st) == SQLITE_ROW;
}

void db_text_chapter_note_of(int chapter_id, const char *src, char *out, int n, char *label, int ln) {
    if (n > 0) out[0] = 0;
    if (ln > 0) label[0] = 0;
    Stmt st;
    /* A content.db from before the label column still has the note. */
    char q[320];
    snprintf(q, sizeof q, "SELECT %s, IFNULL(label,'') FROM c.text_chapter_notes"
                          " WHERE chapter_id=?1%s",
             have_app_only() ? WITH_APP_ONLY("english") : "english", src_cond());
    char q0[160];                       /* the same source condition: its ?9 */
    snprintf(q0, sizeof q0, "SELECT english, '' FROM c.text_chapter_notes"
                            " WHERE chapter_id=?1%s", src_cond());
    if (!st.prepare(D, q) && !st.prepare(D, q0))
        return;
    sqlite3_bind_int(st, 1, chapter_id);
    bind_key(st, src);
    if (sqlite3_step(st) == SQLITE_ROW) {
        cpy(out, (size_t)n, sqlite3_column_text(st, 0));
        cpy(label, (size_t)ln, sqlite3_column_text(st, 1));
    }
}

void db_text_chapter_note(int chapter_id, char *out, int n, char *label, int ln) {
    db_text_chapter_note_of(chapter_id, s_note_src, out, n, label, ln);
}

void db_text_pinyin(int chapter_id, int line, char *out, int n) {
    if (n > 0) out[0] = 0;
    Stmt st;
    if (!st.prepare(D, "SELECT py FROM c.text_pinyin WHERE chapter_id=?1 AND line_ord=?2"))
        return;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    if (sqlite3_step(st) == SQLITE_ROW) cpy(out, (size_t)n, sqlite3_column_text(st, 0));
}

int db_word_entries(const char *w, dictrow_t *out, int max) {
    Stmt st;
    if (!st.prepare(D, "SELECT simp, trad, py_disp, defn FROM c.words WHERE simp=?1 OR trad=?1"
                       " ORDER BY demote ASC, freq DESC LIMIT ?2")) return 0;
    sqlite3_bind_text(st, 1, w, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, max);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].simp,   sizeof out[n].simp,   sqlite3_column_text(st, 0));
        cpy(out[n].trad,   sizeof out[n].trad,   sqlite3_column_text(st, 1));
        cpy(out[n].pinyin, sizeof out[n].pinyin, sqlite3_column_text(st, 2));
        cpy(out[n].defn,   sizeof out[n].defn,   sqlite3_column_text(st, 3));
        n++;
    }
    return n;
}

int db_book_entry(const char *book, const char *chapter, const char *w, dictrow_t *out) {
    Stmt st;
    if (!book || !*book || !w || !*w) return 0;
    if (!st.prepare(D, "SELECT e.headword, e.pinyin, e.defn FROM c.entries e JOIN c.lists l ON l.id=e.list_id"
                       " WHERE l.book=?1 AND e.headword=?3 AND e.defn<>''"
                       " ORDER BY (l.chapter=?2 OR l.chapter=?2||' \345\260\221\350\247\201'"   /* 少见 */
                       "           OR l.chapter=?2||' \347\224\237\351\200\240'"   /* 生造 */
                       "           OR l.chapter=?2||' \344\272\272\345\220\215') DESC, l.id LIMIT 1")) return 0;   /* 人名 */
    sqlite3_bind_text(st, 1, book, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, chapter ? chapter : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, w, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_ROW) return 0;
    memset(out, 0, sizeof *out);
    cpy(out->simp,   sizeof out->simp,   sqlite3_column_text(st, 0));
    cpy(out->pinyin, sizeof out->pinyin, sqlite3_column_text(st, 1));
    cpy(out->defn,   sizeof out->defn,   sqlite3_column_text(st, 2));
    return out->defn[0] != 0;
}

int db_note_src(int chapter_id, int line) {
    Stmt st;
    /* translator B's passages, whichever notes are shown: src_ord is his line */
    if (!st.prepare(D, have_source()
            ? "SELECT MIN(src_ord) FROM c.text_notes WHERE chapter_id=?1 AND line_ord=?2 AND source=''"
            : "SELECT MIN(src_ord) FROM c.text_notes WHERE chapter_id=?1 AND line_ord=?2"))
        return -1;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    if (sqlite3_step(st) != SQLITE_ROW || sqlite3_column_type(st, 0) == SQLITE_NULL) return -1;
    return sqlite3_column_int(st, 0);
}

int db_note_line_for_src(int chapter_id, int src) {
    Stmt st;
    if (!st.prepare(D, have_source()
            ? "SELECT line_ord FROM c.text_notes WHERE chapter_id=?1 AND src_ord<=?2 AND source=''"
              " ORDER BY src_ord DESC, line_ord ASC LIMIT 1"
            : "SELECT line_ord FROM c.text_notes WHERE chapter_id=?1 AND src_ord<=?2"
              " ORDER BY src_ord DESC, line_ord ASC LIMIT 1")) return -1;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, src);
    if (sqlite3_step(st) != SQLITE_ROW) return -1;
    return sqlite3_column_int(st, 0);
}

int db_text_read(int chapter_id) {
    Stmt st;
    if (!st.prepare(D, "SELECT line FROM text_read WHERE chapter_id=?1")) return 0;
    sqlite3_bind_int(st, 1, chapter_id);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

int db_text_read_saved(int chapter_id) {
    Stmt st;
    if (!st.prepare(D, "SELECT line FROM text_read WHERE chapter_id=?1")) return -1;
    sqlite3_bind_int(st, 1, chapter_id);
    return sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : -1;
}

void db_set_text_read(int chapter_id, int line) {
    Stmt st;
    if (!st.prepare(D, "INSERT INTO text_read (chapter_id,line) VALUES (?1,?2)"
                       " ON CONFLICT(chapter_id) DO UPDATE SET line=?2")) return;
    sqlite3_bind_int(st, 1, chapter_id);
    sqlite3_bind_int(st, 2, line);
    sqlite3_step(st);
}

int db_text_last_chapter(int text_id) {
    Stmt st;
    if (!st.prepare(D, "SELECT chapter_id FROM text_last WHERE text_id=?1")) return 0;
    sqlite3_bind_int(st, 1, text_id);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

void db_set_text_last_chapter(int text_id, int chapter_id) {
    Stmt st;
    if (!st.prepare(D, "INSERT INTO text_last (text_id,chapter_id) VALUES (?1,?2)"
                       " ON CONFLICT(text_id) DO UPDATE SET chapter_id=?2")) return;
    sqlite3_bind_int(st, 1, text_id);
    sqlite3_bind_int(st, 2, chapter_id);
    sqlite3_step(st);
}

int db_last_text(void)              { return setting_get("last_text", 0); }
void db_set_last_text(int text_id)  { setting_set("last_text", text_id); }

int db_ahead_available(int list_id, const char *book, int mode, long today) {
    static const char *SQL =
      /* From the cards, as SQL_DUE: through `av` it built every deck word
       * on every draw of the results screen. */
      "SELECT COUNT(*) FROM srs s"
      " WHERE s.mode=?1 AND s.state>0 AND s.due>?3 AND ("
      "   EXISTS (SELECT 1 FROM c.entries e JOIN c.lists l ON l.id=e.list_id"
      "            WHERE e.key=s.key AND (?2=0 OR e.list_id=?2)"
      "              AND (?4 IS NULL OR l.book=?4) AND (?1=1 OR e.writable=1))"
      "   OR EXISTS (SELECT 1 FROM user_entries ue JOIN user_lists ul ON ul.id=ue.list_id"
      "            WHERE ue.headword=s.key AND (?2=0 OR 100000+ul.id=?2)"
      "              AND (?4 IS NULL OR ?4='My lists') AND (?1=1 OR ue.writable=1)))";
    Stmt st;
    if (!st.prepare(D, SQL)) return 0;
    sqlite3_bind_int  (st, 1, mode);
    sqlite3_bind_int  (st, 2, list_id);
    sqlite3_bind_int64(st, 3, today);
    if (book && *book) sqlite3_bind_text(st, 4, book, -1, SQLITE_TRANSIENT);
    else               sqlite3_bind_null(st, 4);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

int db_card_by_key(const char *key, int mode, cardrow_t *o) {
    Stmt st;
    memset(o, 0, sizeof *o);
    srs_init_card(&o->c);
    if (!st.prepare(D, "SELECT stability,difficulty,reps,lapses,state,due,last"
        "  FROM srs WHERE key=?1 AND mode=?2")) return 0;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, mode);
    int got = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        o->c.stability  = sqlite3_column_double(st, 0);
        o->c.difficulty = sqlite3_column_double(st, 1);
        o->c.reps       = sqlite3_column_int(st, 2);
        o->c.lapses     = sqlite3_column_int(st, 3);
        o->c.state      = sqlite3_column_int(st, 4);
        o->c.due        = (long)sqlite3_column_int64(st, 5);
        o->c.last       = (long)sqlite3_column_int64(st, 6);
        snprintf(o->key, sizeof o->key, "%s", key);
        got = 1;
    }
    return got;
}

int db_session_next(cardrow_t *o) {
    /* The next card, and one entry for it -- from the test's own deck if the
     * word is there, then its book, then any. Taking MIN(pinyin) and
     * MIN(defn) over every entry of the key paired one entry's reading with
     * another's meaning (佛 "fó" with the fú definition, 679 words so), and
     * going through `av` built all 61,000 deck words for every card: 100 ms
     * a card on a desktop, a second on the Kobo. This uses the key index. */
    static const char *SQL =
      "WITH i AS (SELECT key FROM session_item WHERE state IN (0,2)"
      "            ORDER BY state, ord LIMIT 1),"
      "     ss AS (SELECT mode, list_id, IFNULL(book,'') AS book FROM session WHERE id=1),"
      "     cand AS ("
      "       SELECT e.headword AS hw, e.pinyin AS py, e.defn AS df, e.writable AS wr,"
      "              CASE WHEN e.list_id=(SELECT list_id FROM ss) THEN 0"
      "                   WHEN l.book=(SELECT book FROM ss) THEN 1 ELSE 2 END AS pref,"
      "              e.list_id AS lid, e.ordinal AS o"
      "         FROM c.entries e JOIN c.lists l ON l.id=e.list_id"
      "        WHERE e.key=(SELECT key FROM i)"
      "       UNION ALL"
      "       SELECT ue.headword, ue.pinyin, ue.defn, ue.writable,"
      "              CASE WHEN 100000+ue.list_id=(SELECT list_id FROM ss) THEN 0 ELSE 3 END,"
      "              100000+ue.list_id, ue.ordinal"
      "         FROM user_entries ue WHERE ue.headword=(SELECT key FROM i))"
      " SELECT i.key, b.hw, b.py, b.df, (SELECT MAX(wr) FROM cand),"
      "        s.stability, s.difficulty, s.reps, s.lapses, s.state, s.due, s.last"
      "   FROM i"
      "   LEFT JOIN (SELECT hw, py, df FROM cand ORDER BY pref, lid, o LIMIT 1) b"
      "   LEFT JOIN srs s ON s.key=i.key AND s.mode=(SELECT mode FROM ss)";
    Stmt st;
    memset(o, 0, sizeof *o);
    srs_init_card(&o->c);
    if (!st.prepare(D, SQL)) return 0;
    int got = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        if ((size_t)sqlite3_column_bytes(st, 0) >= sizeof o->key) {
            /* a key longer than a card holds (added before db_user_add
             * refused them): cut, no answer could ever be kept against it,
             * and the test would never end. Dropped, by the whole key. */
            fprintf(stderr, "db: a card of %d bytes left out of the test: longer than a card holds\n",
                    sqlite3_column_bytes(st, 0));
            Stmt del;
            if (del.prepare(D, "DELETE FROM session_item WHERE key=?1")) {
                sqlite3_bind_text(del, 1, (const char *)sqlite3_column_text(st, 0),
                                  sqlite3_column_bytes(st, 0), SQLITE_TRANSIENT);
                if (sqlite3_step(del) == SQLITE_DONE && sqlite3_changes(D) > 0)
                    return db_session_next(o);
            }
            return 0;
        }
        cpy(o->key,      sizeof o->key,      sqlite3_column_text(st, 0));
        cpy(o->headword, sizeof o->headword, sqlite3_column_text(st, 1));
        cpy(o->pinyin,   sizeof o->pinyin,   sqlite3_column_text(st, 2));
        cpy(o->defn,     sizeof o->defn,     sqlite3_column_text(st, 3));
        o->writable = sqlite3_column_int(st, 4);
        if (sqlite3_column_type(st, 5) != SQLITE_NULL) {
            o->c.stability  = sqlite3_column_double(st, 5);
            o->c.difficulty = sqlite3_column_double(st, 6);
            o->c.reps       = sqlite3_column_int(st, 7);
            o->c.lapses     = sqlite3_column_int(st, 8);
            o->c.state      = sqlite3_column_int(st, 9);
            o->c.due        = (long)sqlite3_column_int64(st, 10);
            o->c.last       = (long)sqlite3_column_int64(st, 11);
        }
        o->is_new = o->c.state == 0;
        got = o->headword[0] != 0;
        if (!got) {      /* the deck it came from was rebuilt away; drop it */
            Stmt del;
            if (del.prepare(D, "DELETE FROM session_item WHERE key=?1")) {
                sqlite3_bind_text(del, 1, o->key, -1, SQLITE_TRANSIENT);
                sqlite3_step(del);
            }
            return db_session_next(o);
        }
    }
    return got;
}

int db_session_is_first(const char *key) {
    Stmt st;
    if (!st.prepare(D, "SELECT answered FROM session_item WHERE key=?1")) return 1;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    int first = 1;
    if (sqlite3_step(st) == SQLITE_ROW) first = sqlite3_column_int(st, 0) == 0;
    return first;
}

void db_session_mark(const char *key, int correct) {
    Stmt st;
    /* Wrong goes to the back of the queue: kept where it was, the first card
     * got wrong came back every time and the others waited behind it. */
    /* answered=2 remembers the wrong answer after the redo comes right,
     * which is what "needed a second go" counts */
    if (!st.prepare(D, "UPDATE session_item SET state=?2,"
                       " answered = CASE WHEN ?2=2 OR answered=2 THEN 2 ELSE 1 END,"
                       " ord = CASE WHEN ?2=2 THEN (SELECT MAX(ord)+1 FROM session_item)"
                       "            ELSE ord END WHERE key=?1")) return;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, correct ? 1 : 2);
    sqlite3_step(st);
}

void db_session_put_off(const char *key) {
    Stmt st;
    if (!st.prepare(D, "UPDATE session_item SET state=3,"
                       " answered = CASE WHEN answered=2 THEN 2 ELSE 1 END WHERE key=?1")) return;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
}

/* ---- what you have studied --------------------------------------------- */

int db_reviewed_count(int mode) {
    Stmt st;
    if (!st.prepare(D, "SELECT COUNT(*) FROM srs WHERE state>0 AND (?1<0 OR mode=?1)")) return 0;
    sqlite3_bind_int(st, 1, mode);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

int db_reviewed(int mode, int offset, int limit, revrow_t *out) {
    /* The page from srs alone (keyed on key and mode: a row each already),
     * then each of its cards' pinyin by key -- the least of its entries',
     * as before. Joined to `av` inside the aggregate, every press on the
     * learned screen materialised all 240,000 deck entries: 0.65 s on a
     * desktop, seconds on the Kobo, per page and per filter. */
    static const char *SQL =
      "SELECT k.key,"
      "       (SELECT MIN(py) FROM (SELECT e.pinyin AS py FROM c.entries e WHERE e.key=k.key"
      "                             UNION ALL"
      "                             SELECT ue.pinyin FROM user_entries ue"
      "                               JOIN user_lists ul ON ul.id=ue.list_id"
      "                              WHERE ue.headword=k.key)),"
      "       k.mode, k.reps, k.lapses, k.state, k.due, k.last, k.stability"
      "  FROM (SELECT * FROM srs s WHERE s.state>0 AND (?1<0 OR s.mode=?1)"
      "         ORDER BY s.last DESC, s.key ASC LIMIT ?2 OFFSET ?3) k"
      " ORDER BY k.last DESC, k.key ASC";
    Stmt st;
    if (!st.prepare(D, SQL)) return 0;
    sqlite3_bind_int(st, 1, mode);
    sqlite3_bind_int(st, 2, limit);
    sqlite3_bind_int(st, 3, offset);
    int n = 0;
    while (n < limit && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].headword, sizeof out[n].headword, sqlite3_column_text(st, 0));
        cpy(out[n].pinyin,   sizeof out[n].pinyin,   sqlite3_column_text(st, 1));
        out[n].mode      = sqlite3_column_int(st, 2);
        out[n].reps      = sqlite3_column_int(st, 3);
        out[n].lapses    = sqlite3_column_int(st, 4);
        out[n].state     = sqlite3_column_int(st, 5);
        out[n].due       = (long)sqlite3_column_int64(st, 6);
        out[n].last      = (long)sqlite3_column_int64(st, 7);
        out[n].stability = sqlite3_column_double(st, 8);
        n++;
    }
    return n;
}

/* ---- Pleco .txt drop folder -------------------------------------------- */

static char *rstrip(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = 0;
    return s;
}

static int list_exists(const char *name) {
    Stmt st;
    if (!st.prepare(D, "SELECT 1 FROM user_lists WHERE name=?1")) return 0;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    int v = sqlite3_step(st) == SQLITE_ROW;
    return v;
}

static int import_pleco_file(const char *path, int *n_lists) {
    FilePtr f(fopen(path, "r"));
    if (!f) return -1;

    /* fall back to the file name when the export carries no // header */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    std::string fallback(base);
    size_t dot = fallback.rfind('.');
    if (dot != std::string::npos) fallback.erase(dot);

    char line[2048];
    int list_id = -1, n = 0;
    while (fgets(line, sizeof line, f.get())) {
        char *p = line;
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB
            && (unsigned char)p[2] == 0xBF) p += 3;      /* UTF-8 BOM */
        rstrip(p);
        if (!*p) continue;
        if (p[0] == '/' && p[1] == '/') {
            char name[160];
            snprintf(name, sizeof name, "%s", p + 2);
            int existed = list_exists(name);
            list_id = db_user_list_create(name);
            if (list_id >= 0 && !existed && n_lists) (*n_lists)++;
            continue;
        }
        char *tab1 = strchr(p, '\t');
        if (!tab1) continue;
        *tab1 = 0;
        char *py = tab1 + 1;
        char *df = strchr(py, '\t');
        if (df) { *df = 0; df++; }
        if (list_id < 0) {
            int existed = list_exists(fallback.c_str());
            list_id = db_user_list_create(fallback.c_str());
            if (list_id >= 0 && !existed && n_lists) (*n_lists)++;
        }
        if (list_id >= 0 && *p && db_user_add(list_id, p, py, df ? df : "") == 0) n++;
    }
    return n;
}

int db_import_pleco_dir(const char *dir, int *n_files, int *n_lists) {
    if (n_files) *n_files = 0;
    if (n_lists) *n_lists = 0;
    DirPtr d(opendir(dir));
    if (!d) return -1;
    struct dirent *e;
    int total = 0;
    sqlite3_exec(D, "BEGIN", NULL, NULL, NULL);
    while ((e = readdir(d.get()))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || (strcmp(dot, ".txt") != 0 && strcmp(dot, ".TXT") != 0)) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        int n = import_pleco_file(path, n_lists);
        if (n >= 0) {                      /* -1 means it would not open */
            total += n;
            if (n_files) (*n_files)++;
        }
    }
    commit_now();
    return total;
}

/* ---- your lists: browse, prune, export ---------------------------------- */

int db_user_entry_count(int list_id) {
    if (list_id >= USER_LIST_BASE) list_id -= USER_LIST_BASE;
    Stmt st;
    if (!st.prepare(D, "SELECT COUNT(*) FROM user_entries WHERE list_id=?1")) return 0;
    sqlite3_bind_int(st, 1, list_id);
    int v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    return v;
}

int db_user_entries(int list_id, int offset, int limit, dictrow_t *out) {
    if (list_id >= USER_LIST_BASE) list_id -= USER_LIST_BASE;
    Stmt st;
    if (!st.prepare(D, "SELECT headword, pinyin, defn FROM user_entries WHERE list_id=?1"
        " ORDER BY ordinal LIMIT ?2 OFFSET ?3")) return 0;
    sqlite3_bind_int(st, 1, list_id);
    sqlite3_bind_int(st, 2, limit);
    sqlite3_bind_int(st, 3, offset);
    int n = 0;
    while (n < limit && sqlite3_step(st) == SQLITE_ROW) {
        cpy(out[n].simp,   sizeof out[n].simp,   sqlite3_column_text(st, 0));
        out[n].trad[0] = 0;
        cpy(out[n].pinyin, sizeof out[n].pinyin, sqlite3_column_text(st, 1));
        cpy(out[n].defn,   sizeof out[n].defn,   sqlite3_column_text(st, 2));
        n++;
    }
    return n;
}

int db_user_entry_delete(int list_id, const char *headword) {
    if (list_id >= USER_LIST_BASE) list_id -= USER_LIST_BASE;
    Stmt st;
    if (!st.prepare(D, "DELETE FROM user_entries WHERE list_id=?1 AND headword=?2")) return -1;
    sqlite3_bind_int (st, 1, list_id);
    sqlite3_bind_text(st, 2, headword, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) return -1;
    if (sqlite3_changes(D) > 0) return 0;
    /* An entry added before db_user_add held headwords to HW_MAX comes back
     * from db_user_entries cut to fit (at a character, so up to 3 bytes
     * short): the entry it was cut from, longer than any row holds. */
    size_t n = strlen(headword);
    if (n + 4 < HW_MAX) return -1;
    if (!st.prepare(D, "DELETE FROM user_entries WHERE list_id=?1"
                       " AND length(CAST(headword AS BLOB)) >= " HW_MAX_SQL
                       " AND substr(CAST(headword AS BLOB), 1, ?3) = CAST(?2 AS BLOB)")) return -1;
    sqlite3_bind_int (st, 1, list_id);
    sqlite3_bind_text(st, 2, headword, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 3, (int)n);
    return sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(D) > 0 ? 0 : -1;
}

int db_user_list_rename(int list_id, const char *name) {
    if (list_id >= USER_LIST_BASE) list_id -= USER_LIST_BASE;
    /* carry the tick over to the new name so a rename does not silently
     * stop the list feeding new cards */
    char old[200] = "";
    Stmt st;
    if (st.prepare(D, "SELECT name FROM user_lists WHERE id=?1")) {
        sqlite3_bind_int(st, 1, list_id);
        if (sqlite3_step(st) == SQLITE_ROW)
            cpy(old, sizeof old, sqlite3_column_text(st, 0));
    }
    int was_w = old[0] ? db_deck_enabled("My lists", old, MODE_WRITE) : 0;
    int was_r = old[0] ? db_deck_enabled("My lists", old, MODE_READ)  : 0;

    if (!st.prepare(D, "UPDATE user_lists SET name=?2 WHERE id=?1")) return -1;
    sqlite3_bind_int (st, 1, list_id);
    sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    if (ok && old[0]) {
        db_deck_set("My lists", old, MODE_WRITE, 0);
        db_deck_set("My lists", old, MODE_READ,  0);
        if (was_w) db_deck_set("My lists", name, MODE_WRITE, 1);
        if (was_r) db_deck_set("My lists", name, MODE_READ,  1);
    }
    return ok ? 0 : -1;
}

int db_user_list_delete(int list_id) {
    if (list_id >= USER_LIST_BASE) list_id -= USER_LIST_BASE;
    char name[200] = "";
    Stmt st;
    if (st.prepare(D, "SELECT name FROM user_lists WHERE id=?1")) {
        sqlite3_bind_int(st, 1, list_id);
        if (sqlite3_step(st) == SQLITE_ROW)
            cpy(name, sizeof name, sqlite3_column_text(st, 0));
    }
    char sql[128];
    snprintf(sql, sizeof sql, "DELETE FROM user_entries WHERE list_id=%d", list_id);
    sqlite3_exec(D, sql, NULL, NULL, NULL);
    snprintf(sql, sizeof sql, "DELETE FROM user_lists WHERE id=%d", list_id);
    sqlite3_exec(D, sql, NULL, NULL, NULL);
    if (name[0]) { db_deck_set("My lists", name, MODE_WRITE, 0);
                   db_deck_set("My lists", name, MODE_READ,  0); }
    /* review history is deliberately left alone: it is keyed on the headword,
     * so re-adding the word later picks up where you left off */
    return 0;
}

/* file names come from list names, so strip anything awkward */
static void safe_name(char *out, size_t n, const char *in) {
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && k + 1 < n; p++) {
        unsigned char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c < 0x20)
            out[k++] = '_';
        else
            out[k++] = (char)c;
    }
    out[k] = 0;
    if (!k) snprintf(out, n, "list");
}

int db_export_pleco(const char *dir, int *n_files) {
    if (n_files) *n_files = 0;
    list_t ls[512];
    int nl = db_user_lists(ls, 512);
    int total = 0;
    for (int i = 0; i < nl; i++) {
        char safe[200], path[1024];
        safe_name(safe, sizeof safe, ls[i].chapter);
        snprintf(path, sizeof path, "%s/%s.txt", dir, safe);
        /* "HSK/1" and "HSK_1" both sanitise to HSK_1; do not let the second
         * silently truncate the first */
        for (int k = 0; k < i; k++) {
            char prev[200];
            safe_name(prev, sizeof prev, ls[k].chapter);
            if (!strcmp(prev, safe)) {
                snprintf(path, sizeof path, "%s/%s (%d).txt", dir, safe, ls[i].id);
                break;
            }
        }
        FilePtr f(fopen(path, "w"));
        if (!f) return -1;
        fprintf(f.get(), "//%s\n", ls[i].chapter);
        /* from the columns, not through a dictrow_t: its fixed fields would
         * clip a long definition that the import kept whole */
        Stmt st;
        if (!st.prepare(D, "SELECT headword, IFNULL(pinyin,''), IFNULL(defn,'')"
            " FROM user_entries WHERE list_id=?1 ORDER BY ordinal")) return -1;
        sqlite3_bind_int(st, 1, ls[i].id - USER_LIST_BASE);
        while (sqlite3_step(st) == SQLITE_ROW) {
            fprintf(f.get(), "%s\t%s\t%s\n", (const char *)sqlite3_column_text(st, 0),
                    (const char *)sqlite3_column_text(st, 1),
                    (const char *)sqlite3_column_text(st, 2));
            total++;
        }
        if (n_files) (*n_files)++;
    }
    return total;
}

/* ---- Kobo's own saved words -------------------------------------------- *
 * .kobo/KoboReader.sqlite holds two things worth stealing:
 *   WordList.Text   every word you tapped to look up in the built-in dict
 *   Bookmark.Text   the text of each highlight
 * Highlights are free text, so only short all-han runs are taken. */

static int looks_like_word(const char *t) {
    int nhan = 0;
    for (const unsigned char *p = (const unsigned char *)t; *p; ) {
        int len = u8seq(p);
        if (len == 3) {
            unsigned cp = ((unsigned)(p[0] & 0x0F) << 12)
                        | ((unsigned)(p[1] & 0x3F) << 6) | (unsigned)(p[2] & 0x3F);
            if (cp >= 0x4E00 && cp <= 0x9FFF) nhan++;
            else return 0;
        } else if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
            return 0;
        } else {
            return 0;
        }
        p += len;
    }
    return nhan >= 1 && nhan <= 8;
}

static int import_one(const char *sql, int list_id) {
    Stmt st;
    if (!st.prepare(D, sql)) return 0;
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *t = sqlite3_column_text(st, 0);
        if (!t) continue;
        const char *txt = (const char *)t;
        if (!looks_like_word(txt)) continue;
        if (db_user_add(list_id, txt, NULL, NULL) == 0) n++;
    }
    return n;
}

int db_import_kobo(const char *path, int *n_words, int *n_highlights) {
    if (n_words) *n_words = 0;
    if (n_highlights) *n_highlights = 0;
    if (access(path, R_OK) != 0) {      /* else ATTACH would create it */
        snprintf(s_err, sizeof s_err, "cannot read %s", path);
        return -1;
    }

    Stmt st;
    if (!st.prepare(D, "ATTACH DATABASE ?1 AS k"))
        return -1;
    std::string kuri = ro_uri(path);
    sqlite3_bind_text(st, 1, kuri.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) {
        snprintf(s_err, sizeof s_err, "attach %s: %s", path, sqlite3_errmsg(D));
        return -1;
    }

    int total = 0;
    /* One transaction: a commit a word was four fsyncs each. */
    sqlite3_exec(D, "BEGIN", NULL, NULL, NULL);
    int lw = db_user_list_create("Kobo lookups");
    if (lw >= 0) {
        int n = import_one("SELECT Text FROM k.WordList", lw);
        if (n_words) *n_words = n;
        total += n;
    }
    int lh = db_user_list_create("Kobo highlights");
    if (lh >= 0) {
        int n = import_one(
            "SELECT Text FROM k.Bookmark WHERE Text IS NOT NULL", lh);
        if (n_highlights) *n_highlights = n;
        total += n;
    }
    commit_now();
    sqlite3_exec(D, "DETACH DATABASE k", NULL, NULL, NULL);
    return total;
}
