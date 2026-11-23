/* Feed each character's own stroke medians back in as if they had been drawn,
 * and check the recogniser returns that character. This is the easy case --
 * perfect input -- but it catches a broken normalisation or a signature that
 * does not round-trip, which would make real recognition hopeless. */
#include "../src/core/db.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *content = argc > 1 ? argv[1] : "out/content.db";
    if (db_open(content, "/tmp/hw_test_user.db") != 0) {
        printf("    FAIL: %s\n", db_err());
        return 1;
    }
    const char *probes[] = { "\xe5\xa5\xbd", "\xe7\x88\xb1", "\xe6\x88\x91",
                             "\xe4\xbd\xa0", "\xe5\xad\x97", "\xe9\xbe\x99",
                             "\xe5\xb1\xb1", "\xe4\xb8\x80", "\xe6\x9c\xa8",
                             "\xe6\xb0\xb4" };
    int n = (int)(sizeof probes / sizeof *probes);
    int top1 = 0, top10 = 0;

    for (int i = 0; i < n; i++) {
        unsigned char sig[HW_MAX_STROKES * 6];
        int ns = db_hw_signature_of(probes[i], sig, HW_MAX_STROKES);
        if (ns <= 0) { printf("    (no signature for %s)\n", probes[i]); continue; }
        hwcand_t out[10];
        int got = db_hw_search(sig, ns, out, 10);
        int rank = -1;
        for (int k = 0; k < got; k++) if (!strcmp(out[k].ch, probes[i])) { rank = k; break; }
        if (rank == 0) top1++;
        if (rank >= 0) top10++;
        printf("    %s -> %s (rank %d of %d)\n", probes[i],
               got ? out[0].ch : "?", rank, got);
    }
    printf("    exact input: %d/%d ranked first, %d/%d in the top 10\n",
           top1, n, top10, n);

    /* Now the case that matters: sloppy input. Jitter every point, which is
     * what a hand actually produces. */
    unsigned seed = 12345;
    for (int noise = 8; noise <= 32; noise += 12) {
        int n1 = 0, n5 = 0, n10 = 0, tried = 0;
        for (int i = 0; i < n; i++) {
            unsigned char sig[HW_MAX_STROKES * 6];
            int ns = db_hw_signature_of(probes[i], sig, HW_MAX_STROKES);
            if (ns <= 0) continue;
            tried++;
            for (int b = 0; b < ns * 6; b++) {
                seed = seed * 1103515245u + 12345u;
                int jitter = (int)((seed >> 16) % (unsigned)(2 * noise + 1)) - noise;
                int v = (int)sig[b] + jitter;
                sig[b] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
            hwcand_t out[10];
            int got = db_hw_search(sig, ns, out, 10);
            for (int k = 0; k < got; k++)
                if (!strcmp(out[k].ch, probes[i])) {
                    if (k == 0) n1++;
                    if (k < 5)  n5++;
                    n10++;
                    break;
                }
        }
        printf("    jitter +-%2d: %d/%d first, %d/%d top 5, %d/%d top 10\n",
               noise, n1, tried, n5, tried, n10, tried);
    }

    db_close();
    return top1 < n ? 1 : 0;
}
