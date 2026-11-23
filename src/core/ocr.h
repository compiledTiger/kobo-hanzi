/* On-device OCR of a printed character: the one under a finger on a page.
 *
 * PaddleOCR's PP-OCRv4 text detector and recogniser (data/models/, converted
 * to ncnn; the preprocessing is data/models/README.md's, which is RapidOCR's).
 * A tap runs the detector on a window around the tap -- never the whole page
 * -- takes the text line under the finger (or the nearest one), straightens
 * and reads it, and returns the hanzi the recogniser considered at the tapped
 * position, most likely first. Vertical columns are read as RapidOCR reads
 * them: a box at least 1.5 times as tall as it is wide is turned a quarter
 * left, so its top is the line's start.
 *
 * Nothing here throws or aborts: a missing model, a failed allocation or an
 * image too small to hold text is a return value of 0 or less and a reason in
 * ocr_error(). The image is 8-bit grey, row by row, `stride` bytes apart.
 * Not thread-safe: one caller at a time. */
#pragma once
#include <stdint.h>

/* A candidate character, UTF-8, and the recogniser's probability for it at
 * the tapped position (0..1; higher is better). */
typedef struct {
    char  ch[8];
    float p;
} ocr_cand_t;

/* What was read around the tap, for context and for showing the user where. */
typedef struct {
    char  text[512];      /* the whole line, greedy CTC decoding, every character
                             (cut at a character boundary if longer) */
    float score;          /* the line's mean confidence, 0..1 */
    float quad[8];        /* the line's box in image pixels, x y for each of
                             top-left, top-right, bottom-right, bottom-left */
    int   vertical;       /* 1 if it was read as a column (turned a quarter) */
    int   char_index;     /* the tapped character's index in text, counted in
                             characters; -1 if the candidates come from where
                             the greedy reading decoded nothing (a character
                             it dropped), or if there are none */
    float char_quad[8];   /* where the candidates were read: a square as wide
                             as the line is thick, as quad; set when the
                             call returned candidates */
    int   ms_det, ms_rec; /* milliseconds in the detector and the recogniser */
} ocr_line_t;

/* Where the models are: a directory holding ppocrv4_det.param/.bin,
 * ppocrv4_rec.param/.bin and ppocrv4_keys.txt. Cheap -- it only checks that
 * they are there; the networks are loaded on first use. 1 if they are. */
int  ocr_open(const char *models_dir);
/* Free the networks and the key table (tens of MB once loaded). The directory
 * is kept, so the next call loads them again. */
void ocr_close(void);
/* Threads for the networks (default 2; an ncnn built without OpenMP uses 1). */
void ocr_set_threads(int n);
/* The detector's window, a square of this many image pixels around the tap
 * (default 640, kept within 320..960). */
void ocr_set_window(int side);
/* The detector reads the window scaled by this (default 1; 0.25..1): text
 * that is large in the image is found as well at half the pixels. */
void ocr_set_det_scale(float s);
/* Why the last call returned nothing, or "". */
const char *ocr_error(void);

/* The hanzi under (tap_x, tap_y): up to `max` candidates, best first, CJK
 * unified ideographs only. Returns how many: 0 if no text line is near the
 * tap or nothing read there is a hanzi, -1 on an error (models missing, out
 * of memory). `line` (may be null) gets the line, its text and the boxes,
 * whenever a line was found. */
int ocr_candidates_at(const uint8_t *grey, int w, int h, int stride,
                      int tap_x, int tap_y, ocr_cand_t *out, int max, ocr_line_t *line);

/* Lower level, for tests: read an image that is one horizontal text line,
 * already cropped and upright. `at_x` is a position along it, in its pixels:
 * the candidates are for the character there (-1: none, the text only). */
int ocr_read_line(const uint8_t *grey, int w, int h, int stride, int at_x,
                  ocr_cand_t *out, int max, ocr_line_t *line);

/* Is the codepoint a CJK unified ideograph (the basic block, extension A and
 * the supplementary-plane extensions)? Punctuation, kana, digits are not. */
int ocr_is_hanzi(uint32_t cp);
