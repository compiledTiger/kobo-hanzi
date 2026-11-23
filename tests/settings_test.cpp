#include "../src/core/db.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv) {
    if (db_open(argv[1], argv[2]) != 0) { printf("cannot open\n"); return 1; }
    int bad = 0;
    printf("defaults on a fresh database:\n");
    struct { const char *n; int got, want; } d[] = {
        { "pen_only",   db_pen_only(),   1 },
        { "guided",     db_guided(),     0 },
        { "show_punct", db_show_punct(), 1 },
        { "text_scale", db_text_scale(), 100 },
        { "light",      db_light(),      -1 },
        { "last_text",  db_last_text(),  0 },
    };
    for (auto &x : d) {
        printf("  %-12s %4d (want %4d) %s\n", x.n, x.got, x.want,
               x.got == x.want ? "ok" : "WRONG");
        if (x.got != x.want) bad = 1;
    }
    printf("round trip:\n");
    db_set_pen_only(0);   if (db_pen_only() != 0)   { printf("  pen_only WRONG\n"); bad=1; }
    db_set_guided(1);     if (db_guided() != 1)     { printf("  guided WRONG\n"); bad=1; }
    db_set_show_punct(0); if (db_show_punct() != 0) { printf("  punct WRONG\n"); bad=1; }
    db_set_light(37);     if (db_light() != 37)     { printf("  light WRONG\n"); bad=1; }
    db_set_last_text(5);  if (db_last_text() != 5)  { printf("  last_text WRONG\n"); bad=1; }
    db_set_new_limit(0, 7);  if (db_new_limit(0) != 7)  { printf("  wlimit WRONG\n"); bad=1; }
    db_set_new_limit(1, 23); if (db_new_limit(1) != 23) { printf("  rlimit WRONG\n"); bad=1; }
    if (db_new_limit(0) != 7) { printf("  limits collided\n"); bad=1; }
    printf("clamping:\n");
    db_set_text_scale(5);   printf("  scale 5   -> %d\n", db_text_scale());
    if (db_text_scale() != TEXT_SCALE_MIN) bad = 1;
    db_set_text_scale(900); printf("  scale 900 -> %d\n", db_text_scale());
    if (db_text_scale() != TEXT_SCALE_MAX) bad = 1;
    db_set_new_limit(0, -5);   if (db_new_limit(0) != 0)   { printf("  limit low WRONG\n"); bad=1; }
    db_set_new_limit(0, 9999); if (db_new_limit(0) != 200) { printf("  limit high WRONG\n"); bad=1; }
    printf(bad ? "FAILED\n" : "all settings round-trip\n");
    return bad;
}
