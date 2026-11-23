// MuPDF and ncnn, linked statically, actually work: a one-page PDF held in
// memory is opened and drawn to greyscale (a black square on white), and a
// one-layer ncnn net is loaded from memory and run. Built for the desktop and
// the Kobo by mk/deps.mk (`make deps-test`); the desktop one is run.
#include <mupdf/fitz.h>
#include <ncnn/net.h>
#include <cstdio>
#include <cstring>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// A 100x100pt page with a 50x50 black square in its middle. The xref offsets
// are computed, so the file is well formed and MuPDF need not repair it.
static std::string tiny_pdf()
{
    const char *content = "0 g 25 25 50 50 re f\n";
    std::string objs[4] = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 4 0 R >>",
        "<< /Length " + std::to_string(std::strlen(content)) + " >>\nstream\n" + content + "endstream",
    };
    std::string pdf = "%PDF-1.4\n";
    long off[4];
    for (int i = 0; i < 4; i++) {
        off[i] = (long)pdf.size();
        pdf += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    long xref = (long)pdf.size();
    pdf += "xref\n0 5\n0000000000 65535 f \n";
    for (int i = 0; i < 4; i++) {
        char line[32];
        std::snprintf(line, sizeof line, "%010ld 00000 n \n", off[i]);
        pdf += line;
    }
    pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

static void test_mupdf()
{
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    std::string pdf = tiny_pdf();
    fz_buffer *buf = nullptr;
    fz_stream *stm = nullptr;
    fz_document *doc = nullptr;
    fz_pixmap *pix = nullptr;
    int pages = -1, centre = -1, corner = -1, w = 0, h = 0;
    fz_var(buf); fz_var(stm); fz_var(doc); fz_var(pix);
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char *)pdf.data(), pdf.size());
        stm = fz_open_buffer(ctx, buf);
        doc = fz_open_document_with_stream(ctx, "application/pdf", stm);
        pages = fz_count_pages(ctx, doc);
        // 2x: 200x200 pixels, one byte each, as the app's buffer is.
        pix = fz_new_pixmap_from_page_number(ctx, doc, 0, fz_scale(2, 2),
                                             fz_device_gray(ctx), 0);
        w = fz_pixmap_width(ctx, pix);
        h = fz_pixmap_height(ctx, pix);
        const unsigned char *s = fz_pixmap_samples(ctx, pix);
        int stride = (int)fz_pixmap_stride(ctx, pix);
        centre = s[(h / 2) * stride + w / 2];
        corner = s[5 * stride + 5];
    }
    fz_always(ctx) {
        fz_drop_pixmap(ctx, pix);
        fz_drop_document(ctx, doc);
        fz_drop_stream(ctx, stm);
        fz_drop_buffer(ctx, buf);
    }
    fz_catch(ctx) {
        std::printf("FAIL mupdf: %s\n", fz_caught_message(ctx));
        fails++;
    }
    fz_drop_context(ctx);
    std::printf("mupdf %s: %d page, %dx%d, centre %d, corner %d\n",
                FZ_VERSION, pages, w, h, centre, corner);
    CHECK(pages == 1);
    CHECK(w == 200 && h == 200);
    CHECK(centre == 0);
    CHECK(corner == 255);
}

static void test_ncnn()
{
    // Input, then ReLU: -2 1 -3 4 comes back as 0 1 0 4.
    const char *param =
        "7767517\n"
        "2 2\n"
        "Input data 0 1 data 0=4 1=1 2=1\n"
        "ReLU relu 1 1 data out\n";
    ncnn::Net net;
    net.opt.num_threads = 1;
    net.opt.use_vulkan_compute = false;
    int r = net.load_param_mem(param);
    CHECK(r == 0);
    net.load_model((const unsigned char *)"");      // no weights in this net
    ncnn::Mat in(4, 1, 1);
    const float v[4] = {-2, 1, -3, 4};
    std::memcpy((float *)in.data, v, sizeof v);
    ncnn::Extractor ex = net.create_extractor();
    ex.input("data", in);
    ncnn::Mat out;
    r = ex.extract("out", out);
    CHECK(r == 0);
    CHECK(out.w == 4);
    if (r == 0 && out.w == 4) {
        const float *o = (const float *)out.data;
        std::printf("ncnn: relu(-2 1 -3 4) = %g %g %g %g\n", o[0], o[1], o[2], o[3]);
        CHECK(o[0] == 0 && o[1] == 1 && o[2] == 0 && o[3] == 4);
    }
}

int main()
{
    test_mupdf();
    test_ncnn();
    std::printf(fails ? "deps_link_test: %d FAILED\n" : "deps_link_test: ok\n", fails);
    return fails ? 1 : 0;
}
