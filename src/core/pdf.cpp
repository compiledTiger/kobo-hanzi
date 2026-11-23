/* See pdf.h. MuPDF reports errors by longjmp (fz_try / fz_catch), which
 * jumps over C++ destructors: so nothing with a destructor is made inside a
 * fz_try here, the MuPDF calls fill plain variables, and the strings and
 * vectors are built after. Every public function catches at its own edge. */
#include "pdf.h"

#include <mupdf/fitz.h>

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <algorithm>
#include <memory>

namespace {

fz_context *ctx = NULL;
fz_document *doc = NULL;
std::string doc_path;
int npages = 0;

/* The page in hand: its bounds, its display list (parsed once, drawn as often
 * as asked) and, once asked, its text and how much pictures cover. */
int cur = -1;
fz_rect cur_bounds;
fz_display_list *cur_list = NULL;
fz_stext_page *cur_text = NULL;
float cur_cover = -1;

char err[256];
int warns = 0;                  /* warnings printed for this document */
const int MAX_WARNS = 20;

/* The CJK font's file, read once per open document; each PDF font that
 * asks gets a font of its own over these bytes, since MuPDF writes a PDF
 * font's widths and metrics into its fz_font. */
std::string cjk_path;
fz_buffer *cjk_data = NULL;
int cjk_failed = 0;             /* tried and could not: do not read it again */

void set_err(const char *what, const char *why) {
    snprintf(err, sizeof err, "%s%s%s", what, why && *why ? ": " : "", why ? why : "");
}

/* MuPDF's own messages, to the app's log (stderr), and not without end: a
 * damaged file can warn on every object. */
void on_warning(void *, const char *msg) {
    if (warns < MAX_WARNS) fprintf(stderr, "pdf: warning: %s\n", msg);
    else if (warns == MAX_WARNS) fprintf(stderr, "pdf: further warnings not shown\n");
    warns++;
}
void on_error(void *, const char *msg) { fprintf(stderr, "pdf: error: %s\n", msg); }

/* The app's Kaiti for a Chinese, Japanese or Korean font the file names but
 * does not carry. MuPDF is built with TOFU_CJK (no fonts of its own for
 * them), so without this such text is not drawn at all. Must not throw. */
fz_font *kaiti(fz_context *c) {
    if (cjk_path.empty() || cjk_failed) return NULL;
    const char *path = cjk_path.c_str();
    fz_font *f = NULL;
    fz_var(f);
    fz_try(c) {
        if (!cjk_data) cjk_data = fz_read_file(c, path);
        f = fz_new_font_from_buffer(c, "Kaiti", cjk_data, 0, 0);
    }
    fz_catch(c) {
        fprintf(stderr, "pdf: the CJK font %s: %s\n", path, fz_caught_message(c));
        cjk_failed = 1;
        f = NULL;
    }
    return f;
}

/* Called for a CID font of a Chinese, Japanese or Korean collection, or
 * one MuPDF knows by name (SimSun, Song, MingLiU ...), that is not embedded.
 * Plain fonts that are not embedded keep MuPDF's own substitutes: a one-byte
 * font cannot carry Chinese whatever it is drawn with. */
fz_font *load_cjk(fz_context *c, const char *, int, int) { return kaiti(c); }

int ensure_ctx() {
    if (ctx) return 1;
    ctx = fz_new_context(NULL, NULL, PDF_STORE_BYTES);
    if (!ctx) { set_err("MuPDF could not start", "out of memory"); return 0; }
    fz_set_warning_callback(ctx, on_warning, NULL);
    fz_set_error_callback(ctx, on_error, NULL);
    int ok = 1;
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        /* 16 levels of grey on the edges, which is what the panel shows:
         * finer antialiasing costs time and cannot be seen. */
        fz_set_text_aa_level(ctx, 4);
        fz_set_graphics_aa_level(ctx, 4);
        /* Colour management turns RGB into grey exactly; the panel is grey
         * and the CPU slow, so the plain formula does. */
        fz_disable_icc(ctx);
        fz_install_load_system_font_funcs(ctx, NULL, load_cjk, NULL);
    }
    fz_catch(ctx) {
        set_err("MuPDF could not start", fz_caught_message(ctx));
        ok = 0;
    }
    if (!ok) { fz_drop_context(ctx); ctx = NULL; }
    return ok;
}

void drop_page() {
    if (ctx) {
        fz_drop_stext_page(ctx, cur_text);
        fz_drop_display_list(ctx, cur_list);
    }
    cur_text = NULL;
    cur_list = NULL;
    cur = -1;
    cur_cover = -1;
}

/* Make `n` the page in hand. The page object itself is let go once its
 * display list is made: the list is all that drawing and the text need. */
int load_page(int n) {
    if (!doc) { set_err("no PDF is open", NULL); return 0; }
    if (n < 0 || n >= npages) { set_err("no such page", NULL); return 0; }
    if (n == cur) return 1;
    drop_page();
    fz_page *pg = NULL;
    fz_display_list *list = NULL;
    fz_rect b = fz_empty_rect;
    int ok = 1;
    fz_var(pg);
    fz_var(list);
    fz_try(ctx) {
        pg = fz_load_page(ctx, doc, n);
        b = fz_bound_page(ctx, pg);
        list = fz_new_display_list_from_page(ctx, pg);
    }
    fz_always(ctx)
        fz_drop_page(ctx, pg);
    fz_catch(ctx) {
        set_err("the page could not be read", fz_caught_message(ctx));
        fz_report_error(ctx);
        fz_drop_display_list(ctx, list);
        ok = 0;
    }
    if (!ok) return 0;
    if (fz_is_empty_rect(b)) b = fz_make_rect(0, 0, 612, 792);   /* a page with no size: Letter */
    cur = n;
    cur_bounds = b;
    cur_list = list;
    return 1;
}

int norm_rot(int rot) { return (((rot / 90) % 4) + 4) % 4 * 90; }

/* Page to view: scaled, turned clockwise (fz_rotate turns x towards y, which
 * with y down is clockwise), then moved so the view starts at (0, 0). */
fz_matrix view_ctm(float scale, int rot) {
    fz_matrix m = fz_concat(fz_scale(scale, scale), fz_rotate((float)norm_rot(rot)));
    fz_rect r = fz_transform_rect(cur_bounds, m);
    return fz_concat(m, fz_translate(-r.x0, -r.y0));
}

/* The text of the page in hand, read now if it has not been. Pictures are
 * kept in it (as their boxes) for pdf_image_cover; spaces MuPDF would add
 * between distant characters are not. */
fz_stext_page *page_text(int n) {
    if (!load_page(n)) return NULL;
    if (cur_text) return cur_text;
    fz_stext_page *t = NULL;
    int ok = 1;
    fz_var(t);
    fz_try(ctx) {
        fz_stext_options opts;
        fz_init_stext_options(ctx, &opts);
        opts.flags = FZ_STEXT_PRESERVE_IMAGES | FZ_STEXT_INHIBIT_SPACES | FZ_STEXT_CLIP;
        t = fz_new_stext_page_from_display_list(ctx, cur_list, &opts);
    }
    fz_catch(ctx) {
        set_err("the page's text could not be read", fz_caught_message(ctx));
        fz_report_error(ctx);
        ok = 0;
    }
    if (!ok) return NULL;
    cur_text = t;
    return t;
}

/* Every block of the text, into structure blocks too (a tagged PDF nests
 * its paragraphs). No MuPDF call here can throw: it is pointer walking. */
template <class F> void each_block(fz_stext_block *b, F &f, int depth = 0) {
    for (; b; b = b->next) {
        if (b->type == FZ_STEXT_BLOCK_STRUCT) {
            if (b->u.s.down && depth < 32) each_block(b->u.s.down->first_block, f, depth + 1);
        } else {
            f(b);
        }
    }
}

int blank(int c) {
    return c <= 32 || c == 0xA0 || c == 0x3000 || c == 0xFEFF ||
           (c >= 0x2000 && c <= 0x200B) || c == 0x2028 || c == 0x2029;
}

pdf_char make_char(const fz_stext_line *ln, const fz_stext_char *ch) {
    pdf_char o;
    memset(&o, 0, sizeof o);
    o.c = ch->c;
    int len = fz_runetochar(o.utf8, ch->c);
    o.utf8[len > 0 && len < (int)sizeof o.utf8 ? len : 0] = 0;
    fz_rect r = fz_rect_from_quad(ch->quad);
    o.box = {r.x0, r.y0, r.x1, r.y1};
    o.vertical = ln->wmode == 1;
    return o;
}

/* How far the point is from the box: 0 inside. */
float dist(fz_rect r, float x, float y) {
    float dx = x < r.x0 ? r.x0 - x : x > r.x1 ? x - r.x1 : 0;
    float dy = y < r.y0 ? r.y0 - y : y > r.y1 ? y - r.y1 : 0;
    return sqrtf(dx * dx + dy * dy);
}

/* The character under (x, y) or nearest within slack, and its line. Among
 * boxes that all hold the point (overlapping lines), the one whose centre is
 * nearest. */
int find_char(int n, float x, float y, float slack,
              const fz_stext_line **lp, const fz_stext_char **cp) {
    fz_stext_page *t = page_text(n);
    if (!t) return 0;
    float best = INFINITY, best_centre = INFINITY;
    const fz_stext_line *bl = NULL;
    const fz_stext_char *bc = NULL;
    auto f = [&](fz_stext_block *b) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) return;
        for (fz_stext_line *ln = b->u.t.first_line; ln; ln = ln->next)
            for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next) {
                if (blank(ch->c)) continue;
                fz_rect r = fz_rect_from_quad(ch->quad);
                float d = dist(r, x, y);
                if (d > slack || d > best) continue;
                float cx = (r.x0 + r.x1) / 2 - x, cy = (r.y0 + r.y1) / 2 - y;
                float dc = cx * cx + cy * cy;
                if (d < best || dc < best_centre) {
                    best = d;
                    best_centre = dc;
                    bl = ln;
                    bc = ch;
                }
            }
    };
    each_block(t->first_block, f);
    if (!bc) { set_err("no text there", NULL); return 0; }
    *lp = bl;
    *cp = bc;
    return 1;
}

}  // namespace

int pdf_open(const char *path) {
    pdf_close();
    err[0] = 0;
    if (!path || !*path) { set_err("no file given", NULL); return 0; }
    if (!ensure_ctx()) return 0;
    warns = 0;
    cjk_failed = 0;
    fz_document *d = NULL;
    int pages = 0, ok = 1;
    const char *why = NULL;
    fz_var(d);
    fz_var(why);
    fz_try(ctx) {
        d = fz_open_document(ctx, path);
        if (fz_needs_password(ctx, d) && !fz_authenticate_password(ctx, d, ""))
            why = "it needs a password";
        else
            pages = fz_count_pages(ctx, d);
    }
    fz_catch(ctx) {
        set_err("the PDF could not be opened", fz_caught_message(ctx));
        fz_report_error(ctx);
        ok = 0;
    }
    if (ok && why) { set_err("the PDF could not be opened", why); ok = 0; }
    if (ok && pages <= 0) { set_err("the PDF has no pages", NULL); ok = 0; }
    if (!ok) {
        fz_drop_document(ctx, d);
        return 0;
    }
    doc = d;
    npages = pages;
    doc_path = path;
    return 1;
}

void pdf_close(void) {
    drop_page();
    if (ctx) {
        fz_drop_document(ctx, doc);
        fz_drop_buffer(ctx, cjk_data);
        /* what was decoded for this document is of no use to the next */
        fz_empty_store(ctx);
    }
    doc = NULL;
    cjk_data = NULL;
    npages = 0;
    doc_path.clear();
}

int pdf_is_open(void) { return doc != NULL; }
const char *pdf_path(void) { return doc_path.c_str(); }
const char *pdf_error(void) { return err; }

void pdf_shutdown(void) {
    pdf_close();
    if (ctx) fz_drop_context(ctx);
    ctx = NULL;
}

void pdf_set_cjk_font(const char *ttf_path) {
    std::string p = ttf_path ? ttf_path : "";
    if (p == cjk_path) return;
    cjk_path = p;
    if (ctx) fz_drop_buffer(ctx, cjk_data);
    cjk_data = NULL;
    cjk_failed = 0;
}

int pdf_pages(void) { return doc ? npages : 0; }

int pdf_page_size(int page, float *w, float *h) {
    err[0] = 0;
    if (!load_page(page)) return 0;
    if (w) *w = cur_bounds.x1 - cur_bounds.x0;
    if (h) *h = cur_bounds.y1 - cur_bounds.y0;
    return 1;
}

int pdf_view_size(int page, float scale, int rot, int *w, int *h) {
    err[0] = 0;
    if (!load_page(page)) return 0;
    fz_irect r = fz_round_rect(fz_transform_rect(cur_bounds, view_ctm(scale, rot)));
    if (w) *w = r.x1 - r.x0;
    if (h) *h = r.y1 - r.y0;
    return 1;
}

int pdf_view_to_page(int page, float scale, int rot, float vx, float vy, float *px, float *py) {
    err[0] = 0;
    if (!load_page(page) || scale <= 0) return 0;
    fz_point p = fz_transform_point_xy(vx, vy, fz_invert_matrix(view_ctm(scale, rot)));
    if (px) *px = p.x;
    if (py) *py = p.y;
    return 1;
}

int pdf_page_to_view(int page, float scale, int rot, float px, float py, float *vx, float *vy) {
    err[0] = 0;
    if (!load_page(page)) return 0;
    fz_point p = fz_transform_point_xy(px, py, view_ctm(scale, rot));
    if (vx) *vx = p.x;
    if (vy) *vy = p.y;
    return 1;
}

int pdf_box_to_view(int page, float scale, int rot, pdf_box b, pdf_box *out) {
    err[0] = 0;
    if (!load_page(page)) return 0;
    fz_rect r = fz_transform_rect(fz_make_rect(b.x0, b.y0, b.x1, b.y1), view_ctm(scale, rot));
    if (out) *out = {r.x0, r.y0, r.x1, r.y1};
    return 1;
}

int pdf_render(int page, float scale, int rot, int x, int y, int w, int h,
               uint8_t *dst, int stride) {
    err[0] = 0;
    if (!dst || w <= 0 || h <= 0 || stride < w || scale <= 0) {
        set_err("nothing to draw into", NULL);
        return 0;
    }
    for (int r = 0; r < h; r++) memset(dst + (size_t)r * stride, 0xFF, (size_t)w);
    if (!load_page(page)) return 0;
    fz_matrix ctm = view_ctm(scale, rot);
    fz_pixmap *pix = NULL;
    fz_device *dev = NULL;
    int ok = 1;
    fz_var(pix);
    fz_var(dev);
    fz_try(ctx) {
        /* the caller's rows, as a pixmap placed at (x, y) of the view: the
         * draw device clips to it and the list skips what lies outside */
        pix = fz_new_pixmap_with_data(ctx, fz_device_gray(ctx), w, h, NULL, 0, stride, dst);
        pix->x = x;
        pix->y = y;
        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_display_list(ctx, cur_list, dev, ctm, fz_make_rect((float)x, (float)y,
                            (float)(x + w), (float)(y + h)), NULL);
        fz_close_device(ctx, dev);
    }
    fz_always(ctx) {
        fz_drop_device(ctx, dev);
        fz_drop_pixmap(ctx, pix);           /* the samples are the caller's: not freed */
    }
    fz_catch(ctx) {
        set_err("the page could not be drawn", fz_caught_message(ctx));
        fz_report_error(ctx);
        ok = 0;
    }
    if (!ok)
        for (int r = 0; r < h; r++) memset(dst + (size_t)r * stride, 0xFF, (size_t)w);
    return ok;
}

int pdf_char_at(int page, float px, float py, float slack, pdf_char *out) {
    err[0] = 0;
    const fz_stext_line *ln;
    const fz_stext_char *ch;
    if (!find_char(page, px, py, slack, &ln, &ch)) return 0;
    if (out) *out = make_char(ln, ch);
    return 1;
}

/* Whether stext line `b` carries on from line `a`, the next piece of the
 * same line of print. MuPDF makes every character of vertical text a line of
 * its own, and may split a horizontal line where it has a wide gap; a column
 * of 道可道 is one line to a reader. The two must run the same way, side by
 * side across it (half the narrower overlapping), and follow on along it with
 * at most a character's gap. */
static bool continues(const fz_stext_line *a, const fz_stext_line *b) {
    bool va = a->wmode == 1 || fabsf(a->dir.y) > fabsf(a->dir.x);
    bool vb = b->wmode == 1 || fabsf(b->dir.y) > fabsf(b->dir.x);
    if (va != vb) return false;
    fz_rect r = a->bbox, q = b->bbox;
    float c0 = va ? r.x0 : r.y0, c1 = va ? r.x1 : r.y1;     /* across */
    float d0 = va ? q.x0 : q.y0, d1 = va ? q.x1 : q.y1;
    float overlap = fminf(c1, d1) - fmaxf(c0, d0);
    float size = fminf(c1 - c0, d1 - d0);
    if (size <= 0 || overlap < size / 2) return false;
    float end = va ? r.y1 : r.x1, start = va ? q.y0 : q.x0;  /* along */
    float gap = start - end;
    return gap > -size / 2 && gap < size * 1.5f;
}

int pdf_line_at(int page, float px, float py, float slack,
                std::vector<pdf_char> *line, int *at) {
    err[0] = 0;
    const fz_stext_line *ln;
    const fz_stext_char *hit;
    if (!find_char(page, px, py, slack, &ln, &hit)) return 0;
    const fz_stext_line *first = ln, *last = ln;
    for (int i = 0; i < 4096 && first->prev && continues(first->prev, first); i++) first = first->prev;
    for (int i = 0; i < 4096 && last->next && continues(last, last->next); i++) last = last->next;
    if (line) line->clear();
    int k = 0, idx = 0;
    for (const fz_stext_line *l = first;; l = l->next) {
        for (const fz_stext_char *ch = l->first_char; ch; ch = ch->next) {
            if (blank(ch->c)) continue;
            if (ch == hit) idx = k;
            if (line) line->push_back(make_char(l, ch));
            k++;
        }
        if (l == last) break;
    }
    if (at) *at = idx;
    return 1;
}

int pdf_text_in(int page, pdf_box area, std::vector<pdf_char> *out) {
    err[0] = 0;
    if (out) out->clear();
    fz_stext_page *t = page_text(page);
    if (!t) return 0;
    auto f = [&](fz_stext_block *b) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) return;
        for (fz_stext_line *ln = b->u.t.first_line; ln; ln = ln->next)
            for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next) {
                if (blank(ch->c)) continue;
                fz_rect r = fz_rect_from_quad(ch->quad);
                float cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
                if (cx >= area.x0 && cx <= area.x1 && cy >= area.y0 && cy <= area.y1 && out)
                    out->push_back(make_char(ln, ch));
            }
    };
    each_block(t->first_block, f);
    return 1;
}

int pdf_text_count(int page) {
    err[0] = 0;
    fz_stext_page *t = page_text(page);
    if (!t) return -1;
    int n = 0;
    auto f = [&](fz_stext_block *b) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) return;
        for (fz_stext_line *ln = b->u.t.first_line; ln; ln = ln->next)
            for (fz_stext_char *ch = ln->first_char; ch; ch = ch->next)
                if (!blank(ch->c)) n++;
    };
    each_block(t->first_block, f);
    return n;
}

float pdf_image_cover(int page) {
    err[0] = 0;
    fz_stext_page *t = page_text(page);
    if (!t) return -1;
    if (cur_cover >= 0) return cur_cover;
    /* Overlaps count once: the page is a 64 x 64 grid, and a cell is
     * covered when its centre lies under some picture. A scan cut into
     * strips is still the whole page; two pictures stacked are not two. */
    const int G = 64;
    static unsigned char cell[G * G];
    memset(cell, 0, sizeof cell);
    fz_rect pb = cur_bounds;
    float cw = (pb.x1 - pb.x0) / G, chh = (pb.y1 - pb.y0) / G;
    auto f = [&](fz_stext_block *b) {
        if (b->type != FZ_STEXT_BLOCK_IMAGE) return;
        fz_rect r = fz_intersect_rect(b->bbox, pb);
        if (fz_is_empty_rect(r)) return;
        for (int gy = 0; gy < G; gy++) {
            float cy = pb.y0 + (gy + 0.5f) * chh;
            if (cy < r.y0 || cy > r.y1) continue;
            for (int gx = 0; gx < G; gx++) {
                float cx = pb.x0 + (gx + 0.5f) * cw;
                if (cx >= r.x0 && cx <= r.x1) cell[gy * G + gx] = 1;
            }
        }
    };
    each_block(t->first_block, f);
    int n = 0;
    for (int i = 0; i < G * G; i++) n += cell[i];
    cur_cover = (float)n / (G * G);
    return cur_cover;
}

int pdf_is_scan(int page) {
    float c = pdf_image_cover(page);
    if (c < 0) return -1;
    return c >= PDF_SCAN_COVER;
}

/* ---- finding PDFs ---- */

namespace {

struct DirCloser { void operator()(DIR *d) const { if (d) closedir(d); } };
using DirPtr = std::unique_ptr<DIR, DirCloser>;

/* Enough for any card, and a bound on a tree that is not one (a mounted
 * network share, a directory of a million files). */
const size_t MAX_DIRS = 20000;

int is_pdf_name(const char *n) {
    size_t l = strlen(n);
    return l > 4 && strcasecmp(n + l - 4, ".pdf") == 0;
}

int fold(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

bool name_less(const pdf_file &a, const pdf_file &b) {
    const unsigned char *p = (const unsigned char *)a.name.c_str();
    const unsigned char *q = (const unsigned char *)b.name.c_str();
    for (; *p && fold(*p) == fold(*q); p++, q++) {}
    if (fold(*p) != fold(*q)) return fold(*p) < fold(*q);
    return a.path < b.path;
}

}  // namespace

std::vector<pdf_file> pdf_scan(const std::vector<std::string> &roots, int max_depth,
                               size_t max_files) {
    std::vector<pdf_file> out;
    struct Todo { std::string dir; int depth; };
    std::vector<Todo> todo;
    for (const std::string &r0 : roots) {
        std::string r = r0;
        while (r.size() > 1 && r.back() == '/') r.pop_back();
        if (!r.empty()) todo.push_back({r, 0});
    }
    size_t dirs = 0;
    while (!todo.empty() && out.size() < max_files && dirs < MAX_DIRS) {
        Todo t = todo.back();
        todo.pop_back();
        dirs++;
        DirPtr d(opendir(t.dir.c_str()));
        if (!d) continue;
        int dfd = dirfd(d.get());
        const std::string base = t.dir == "/" ? "" : t.dir;
        while (struct dirent *e = readdir(d.get())) {
            const char *n = e->d_name;
            if (n[0] == '.') continue;          /* ., .., .kobo, .adds, ._mac */
            int type = e->d_type;
            if (type == DT_UNKNOWN) {          /* some filesystems do not say */
                struct stat st;
                if (fstatat(dfd, n, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
                type = S_ISDIR(st.st_mode) ? DT_DIR : S_ISREG(st.st_mode) ? DT_REG
                     : S_ISLNK(st.st_mode) ? DT_LNK : DT_UNKNOWN;
            }
            if (type == DT_DIR) {
                if (t.depth < max_depth) todo.push_back({base + "/" + n, t.depth + 1});
                continue;
            }
            if (!is_pdf_name(n)) continue;
            if (type == DT_LNK) {              /* a link to a PDF is one; to a directory, no */
                struct stat st;
                if (fstatat(dfd, n, &st, 0) != 0 || !S_ISREG(st.st_mode)) continue;
            } else if (type != DT_REG) {
                continue;
            }
            size_t l = strlen(n);
            out.push_back({base + "/" + n, std::string(n, l - 4)});
            if (out.size() >= max_files) break;
        }
    }
    std::sort(out.begin(), out.end(), name_less);
    out.erase(std::unique(out.begin(), out.end(),
                          [](const pdf_file &a, const pdf_file &b) { return a.path == b.path; }),
              out.end());
    return out;
}

std::vector<std::string> pdf_default_roots(void) {
    std::vector<std::string> r;
#ifdef HANZI_DESKTOP
    const char *env = getenv("HANZI_PDF_DIRS");
    if (env && *env) {
        std::string s = env;
        size_t i = 0;
        while (i <= s.size()) {
            size_t j = s.find(':', i);
            if (j == std::string::npos) j = s.size();
            if (j > i) r.push_back(s.substr(i, j - i));
            i = j + 1;
        }
    } else {
        const char *home = getenv("HOME");
        if (home && *home) r.push_back(std::string(home) + "/Documents");
    }
#else
    r.push_back("/mnt/onboard");
#endif
    return r;
}
