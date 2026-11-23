/* Dictionary search, as it is typed: characters, pinyin with or without
 * tones, and the two mixed -- you know part of a word by sight and the rest
 * by sound. Mixed queries matched nothing until they were split in two. */
#include "../src/core/db.h"
#include <cstdio>
#include <cstring>

static int first_is(const char *q, const char *want) {
    dictrow_t r[8];
    int n = db_dict(q, r, 8);
    if (n > 0 && !strcmp(r[0].simp, want)) return 1;
    printf("    FAIL: \"%s\" gave %s first, wanted %s\n", q, n ? r[0].simp : "nothing", want);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3 || db_open(argv[1], argv[2]) != 0) { printf("    FAIL: no db\n"); return 1; }
    int ok = 1;
    ok &= first_is("你好", "你好");        /* 你好 */
    ok &= first_is("nihao", "你好");
    ok &= first_is("ni3hao3", "你好");
    ok &= first_is("你hao", "你好");           /* 你hao */
    ok &= first_is("ni好", "你好");            /* ni好 */
    ok &= first_is("你hao3", "你好");          /* 你hao3 */
    ok &= first_is("不keqi", "不客气");    /* 不keqi -> 不客气 */
    if (ok) printf("    -> characters, pinyin and the two mixed all find the word\n");
    return ok ? 0 : 1;
}
