/* Drive the ink path the way a stroke does and check pixels actually change.
 * This path had never been exercised by a test: the screenshot harness only
 * renders screens, and a stroke needs input. */
#include "../src/ui/ui.h"
#include <stdio.h>
#include <string.h>

static long dark_pixels(void) {
    long n = 0;
    for (long i = 0; i < (long)gfx_w * gfx_h; i++) if (gfx_fb[i] < 0x40) n++;
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("    usage: ink_test <font.ttf>\n"); return 2; }
    gfx_init(1264, 1680);
    if (gfx_font_load(argv[1]) != 0) { printf("    FAIL: no font\n"); return 1; }
    gfx_clear(WHITE);

    ink_t k;
    ui_ink_clear(&k);
    long before = dark_pixels();

    /* a stroke like the one in the device log: 456,566 -> 480,741 */
    ui_ink_down(&k, 456, 566);
    int moves = 0, presented = 0;
    for (int i = 1; i <= 20; i++) {
        ui_ink_move(&k, 456 + i, 566 + i * 9);
        moves++;
        rect_t d = rc(0, 0, 0, 0);
        ui_ink_draw_last_seg(&k, 7, &d);
        if (d.w > 0) presented++;
    }
    long after = dark_pixels();

    printf("    points recorded: %d of %d moves\n", k.s[0].n, moves);
    printf("    segments with a dirty rect: %d\n", presented);
    printf("    dark pixels %ld -> %ld\n", before, after);

    int bad = 0;
    if (k.n != 1)            { printf("    FAIL: stroke not started\n"); bad = 1; }
    if (k.s[0].n < 10)       { printf("    FAIL: points dropped\n"); bad = 1; }
    if (presented < 10)      { printf("    FAIL: no dirty rects produced\n"); bad = 1; }
    if (after <= before)     { printf("    FAIL: nothing was drawn\n"); bad = 1; }

    /* and the full redraw path used after a clear */
    gfx_clear(WHITE);
    ui_ink_draw(&k, 7);
    if (dark_pixels() <= 0)  { printf("    FAIL: ui_ink_draw drew nothing\n"); bad = 1; }

    /* a tap: one point, which is a dot -- drawn as the pen goes down, with
     * a rect to show it by, and on a whole redraw */
    gfx_clear(WHITE);
    ui_ink_clear(&k);
    ui_ink_down(&k, 300, 300);
    rect_t d1 = rc(0, 0, 0, 0);
    ui_ink_draw_last_seg(&k, 7, &d1);
    if (d1.w <= 0 || dark_pixels() <= 0) { printf("    FAIL: a tap shows nothing\n"); bad = 1; }
    gfx_clear(WHITE);
    ui_ink_draw(&k, 7);
    if (dark_pixels() <= 0)  { printf("    FAIL: ui_ink_draw leaves a dot out\n"); bad = 1; }

    printf(bad ? "    -> FAILED\n" : "    -> ink path draws\n");
    gfx_free();
    return bad;
}
