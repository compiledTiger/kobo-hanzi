/* A picture read straight out of an EPUB -- a zip -- and drawn into the
 * greyscale buffer: the 连环画's plates. The zip's directory is read once
 * per file and kept; the last picture decoded is kept too, so a redraw of
 * the same plate (pinyin on, a toast) does not decode it again. */
#pragma once
#include "gfx.h"

/* Load `member` of the zip at `zip_path`; 1 if it is there and decodes. */
int  pic_load(const char *zip_path, const char *member);
int  pic_w(void);
int  pic_h(void);
/* Draw the loaded picture scaled to fill `r` (the caller keeps the aspect). */
void pic_draw(rect_t r);
/* How many entries the zip's directory has, or -1 if it cannot be read. */
int  pic_zip_entries(const char *zip_path);
