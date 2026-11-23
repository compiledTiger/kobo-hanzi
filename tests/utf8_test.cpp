/* Text arriving truncated mid-sequence must not be read past its end.
 * Build with -fsanitize=address to make an overread fatal. */
#include "../src/core/gfx.h"
#include "../src/ui/ui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) { printf("    usage: utf8_test <font.ttf>\n"); return 2; }
    gfx_init(400, 200);
    if (gfx_font_load(argv[1]) != 0) { printf("    FAIL: no font\n"); return 1; }

    /* every prefix of a 3-byte character, each at the end of its own block */
    const unsigned char full[] = { 0xE5, 0xAD, 0x97, 0 };     /* 字 */
    for (int keep = 1; keep <= 3; keep++) {
        char *p = static_cast<char *>(malloc((size_t)keep + 1));
        memcpy(p, full, (size_t)keep);
        p[keep] = 0;
        gfx_text_w(38, p);
        gfx_text(0, 50, 38, 0, p);
        free(p);
    }
    /* a stray continuation byte, and a lone lead byte */
    char *q = static_cast<char *>(malloc(2)); q[0] = (char)0x80; q[1] = 0;
    gfx_text_w(38, q); free(q);
    char *r = static_cast<char *>(malloc(2)); r[0] = (char)0xF0; r[1] = 0;
    gfx_text_w(38, r); free(r);

    /* Pinyin spacing: Kaiti draws accented vowels as full-width CJK glyphs,
     * which made "ren" with a tone mark render as "r e n" on every card. */
    {
        static const char *VOWEL[] = {
            "\u0101","\u00e1","\u01ce","\u00e0","\u0113","\u00e9","\u011b","\u00e8",
            "\u012b","\u00ed","\u01d0","\u00ec","\u014d","\u00f3","\u01d2","\u00f2",
            "\u016b","\u00fa","\u01d4","\u00f9","\u01d6","\u01d8","\u01da","\u01dc",
            "\u00fc"
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof VOWEL / sizeof *VOWEL; i++)
            if (gfx_text_w(38, VOWEL[i]) > (gfx_text_w(38, "n") * 7) / 5 + 1) bad++;
        if (bad || abs(gfx_text_w(38, "r\u00e8n shi") - gfx_text_w(38, "ren shi")) > 4) {
            printf("    FAIL: %d accented vowels are not at Latin width\n", bad);
            return 1;
        }
        printf("    -> accented pinyin sits at Latin width\n");
    }

    /* The helpers that index book text by character. Every draw walks a
     * line with these, and a lead byte with its tail cut off must stop at
     * the terminator rather than read the three bytes it promised. Each
     * string is its own allocation so the sanitiser can see the end of it. */
    {
        static const char *NASTY[] = {
            "\xE5\xAD\x97", "\xE5\xAD", "\xE5", "\x80", "\xF0\x9F\x98",
            "\xE5\xAD\x97\xE5", "a\xE5", "\xC3", "\xF4\x8F\xBF\xBF", "",
        };
        for (size_t i = 0; i < sizeof NASTY / sizeof *NASTY; i++) {
            size_t len = strlen(NASTY[i]);
            char *p = static_cast<char *>(malloc(len + 1));
            memcpy(p, NASTY[i], len + 1);
            int n = u8_len(p);
            for (int k = -2; k <= n + 2; k++) {
                char out[8];
                int got = u8_at(p, k, out);
                if (got < 0 || got > 4) {
                    printf("    FAIL: u8_at returned %d\n", got);
                    return 1;
                }
                if (got && (int)strlen(out) != got) {
                    printf("    FAIL: u8_at length %d, string %zu\n",
                           got, strlen(out));
                    return 1;
                }
            }
            gfx_text_w(38, p);
            free(p);
        }
        printf("    -> character indexing stops at the end of the string\n");
    }

    /* Marks the interface uses must actually be in the font. A glyph Kaiti
     * lacks is rendered blank, keeping its advance, so a missing one is not
     * a tofu box you would notice -- it is a gap where the answer was. Every
     * check mark in this app was invisible for weeks. */
    {
        gfx_clear(255);
        struct { const char *s; const char *what; int want; } M[] = {
            { "\u00b7",  "the middot used as a separator", 1 },
            { "\u2014",  "the dash",                       1 },
            { "\u2026",  "the ellipsis",                   1 },
            { "\U0010FFFD", "U+10FFFD, which no font has",  0 },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof M / sizeof *M; i++) {
            gfx_clear(255);
            gfx_text(10, 70, 40, 0, M[i].s);
            int n = 0;
            for (int y = 0; y < 100 && y < 200; y++)
                for (int x = 0; x < 100; x++)
                    if (gfx_fb[y * gfx_w + x] < 200) n++;
            int have = n > 0;
            if (have != M[i].want) {
                printf("    FAIL: %s %s\n", M[i].what,
                       have ? "is in the font after all -- use it"
                            : "is missing: it would draw as nothing");
                bad = 1;
            }
        }
        if (bad) return 1;
        printf("    -> the marks the interface draws are in the font\n");
    }

    /* The same past the glyph cache's size (GC_MAX_PX, 110): big glyphs are
     * drawn straight through, and drew the tofu box there -- the dictionary's
     * big word showed one for a character Kaiti lacks. */
    {
        struct { const char *s; int want; } B[] = {
            { "\u5b57", 1 },                     /* 字 */
            { "\U0010FFFD", 0 },                 /* in no font */
        };
        for (size_t i = 0; i < sizeof B / sizeof *B; i++) {
            gfx_clear(255);
            int adv = gfx_text(10, 170, 160, 0, B[i].s);
            int n = 0;
            for (long k = 0; k < (long)gfx_w * gfx_h; k++) if (gfx_fb[k] < 200) n++;
            if ((n > 0) != B[i].want || adv <= 0) {
                printf("    FAIL: at 160px, %s draws %d dark pixels, advance %d\n",
                       B[i].want ? "a character the font has" : "one it lacks", n, adv);
                return 1;
            }
        }
        printf("    -> a large glyph the font lacks is blank, its advance kept\n");
    }

    printf("    -> truncated and malformed UTF-8 handled\n");
    gfx_free();
    return 0;
}
