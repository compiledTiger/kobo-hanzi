#!/usr/bin/env python3
"""Export a text from content.db as an EPUB or a PDF for reading elsewhere.

  export_book.py DB TEXT_ID CHAPTER_ID|all MODE EN_SRC FORMAT OUT [--font F]

  MODE    zh  Chinese only
          py  Chinese page, then the same passage's pinyin page
          en  Chinese page, pinyin page, English page, repeating
  EN_SRC  the English source's key ('' = the book's own); used by mode en
  FORMAT  epub | pdf

Desktop only (the app's export screen runs it).  Needs Python 3 and Pillow
(Pillow measures text for the screenful pages, and draws the PDF's pages).

The pinyin is the app's own: text_pinyin, one syllable per hanzi, and the
dictionary's first reading where the build has none (reading_at in app.cpp).
The English is text_notes for the chosen source, by line; a covered-line
pointer row (no English, src_ord set) shows the sentence's English once, for
the group, on the first line that points at it.

A page is a screenful for a Kobo Libra (1264x1680): consecutive lines grouped
so that the Chinese (about 150-190 hanzi), its pinyin and its English each
fit one page, never splitting a line.  A chapter's title starts a new group.
The last line printed is "ok N pages" (or "error: ..." on stderr, exit 1).
"""
import os, re, sys, sqlite3, zipfile, zlib, uuid, datetime
from xml.sax.saxutils import escape

W, H = 1264, 1680
DPI = 200
MARGIN = 96
TOP, BOTTOM = 130, 1590          # the text area; the footer sits below it
ZH_PX, ZH_LH = 64, 104           # Chinese: 16 hanzi a row, 14 rows
LT_PX, LT_LH = 40, 62            # pinyin (Latin font)
EN_PX, EN_LH = 42, 66            # English
MAX_HANZI = 190
GAP = 22                         # between paragraphs on a page

MODES = {"zh": "中文", "py": "拼音", "en": "中英拼音"}
MODE_TITLE = {"zh": "中文", "py": "中文+拼音", "en": "中英"}

LATIN_FONTS = ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
               "/usr/share/fonts/dejavu/DejaVuSans.ttf",
               "/usr/share/fonts/TTF/DejaVuSans.ttf",
               "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
               "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"]

PUNCT = {"。": ".", "，": ",", "、": ",", "；": ";", "：": ":", "？": "?", "！": "!",
         "“": '"', "”": '"', "‘": "'", "’": "'", "「": '"', "」": '"', "『": '"',
         "』": '"', "《": '"', "》": '"', "〈": '"', "〉": '"', "（": "(", "）": ")",
         "—": "-", "…": "...", "・": "-", "·": "-"}
OPENING = set('“‘「『《〈（(')


def die(msg):
    sys.stderr.write("error: %s\n" % msg)
    sys.exit(1)


def is_han(ch):
    c = ord(ch)
    return 0x3400 <= c <= 0x4DBF or 0x4E00 <= c <= 0x9FFF


def find_font(arg, dbpath):
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [arg] if arg else []
    d = os.path.dirname(os.path.abspath(dbpath))
    cands += [os.path.join(d, "LXGWWenKaiLite-Regular.ttf"), os.path.join(d, "..", "data", "fonts", "LXGWWenKaiLite-Regular.ttf"),
              os.path.join(here, "..", "data", "fonts", "LXGWWenKaiLite-Regular.ttf"),
              os.path.join(here, "LXGWWenKaiLite-Regular.ttf")]
    for c in cands:
        if c and os.path.exists(c):
            return c
    die("LXGWWenKaiLite-Regular.ttf not found")


def find_latin():
    for f in LATIN_FONTS:
        if os.path.exists(f):
            return f
    die("no Latin font found (DejaVu Sans)")


# ---- data ------------------------------------------------------------------

class Data:
    def __init__(self, path):
        if not os.path.exists(path):
            die("no database at %s" % path)
        self.c = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
        self._first = {}

    def first_reading(self, ch):
        if ch not in self._first:
            r = self.c.execute("SELECT pinyin FROM chars WHERE ch=?", (ch,)).fetchone()
            s = (r[0] if r else "") or ""
            self._first[ch] = re.split(r"[ ,/]", s.strip())[0] if s.strip() else ""
        return self._first[ch]

    def pinyin_line(self, chapter, ord_, text):
        """Syllables for the hanzi of `text`, as the app shows them."""
        r = self.c.execute("SELECT py FROM text_pinyin WHERE chapter_id=? AND line_ord=?",
                           (chapter, ord_)).fetchone()
        syl = [(s if s != "-" else "") for s in (r[0].split() if r and r[0] else [])]
        out, h = [], 0
        for ch in text:
            if is_han(ch):
                s = syl[h] if h < len(syl) else ""
                h += 1
                out.append(s or self.first_reading(ch) or ch)
        return out

    def english_rows(self, chapter, ord_, src):
        rows = self.c.execute(
            "SELECT english, src_ord, IFNULL(notes,''), IFNULL(gloss,''), IFNULL(zh,'')"
            " FROM text_notes WHERE chapter_id=? AND line_ord=? AND source=? ORDER BY ord",
            (chapter, ord_, src)).fetchall()
        return rows


def pinyin_text(data, chapter, ord_, text):
    """One line's pinyin, with the punctuation carried over as ASCII."""
    syl = iter(data.pinyin_line(chapter, ord_, text))
    toks, pend = [], ""
    for ch in text:
        if is_han(ch):
            toks.append(pend + next(syl))
            pend = ""
        elif ch in PUNCT:
            p = PUNCT[ch]
            if ch in OPENING:
                pend += p
            elif toks:
                toks[-1] += p
        elif ch.isalpha():
            toks.append(pend + ch)
            pend = ""
    return " ".join(toks)


def load(data, text_id, chapter, mode, src):
    t = data.c.execute("SELECT title, author FROM texts WHERE id=?", (text_id,)).fetchone()
    if not t:
        die("no text %s" % text_id)
    if data.c.execute("SELECT 1 FROM text_epub WHERE text_id=?", (text_id,)).fetchone():
        die("a 连环画 is not exported (its pictures are in its EPUB)")
    q = "SELECT id, title FROM text_chapters WHERE text_id=?"
    args = [text_id]
    if chapter != "all":
        q += " AND id=?"
        args.append(int(chapter))
    chaps = data.c.execute(q + " ORDER BY ord", args).fetchall()
    if not chaps:
        die("no such chapter")
    out = []
    shown = set()
    for cid, ctitle in chaps:
        lines = []
        for ord_, text in data.c.execute(
                "SELECT ord, text FROM text_lines WHERE chapter_id=? ORDER BY ord", (cid,)):
            zh = re.sub(r"[0-9]", "", text)          # the footnote digits (is_footnote)
            if not zh.strip():
                continue
            py = pinyin_text(data, cid, ord_, zh) if mode != "zh" else ""
            en = None
            if mode == "en":
                en = english_for(data, cid, ord_, src, shown)
            lines.append({"zh": zh, "py": py, "en": en})
        out.append({"id": cid, "title": ctitle, "lines": lines})
    if mode == "en" and not any(l["en"] for c in out for l in c["lines"]):
        die("the source '%s' has no English for this text" % src)
    return t[0], t[1], out


def english_for(data, cid, ord_, src, shown):
    """This line's English, or the English of the sentence it is covered by,
    shown once: on the first line that reaches it."""
    rows = data.english_rows(cid, ord_, src)
    own = [r[0] for r in rows if (r[0] or "").strip()]
    if own:
        key = (cid, ord_)
        if key in shown:
            return None
        shown.add(key)
        return "\n".join(s.strip() for s in own)
    for r in rows:
        if r[1] is not None and not (r[2] or r[3] or r[4]) and r[1] != ord_:
            key = (cid, r[1])
            if key in shown:
                return None
            shown.add(key)
            tgt = [x[0] for x in data.english_rows(cid, r[1], src) if (x[0] or "").strip()]
            return "\n".join(s.strip() for s in tgt) if tgt else None
    return None


# ---- layout (shared by both formats, so the pages are the same) --------------

class Layout:
    def __init__(self, zh_font, latin_font):
        from PIL import ImageFont
        self.F = ImageFont
        self.zh_path, self.lt_path = zh_font, latin_font
        self.zh = ImageFont.truetype(zh_font, ZH_PX)
        self.zh_title = ImageFont.truetype(zh_font, 80)
        self.zh_small = ImageFont.truetype(zh_font, 30)
        self.lt = ImageFont.truetype(latin_font, LT_PX)
        self.en = ImageFont.truetype(latin_font, EN_PX)
        self.lt_small = ImageFont.truetype(latin_font, 26)
        self.textw = W - 2 * MARGIN

    def wrap(self, font, s, closing_ok=True):
        """Rows of `s`, by measured width.  Chinese breaks anywhere (a closing
        mark never starts a row); Latin breaks at spaces."""
        rows = []
        for para in s.split("\n"):
            if not para:
                rows.append("")
                continue
            if " " in para and not any(is_han(ch) for ch in para):
                cur = ""
                for wd in para.split(" "):
                    t = wd if not cur else cur + " " + wd
                    if cur and font.getlength(t) > self.textw:
                        rows.append(cur)
                        cur = wd
                    else:
                        cur = t
                rows.append(cur)
            else:
                cur = ""
                for ch in para:
                    if cur and font.getlength(cur + ch) > self.textw and ch not in "，。、；：？！”’》」』）…":
                        rows.append(cur)
                        cur = ch
                    else:
                        cur += ch
                rows.append(cur)
        return rows

    def zh_rows(self, text):
        return len(self.wrap(self.zh, text))

    def lt_rows(self, text):
        return len(self.wrap(self.lt, text))

    def en_rows(self, text):
        return len(self.wrap(self.en, text))


def chunks(chaps, mode, L):
    """Screenfuls: lists of lines, each fitting one page in each kind."""
    zh_budget = (BOTTOM - TOP) // ZH_LH
    lt_budget = (BOTTOM - TOP) // LT_LH
    en_budget = (BOTTOM - TOP) // EN_LH
    out = []
    for ch in chaps:
        cur, hz, zr, lr, er = [], 0, 2, 2, 2          # the title takes two rows
        for ln in ch["lines"]:
            n = sum(1 for c in ln["zh"] if is_han(c))
            z = L.zh_rows(ln["zh"])
            l = L.lt_rows(ln["py"]) if mode != "zh" else 0
            e = 0
            if mode == "en":
                e = L.en_rows(ln["en"] or "")
            gaps = len(cur)
            over = (hz + n > MAX_HANZI or zr + z > zh_budget
                    or lr + l + (gaps * GAP) // LT_LH > lt_budget
                    or er + e + (gaps * GAP) // EN_LH > en_budget)
            if cur and over:
                out.append((ch, cur))
                cur, hz, zr, lr, er = [], 0, 0, 0, 0
            cur.append(ln)
            hz += n; zr += z; lr += l; er += e
        if cur:
            out.append((ch, cur))
    return out


def pages_of(groups, mode):
    """[(kind, chapter, first_of_chapter, [paragraph strings])] in reading order."""
    pages, seen = [], set()
    for ch, lines in groups:
        first = ch["id"] not in seen
        seen.add(ch["id"])
        pages.append(("zh", ch, first, [l["zh"] for l in lines]))
        if mode in ("py", "en"):
            pages.append(("py", ch, first, [l["py"] for l in lines]))
        if mode == "en":
            en = [l["en"] for l in lines if l["en"]]
            pages.append(("en", ch, first, en or ["(no English for this passage in this translation)"]))
    return pages


# ---- PDF -----------------------------------------------------------------------

def render_page(L, kind, ch, first, paras, cont=False):
    """One page image (mode L), plus the paragraphs that did not fit."""
    from PIL import Image, ImageDraw
    im = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(im)
    y = TOP
    if first and not cont:
        d.text((MARGIN, y + 60), ch["title"], font=L.zh_title, fill=0, anchor="ls")
        y += 140 if kind == "zh" else 130
        d.line([(MARGIN, y - 36), (MARGIN + 200, y - 36)], fill=120, width=3)
    font, lh = {"zh": (L.zh, ZH_LH), "py": (L.lt, LT_LH), "en": (L.en, EN_LH)}[kind]
    base = int(lh * 0.72)
    rest = []
    for i, p in enumerate(paras):
        rows = L.wrap(font, p)
        if y + len(rows) * lh > BOTTOM and y > TOP + 10:
            rest = paras[i:]
            break
        for r in rows:
            d.text((MARGIN, y + base), r, font=font, fill=0 if kind != "py" else 20, anchor="ls")
            y += lh
        y += GAP if kind != "zh" else GAP // 2
    tag = {"zh": "中文", "py": "pinyin", "en": "English"}[kind]
    d.text((MARGIN, H - 70), ch["title"], font=L.zh_small, fill=110, anchor="ls")
    d.text((W - MARGIN, H - 70), tag, font=L.lt_small if kind != "zh" else L.zh_small,
           fill=110, anchor="rs")
    return im, rest


class PdfWriter:
    """A small streaming PDF writer: one Flate-compressed greyscale image a
    page, the pages' bookmarks as an outline.  Pillow's own PDF save keeps
    every page in memory, which a 700-page book cannot afford."""

    def __init__(self, path, title):
        self.f = open(path, "wb")
        self.f.write(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        self.offs, self.pages, self.marks, self.title = {}, [], [], title
        self.next = 3                                  # 1 catalog, 2 pages

    def _obj(self, n, body):
        self.offs[n] = self.f.tell()
        self.f.write(b"%d 0 obj\n" % n + body + b"\nendobj\n")

    def add(self, im, mark=None):
        data = zlib.compress(im.tobytes(), 6)
        img, content, page = self.next, self.next + 1, self.next + 2
        self.next += 3
        self._obj(img, b"<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray"
                       b" /BitsPerComponent 8 /Filter /FlateDecode /Length %d >>\nstream\n"
                  % (W, H, len(data)) + data + b"\nendstream")
        pw, ph = W * 72.0 / DPI, H * 72.0 / DPI
        cs = ("q %.3f 0 0 %.3f 0 0 cm /Im Do Q" % (pw, ph)).encode()
        self._obj(content, b"<< /Length %d >>\nstream\n" % len(cs) + cs + b"\nendstream")
        self._obj(page, ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.3f %.3f] /Contents %d 0 R"
                         " /Resources << /XObject << /Im %d 0 R >> >> >>" % (pw, ph, content, img)).encode())
        self.pages.append(page)
        if mark:
            self.marks.append((mark, page))

    def close(self):
        kids = " ".join("%d 0 R" % p for p in self.pages)
        self._obj(2, ("<< /Type /Pages /Count %d /Kids [%s] >>" % (len(self.pages), kids)).encode())
        root = "/Type /Catalog /Pages 2 0 R"
        if self.marks:
            o = self.next
            ids = [o + 1 + i for i in range(len(self.marks))]
            self._obj(o, ("<< /Type /Outlines /First %d 0 R /Last %d 0 R /Count %d >>"
                          % (ids[0], ids[-1], len(ids))).encode())
            for i, (t, pg) in enumerate(self.marks):
                s = b"\xfe\xff" + t.encode("utf-16-be")
                hexs = "<" + s.hex() + ">"
                links = ""
                if i > 0: links += " /Prev %d 0 R" % ids[i - 1]
                if i + 1 < len(ids): links += " /Next %d 0 R" % ids[i + 1]
                self._obj(ids[i], ("<< /Title %s /Parent %d 0 R%s /Dest [%d 0 R /Fit] >>"
                                   % (hexs, o, links, pg)).encode())
            self.next = ids[-1] + 1
            root += " /Outlines %d 0 R /PageMode /UseOutlines" % o
        self._obj(1, ("<< %s >>" % root).encode())
        info = self.next
        self._obj(info, ("<< /Title <%s> >>" % (b"\xfe\xff" + self.title.encode("utf-16-be")).hex()).encode())
        xref = self.f.tell()
        n = info + 1
        self.f.write(b"xref\n0 %d\n0000000000 65535 f \n" % n)
        for i in range(1, n):
            self.f.write(b"%010d 00000 n \n" % self.offs[i])
        self.f.write(("trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%d\n%%%%EOF\n"
                      % (n, info, xref)).encode())
        self.f.close()


def write_pdf(path, title, pages, L):
    pw = PdfWriter(path, title)
    count = 0
    for kind, ch, first, paras in pages:
        cont = False
        while True:
            im, rest = render_page(L, kind, ch, first, paras, cont)
            pw.add(im, ch["title"] if first and not cont and kind == "zh" else None)
            count += 1
            if not rest:
                break
            paras, cont = rest, True
    pw.close()
    return count


# ---- EPUB ----------------------------------------------------------------------

CSS = """\
body { margin: 0; padding: 0; line-height: 1.7; }
.page { page-break-after: always; break-after: page; padding: 0.4em 0.6em; }
.page:last-child { page-break-after: auto; break-after: auto; }
h1 { font-size: 1.6em; margin: 0 0 0.8em 0; font-weight: normal; }
p { margin: 0 0 0.45em 0; text-indent: 0; }
.zh p { font-size: 1.7em; line-height: 1.6; }
.py p { font-size: 1.15em; line-height: 1.6; margin-bottom: 0.7em; }
.en p { font-size: 1.1em; line-height: 1.55; margin-bottom: 0.7em; }
.tag { font-size: 0.7em; color: #666; margin-top: 1em; }
"""


def xe(s):
    return escape(s)


def write_epub(path, title, author, text_id, pages, mode, chaps, L):
    from itertools import groupby
    ident = "urn:uuid:" + str(uuid.uuid5(uuid.NAMESPACE_URL, "hanzi-trainer-export-%s-%s" % (text_id, mode)))
    files, order = [], []
    by_ch = []
    for cid, grp in groupby(pages, key=lambda p: p[1]["id"]):
        g = list(grp)
        by_ch.append((g[0][1], g))
    for i, (ch, g) in enumerate(by_ch):
        name = "c%03d.xhtml" % (i + 1)
        body = []
        for kind, _, first, paras in g:
            cls = {"zh": "zh", "py": "py", "en": "en"}[kind]
            ps = []
            for p in paras:
                ps.append("<p>%s</p>" % "<br/>".join(xe(x) for x in p.split("\n")))
            head = "<h1>%s</h1>" % xe(ch["title"]) if first else ""
            tag = '<p class="tag">%s</p>' % {"zh": "中文", "py": "pinyin", "en": "English"}[kind]
            body.append('<div class="page %s">%s%s%s</div>' % (cls, head, "".join(ps), tag))
        doc = ('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
               '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" '
               'lang="zh" xml:lang="zh">\n<head><meta charset="utf-8"/><title>%s</title>'
               '<link rel="stylesheet" type="text/css" href="style.css"/></head>\n<body>\n%s\n</body></html>\n'
               % (xe(ch["title"]), "\n".join(body)))
        files.append(("OEBPS/" + name, doc))
        order.append((name, ch["title"]))
    nav = ('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
           '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="zh" xml:lang="zh">'
           '<head><meta charset="utf-8"/><title>%s</title></head><body>'
           '<nav epub:type="toc" id="toc"><h1>%s</h1><ol>%s</ol></nav></body></html>\n'
           % (xe(title), xe(title),
              "".join('<li><a href="%s">%s</a></li>' % (n, xe(t)) for n, t in order)))
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    items = "".join('<item id="c%d" href="%s" media-type="application/xhtml+xml"/>' % (i + 1, n)
                    for i, (n, _) in enumerate(order))
    spine = "".join('<itemref idref="c%d"/>' % (i + 1) for i in range(len(order)))
    opf = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid" xml:lang="zh">\n'
           '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
           '<dc:identifier id="uid">%s</dc:identifier><dc:title>%s</dc:title>'
           '<dc:language>zh</dc:language>%s'
           '<dc:rights>Private copy for personal use</dc:rights>'
           '<meta property="dcterms:modified">%s</meta></metadata>\n'
           '<manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
           '<item id="css" href="style.css" media-type="text/css"/>%s</manifest>\n'
           '<spine>%s</spine></package>\n'
           % (ident, xe(title), ("<dc:creator>%s</dc:creator>" % xe(author)) if author else "",
              now, items, spine))
    container = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                 '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                 '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
                 '</rootfiles></container>\n')
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", container, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/nav.xhtml", nav, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/style.css", CSS, compress_type=zipfile.ZIP_DEFLATED)
        for n, c in files:
            z.writestr(n, c, compress_type=zipfile.ZIP_DEFLATED)
    return len(pages)


def main(argv):
    font = None
    a = []
    i = 0
    while i < len(argv):
        if argv[i] == "--font" and i + 1 < len(argv):
            font = argv[i + 1]
            i += 2
        else:
            a.append(argv[i])
            i += 1
    if len(a) != 7:
        die("usage: export_book.py DB TEXT_ID CHAPTER|all MODE EN_SRC epub|pdf OUT [--font F]")
    db, text_id, chapter, mode, src, fmt, out = a
    if mode not in MODES:
        die("mode must be zh, py or en")
    if fmt not in ("epub", "pdf"):
        die("format must be epub or pdf")
    try:
        import PIL  # noqa: F401
    except ImportError:
        die("Pillow is not installed (python3 -m pip install pillow)")
    data = Data(db)
    title, author, chaps = load(data, int(text_id), chapter, mode, src)
    L = Layout(find_font(font, db), find_latin())
    groups = chunks(chaps, mode, L)
    if not groups:
        die("nothing to export")
    pages = pages_of(groups, mode)
    full = "%s · %s" % (title, MODE_TITLE[mode])
    d = os.path.dirname(os.path.abspath(out))
    os.makedirs(d, exist_ok=True)
    tmp = out + ".part"
    n = write_pdf(tmp, full, pages, L) if fmt == "pdf" else write_epub(tmp, full, author, text_id, pages, mode, chaps, L)
    os.replace(tmp, out)
    print("ok %d pages" % n)


if __name__ == "__main__":
    main(sys.argv[1:])
