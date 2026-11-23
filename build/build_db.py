#!/usr/bin/env python3
"""Build out/content.db, the app's read-only database, from data/.

    python3 build/build_db.py [OUT]

Standard library only; every input is committed. In the database:

  * CC-CEDICT, with searchable pinyin
  * Make Me a Hanzi's stroke outlines and medians, flattened to polygons,
    and a small signature of each character for handwriting search
  * decks: the HSK word lists, the HSK handwriting characters, and one deck
    per chapter of each text, holding the words that chapter introduces
  * the texts in data/texts/, and example sentences taken from them

A card's progress is keyed by its headword and a reading place by its chapter
id, so this file can be rebuilt freely as long as new texts go after old ones
(TEXTS, below).

Coordinates: Make Me a Hanzi uses a 1024 em box with y upward over
[-124, 900]. Everything is converted here to y-down, and SVG paths are
flattened to polygons, so the device never parses a path.
"""
import json, os, re, sqlite3, struct, sys
from collections import Counter

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DATA = os.path.join(REPO, "data")
OUT = os.path.join(REPO, "out", "content.db")
CEDICT = os.path.join(DATA, "dicts", "cedict_ts.u8")
MMAH_DIC = os.path.join(DATA, "dicts", "makemeahanzi_dictionary.txt")
MMAH_GFX = os.path.join(DATA, "dicts", "makemeahanzi_graphics.txt")
HSK_DIR = os.path.join(REPO, "build", "data", "hsk31")
HANDWRITING = os.path.join(DATA, "hsk", "HSK30_Handwriting_Pleco.txt")

# In this order for good: a new text goes at the end, so no chapter id moves.
TEXTS = ["道德经", "论语", "孙子兵法", "孟子", "大学", "中庸", "三字经", "千字文"]

HAN = re.compile(r'[㐀-䶿一-鿿豈-﫿]')

_TOKEN = re.compile(r'([MLCQZmlcqz])|(-?\d+\.?\d*)')

def parse_path(path):
    """(cmd, [floats]) tuples from an absolute-command SVG path string."""
    cur, nums, out = None, [], []
    for cmd, num in _TOKEN.findall(path):
        if cmd:
            if cur is not None:
                out.append((cur, [float(x) for x in nums]))
            cur, nums = cmd.upper(), []
        else:
            nums.append(num)
    if cur is not None:
        out.append((cur, [float(x) for x in nums]))
    return out

def flatten(path, steps=8):
    """Flatten one stroke outline to a polygon in font coords (y still up)."""
    pts, cx, cy = [], 0.0, 0.0
    for cmd, a in parse_path(path):
        if cmd == 'M' and len(a) >= 2:
            cx, cy = a[0], a[1]; pts.append((cx, cy))
        elif cmd == 'L' and len(a) >= 2:
            cx, cy = a[0], a[1]; pts.append((cx, cy))
        elif cmd == 'Q' and len(a) >= 4:
            x1, y1, x2, y2 = a[:4]
            for s in range(1, steps + 1):
                t = s / steps; mt = 1 - t
                pts.append((mt*mt*cx + 2*mt*t*x1 + t*t*x2,
                            mt*mt*cy + 2*mt*t*y1 + t*t*y2))
            cx, cy = x2, y2
        elif cmd == 'C' and len(a) >= 6:
            x1, y1, x2, y2, x3, y3 = a[:6]
            for s in range(1, steps + 1):
                t = s / steps; mt = 1 - t
                pts.append((mt**3*cx + 3*mt*mt*t*x1 + 3*mt*t*t*x2 + t**3*x3,
                            mt**3*cy + 3*mt*mt*t*y1 + 3*mt*t*t*y2 + t**3*y3))
            cx, cy = x3, y3
        # Z: outlines are closed by the renderer
    return pts

def pack_pts(pts):
    """[(x,y)...] in font coords -> int16 LE blob in y-down 0..1024 screen space."""
    out = bytearray()
    for x, y in pts:
        xi = max(-32000, min(32000, int(round(x))))
        yi = max(-32000, min(32000, int(round(900.0 - y))))
        out += struct.pack('<hh', xi, yi)
    return bytes(out)

TONE = {
    'ā':'a1','á':'a2','ǎ':'a3','à':'a4','a':'a',
    'ē':'e1','é':'e2','ě':'e3','è':'e4','e':'e',
    'ī':'i1','í':'i2','ǐ':'i3','ì':'i4','i':'i',
    'ō':'o1','ó':'o2','ǒ':'o3','ò':'o4','o':'o',
    'ū':'u1','ú':'u2','ǔ':'u3','ù':'u4','u':'u',
    'ǖ':'v1','ǘ':'v2','ǚ':'v3','ǜ':'v4','ü':'v',
    'ń':'n2','ň':'n3','ǹ':'n4','ḿ':'m2',
}

DEMOTE = (
    "variant of", "old variant", "archaic variant", "surname ", "used in",
    "see ", "also written", "abbr. for", "erhua variant",
)

def demote_of(defn, simp):
    """Dictionary noise that should never outrank a real word: variant forms,
    surnames, place names. Cheap, local, and it is what actually crowds the
    top of a pinyin search."""
    d = defn.lower().lstrip()
    for k in DEMOTE:
        if d.startswith(k):
            return 2
    if "surname" in d[:40] or "place name" in d[:40]:
        return 1
    return 0

ACC = {
    'a': "a\u0101\u00e1\u01ce\u00e0a", 'e': "e\u0113\u00e9\u011b\u00e8e",
    'i': "i\u012b\u00ed\u01d0\u00eci", 'o': "o\u014d\u00f3\u01d2\u00f2o",
    'u': "u\u016b\u00fa\u01d4\u00f9u", 'v': "\u00fc\u01d6\u01d8\u01da\u01dc\u00fc",
}

def accent_syllable(syl):
    """ni3 -> ni, hao3 -> h\u01ceo. Mark goes on a/o/e if present, else on the
    last vowel; 'v' and 'u:' are \u00fc."""
    tone = 0
    if syl and syl[-1].isdigit():
        tone = int(syl[-1]); syl = syl[:-1]
    syl = syl.replace('u:', 'v').replace('U:', 'v')
    if tone < 1 or tone > 4:
        return syl.replace('v', '\u00fc')
    low = syl.lower()
    idx = -1
    for want in ('a', 'o', 'e'):
        k = low.find(want)
        if k >= 0:
            idx = k
            break
    if idx < 0:
        for k in range(len(low) - 1, -1, -1):
            if low[k] in 'iuv':
                idx = k
                break
    if idx < 0:
        return syl.replace('v', '\u00fc')
    ch = low[idx]
    out = syl[:idx] + ACC[ch][tone] + syl[idx+1:]
    return out.replace('v', '\u00fc')

def accent_pinyin(s):
    """CC-CEDICT 'ni3 hao3' -> 'n\u01d0 h\u01ceo' for display."""
    if not s:
        return s
    parts = []
    for tok in s.split():
        if tok and (tok[-1].isdigit() or 'u:' in tok):
            parts.append(accent_syllable(tok))
        else:
            parts.append(tok)
    return ' '.join(parts)

def num_pinyin(s):
    """CC-CEDICT pinyin ('ni3 hao3') -> searchable tone-numbered form ('ni3hao3').
    Accented input is converted too, so both 'hao3' and 'hǎo' find the word."""
    out = []
    # ü is v everywhere: CC-CEDICT writes it u:, the glossary ü, and as u
    # and v the same word was two entries (绿, 女) and "lv" found only one.
    for ch in s.lower().replace('u:', 'v'):
        if ch in TONE:
            out.append(TONE[ch])
        elif ch.isalpha() or ch.isdigit():
            out.append(ch)
    return ''.join(out)

def num_from_accent(s):
    """Accented pinyin -> the numbered form search produces, with each tone at
    the end of its own syllable: 'hóuzi' is 'hou2zi', not 'houzi2'.

    A syllable ends before a consonant that begins the next one. n, ng and r
    can end a syllable, but only where no vowel follows them: yóuguò is
    you2guo4 -- taking g as a final made it "youguo4" -- tōngguān is
    tong1guan1, and sūnnǚ sun1nv3.
    """
    s = s.lower().replace('u:', 'v')
    out, cur, tone = [], '', ''
    vowel = lambda c: c in TONE

    def close():
        nonlocal cur, tone
        if cur:
            out.append(cur + tone)
        cur, tone = '', ''
    i, n = 0, len(s)
    while i < n:
        ch = s[i]
        t = TONE.get(ch)
        if t is not None:                          # a vowel, tone-marked or not
            cur += t[0]
            if len(t) == 2:
                tone = t[1]
            i += 1
            continue
        if not ch.isalpha():
            close()
            i += 1
            continue
        has_vowel = any(vowel(c) or c == 'v' for c in cur)
        nxt = s[i + 1] if i + 1 < n else ''
        if has_vowel and ch == 'n':
            if nxt == 'g' and not (i + 2 < n and vowel(s[i + 2])):
                cur += 'ng'
                i += 2
                continue
            if not vowel(nxt):
                cur += 'n'
                i += 1
                continue
        if has_vowel and ch == 'r' and not vowel(nxt):
            cur += 'r'
            i += 1
            continue
        if has_vowel:
            close()
        cur += ch
        i += 1
    close()
    return ''.join(out)

def plain_pinyin(s):
    """Accented or numbered pinyin -> bare lowercase letters, for search."""
    out = []
    for ch in s.lower().replace('u:', 'v'):
        if ch in TONE:
            out.append(TONE[ch][0])
        elif ch.isalpha():
            out.append(ch)
    return ''.join(out)

def segment(line, known, longest=4):
    """Longest match against the dictionary, falling back to one character."""
    out, i, n = [], 0, len(line)
    while i < n:
        if not HAN.match(line[i]):
            i += 1
            continue
        for ln in range(min(longest, n - i), 1, -1):
            w = line[i:i+ln]
            if len(HAN.findall(w)) == ln and w in known:
                out.append(w)
                i += ln
                break
        else:
            out.append(line[i])
            i += 1
    return out

HW_MAX_STROKES = 32

def hw_signature(medians):
    """[(sx,sy,mx,my,ex,ey), ...] as bytes, normalised over the whole glyph.
    Uniform scale keeps 一 from looking like 丨."""
    pts = [p for med in medians for p in med]
    if not pts:
        return None
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    w, h = maxx - minx, maxy - miny
    scale = 255.0 / max(w, h, 1)
    ox = (255.0 - w * scale) / 2.0
    oy = (255.0 - h * scale) / 2.0

    def nx(x): return max(0, min(255, int(round((x - minx) * scale + ox))))
    def ny(y): return max(0, min(255, int(round((y - miny) * scale + oy))))

    out = bytearray()
    for med in medians[:HW_MAX_STROKES]:
        if not med:
            return None
        a = med[0]
        b = med[len(med) // 2]
        c = med[-1]
        out += bytes((nx(a[0]), ny(a[1]), nx(b[0]), ny(b[1]), nx(c[0]), ny(c[1])))
    return bytes(out)

def read_hsk():
    """HSK 3.1 (the 2025 revision of HSK 3.0), levels 1-6.
    The lists are headwords only, so pinyin and glosses are joined from
    CC-CEDICT at build time. -> [(book, chapter, ordinal, headword)]"""
    rows = []
    for lvl in range(1, 7):
        fp = os.path.join(HSK_DIR, "HSK3.1_words_level%d.json" % lvl)
        if not os.path.exists(fp):
            print("  (no HSK file %s, skipping)" % fp)
            continue
        with open(fp, encoding="utf-8") as f:
            words = json.load(f)
        for i, w in enumerate(words):
            # a few items are variant pairs, e.g. ["\u6ca1", "\u6ca1\u6709"];
            # the syllabus counts them as one word, so take the first form
            alts = []
            if isinstance(w, list):
                if not w:
                    continue
                alts = [str(x).strip() for x in w[1:] if str(x).strip()]
                w = str(w[0])
            w = str(w).strip()
            if w:
                rows.append(("HSK 3.1", "Level %d" % lvl, i, w, alts))
    return rows

def read_cedict():
    """-> [(trad, simp, pinyin, defn)]"""
    rows = []
    pat = re.compile(r'^(\S+)\s+(\S+)\s+\[([^\]]*)\]\s+/(.*)/\s*$')
    with open(CEDICT, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                continue
            m = pat.match(line.rstrip("\n"))
            if not m:
                continue
            trad, simp, py, defs = m.groups()
            rows.append((trad, simp, py, defs.replace("/", "; ")))
    return rows

# Where makemeahanzi's reading is not the character's ordinary one -- most
# list a single reading, and for these it is the rare one: 佛 fú (as in 仿佛)
# put "fú" under every Buddha in the sutras. The ordinary reading goes first,
# which is the one set under a character in a text; the old one stays after
# it. Chosen where CC-CEDICT's words use the new reading at least three times
# as often, then checked by hand against both kinds of text. That reading is
# set per character, inside words too, so a character common in a modern word
# with the old reading keeps it: 还 (还是), 兴 (高兴), 尽 (尽管), 漂 (漂亮),
# 薄, 泊 (梁山泊). So do the particles (的 了 子 着 地) and the pairs where
# both readings are ordinary (为 应 更 都 耶 沈 率 谁).
READING_FIRST = {
    "佛": "fó", "罗": "luó", "甚": "shèn", "卓": "zhuó", "迹": "jì",
    "绩": "jì", "乾": "qián", "似": "sì", "著": "zhù", "勒": "lè",
    "脊": "jǐ", "甸": "diàn", "椎": "zhuī", "嚼": "jiáo", "卜": "bǔ",
    "靡": "mǐ", "框": "kuàng", "杠": "gàng", "驯": "xùn", "萎": "wěi",
    "茸": "róng", "茄": "qié", "拓": "tuò", "刨": "bào", "龈": "yín",
    "矽": "xī", "肋": "lèi",
}

def read_mmah_dict():
    """-> {char: (pinyin, defn, radical, decomposition)}"""
    out = {}
    with open(MMAH_DIC, encoding="utf-8") as f:
        for line in f:
            try:
                d = json.loads(line)
            except Exception:
                continue
            c = d.get("character")
            if not c:
                continue
            py = list(d.get("pinyin") or [])
            if c in READING_FIRST:
                first = READING_FIRST[c]
                py = [first] + [r for r in py if r != first]
            out[c] = (
                " ".join(py),
                d.get("definition") or "",
                d.get("radical") or "",
                d.get("decomposition") or "",
            )
    return out

def read_mmah_gfx():
    """-> {char: ([outline blobs], [median blobs])}"""
    out = {}
    with open(MMAH_GFX, encoding="utf-8") as f:
        for line in f:
            try:
                d = json.loads(line)
            except Exception:
                continue
            c = d.get("character")
            if not c:
                continue
            outlines = [pack_pts(flatten(p)) for p in (d.get("strokes") or [])]
            medians  = [pack_pts(m) for m in (d.get("medians") or [])]
            out[c] = (outlines, medians)
    return out

SCHEMA = """
PRAGMA journal_mode = OFF;
PRAGMA synchronous  = OFF;

CREATE TABLE lists (
  id       INTEGER PRIMARY KEY,
  book     TEXT NOT NULL,
  chapter  TEXT NOT NULL,
  n        INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX lists_book ON lists(book);

CREATE TABLE entries (
  id       INTEGER PRIMARY KEY,
  list_id  INTEGER NOT NULL,
  ordinal  INTEGER NOT NULL,
  headword TEXT NOT NULL,
  pinyin   TEXT NOT NULL DEFAULT '',
  defn     TEXT NOT NULL DEFAULT '',
  key      TEXT NOT NULL,          -- = headword; stable across rebuilds, srs joins on this
  writable INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX entries_list ON entries(list_id, ordinal);
CREATE INDEX entries_key  ON entries(key);

CREATE TABLE chars (
  ch       TEXT PRIMARY KEY,
  pinyin   TEXT NOT NULL DEFAULT '',
  defn     TEXT NOT NULL DEFAULT '',
  radical  TEXT NOT NULL DEFAULT '',
  decomp   TEXT NOT NULL DEFAULT '',
  n_stroke INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE strokes (
  ch       TEXT NOT NULL,
  idx      INTEGER NOT NULL,
  outline  BLOB NOT NULL,          -- int16 LE x,y pairs, 1024 box, y-down
  median   BLOB NOT NULL,
  PRIMARY KEY (ch, idx)
) WITHOUT ROWID;

CREATE TABLE words (
  id       INTEGER PRIMARY KEY,
  simp     TEXT NOT NULL,
  trad     TEXT NOT NULL,
  pinyin   TEXT NOT NULL,
  py_disp  TEXT NOT NULL,        -- for display:     h\u01ceo
  py_plain TEXT NOT NULL,        -- tones stripped:  hao
  py_num   TEXT NOT NULL,        -- tones as numbers: hao3
  defn     TEXT NOT NULL,
  freq     INTEGER NOT NULL DEFAULT 0,
  demote   INTEGER NOT NULL DEFAULT 0,  -- variants, surnames, place names last
  src      TEXT NOT NULL DEFAULT ''     -- '' CC-CEDICT, 'jttw' the book's own
);
CREATE INDEX words_simp  ON words(simp);
CREATE INDEX words_plain ON words(py_plain);
CREATE INDEX words_num   ON words(py_num);
CREATE INDEX words_freq  ON words(freq DESC);
CREATE INDEX words_trad  ON words(trad);   -- simp=? OR trad=? on every character opened

-- Handwriting search. Each character is reduced to a fixed signature: per
-- stroke, the start, middle and end of its median, normalised into a
-- 0..255 box with aspect preserved. Matching is then a cheap sum of absolute
-- differences over strokes, filtered first by stroke count. Derived from the
-- median data already shipped, so no extra dataset is needed.
CREATE TABLE hwsig (
  ch      TEXT PRIMARY KEY,
  n       INTEGER NOT NULL,          -- stroke count
  sig     BLOB NOT NULL              -- 6 bytes per stroke
) WITHOUT ROWID;
CREATE INDEX hwsig_n ON hwsig(n);

CREATE TABLE examples (
  key   TEXT NOT NULL,           -- headword the sentence illustrates
  ord   INTEGER NOT NULL,
  book  TEXT NOT NULL,
  text  TEXT NOT NULL,
  PRIMARY KEY (key, ord)
) WITHOUT ROWID;

/* ---- texts: books read line by line rather than studied word by word ---- */
CREATE TABLE texts (
  id      INTEGER PRIMARY KEY,
  title   TEXT NOT NULL,
  author  TEXT NOT NULL DEFAULT '',
  n_chap  INTEGER NOT NULL DEFAULT 0,
  n_line  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE text_chapters (
  id      INTEGER PRIMARY KEY,
  text_id INTEGER NOT NULL,
  ord     INTEGER NOT NULL,
  title   TEXT NOT NULL,
  n_line  INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX text_chapters_by_text ON text_chapters (text_id, ord);

CREATE TABLE text_lines (
  chapter_id INTEGER NOT NULL,
  ord        INTEGER NOT NULL,
  text       TEXT NOT NULL,
  PRIMARY KEY (chapter_id, ord)
) WITHOUT ROWID;

/* optional per-text data the app can show: translations, per-line pinyin,
   picture-book plates. Empty in this build. */
CREATE TABLE text_notes (
  chapter_id INTEGER NOT NULL, line_ord INTEGER NOT NULL, ord INTEGER NOT NULL,
  english TEXT, zh TEXT, zh_tc TEXT, pinyin TEXT, gloss TEXT, notes TEXT,
  src_ord INTEGER, source TEXT NOT NULL DEFAULT '', app_only TEXT);
CREATE INDEX text_notes_line ON text_notes (chapter_id, source, line_ord, ord);
CREATE TABLE text_chapter_notes (chapter_id INTEGER NOT NULL, english TEXT, label TEXT,
  source TEXT NOT NULL DEFAULT '', app_only TEXT, PRIMARY KEY (chapter_id, source));
CREATE TABLE note_sources (text_id INTEGER NOT NULL, source TEXT NOT NULL,
  label TEXT NOT NULL, ord INTEGER NOT NULL, PRIMARY KEY (text_id, source));
CREATE TABLE text_pinyin (
  chapter_id INTEGER NOT NULL, line_ord INTEGER NOT NULL,
  py TEXT NOT NULL,                  -- one syllable per hanzi, '-' if unknown
  PRIMARY KEY (chapter_id, line_ord));
CREATE TABLE text_passages (chapter_id INTEGER NOT NULL, ord INTEGER NOT NULL,
  first_line INTEGER NOT NULL, n_lines INTEGER NOT NULL,
  english TEXT, notes TEXT, gloss TEXT, PRIMARY KEY (chapter_id, ord));
CREATE TABLE text_plates (chapter_id INTEGER NOT NULL, ord INTEGER NOT NULL,
  line_first INTEGER NOT NULL, n_lines INTEGER NOT NULL, image TEXT NOT NULL,
  PRIMARY KEY (chapter_id, ord)) WITHOUT ROWID;
CREATE TABLE text_epub (text_id INTEGER PRIMARY KEY, file TEXT NOT NULL,
  apart INTEGER NOT NULL DEFAULT 0);
CREATE TABLE chapter_moves (old INTEGER PRIMARY KEY, title TEXT NOT NULL,
  new INTEGER NOT NULL, line_add INTEGER NOT NULL);
CREATE TABLE deck_renames (old TEXT PRIMARY KEY, new TEXT NOT NULL);

CREATE TABLE meta (k TEXT PRIMARY KEY, v TEXT);
"""

def read_pleco(path):
    """A Pleco export -> [(book, chapter, ordinal, headword, pinyin, gloss)]"""
    rows, book, chapter, n = [], "", "", 0
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("//"):
            book, _, chapter = line[2:].partition("/")
            n = 0
        elif line.strip():
            hw, py, df = (line.split("\t") + ["", ""])[:3]
            rows.append((book, chapter, n, hw, py, df))
            n += 1
    return rows


def read_text(title):
    """data/texts/TITLE.txt -> (author, [(chapter, [line, ...]), ...])"""
    author, chapters, head = "", [], 0
    for line in open(os.path.join(DATA, "texts", title + ".txt"), encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("## "):
            chapters.append((line[3:], []))
        elif line.startswith("# "):
            head += 1
            if head == 2:
                author = line[2:]
        elif line:
            chapters[-1][1].append(line)
    return author, chapters


def build(db):
    db.executescript(SCHEMA)
    gfx, mdic = read_mmah_gfx(), read_mmah_dict()
    db.executemany("INSERT INTO chars VALUES (?,?,?,?,?,?)",
                   [(c,) + mdic.get(c, ("", "", "", "")) + (len(gfx.get(c, ([], []))[0]),)
                    for c in sorted(set(gfx) | set(mdic))])
    for c, (outlines, medians) in gfx.items():
        db.executemany("INSERT INTO strokes VALUES (?,?,?,?)",
                       [(c, i, o, medians[i] if i < len(medians) else b"") for i, o in enumerate(outlines)])
        sig = hw_signature([[struct.unpack_from("<hh", m, i) for i in range(0, len(m), 4)]
                            for m in medians[:HW_MAX_STROKES]])
        if sig:
            db.execute("INSERT INTO hwsig VALUES (?,?,?)", (c, len(medians[:HW_MAX_STROKES]), sig))
    print("%d characters with strokes" % len(gfx))

    best = {}            # headword -> (demote, capital, pinyin, gloss): the best entry
    known = set()
    for trad, simp, py, df in read_cedict():
        dem, disp = demote_of(df, simp), accent_pinyin(py)
        db.execute("INSERT INTO words (simp,trad,pinyin,py_disp,py_plain,py_num,defn,demote)"
                   " VALUES (?,?,?,?,?,?,?,?)", (simp, trad, py, disp, plain_pinyin(py), num_pinyin(py), df, dem))
        known.add(simp)
        # a character by its everyday reading, where Make Me a Hanzi names one
        first = mdic.get(simp, ("",))[0].split()[:1] if len(simp) == 1 else []
        e = (dem, disp[:1].isupper(), bool(first) and disp != first[0], disp, df)
        if simp not in best or e[:3] < best[simp][:3]:
            best[simp] = e
    print("%d dictionary entries" % db.execute("SELECT COUNT(*) FROM words").fetchone()[0])

    def gloss(w):
        e = best.get(w)
        if e:
            return e[3], e[4]
        py, df = mdic.get(w, ("", ""))[:2]
        return py.split(" ")[0], df

    rows = []
    for book, chapter, n, hw, alts in read_hsk():
        py, df = gloss(hw)
        if alts:
            df += ("; " if df else "") + "also " + "/".join(alts)
        rows.append((book, chapter, n, hw, py, df))
    rows += read_pleco(HANDWRITING)

    sents = []           # (title, line), for the examples
    for title in TEXTS:
        author, chapters = read_text(title)
        tid = db.execute("INSERT INTO texts (title,author,n_chap,n_line) VALUES (?,?,?,?)",
                         (title, author, len(chapters), sum(len(l) for _, l in chapters))).lastrowid
        seen = set()
        for ci, (ctitle, lines) in enumerate(chapters):
            cid = db.execute("INSERT INTO text_chapters (text_id,ord,title,n_line) VALUES (?,?,?,?)",
                             (tid, ci, ctitle, len(lines))).lastrowid
            db.executemany("INSERT INTO text_lines VALUES (?,?,?)", [(cid, i, l) for i, l in enumerate(lines)])
            # the chapter's deck: each word where the book first uses it
            n = 0
            for line in lines:
                sents.append((title, line))
                for w in segment(line, known):
                    py, df = gloss(w)
                    if w in seen or not (py or df):
                        continue
                    seen.add(w)
                    rows.append((title, ctitle, n, w, py, df))
                    n += 1
        print("  %s: %d chapters, %d words" % (title, len(chapters), len(seen)))

    ids, used, freq = {}, set(), Counter()
    for book, chapter, n, hw, py, df in rows:
        if (book, chapter, hw) in used:      # a homograph listed twice in a level
            continue
        used.add((book, chapter, hw))
        if (book, chapter) not in ids:
            ids[book, chapter] = db.execute("INSERT INTO lists (book,chapter) VALUES (?,?)", (book, chapter)).lastrowid
        hans = HAN.findall(hw)
        db.execute("INSERT INTO entries (list_id,ordinal,headword,pinyin,defn,key,writable) VALUES (?,?,?,?,?,?,?)",
                   (ids[book, chapter], n, hw, py, df, hw, int(bool(hans) and all(c in gfx for c in hans))))
        freq[hw] += 1
        if len(hans) > 1:
            freq.update(set(hans))
    db.execute("UPDATE lists SET n = (SELECT COUNT(*) FROM entries WHERE list_id = lists.id)")
    db.executemany("UPDATE words SET freq=? WHERE simp=?", [(n, w) for w, n in freq.items()])
    print("%d decks, %d cards" % (len(ids), len(used)))

    # up to three short sentences a word, the shortest first
    want, ex = {r[3] for r in rows}, {}
    for title, line in sorted(sents, key=lambda s: len(s[1])):
        if 6 <= len(line) <= 40:
            for w in set(segment(line, known)):
                if w in want and len(ex.setdefault(w, [])) < 3:
                    ex[w].append((title, line))
    db.executemany("INSERT INTO examples VALUES (?,?,?,?)",
                   [(w, i, t, l) for w, e in ex.items() for i, (t, l) in enumerate(e)])
    print("%d words with example sentences" % len(ex))
    db.execute("INSERT INTO meta VALUES ('schema','2')")
    db.commit()
    db.execute("VACUUM")


def main(out):
    """Built beside the database it replaces, so a failed build leaves the
    last good one where it was."""
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    tmp = out + ".tmp"
    if os.path.exists(tmp):
        os.remove(tmp)
    db = sqlite3.connect(tmp)
    build(db)
    db.close()
    os.replace(tmp, out)
    print("wrote %s (%.1f MB)" % (os.path.normpath(out), os.path.getsize(out) / 1e6))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else OUT)
