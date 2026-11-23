/* See pic.h. The zip reading is the least a zip needs: the end record, the
 * central directory, a local header, and the entry stored or deflated --
 * which is all an EPUB uses. stb_image inflates it (its raw-deflate entry
 * point) and decodes the PNG or JPEG inside. */
#include "pic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <unordered_map>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
/* a plate is at most a few thousand pixels a side; a damaged file claiming
 * more must not take the device's memory */
#define STBI_MAX_DIMENSIONS 4096   /* the largest plate is 1400x1100 */
#include "../vendor/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

/* Larger than any plate by far; an entry claiming more is damage. */
const uint32_t MAX_ENTRY = 32u << 20;

struct Entry { uint32_t local, csize, usize; uint16_t method; };

std::string zip_name;                                 /* whose directory this is */
std::unordered_map<std::string, Entry> zip_dir;

std::string pic_key;                                  /* what the pixels are of */
unsigned char *pix = NULL;
int pw = 0, ph = 0;

uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

struct File {
    FILE *f;
    explicit File(const char *p) : f(fopen(p, "rb")) {}
    ~File() { if (f) fclose(f); }
};

int read_dir(const char *path) {
    if (zip_name == path) return 1;
    zip_dir.clear();
    zip_name.clear();
    File z(path);
    if (!z.f) return 0;
    if (fseeko(z.f, 0, SEEK_END) != 0) return 0;
    off_t size = ftello(z.f);
    /* the end record: 22 bytes, after a comment of at most 64KB */
    off_t tail = size < 65557 ? size : 65557;
    std::string buf((size_t)tail, '\0');
    if (fseeko(z.f, size - tail, SEEK_SET) != 0 ||
        fread(&buf[0], 1, (size_t)tail, z.f) != (size_t)tail) return 0;
    const unsigned char *b = (const unsigned char *)buf.data();
    long eocd = -1;
    for (long i = (long)tail - 22; i >= 0; i--)
        if (u32(b + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) return 0;
    uint32_t cd_size = u32(b + eocd + 12), cd_off = u32(b + eocd + 16);
    /* The directory lies before its end record. A damaged record -- or a
     * false match in a truncated file -- must fail here, not allocate the
     * gigabytes its fields claim. */
    if ((uint64_t)cd_off + cd_size > (uint64_t)(size - tail + eocd)) return 0;
    std::string cd(cd_size, '\0');
    if (fseeko(z.f, (off_t)cd_off, SEEK_SET) != 0 ||
        fread(&cd[0], 1, cd_size, z.f) != cd_size) return 0;
    const unsigned char *c = (const unsigned char *)cd.data();
    size_t at = 0;
    while (at + 46 <= cd_size && u32(c + at) == 0x02014b50) {
        uint16_t nlen = u16(c + at + 28), xlen = u16(c + at + 30), klen = u16(c + at + 32);
        if (at + 46 + nlen > cd_size) break;
        Entry e;
        e.method = u16(c + at + 10);
        e.csize  = u32(c + at + 20);
        e.usize  = u32(c + at + 24);
        e.local  = u32(c + at + 42);
        zip_dir[std::string((const char *)c + at + 46, nlen)] = e;
        at += 46 + nlen + xlen + klen;
    }
    zip_name = path;
    return !zip_dir.empty();
}

/* The entry's bytes, unpacked; NULL if anything is off. Caller frees. */
unsigned char *read_entry(const char *path, const Entry &e, int *len) {
    File z(path);
    if (!z.f) return NULL;
    unsigned char lh[30];
    if (fseeko(z.f, (off_t)e.local, SEEK_SET) != 0 || fread(lh, 1, 30, z.f) != 30
        || u32(lh) != 0x04034b50) return NULL;
    off_t data = (off_t)e.local + 30 + u16(lh + 26) + u16(lh + 28);
    if (e.csize > MAX_ENTRY || e.usize > MAX_ENTRY) return NULL;
    unsigned char *raw = (unsigned char *)malloc(e.csize ? e.csize : 1);
    if (!raw) return NULL;
    if (fseeko(z.f, data, SEEK_SET) != 0 || fread(raw, 1, e.csize, z.f) != e.csize) {
        free(raw);
        return NULL;
    }
    if (e.method == 0) { *len = (int)e.csize; return raw; }
    if (e.method != 8) { free(raw); return NULL; }
    /* Into a buffer of the size the directory states, and no larger: an
     * entry whose header lies about its size would otherwise inflate for as
     * long as its data runs. It must fill the buffer exactly. */
    char *un = (char *)malloc(e.usize ? e.usize : 1);
    if (!un) { free(raw); return NULL; }
    int out = stbi_zlib_decode_noheader_buffer(un, (int)e.usize, (const char *)raw, (int)e.csize);
    free(raw);
    if (out < 0 || (uint32_t)out != e.usize) { free(un); return NULL; }
    *len = out;
    return (unsigned char *)un;
}

}  // namespace

int pic_zip_entries(const char *zip_path) {
    return read_dir(zip_path) ? (int)zip_dir.size() : -1;
}

int pic_load(const char *zip_path, const char *member) {
    std::string key = std::string(zip_path) + "\n" + member;
    if (key == pic_key && pix) return 1;
    /* A miss drops the last picture first, so a load that fails leaves
     * nothing behind to be taken for this one. */
    stbi_image_free(pix);
    pix = NULL; pw = ph = 0;
    pic_key.clear();
    if (!read_dir(zip_path)) return 0;
    auto it = zip_dir.find(member);
    if (it == zip_dir.end()) return 0;
    int len = 0;
    unsigned char *data = read_entry(zip_path, it->second, &len);
    if (!data) return 0;
    int w = 0, h = 0, n = 0;
    unsigned char *p = stbi_load_from_memory(data, len, &w, &h, &n, 1);
    free(data);
    if (!p) return 0;
    pix = p; pw = w; ph = h;
    pic_key = key;
    return 1;
}

int pic_w(void) { return pix ? pw : 0; }
int pic_h(void) { return pix ? ph : 0; }

void pic_draw(rect_t r) {
    if (!pix || r.w <= 0 || r.h <= 0) return;
    if (r.w > pw && r.w % pw != 0) {
        /* enlarged by an uneven amount (full screen): bilinear, so no
         * source pixel is drawn twice as wide as its neighbour */
        for (int y = 0; y < r.h; y++) {
            int dy = r.y + y;
            if (dy < 0 || dy >= gfx_h) continue;
            long fy = (long)y * (ph - 1) * 256 / (r.h > 1 ? r.h - 1 : 1);
            int y0 = (int)(fy >> 8), y1 = y0 + 1 < ph ? y0 + 1 : y0, wy = (int)(fy & 255);
            uint8_t *out = gfx_fb + (size_t)dy * gfx_w;
            for (int x = 0; x < r.w; x++) {
                int dx = r.x + x;
                if (dx < 0 || dx >= gfx_w) continue;
                long fx = (long)x * (pw - 1) * 256 / (r.w > 1 ? r.w - 1 : 1);
                int x0 = (int)(fx >> 8), x1 = x0 + 1 < pw ? x0 + 1 : x0, wx = (int)(fx & 255);
                const unsigned char *a0 = pix + (size_t)y0 * pw, *a1 = pix + (size_t)y1 * pw;
                int top = a0[x0] * (256 - wx) + a0[x1] * wx;
                int bot = a1[x0] * (256 - wx) + a1[x1] * wx;
                out[dx] = (uint8_t)((top * (256 - wy) + bot * wy) >> 16);
            }
        }
        return;
    }
    for (int y = 0; y < r.h; y++) {
        int dy = r.y + y;
        if (dy < 0 || dy >= gfx_h) continue;
        /* the source rows this row covers; at least one */
        int sy0 = (int)((long)y * ph / r.h), sy1 = (int)((long)(y + 1) * ph / r.h);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        uint8_t *out = gfx_fb + (size_t)dy * gfx_w;
        for (int x = 0; x < r.w; x++) {
            int dx = r.x + x;
            if (dx < 0 || dx >= gfx_w) continue;
            int sx0 = (int)((long)x * pw / r.w), sx1 = (int)((long)(x + 1) * pw / r.w);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            /* the average of the source pixels under it: downscaling a
             * line drawing by sampling one pixel loses its thin lines */
            unsigned sum = 0, cnt = 0;
            for (int sy = sy0; sy < sy1 && sy < ph; sy++) {
                const unsigned char *row = pix + (size_t)sy * pw;
                for (int sx = sx0; sx < sx1 && sx < pw; sx++) { sum += row[sx]; cnt++; }
            }
            out[dx] = (uint8_t)(cnt ? sum / cnt : 255);
        }
    }
}
