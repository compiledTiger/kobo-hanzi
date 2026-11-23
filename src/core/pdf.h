/* PDF pages through MuPDF: one document open at a time, drawn into the
 * app's 8-bit greyscale buffer a region at a time, with the text layer under
 * a point and a guess at whether that layer can be believed; and the finder
 * that lists the PDFs on the device.
 *
 * Nothing here throws. MuPDF's errors are caught at the edge of every call
 * (fz_try), the call returns 0 / -1 / an empty result, and pdf_error() says
 * why. One fz_context lives for the whole run, its store of decoded pictures
 * and fonts bounded (PDF_STORE_BYTES) for the Kobo's memory; the page last
 * used is kept parsed (its display list and, once asked, its text), and
 * anything older is dropped as soon as another page is asked for.
 *
 * Coordinates. "Page" coordinates are MuPDF's: points, origin top left, y
 * down, with the page's own /Rotate already applied -- so pdf_page_size is
 * the page as it is meant to be read. A "view" is the page scaled by `scale`
 * (pixels a point) and turned `rot` degrees clockwise (0, 90, 180, 270),
 * with its top-left corner at (0, 0): pdf_render draws a rectangle of a
 * view, and pdf_view_to_page / pdf_page_to_view map a tap or a box between
 * the two. The view rotation is the reader's, separate from the Kobo
 * screen's flip. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

/* MuPDF's cache of decoded pictures, fonts and the like. A scanned page at
 * 300dpi is 8-25MB decoded, so this holds the page in hand and not much
 * more. */
#define PDF_STORE_BYTES (40u << 20)
/* A page counts as a scan when pictures cover this much of it. */
#define PDF_SCAN_COVER 0.5f

struct pdf_box { float x0, y0, x1, y1; };

/* One character of the text layer: its codepoint, as UTF-8, and its box in
 * page coordinates. `vertical` when its line runs down the page. */
struct pdf_char {
    int c;
    char utf8[8];
    pdf_box box;
    int vertical;
};

/* Open `path`, closing whatever was open; 1 if it opened and has a page.
 * A PDF that wants a password is refused (pdf_error says so) unless the
 * empty one opens it. */
int  pdf_open(const char *path);
void pdf_close(void);
int  pdf_is_open(void);
const char *pdf_path(void);              /* "" when none is open */
/* Why the last call that failed did; "" after a call that worked. */
const char *pdf_error(void);
/* Close and free the context as well (tests, valgrind, exit). The next call
 * makes a new one. */
void pdf_shutdown(void);

/* The font for Chinese a PDF names but does not embed (STSong-Light, SimSun
 * and the like): MuPDF is built without its own CJK fonts, so without this
 * such a page draws no Chinese. Read from `ttf_path` when first needed and
 * kept while a document is open. NULL or "" forgets it. */
void pdf_set_cjk_font(const char *ttf_path);

int  pdf_pages(void);                    /* 0 when none is open */
/* The page's size in points; 0 if there is no such page. */
int  pdf_page_size(int page, float *w, float *h);

/* The view of `page` at `scale`, turned `rot`: its size in whole pixels. */
int  pdf_view_size(int page, float scale, int rot, int *w, int *h);
/* A point of the view to the page and back. 0 if there is no such page. */
int  pdf_view_to_page(int page, float scale, int rot, float vx, float vy, float *px, float *py);
int  pdf_page_to_view(int page, float scale, int rot, float px, float py, float *vx, float *vy);
/* A box of the page in the view (still a box: the turns are right angles). */
int  pdf_box_to_view(int page, float scale, int rot, pdf_box b, pdf_box *out);

/* Draw the rectangle (x, y, w, h) of the view of `page` at `scale`, turned
 * `rot`, into `dst`: w x h bytes of grey, `stride` bytes a row, white where
 * the page is not. Only that rectangle is rasterised -- the rest of the page
 * is skipped, not drawn and thrown away -- so a zoomed quarter costs a
 * quarter. A rectangle running off the view is fine (white there). 1 if
 * drawn; on 0 the rectangle is left white, or as it was if `dst` is NULL. */
int  pdf_render(int page, float scale, int rot, int x, int y, int w, int h,
                uint8_t *dst, int stride);

/* The text layer. A page's text is read on first use and kept with it.
 * Spaces and other blanks are never returned.
 *
 * The character under (px, py), in page coordinates: one whose box holds the
 * point, else the nearest whose box is within `slack` points of it (a finger
 * is wide). 1 if found. On a scan (pdf_is_scan) the layer is somebody's OCR
 * and may be wrong: treat what this says as one candidate, not the answer. */
int  pdf_char_at(int page, float px, float py, float slack, pdf_char *out);
/* The line the character under (px, py) is on, in reading order (down the
 * column for vertical text), and that character's index in it; 1 if found. */
int  pdf_line_at(int page, float px, float py, float slack,
                 std::vector<pdf_char> *line, int *at);
/* Every character whose centre lies in `area`, in the layer's order. */
int  pdf_text_in(int page, pdf_box area, std::vector<pdf_char> *out);
/* How many characters (not blanks) the page's layer has; -1 on error. */
int  pdf_text_count(int page);

/* How much of the page pictures cover, 0..1, overlaps counted once; -1 on
 * error. A scanned page is one picture over all of it, sometimes in strips. */
float pdf_image_cover(int page);
/* Whether the page is mostly picture: its text layer, if any, is somebody's
 * OCR and not to be trusted alone. 1 scan, 0 not, -1 on error. */
int  pdf_is_scan(int page);

/* The PDFs under `roots`: every regular file whose name ends ".pdf" in any
 * case, through subdirectories `max_depth` deep (0: the root alone), never
 * into hidden ones (.kobo, .adds) or through a symlinked directory, stopping
 * at `max_files`. Sorted by name, case folded for Latin, then by path. Uses
 * the directory entries' types, so a large card is not one stat per file. */
struct pdf_file {
    std::string path;    /* as found: root + "/" + ... */
    std::string name;    /* the file's name without ".pdf" */
};
std::vector<pdf_file> pdf_scan(const std::vector<std::string> &roots, int max_depth,
                               size_t max_files);
/* Where to look: the Kobo's /mnt/onboard; on the desktop $HANZI_PDF_DIRS
 * (a colon list), else ~/Documents. */
std::vector<std::string> pdf_default_roots(void);
