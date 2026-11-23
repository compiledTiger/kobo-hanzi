/* src/core/pdf.cpp against the PDFs tests/data/make_test_pdfs.py writes,
 * whose every box is known.
 *
 *   pdf_test tests/data font.ttf [more.pdf ...]
 *
 * Drawing: a region drawn alone is exactly those pixels of the whole page;
 * the view turns clockwise, and tap coordinates come back through every
 * turn; the Chinese of a font the file does not carry is drawn (with the
 * font given) where the text layer says it is. The text layer: the character
 * under a point, its line across and down a column, the text in a box, and
 * a scan told from a page with a picture on it -- including a scan whose
 * invisible layer is wrong. Damage: a file that is not a PDF, an empty one,
 * one cut in half, pages that do not exist, calls with nothing open -- each
 * says no and nothing breaks. The finder: depth, hidden directories, case,
 * links, the limit, the order. Any further PDFs given are opened, drawn and
 * classified, as a smoke test of real files. */
#include "../src/core/pdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>

static int bad = 0;
#define CHECK(c) do { if (!(c)) { printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); bad = 1; } } while (0)

static std::string dir, font;

/* The darkest pixel of a box of a buffer. */
static int darkest(const std::vector<uint8_t> &b, int stride, int x0, int y0, int x1, int y1) {
    int m = 255;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) m = std::min(m, (int)b[(size_t)y * stride + x]);
    return m;
}

static std::vector<uint8_t> render_all(int page, float scale, int rot, int *w, int *h) {
    std::vector<uint8_t> b;
    if (!pdf_view_size(page, scale, rot, w, h)) return b;
    b.assign((size_t)*w * *h, 0x55);
    CHECK(pdf_render(page, scale, rot, 0, 0, *w, *h, b.data(), *w));
    return b;
}

static void test_text_pdf() {
    printf("  text.pdf\n");
    std::string p = dir + "/text.pdf";
    CHECK(pdf_open(p.c_str()));
    CHECK(pdf_is_open());
    CHECK(strcmp(pdf_path(), p.c_str()) == 0);
    CHECK(pdf_pages() == 3);
    float w = 0, h = 0;
    CHECK(pdf_page_size(0, &w, &h) && w == 420 && h == 595);
    CHECK(pdf_page_size(2, &w, &h) && w == 595 && h == 420);   /* /Rotate 90 */

    /* page 1: 学而时习之 at 36pt from x=60, baseline 500 from the bottom */
    const char *want[] = {"学", "而", "时", "习", "之"};
    float base = 595 - 500;
    for (int i = 0; i < 5; i++) {
        pdf_char c;
        float x = 60 + 36 * i + 18, y = base - 12;
        CHECK(pdf_char_at(0, x, y, 0, &c));
        CHECK(strcmp(c.utf8, want[i]) == 0);
        CHECK(!c.vertical);
        CHECK(c.box.x0 <= x && x <= c.box.x1 && c.box.y0 <= y && y <= c.box.y1);
        CHECK(c.box.x1 - c.box.x0 > 30 && c.box.x1 - c.box.x0 < 40);
    }
    /* beside the line: nothing without slack, the nearest with it */
    pdf_char c;
    CHECK(!pdf_char_at(0, 60 + 36 * 4 + 18, base - 70, 0, &c));
    CHECK(pdf_error()[0] != 0);
    CHECK(pdf_char_at(0, 60 + 36 * 4 + 18, base - 70, 40, &c) && strcmp(c.utf8, "之") == 0);
    CHECK(!pdf_char_at(0, 400, 300, 10, &c));
    /* the line, and where the tap is in it */
    std::vector<pdf_char> line;
    int at = -1;
    CHECK(pdf_line_at(0, 60 + 36 * 2 + 18, base - 12, 0, &line, &at));
    std::string s;
    for (auto &k : line) s += k.utf8;
    CHECK(s == "学而时习之");
    CHECK(at == 2);
    CHECK(pdf_line_at(0, 70, 595 - 400 - 8, 0, &line, &at));
    s.clear();
    for (auto &k : line) s += k.utf8;
    CHECK(s == "Hello");
    CHECK(at == 0);
    /* the text in a box: 时习 and nothing of Hello */
    std::vector<pdf_char> in;
    CHECK(pdf_text_in(0, {60 + 72, 0, 60 + 144, 300}, &in));
    s.clear();
    for (auto &k : in) s += k.utf8;
    CHECK(s == "时习");
    CHECK(pdf_text_count(0) == 10);
    /* a page with a picture on 4% of it is not a scan */
    float cov = pdf_image_cover(0);
    CHECK(cov > 0.02f && cov < 0.08f);
    CHECK(pdf_is_scan(0) == 0);
    printf("    page 1: cover %.3f, line %s\n", cov, "学而时习之");

    /* drawing: white margins, ink where the layer says 学 is (in the font
     * given: MuPDF has no Chinese of its own) */
    int vw, vh;
    std::vector<uint8_t> full = render_all(0, 2, 0, &vw, &vh);
    CHECK(vw == 840 && vh == 1190);
    if (vw == 840 && vh == 1190) {
        CHECK(darkest(full, vw, 0, 0, 40, 40) == 255);
        {
            CHECK(pdf_char_at(0, 78, base - 12, 0, &c));
            pdf_box vb;
            CHECK(pdf_box_to_view(0, 2, 0, c.box, &vb));
            int d = darkest(full, vw, (int)vb.x0, (int)vb.y0, (int)vb.x1, (int)vb.y1);
            printf("    学 drawn: darkest %d in its box\n", d);
            CHECK(d < 64);
        }
        /* "Hello" is the base 14, which is always there */
        CHECK(darkest(full, vw, 120, 2 * (595 - 400 - 18), 240, 2 * (595 - 400)) < 64);
        /* a region drawn alone, into a wider buffer, is those pixels exactly */
        const int rx = 101, ry = 157, rw = 333, rh = 211, stride = 400;
        std::vector<uint8_t> part((size_t)stride * rh, 0x33);
        CHECK(pdf_render(0, 2, 0, rx, ry, rw, rh, part.data(), stride));
        int diff = 0;
        for (int y = 0; y < rh; y++)
            for (int x = 0; x < rw; x++)
                diff = std::max(diff, abs((int)part[(size_t)y * stride + x] - full[(size_t)(ry + y) * vw + rx + x]));
        CHECK(diff == 0);
        /* and the bytes past its width are not touched */
        CHECK(part[stride - 1] == 0x33 && part[(size_t)(rh - 1) * stride + rw] == 0x33);
        /* a region off the page: white, and fine */
        std::vector<uint8_t> off(100 * 100, 0);
        CHECK(pdf_render(0, 2, 0, vw - 50, vh - 50, 100, 100, off.data(), 100));
        CHECK(off[99 * 100 + 99] == 255);
    }

    /* the turns: the view's size swaps, a point goes there and back, and 学
     * lands where its box is said to be in every turn */
    for (int rot = 0; rot < 360; rot += 90) {
        int tw, th;
        CHECK(pdf_view_size(0, 1.5f, rot, &tw, &th));
        CHECK((rot % 180 ? th : tw) == 630 && (rot % 180 ? tw : th) == 893);
        float vx, vy, px, py;
        CHECK(pdf_page_to_view(0, 1.5f, rot, 100, 200, &vx, &vy));
        CHECK(vx >= 0 && vy >= 0 && vx <= tw && vy <= th);
        CHECK(pdf_view_to_page(0, 1.5f, rot, vx, vy, &px, &py));
        CHECK(fabsf(px - 100) < 0.01f && fabsf(py - 200) < 0.01f);
        {
            std::vector<uint8_t> b = render_all(0, 1.5f, rot, &tw, &th);
            pdf_char k;
            CHECK(pdf_char_at(0, 78, base - 12, 0, &k));
            pdf_box vb;
            CHECK(pdf_box_to_view(0, 1.5f, rot, k.box, &vb));
            CHECK(b.size() == (size_t)tw * th);
            if (b.size() == (size_t)tw * th)
                CHECK(darkest(b, tw, (int)vb.x0, (int)vb.y0, (int)vb.x1, (int)vb.y1) < 64);
        }
    }
    /* clockwise: the page's top-left corner is the view's top-right at 90 */
    {
        float vx, vy;
        CHECK(pdf_page_to_view(0, 1, 90, 0, 0, &vx, &vy));
        CHECK(fabsf(vx - 595) < 0.01f && fabsf(vy) < 0.01f);
        CHECK(pdf_page_to_view(0, 1, 270, 0, 0, &vx, &vy));
        CHECK(fabsf(vx) < 0.01f && fabsf(vy - 420) < 0.01f);
        CHECK(pdf_page_to_view(0, 1, -90, 0, 0, &vx, &vy));       /* -90 is 270 */
        CHECK(fabsf(vx) < 0.01f && fabsf(vy - 420) < 0.01f);
    }

    /* page 2: 道可道非常道 down a column */
    {
        std::vector<pdf_char> col;
        int k = -1;
        CHECK(pdf_text_in(1, {0, 0, 420, 595}, &col));
        s.clear();
        for (auto &x : col) s += x.utf8;
        CHECK(s == "道可道非常道");
        if (col.size() == 6) {
            CHECK(col[0].vertical);
            CHECK(col[1].box.y0 > col[0].box.y0);              /* down the page */
            pdf_box b = col[3].box;                            /* 非 */
            CHECK(pdf_line_at(1, (b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2, 0, &line, &k));
            printf("    line at 非: %zu chars, at %d\n", line.size(), k);
            CHECK(line.size() == 6 && k == 3);
            CHECK(pdf_char_at(1, (b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2, 0, &c) && strcmp(c.utf8, "非") == 0);
            printf("    page 2: column %s, 非 at (%.0f,%.0f)-(%.0f,%.0f)\n", s.c_str(), b.x0, b.y0, b.x1, b.y1);
        }
        CHECK(pdf_image_cover(1) == 0 && pdf_is_scan(1) == 0);
    }

    /* page 3: a landscape page; 天 where /Rotate puts it, and drawn there */
    {
        std::vector<pdf_char> all;
        CHECK(pdf_text_in(2, {0, 0, 595, 420}, &all));
        CHECK(all.size() == 1 && strcmp(all.size() ? all[0].utf8 : "", "天") == 0);
        if (all.size() == 1) {
            int tw, th;
            std::vector<uint8_t> b = render_all(2, 1, 0, &tw, &th);
            CHECK(tw == 595 && th == 420);
            pdf_box vb;
            CHECK(pdf_box_to_view(2, 1, 0, all[0].box, &vb));
            if (b.size() == (size_t)tw * th)
                CHECK(darkest(b, tw, (int)vb.x0, (int)vb.y0, (int)vb.x1, (int)vb.y1) < 64);
        }
    }

    /* pages that are not there */
    CHECK(!pdf_page_size(3, &w, &h) && pdf_error()[0]);
    CHECK(!pdf_page_size(-1, &w, &h));
    std::vector<uint8_t> tiny(16, 7);
    CHECK(!pdf_render(5, 1, 0, 0, 0, 4, 4, tiny.data(), 4));
    CHECK(tiny[0] == 255);                                    /* left white */
    CHECK(!pdf_render(0, 1, 0, 0, 0, 4, 4, NULL, 4));
    CHECK(!pdf_render(0, 1, 0, 0, 0, 4, 4, tiny.data(), 3));  /* stride too small */
    CHECK(!pdf_char_at(9, 1, 1, 1, &c));
    CHECK(pdf_is_scan(9) == -1 && pdf_text_count(9) == -1);
    pdf_close();
    CHECK(!pdf_is_open() && pdf_pages() == 0 && pdf_path()[0] == 0);
}

static void test_scan_pdf() {
    printf("  scan.pdf\n");
    std::string p = dir + "/scan.pdf";
    CHECK(pdf_open(p.c_str()));
    CHECK(pdf_pages() == 1);
    float cov = pdf_image_cover(0);
    CHECK(cov > 0.99f);
    CHECK(pdf_is_scan(0) == 1);
    /* the invisible layer is there, and wrong: B over 地 -- found, as the
     * candidate it is, while the page says it is a scan */
    pdf_char c;
    CHECK(pdf_char_at(0, 48 + 60 + 24, 104, 4, &c));
    CHECK(strcmp(c.utf8, "B") == 0);
    std::vector<pdf_char> line;
    int at = -1;
    CHECK(pdf_line_at(0, 48 + 60 + 24, 104, 4, &line, &at));
    printf("    cover %.3f, layer under 地: %s (%zu in the line, at %d)\n", cov, c.utf8, line.size(), at);
    /* the picture: ink in 地's cell (x 108..156, y 80..128 points), paper
     * in the margin */
    int vw, vh;
    std::vector<uint8_t> b = render_all(0, 1, 0, &vw, &vh);
    CHECK(vw == 420 && vh == 595);
    if (b.size() == 420u * 595u) {
        CHECK(darkest(b, vw, 108, 80, 156, 128) < 80);
        int m = darkest(b, vw, 5, 5, 40, 40);
        CHECK(m > 200 && m < 255);                            /* grey paper, not white */
    }
    /* half the page at twice the size, in two halves: as one */
    int hw, hh;
    CHECK(pdf_view_size(0, 2, 0, &hw, &hh));
    std::vector<uint8_t> top((size_t)hw * (hh / 2)), whole;
    whole = render_all(0, 2, 0, &hw, &hh);
    CHECK(pdf_render(0, 2, 0, 0, 0, hw, hh / 2, top.data(), hw));
    CHECK(memcmp(top.data(), whole.data(), top.size()) == 0);
    pdf_close();
}

static void write_file(const std::string &p, const std::string &data) {
    FILE *f = fopen(p.c_str(), "wb");
    if (!f) { printf("    cannot write %s\n", p.c_str()); bad = 1; return; }
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static std::string read_file(const std::string &p) {
    std::string s;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static void test_damage(const std::string &tmp) {
    printf("  damage\n");
    /* with nothing open */
    float w, h;
    pdf_char c;
    CHECK(!pdf_is_open() && pdf_pages() == 0);
    CHECK(!pdf_page_size(0, &w, &h));
    CHECK(!pdf_char_at(0, 1, 1, 1, &c));
    CHECK(!pdf_open(NULL) && !pdf_open(""));
    CHECK(!pdf_open((tmp + "/no such file.pdf").c_str()));
    CHECK(pdf_error()[0] != 0);
    printf("    missing: %s\n", pdf_error());
    write_file(tmp + "/junk.pdf", std::string(5000, 'x'));
    CHECK(!pdf_open((tmp + "/junk.pdf").c_str()));
    printf("    junk: %s\n", pdf_error());
    write_file(tmp + "/empty.pdf", "");
    CHECK(!pdf_open((tmp + "/empty.pdf").c_str()));
    /* half a file: MuPDF may repair it or refuse it; either way, no crash,
     * and whatever it opens can be asked about */
    std::string t = read_file(dir + "/scan.pdf");
    write_file(tmp + "/half.pdf", t.substr(0, t.size() / 2));
    if (pdf_open((tmp + "/half.pdf").c_str())) {
        int n = pdf_pages(), vw = 0, vh = 0;
        std::vector<uint8_t> b(100 * 100);
        for (int i = 0; i < n; i++) {
            pdf_view_size(i, 0.2f, 0, &vw, &vh);
            pdf_render(i, 0.2f, 0, 0, 0, 100, 100, b.data(), 100);
            pdf_is_scan(i);
            pdf_char_at(i, 10, 10, 5, &c);
        }
        printf("    half: opened, %d page(s)\n", n);
    } else {
        printf("    half: %s\n", pdf_error());
    }
    /* a CJK font that cannot be read: the page still opens and draws, its
     * Chinese missing, and nothing breaks */
    pdf_set_cjk_font((tmp + "/no such font.ttf").c_str());
    CHECK(pdf_open((dir + "/text.pdf").c_str()));
    {
        std::vector<uint8_t> b(200 * 200);
        CHECK(pdf_render(0, 0.5f, 0, 0, 0, 200, 200, b.data(), 200));
        pdf_text_count(0);
    }
    pdf_set_cjk_font(NULL);
    /* the text file again after all that */
    CHECK(pdf_open((dir + "/text.pdf").c_str()) && pdf_pages() == 3);
    pdf_close();
    pdf_close();                                              /* twice is fine */
}

static void test_scan(const std::string &tmp) {
    printf("  finding PDFs\n");
    std::string r = tmp + "/tree";
    const char *dirs[] = {"", "/sub", "/sub/deep", "/sub/deep/deeper", "/.kobo", "/.adds", "/.adds/x",
                          "/folder.pdf", "/Zed"};
    for (const char *d : dirs) mkdir((r + d).c_str(), 0755);
    write_file(r + "/b.pdf", "x");
    write_file(r + "/A.PDF", "x");
    write_file(r + "/notes.txt", "x");
    write_file(r + "/x.pdf.part", "x");
    write_file(r + "/._b.pdf", "x");                          /* macOS litter */
    write_file(r + "/.kobo/hidden.pdf", "x");
    write_file(r + "/.adds/x/hidden.pdf", "x");
    write_file(r + "/sub/c.Pdf", "x");
    write_file(r + "/sub/deep/d.pdf", "x");
    write_file(r + "/sub/deep/deeper/e.pdf", "x");
    write_file(r + "/Zed/\xe9\x81\x93\xe5\xbe\xb7\xe7\xbb\x8f.pdf", "x");   /* 道德经 */
    write_file(r + "/pdf", "x");
    CHECK(symlink(r.c_str(), (r + "/sub/loop").c_str()) == 0);              /* a cycle */
    CHECK(symlink((r + "/b.pdf").c_str(), (r + "/sub/link.pdf").c_str()) == 0);

    auto names = [](const std::vector<pdf_file> &v) {
        std::string s;
        for (auto &f : v) s += (s.empty() ? "" : ",") + f.name;
        return s;
    };
    std::vector<pdf_file> v = pdf_scan({r}, 8, 1000);
    printf("    %s\n", names(v).c_str());
    CHECK(names(v) == "A,b,c,d,e,link,\xe9\x81\x93\xe5\xbe\xb7\xe7\xbb\x8f");
    CHECK(v.size() == 7 && v[0].path == r + "/A.PDF" && v[2].path == r + "/sub/c.Pdf");
    CHECK(names(pdf_scan({r}, 0, 1000)) == "A,b");
    CHECK(names(pdf_scan({r}, 1, 1000)) == "A,b,c,link,\xe9\x81\x93\xe5\xbe\xb7\xe7\xbb\x8f");
    CHECK(pdf_scan({r}, 8, 3).size() == 3);
    /* a root twice, with a slash, and one nested in another: each file once */
    CHECK(pdf_scan({r, r + "/", r + "/sub"}, 8, 1000).size() == 7);
    CHECK(pdf_scan({tmp + "/nowhere", ""}, 8, 1000).empty());
    CHECK(pdf_scan({}, 8, 1000).empty());
    std::vector<std::string> roots = pdf_default_roots();
    CHECK(!roots.empty());
}

static double now_ms() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* A real file: the first pages drawn 1264 pixels wide (the Libra's width),
 * timed, and classified; the text at the page's middle. */
static void smoke(const char *path) {
    printf("  %s\n", path);
    double t0 = now_ms();
    if (!pdf_open(path)) { printf("    could not open: %s\n", pdf_error()); bad = 1; return; }
    int n = pdf_pages();
    printf("    %d pages, opened in %.0fms\n", n, now_ms() - t0);
    for (int i = 0; i < n && i < 3; i++) {
        float pw = 0, ph = 0;
        CHECK(pdf_page_size(i, &pw, &ph));
        float scale = pw > 0 ? 1264 / pw : 1;
        int w, h;
        t0 = now_ms();
        std::vector<uint8_t> b = render_all(i, scale, 0, &w, &h);
        double t1 = now_ms();
        render_all(i, scale, 0, &w, &h);
        double t2 = now_ms();
        std::vector<pdf_char> line;
        int at = -1;
        std::string s;
        if (pdf_line_at(i, pw / 2, ph / 2, pw / 4, &line, &at))
            for (size_t k = 0; k < line.size() && k < 30; k++) s += line[k].utf8;
        printf("    page %d: %dx%d in %.0fms (again %.0fms), cover %.2f, scan %d, %d chars; mid: %s\n",
               i + 1, w, h, t1 - t0, t2 - t1, pdf_image_cover(i), pdf_is_scan(i), pdf_text_count(i),
               s.c_str());
    }
    pdf_close();
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: pdf_test tests/data font.ttf [more.pdf ...]\n"); return 2; }
    dir = argv[1];
    font = argv[2];
    const char *td = getenv("TMPDIR");
    std::string tmpl = std::string(td && *td ? td : "/tmp") + "/pdf_test.XXXXXX";
    const char *tmp = mkdtemp(&tmpl[0]);
    if (!tmp) { perror("mkdtemp"); return 2; }
    printf("pdf\n");
    test_damage(tmp);
    pdf_set_cjk_font(font.c_str());
    test_text_pdf();
    test_scan_pdf();
    test_scan(tmp);
    for (int i = 3; i < argc; i++) smoke(argv[i]);
    pdf_shutdown();
    std::string rm = std::string("rm -r -- '") + tmp + "'";
    if (system(rm.c_str()) != 0) printf("    (could not remove %s)\n", tmp);
    printf(bad ? "pdf_test: FAILED\n" : "pdf_test: ok\n");
    return bad;
}
