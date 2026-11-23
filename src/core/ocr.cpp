/* See ocr.h. Each step follows RapidOCR 1.4.4 (rapidocr_onnxruntime), which
 * is what the models were verified against (data/models/README.md):
 *
 *   detector   DetPreProcess (limit_type max, 960), DBPostProcess (thresh
 *              0.3, box_thresh 0.5, unclip_ratio 1.6, use_dilation, score
 *              "fast"), filter_tag_det_res
 *   crop       get_rotate_crop_image: perspective warp, cubic, replicated
 *              border; turned a quarter left when height/width >= 1.5
 *   recogniser one line at a time, height 48, padded to at least 320 with
 *              0.0, greedy CTC decoding
 *
 * OpenCV is not here, so its few calls are written out: findContours (as
 * 8-connected components; a contour's minimum rectangle is that of the
 * component's hull), minAreaRect (rotating calipers over that hull), the
 * pyclipper offset of a rectangle (the rectangle grown by the distance on
 * every side -- the round corners lie inside it, so its minimum rectangle is
 * exactly that), fillPoly, getPerspectiveTransform and warpPerspective. */
#include "ocr.h"

#include <ncnn/net.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <new>
#include <string>
#include <vector>

namespace {

/* RapidOCR's settings (config.yaml of 1.4.4), and the models' shapes. */
const float DET_THRESH = 0.3f, BOX_THRESH = 0.5f, UNCLIP = 1.6f;
const int   DET_LIMIT = 960, MIN_SIZE = 3, MAX_CANDIDATES = 1000;
const int   REC_H = 48, REC_MIN_W = 320, REC_STEP = 8;   /* a column is 8 px */
const int   PAD = 32;               /* margin given to an image smaller than the window */
const float CAND_MIN_P = 1e-5f;     /* below this a class is noise, not a candidate */
/* a tap further than this many glyphs from every decoded character is on one
 * the greedy reading dropped */
const float GAP = 0.6f;
/* and there, unless some hanzi reaches this, nothing is there to read */
const float LOCAL_MIN_P = 0.05f;

struct State {
    std::string dir;
    bool have_files = false, load_failed = false;
    ncnn::Net *det = nullptr, *rec = nullptr;
    /* the networks' blobs and scratch, emptied after every call: ncnn's own
     * pools keep every size they were asked for, and each line is a new
     * width, so they grew by tens of MB over twenty taps */
    ncnn::UnlockedPoolAllocator blobs, scratch;
    std::vector<std::string> keys;     /* by class: 0 blank, 1..n the file, n+1 space */
    std::vector<unsigned char> han;    /* by class: a hanzi? */
    int threads = 2, window = 640;
    float det_scale = 1.f;
    char err[200] = "";
    char load_err[200] = "";           /* why the models would not load, said on every call */
};
State S;

void set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(S.err, sizeof S.err, fmt, ap);
    va_end(ap);
}

int ms_since(const timespec &t0)
{
    timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (int)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
}

bool readable(const std::string &p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

/* One UTF-8 character's codepoint, or 0 if it is not exactly one. */
uint32_t utf8_one(const std::string &s)
{
    const unsigned char *p = (const unsigned char *)s.c_str();
    size_t n = s.size();
    if (n == 1 && p[0] < 0x80) return p[0];
    if (n == 2 && (p[0] & 0xE0) == 0xC0) return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    if (n == 3 && (p[0] & 0xF0) == 0xE0)
        return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    if (n == 4 && (p[0] & 0xF8) == 0xF0)
        return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    return 0;
}

void configure(ncnn::Net &n)
{
    n.opt.lightmode = true;
    n.opt.num_threads = S.threads;
    n.opt.use_vulkan_compute = false;
    /* fp32 throughout: what was verified (README "fp16"); fp16 is a speed
     * option to measure on the device later. */
    n.opt.use_fp16_storage = false;
    n.opt.use_fp16_packed = false;
    n.opt.use_fp16_arithmetic = false;
    n.opt.use_bf16_storage = false;
    n.opt.use_bf16_packed = false;
}

void unload()
{
    delete S.det;
    delete S.rec;
    S.det = S.rec = nullptr;
    S.blobs.clear();
    S.scratch.clear();
    std::vector<std::string>().swap(S.keys);
    std::vector<unsigned char>().swap(S.han);
}

/* The models would not load: free what did, and keep the reason (S.err). */
bool load_fail()
{
    unload();
    S.load_failed = true;
    snprintf(S.load_err, sizeof S.load_err, "%s", S.err);
    return false;
}

bool load()
{
    if (S.det && S.rec && !S.keys.empty()) return true;
    if (!S.have_files) { set_err("ocr: no models (ocr_open was not given a directory holding them)"); return false; }
    /* not tried again until ocr_open, but said again on every call: a
     * caller that shows ocr_error() would otherwise show nothing after the
     * first tap */
    if (S.load_failed) { set_err("%s", S.load_err); return false; }
    unload();
    try {
        std::string kp = S.dir + "/ppocrv4_keys.txt";
        FILE *f = fopen(kp.c_str(), "rb");
        if (!f) { set_err("ocr: cannot read %s", kp.c_str()); return load_fail(); }
        std::string all;
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
        fclose(f);
        /* one character a line, whitespace kept (line 5710 is U+3000) */
        S.keys.push_back("");
        size_t at = 0;
        while (at < all.size()) {
            size_t nl = all.find('\n', at);
            if (nl == std::string::npos) nl = all.size();
            S.keys.push_back(all.substr(at, nl - at));
            at = nl + 1;
        }
        S.keys.push_back(" ");                /* use_space_char */
        S.han.resize(S.keys.size());
        for (size_t i = 0; i < S.keys.size(); i++) S.han[i] = ocr_is_hanzi(utf8_one(S.keys[i])) ? 1 : 0;

        S.det = new ncnn::Net;
        S.rec = new ncnn::Net;
        configure(*S.det);
        configure(*S.rec);
        const char *names[4] = {"ppocrv4_det.param", "ppocrv4_det.bin", "ppocrv4_rec.param", "ppocrv4_rec.bin"};
        std::string p[4];
        for (int i = 0; i < 4; i++) p[i] = S.dir + "/" + names[i];
        if (S.det->load_param(p[0].c_str()) != 0 || S.det->load_model(p[1].c_str()) != 0 ||
            S.rec->load_param(p[2].c_str()) != 0 || S.rec->load_model(p[3].c_str()) != 0) {
            set_err("ocr: cannot load the models in %s", S.dir.c_str());
            return load_fail();
        }
    } catch (const std::bad_alloc &) {
        set_err("ocr: out of memory loading the models");
        return load_fail();
    }
    return true;
}

/* ---- geometry ------------------------------------------------------------ */

struct Pt { float x, y; };

float cross(Pt o, Pt a, Pt b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
float dist(Pt a, Pt b) { return hypotf(a.x - b.x, a.y - b.y); }

/* Andrew's monotone chain; collinear points dropped. */
std::vector<Pt> hull(std::vector<Pt> &p)
{
    std::sort(p.begin(), p.end(), [](Pt a, Pt b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    p.erase(std::unique(p.begin(), p.end(), [](Pt a, Pt b) { return a.x == b.x && a.y == b.y; }), p.end());
    if (p.size() < 3) return p;
    std::vector<Pt> h(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); i++) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) k--;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i]) <= 0) k--;
        h[k++] = p[i];
    }
    h.resize(k - 1);
    return h;
}

/* A rotated rectangle: centre, unit axes, half sizes along them. */
struct RRect { Pt c, u, v; float hu, hv; };

/* cv2.minAreaRect over a convex hull: one side lies along an edge. */
bool min_area_rect(const std::vector<Pt> &h, RRect &r)
{
    if (h.size() < 3) return false;
    float best = -1;
    for (size_t i = 0; i < h.size(); i++) {
        Pt a = h[i], b = h[(i + 1) % h.size()];
        float len = dist(a, b);
        if (len <= 0) continue;
        Pt u = {(b.x - a.x) / len, (b.y - a.y) / len}, v = {-u.y, u.x};
        float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        for (const Pt &q : h) {
            float pu = q.x * u.x + q.y * u.y, pv = q.x * v.x + q.y * v.y;
            u0 = std::min(u0, pu); u1 = std::max(u1, pu);
            v0 = std::min(v0, pv); v1 = std::max(v1, pv);
        }
        float area = (u1 - u0) * (v1 - v0);
        if (best < 0 || area < best) {
            best = area;
            float cu = (u0 + u1) / 2, cv = (v0 + v1) / 2;
            r.c = {cu * u.x + cv * v.x, cu * u.y + cv * v.y};
            r.u = u; r.v = v;
            r.hu = (u1 - u0) / 2; r.hv = (v1 - v0) / 2;
        }
    }
    return best >= 0;
}

/* get_mini_boxes's corner order: sorted by x, then the left pair and the
 * right pair each by y -- top-left, top-right, bottom-right, bottom-left. */
void mini_box(const RRect &r, Pt out[4])
{
    Pt p[4];
    const int su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; i++)
        p[i] = {r.c.x + su[i] * r.hu * r.u.x + sv[i] * r.hv * r.v.x,
                r.c.y + su[i] * r.hu * r.u.y + sv[i] * r.hv * r.v.y};
    std::stable_sort(p, p + 4, [](Pt a, Pt b) { return a.x < b.x; });
    int i1, i2, i3, i4;
    if (p[1].y > p[0].y) { i1 = 0; i4 = 1; } else { i1 = 1; i4 = 0; }
    if (p[3].y > p[2].y) { i2 = 2; i3 = 3; } else { i2 = 3; i3 = 2; }
    out[0] = p[i1]; out[1] = p[i2]; out[2] = p[i3]; out[3] = p[i4];
}

/* Inside a convex quad (either winding), edges included. */
bool in_quad(const Pt q[4], Pt p)
{
    int pos = 0, neg = 0;
    for (int i = 0; i < 4; i++) {
        float c = cross(q[i], q[(i + 1) % 4], p);
        if (c > 1e-6f) pos++;
        else if (c < -1e-6f) neg++;
    }
    return !(pos && neg);
}

float seg_dist(Pt p, Pt a, Pt b)
{
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    t = std::max(0.f, std::min(1.f, t));
    return dist(p, {a.x + t * dx, a.y + t * dy});
}

/* box_score_fast: the mean probability inside the box (cv2.fillPoly of its
 * corners truncated to integers) over its bounding rectangle. */
float box_score(const float *pred, int W, int H, const Pt b[4])
{
    float fx0 = 1e30f, fx1 = -1e30f, fy0 = 1e30f, fy1 = -1e30f;
    Pt q[4];
    for (int i = 0; i < 4; i++) {
        fx0 = std::min(fx0, b[i].x); fx1 = std::max(fx1, b[i].x);
        fy0 = std::min(fy0, b[i].y); fy1 = std::max(fy1, b[i].y);
        q[i] = {(float)(int)b[i].x, (float)(int)b[i].y};
    }
    int x0 = std::max(0, std::min(W - 1, (int)floorf(fx0))), x1 = std::max(0, std::min(W - 1, (int)ceilf(fx1)));
    int y0 = std::max(0, std::min(H - 1, (int)floorf(fy0))), y1 = std::max(0, std::min(H - 1, (int)ceilf(fy1)));
    double sum = 0;
    long n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (in_quad(q, {(float)x, (float)y})) { sum += pred[(size_t)y * W + x]; n++; }
    return n ? (float)(sum / n) : 0.f;
}

struct Box { Pt p[4]; float score; };

/* ---- detector ------------------------------------------------------------ */

/* Text line boxes in the window (x0, y0, ww, wh) of the image, in image
 * pixels. False on an error (set_err), true with no boxes if there is no text. */
bool detect(const uint8_t *grey, int w, int h, int stride, int x0, int y0, int ww, int wh,
            std::vector<Box> &boxes)
{
    float ratio = std::max(ww, wh) > DET_LIMIT ? (float)DET_LIMIT / std::max(ww, wh) : 1.f;
    ratio *= S.det_scale;
    int rh = (int)(wh * ratio), rw = (int)(ww * ratio);
    int H = (int)nearbyint(rh / 32.0) * 32, W = (int)nearbyint(rw / 32.0) * 32;
    if (H <= 0 || W <= 0) return true;

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(grey + (size_t)y0 * stride + x0, ncnn::Mat::PIXEL_GRAY2BGR,
                                                 ww, wh, stride, W, H);
    if (in.empty()) { set_err("ocr: out of memory (detector input)"); return false; }
    const float mean[3] = {127.5f, 127.5f, 127.5f};
    const float norm[3] = {1 / 127.5f, 1 / 127.5f, 1 / 127.5f};
    in.substract_mean_normalize(mean, norm);
    ncnn::Mat prob;
    {
        ncnn::Extractor ex = S.det->create_extractor();
        ex.set_light_mode(true);
        ex.set_blob_allocator(&S.blobs);
        ex.set_workspace_allocator(&S.scratch);
        if (ex.input("in0", in) != 0 || ex.extract("out0", prob) != 0 || prob.empty()) {
            set_err("ocr: the detector failed");
            return false;
        }
    }
    in.release();
    if (prob.w != W || prob.h != H) { set_err("ocr: the detector gave %dx%d for %dx%d", prob.w, prob.h, W, H); return false; }
    const float *pred = prob.channel(0);

    /* binarise at 0.3 and dilate with a 2x2 kernel (anchor at 1,1: a pixel is
     * set when it or its left, upper or upper-left neighbour is) */
    std::vector<unsigned char> bin((size_t)W * H), mask((size_t)W * H);
    for (size_t i = 0; i < bin.size(); i++) bin[i] = pred[i] > DET_THRESH;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x;
            unsigned char m = bin[i];
            if (x > 0) m |= bin[i - 1];
            if (y > 0) { m |= bin[i - W]; if (x > 0) m |= bin[i - W - 1]; }
            mask[i] = m;
        }
    std::vector<unsigned char>().swap(bin);

    /* 8-connected components; each one's hull from its rows' end pixels */
    std::vector<int> rmin(H, INT32_MAX), rmax(H, -1), stack;
    std::vector<Pt> pts;
    int found = 0;
    for (int sy = 0; sy < H && found < MAX_CANDIDATES; sy++)
        for (int sx = 0; sx < W && found < MAX_CANDIDATES; sx++) {
            if (mask[(size_t)sy * W + sx] != 1) continue;
            found++;
            int ymin = sy, ymax = sy;
            stack.clear();
            stack.push_back(sy * W + sx);
            mask[(size_t)sy * W + sx] = 2;
            while (!stack.empty()) {
                int i = stack.back();
                stack.pop_back();
                int x = i % W, y = i / W;
                rmin[y] = std::min(rmin[y], x);
                rmax[y] = std::max(rmax[y], x);
                ymin = std::min(ymin, y);
                ymax = std::max(ymax, y);
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                        size_t j = (size_t)ny * W + nx;
                        if (mask[j] == 1) { mask[j] = 2; stack.push_back((int)j); }
                    }
            }
            pts.clear();
            for (int y = ymin; y <= ymax; y++) {
                if (rmax[y] < 0) continue;
                pts.push_back({(float)rmin[y], (float)y});
                if (rmax[y] != rmin[y]) pts.push_back({(float)rmax[y], (float)y});
                rmin[y] = INT32_MAX;
                rmax[y] = -1;
            }
            std::vector<Pt> hl = hull(pts);
            RRect r;
            if (!min_area_rect(hl, r)) continue;
            if (2 * std::min(r.hu, r.hv) < MIN_SIZE) continue;
            Pt b[4];
            mini_box(r, b);
            float score = box_score(pred, W, H, b);
            if (score < BOX_THRESH) continue;
            /* unclip: offset by area * ratio / perimeter */
            float a = 2 * r.hu, c = 2 * r.hv;
            float d = a * c * UNCLIP / (2 * (a + c));
            r.hu += d;
            r.hv += d;
            if (2 * std::min(r.hu, r.hv) < MIN_SIZE + 2) continue;
            mini_box(r, b);
            Box box;
            box.score = score;
            for (int i = 0; i < 4; i++) {
                float bx = std::max(0.f, std::min((float)ww, (float)nearbyint(b[i].x / W * ww)));
                float by = std::max(0.f, std::min((float)wh, (float)nearbyint(b[i].y / H * wh)));
                box.p[i] = {bx + x0, by + y0};
            }
            /* filter_tag_det_res: order_points_clockwise, clip to the image */
            Pt s[4] = {box.p[0], box.p[1], box.p[2], box.p[3]};
            std::stable_sort(s, s + 4, [](Pt p1, Pt p2) { return p1.x < p2.x; });
            Pt lm[2] = {s[0], s[1]}, rm[2] = {s[2], s[3]};
            if (lm[1].y < lm[0].y) std::swap(lm[0], lm[1]);
            if (rm[1].y < rm[0].y) std::swap(rm[0], rm[1]);
            Pt o[4] = {lm[0], rm[0], rm[1], lm[1]};
            for (int i = 0; i < 4; i++) {
                o[i].x = (float)(int)std::min(std::max(o[i].x, 0.f), (float)(w - 1));
                o[i].y = (float)(int)std::min(std::max(o[i].y, 0.f), (float)(h - 1));
                box.p[i] = o[i];
            }
            if ((int)dist(o[0], o[1]) <= 3 || (int)dist(o[0], o[3]) <= 3) continue;
            boxes.push_back(box);
        }
    return true;
}

/* ---- crop ---------------------------------------------------------------- */

/* The perspective transform taking s[i] to d[i] (getPerspectiveTransform). */
bool persp(const Pt s[4], const Pt d[4], double m[9])
{
    double A[8][9];
    for (int i = 0; i < 4; i++) {
        double x = s[i].x, y = s[i].y, u = d[i].x, v = d[i].y;
        double r1[9] = {x, y, 1, 0, 0, 0, -x * u, -y * u, u};
        double r2[9] = {0, 0, 0, x, y, 1, -x * v, -y * v, v};
        memcpy(A[2 * i], r1, sizeof r1);
        memcpy(A[2 * i + 1], r2, sizeof r2);
    }
    for (int c = 0; c < 8; c++) {
        int piv = c;
        for (int r = c + 1; r < 8; r++)
            if (fabs(A[r][c]) > fabs(A[piv][c])) piv = r;
        if (fabs(A[piv][c]) < 1e-12) return false;
        if (piv != c)
            for (int k = 0; k < 9; k++) std::swap(A[c][k], A[piv][k]);
        for (int r = 0; r < 8; r++) {
            if (r == c) continue;
            double f = A[r][c] / A[c][c];
            for (int k = c; k < 9; k++) A[r][k] -= f * A[c][k];
        }
    }
    for (int i = 0; i < 8; i++) m[i] = A[i][8] / A[i][i];
    m[8] = 1;
    return true;
}

Pt apply(const double m[9], double x, double y)
{
    double z = m[6] * x + m[7] * y + m[8];
    if (fabs(z) < 1e-12) z = 1e-12;
    return {(float)((m[0] * x + m[1] * y + m[2]) / z), (float)((m[3] * x + m[4] * y + m[5]) / z)};
}

/* OpenCV's cubic kernel, a = -0.75 */
void cubic_w(float t, float w[4])
{
    const float A = -0.75f;
    float x0 = 1 + t, x1 = t, x2 = 1 - t, x3 = 2 - t;
    w[0] = ((A * x0 - 5 * A) * x0 + 8 * A) * x0 - 4 * A;
    w[1] = ((A + 2) * x1 - (A + 3)) * x1 * x1 + 1;
    w[2] = ((A + 2) * x2 - (A + 3)) * x2 * x2 + 1;
    w[3] = 1 - w[0] - w[1] - w[2];
    (void)x3;
}

/* The line straightened: dst (cw x ch) pixel (x, y) is source m(x, y). */
void warp(const uint8_t *g, int w, int h, int stride, const double m[9], uint8_t *dst, int cw, int ch)
{
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            Pt s = apply(m, x, y);
            int ix = (int)floorf(s.x), iy = (int)floorf(s.y);
            float wx[4], wy[4];
            cubic_w(s.x - ix, wx);
            cubic_w(s.y - iy, wy);
            float acc = 0;
            for (int j = 0; j < 4; j++) {
                int yy = std::max(0, std::min(h - 1, iy - 1 + j));
                const uint8_t *row = g + (size_t)yy * stride;
                float r = 0;
                for (int i = 0; i < 4; i++) r += wx[i] * row[std::max(0, std::min(w - 1, ix - 1 + i))];
                acc += wy[j] * r;
            }
            int v = (int)lrintf(acc);
            dst[(size_t)y * cw + x] = (uint8_t)std::max(0, std::min(255, v));
        }
}

/* ---- recogniser ---------------------------------------------------------- */

struct Run { int s, e, cls; };     /* columns s..e, all argmax cls */

struct Read {
    ncnn::Mat prob;                /* T x classes */
    int rw = 0;                    /* width of the line in the network's input */
    std::vector<Run> runs;         /* one per decoded character */
    float score = 0;
};

bool recognise(const uint8_t *line, int lw, int lh, int stride, Read &rd)
{
    float ratio = (float)lw / lh;
    int W = (int)(REC_H * std::max((float)REC_MIN_W / REC_H, ratio));
    int rw = std::min((int)ceilf(REC_H * ratio), W);
    if (rw < 1) rw = 1;
    ncnn::Mat l = ncnn::Mat::from_pixels_resize(line, ncnn::Mat::PIXEL_GRAY2BGR, lw, lh, stride, rw, REC_H);
    if (l.empty()) { set_err("ocr: out of memory (line)"); return false; }
    const float mean[3] = {127.5f, 127.5f, 127.5f};
    const float norm[3] = {1 / 127.5f, 1 / 127.5f, 1 / 127.5f};
    l.substract_mean_normalize(mean, norm);
    ncnn::Mat in(W, REC_H, 3);
    if (in.empty()) { set_err("ocr: out of memory (recogniser input)"); return false; }
    in.fill(0.f);
    for (int c = 0; c < 3; c++)
        for (int y = 0; y < REC_H; y++)
            memcpy(in.channel(c).row(y), l.channel(c).row(y), rw * sizeof(float));
    l.release();
    {
        ncnn::Extractor ex = S.rec->create_extractor();
        ex.set_light_mode(true);
        ex.set_blob_allocator(&S.blobs);
        ex.set_workspace_allocator(&S.scratch);
        if (ex.input("in0", in) != 0 || ex.extract("out0", rd.prob) != 0 || rd.prob.empty()) {
            set_err("ocr: the recogniser failed");
            return false;
        }
    }
    if (rd.prob.dims != 2 || rd.prob.w != (int)S.keys.size()) {
        set_err("ocr: the recogniser gave %d classes, the keys %d", rd.prob.w, (int)S.keys.size());
        return false;
    }
    rd.rw = rw;
    /* greedy CTC: argmax per column, repeats merged, blanks dropped */
    int T = rd.prob.h, C = rd.prob.w, prev = -1;
    double sum = 0;
    int kept = 0;
    for (int t = 0; t < T; t++) {
        const float *p = rd.prob.row(t);
        int a = (int)(std::max_element(p, p + C) - p);
        if (a != 0 && a == prev && !rd.runs.empty() && rd.runs.back().e == t - 1) rd.runs.back().e = t;
        else if (a != 0) { rd.runs.push_back({t, t, a}); sum += p[a]; kept++; }
        prev = a;
    }
    rd.score = kept ? (float)(sum / kept) : 0.f;
    return true;
}

void fill_text(const Read &rd, ocr_line_t *line)
{
    size_t n = 0;
    line->text[0] = 0;
    for (const Run &r : rd.runs) {
        const std::string &k = S.keys[r.cls];
        if (n + k.size() >= sizeof line->text) break;
        memcpy(line->text + n, k.data(), k.size());
        n += k.size();
        line->text[n] = 0;
    }
    line->score = rd.score;
}

/* Each hanzi class's best probability over columns t0..t1, leaving out the
 * columns of every decoded character but `keep`: neighbouring columns
 * carrying the same character merge into one candidate. Best first. */
std::vector<std::pair<float, int>> gather(const Read &rd, int t0, int t1, int keep)
{
    int T = rd.prob.h, C = rd.prob.w;
    t0 = std::max(t0, 0);
    t1 = std::min(t1, T - 1);
    std::vector<unsigned char> other(T, 0);
    for (size_t k = 0; k < rd.runs.size(); k++)
        if ((int)k != keep)
            for (int t = rd.runs[k].s; t <= rd.runs[k].e; t++) other[t] = 1;
    std::vector<float> best(C, 0.f);
    for (int t = t0; t <= t1; t++) {
        if (other[t]) continue;
        const float *p = rd.prob.row(t);
        for (int c = 1; c < C; c++)
            if (p[c] > best[c]) best[c] = p[c];
    }
    std::vector<std::pair<float, int>> got;
    for (int c = 1; c < C; c++)
        if (best[c] >= CAND_MIN_P && S.han[c]) got.push_back({best[c], c});
    std::sort(got.begin(), got.end(), [](const std::pair<float, int> &a, const std::pair<float, int> &b) {
        return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    return got;
}

/* The candidates at x (pixels of the network's input; < 0: none). *which
 * gets the decoded character read there, or -1 if the candidates come from
 * columns no character was decoded from (a character the greedy reading
 * dropped: its alternatives are still in those columns). */
int candidates(const Read &rd, float x, ocr_cand_t *out, int max, int *which)
{
    *which = -1;
    if (x < 0 || max <= 0) return 0;
    /* the decoded character nearest x; a hanzi within a glyph's width (the
     * line's height, 48) is preferred to punctuation nearer still */
    int best = -1, bestz = -1;
    float bd = 1e30f, bdz = 1e30f;
    for (size_t k = 0; k < rd.runs.size(); k++) {
        float c = REC_STEP * (rd.runs[k].s + rd.runs[k].e + 1) / 2.f;
        float d = fabsf(c - x);
        if (d < bd) { bd = d; best = (int)k; }
        if (S.han[rd.runs[k].cls] && d < bdz) { bdz = d; bestz = (int)k; }
    }
    if (best >= 0 && !S.han[rd.runs[best].cls] && bestz >= 0 && bdz <= REC_H) best = bestz, bd = bdz;
    std::vector<std::pair<float, int>> got;
    if (best >= 0 && bd <= GAP * REC_H) {
        got = gather(rd, rd.runs[best].s - 1, rd.runs[best].e + 1, best);
        *which = best;
    } else {
        /* nothing decoded under the finger: what the columns there hold,
         * or failing that the nearest character within a glyph and a half */
        int t = (int)(x / REC_STEP);
        got = gather(rd, t - 2, t + 2, -1);
        if (!got.empty() && got[0].first < LOCAL_MIN_P) got.clear();     /* noise */
        if (got.empty() && best >= 0 && bd <= 1.5f * REC_H) {
            got = gather(rd, rd.runs[best].s - 1, rd.runs[best].e + 1, best);
            *which = best;
        }
    }
    int n = 0;
    for (const auto &g : got) {
        if (n >= max) break;
        const std::string &k = S.keys[g.second];
        if (k.size() >= sizeof out[n].ch) continue;
        memcpy(out[n].ch, k.c_str(), k.size() + 1);
        out[n].p = g.first;
        n++;
    }
    return n;
}

void clear_line(ocr_line_t *line)
{
    if (!line) return;
    memset(line, 0, sizeof *line);
    line->char_index = -1;
}

}  // namespace

int ocr_is_hanzi(uint32_t cp)
{
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x20000 && cp <= 0x323AF);
}

int ocr_open(const char *models_dir)
{
    unload();
    S.load_failed = false;
    S.err[0] = S.load_err[0] = 0;
    S.dir = models_dir ? models_dir : "";
    const char *names[5] = {"ppocrv4_det.param", "ppocrv4_det.bin", "ppocrv4_rec.param",
                            "ppocrv4_rec.bin", "ppocrv4_keys.txt"};
    S.have_files = !S.dir.empty();
    for (int i = 0; i < 5 && S.have_files; i++)
        if (!readable(S.dir + "/" + names[i])) {
            set_err("ocr: %s/%s is missing", S.dir.c_str(), names[i]);
            S.have_files = false;
        }
    return S.have_files ? 1 : 0;
}

void ocr_close(void) { unload(); }

void ocr_set_threads(int n)
{
    S.threads = std::max(1, std::min(8, n));
    if (S.det) S.det->opt.num_threads = S.threads;
    if (S.rec) S.rec->opt.num_threads = S.threads;
}

void ocr_set_window(int side) { S.window = std::max(320, std::min(DET_LIMIT, side)); }

void ocr_set_det_scale(float s) { S.det_scale = std::max(0.25f, std::min(1.f, s)); }

const char *ocr_error(void) { return S.err; }

namespace {

int read_line(const uint8_t *grey, int w, int h, int stride, int at_x,
              ocr_cand_t *out, int max, ocr_line_t *line)
{
    clear_line(line);
    S.err[0] = 0;
    if (!grey || w < 1 || h < 1 || stride < w) { set_err("ocr: no image"); return -1; }
    try {
        if (!load()) return -1;
        timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        Read rd;
        if (!recognise(grey, w, h, stride, rd)) return -1;
        int which = -1;
        int n = candidates(rd, at_x < 0 ? -1.f : at_x * (float)rd.rw / w, out, max, &which);
        if (line) {
            fill_text(rd, line);
            line->ms_rec = ms_since(t0);
            const Pt q[4] = {{0, 0}, {(float)w, 0}, {(float)w, (float)h}, {0, (float)h}};
            for (int i = 0; i < 4; i++) line->quad[2 * i] = q[i].x, line->quad[2 * i + 1] = q[i].y;
            line->char_index = which;
            if (n > 0) {
                float c = which >= 0 ? REC_STEP * (rd.runs[which].s + rd.runs[which].e + 1) / 2.f * w / rd.rw
                                     : (float)at_x;
                float a = std::max(0.f, c - h / 2.f), b = std::min((float)w, c + h / 2.f);
                const float cq[8] = {a, 0, b, 0, b, (float)h, a, (float)h};
                memcpy(line->char_quad, cq, sizeof cq);
            }
        }
        return n;
    } catch (const std::bad_alloc &) {
        set_err("ocr: out of memory");
        return -1;
    }
}

int candidates_at(const uint8_t *grey, int w, int h, int stride,
                  int tap_x, int tap_y, ocr_cand_t *out, int max, ocr_line_t *line)
{
    clear_line(line);
    S.err[0] = 0;
    if (!grey || w < 8 || h < 8 || stride < w) { set_err("ocr: no image"); return -1; }
    if (tap_x < 0 || tap_y < 0 || tap_x >= w || tap_y >= h) { set_err("ocr: the tap is off the image"); return 0; }
    try {
        if (!load()) return -1;
        timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        /* the detector's window: centred on the tap, inside the image. An
         * image smaller than the window -- a strip of a line or two -- gets a
         * margin of its own edge pixels first: text touching the edge of the
         * detector's input is found in pieces, or not at all. */
        int px = w < S.window ? std::min(PAD, (S.window - w) / 2) : 0;
        int py = h < S.window ? std::min(PAD, (S.window - h) / 2) : 0;
        const uint8_t *dg = grey;
        int dw = w, dh = h, ds = stride;
        std::vector<uint8_t> padded;
        if (px || py) {
            dw = w + 2 * px;
            dh = h + 2 * py;
            ds = dw;
            padded.resize((size_t)dw * dh);
            for (int y = 0; y < dh; y++) {
                const uint8_t *src = grey + (size_t)std::max(0, std::min(h - 1, y - py)) * stride;
                uint8_t *d = padded.data() + (size_t)y * dw;
                memset(d, src[0], px);
                memcpy(d + px, src, w);
                memset(d + px + w, src[w - 1], px);
            }
            dg = padded.data();
        }
        int ww = std::min(S.window, dw), wh = std::min(S.window, dh);
        int x0 = std::max(0, std::min(dw - ww, tap_x + px - ww / 2));
        int y0 = std::max(0, std::min(dh - wh, tap_y + py - wh / 2));
        std::vector<Box> boxes;
        if (!detect(dg, dw, dh, ds, x0, y0, ww, wh, boxes)) return -1;
        std::vector<uint8_t>().swap(padded);
        for (Box &b : boxes)
            for (Pt &q : b.p) {
                q.x = std::max(0.f, std::min((float)(w - 1), q.x - px));
                q.y = std::max(0.f, std::min((float)(h - 1), q.y - py));
            }
        int ms_det = ms_since(t0);

        /* the line under the tap (the smallest, if boxes overlap), or else the
         * nearest within a line's thickness of it */
        Pt tap = {(float)tap_x, (float)tap_y};
        int pick = -1;
        float pick_area = 1e30f, pick_d = 1e30f;
        for (size_t i = 0; i < boxes.size(); i++) {
            const Pt *q = boxes[i].p;
            float area = fabsf(cross(q[0], q[1], q[2])) / 2 + fabsf(cross(q[0], q[2], q[3])) / 2;
            if (in_quad(q, tap)) {
                if (pick_d > 0 || area < pick_area) { pick = (int)i; pick_area = area; pick_d = 0; }
                continue;
            }
            if (pick_d == 0) continue;
            float d = 1e30f;
            for (int k = 0; k < 4; k++) d = std::min(d, seg_dist(tap, q[k], q[(k + 1) % 4]));
            float thick = std::min(dist(q[0], q[1]), dist(q[0], q[3]));
            if (d <= thick && d < pick_d) { pick = (int)i; pick_d = d; }
        }
        if (pick < 0) {
            set_err("ocr: no text line at the tap (%d lines in the window)", (int)boxes.size());
            if (line) line->ms_det = ms_det;
            return 0;
        }
        const Box &b = boxes[pick];

        /* straightened: get_rotate_crop_image */
        int cw = (int)std::max(dist(b.p[0], b.p[1]), dist(b.p[2], b.p[3]));
        int ch = (int)std::max(dist(b.p[0], b.p[3]), dist(b.p[1], b.p[2]));
        if (cw < 1 || ch < 1) { set_err("ocr: an empty line box"); return 0; }
        const Pt rect[4] = {{0, 0}, {(float)cw, 0}, {(float)cw, (float)ch}, {0, (float)ch}};
        double to_img[9], to_crop[9];
        if (!persp(rect, b.p, to_img) || !persp(b.p, rect, to_crop)) { set_err("ocr: a degenerate line box"); return 0; }
        std::vector<uint8_t> crop((size_t)cw * ch);
        warp(grey, w, h, stride, to_img, crop.data(), cw, ch);
        bool vertical = ch >= 1.5 * cw;
        int lw = cw, lh = ch;
        if (vertical) {
            /* np.rot90: a quarter left, the column's top at the line's left */
            std::vector<uint8_t> r((size_t)cw * ch);
            for (int y = 0; y < cw; y++)
                for (int x = 0; x < ch; x++) r[(size_t)y * ch + x] = crop[(size_t)x * cw + (cw - 1 - y)];
            crop.swap(r);
            lw = ch;
            lh = cw;
        }

        timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        Read rd;
        if (!recognise(crop.data(), lw, lh, lw, rd)) return -1;

        /* the tap along the line, in the network's input pixels */
        Pt c = apply(to_crop, tap.x, tap.y);
        float along = vertical ? c.y : c.x;
        along = std::max(0.f, std::min((float)lw, along));
        int which = -1;
        int n = candidates(rd, along * rd.rw / lw, out, max, &which);
        if (line) {
            fill_text(rd, line);
            line->ms_det = ms_det;
            line->ms_rec = ms_since(t1);
            line->vertical = vertical;
            for (int i = 0; i < 4; i++) line->quad[2 * i] = b.p[i].x, line->quad[2 * i + 1] = b.p[i].y;
            line->char_index = which;
            if (n > 0) {
                /* a square glyph, as wide as the line is high, centred on the
                 * character read (or on the tap, if none was decoded there) */
                float m = which >= 0 ? REC_STEP * (rd.runs[which].s + rd.runs[which].e + 1) / 2.f * lw / rd.rw
                                     : along;
                float a = std::max(0.f, m - lh / 2.f), e = std::min((float)lw, m + lh / 2.f);
                Pt q[4];
                if (vertical) q[0] = {0, a}, q[1] = {(float)cw, a}, q[2] = {(float)cw, e}, q[3] = {0, e};
                else q[0] = {a, 0}, q[1] = {e, 0}, q[2] = {e, (float)ch}, q[3] = {a, (float)ch};
                for (int i = 0; i < 4; i++) {
                    Pt p = apply(to_img, q[i].x, q[i].y);
                    line->char_quad[2 * i] = p.x;
                    line->char_quad[2 * i + 1] = p.y;
                }
            }
        }
        if (n == 0 && !S.err[0]) set_err("ocr: no hanzi at the tap");
        return n;
    } catch (const std::bad_alloc &) {
        set_err("ocr: out of memory");
        return -1;
    }
}

}  // namespace

int ocr_read_line(const uint8_t *grey, int w, int h, int stride, int at_x,
                  ocr_cand_t *out, int max, ocr_line_t *line)
{
    int n = read_line(grey, w, h, stride, at_x, out, out ? max : 0, line);
    S.blobs.clear();
    S.scratch.clear();
    return n;
}

int ocr_candidates_at(const uint8_t *grey, int w, int h, int stride,
                      int tap_x, int tap_y, ocr_cand_t *out, int max, ocr_line_t *line)
{
    int n = candidates_at(grey, w, h, stride, tap_x, tap_y, out, out ? max : 0, line);
    S.blobs.clear();
    S.scratch.clear();
    return n;
}
