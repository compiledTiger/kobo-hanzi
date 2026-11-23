/* hanzi -- flashcards with stroke order, for Kobo. */
#include "core/gfx.h"
#include "core/db.h"
#include "backend/surf.h"
#include "core/pdf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <gnu/libc-version.h>
#include <string>

/* If the app dies on the device, the log is all we get. Name the signal and
 * flush, so the next report says what happened instead of just stopping.
 * Async-signal-safe: write(2) only, then re-raise for the real exit status. */
static void on_fatal(int sig) {
    const char *name =
        sig == SIGSEGV ? "\n*** FATAL: SIGSEGV (bad memory access)\n" :
        sig == SIGBUS  ? "\n*** FATAL: SIGBUS (bad address)\n" :
        sig == SIGFPE  ? "\n*** FATAL: SIGFPE (arithmetic)\n" :
        sig == SIGILL  ? "\n*** FATAL: SIGILL\n" :
        sig == SIGABRT ? "\n*** FATAL: SIGABRT (abort)\n" :
        sig == SIGTERM ? "\n*** terminated (SIGTERM -- killed from outside,"
                         " usually out of memory)\n" :
                         "\n*** FATAL: signal\n";
    ssize_t n = write(STDERR_FILENO, name, strlen(name));
    (void) n;
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_fatal_handlers(void) {
    signal(SIGSEGV, on_fatal);
    signal(SIGBUS,  on_fatal);
    signal(SIGFPE,  on_fatal);
    signal(SIGILL,  on_fatal);
    signal(SIGABRT, on_fatal);
    signal(SIGTERM, on_fatal);
#ifdef HANZI_DESKTOP
    /* Ctrl-C stops it even when a script started it in the background --
     * the AppImage's AppRun does, to pass a stop on to it -- which leaves
     * SIGINT ignored in it */
    signal(SIGINT, SIG_DFL);
#endif
}

/* ---- --diag ---------------------------------------------------------------
 * Everything a report of "it does not start" needs, on stdout, without
 * opening a database, loading the font or creating a file: the C library,
 * the backend's view of the display, and each file the app would use, where
 * it resolved to and whether it can be read. Exits 0 whatever it finds. */
static std::string abs_path(const char *p) {
    static char buf[PATH_MAX];
    if (realpath(p, buf)) return buf;
    /* not there (yet): its directory resolved, and the name */
    std::string s(p);
    size_t k = s.find_last_of('/');
    std::string dir = k == std::string::npos ? "." : (k == 0 ? "/" : s.substr(0, k));
    std::string base = k == std::string::npos ? s : s.substr(k + 1);
    if (realpath(dir.c_str(), buf)) return std::string(buf) + "/" + base;
    return s;
}

static void diag_file(const char *what, const char *p, int is_userdb) {
    std::string a = abs_path(p);
    struct stat st;
    if (stat(p, &st) != 0) {
        if (is_userdb) {
            std::string dir = a.substr(0, a.find_last_of('/') + 1);
            int w = access(dir.empty() ? "." : dir.c_str(), W_OK | X_OK) == 0;
            printf("  %-10s %s\n            not there yet: %s\n", what, a.c_str(),
                   w ? "it will be created (its folder is writable)"
                     : "FAILS: its folder is not writable, so it cannot be created");
        } else {
            printf("  %-10s %s\n            MISSING\n", what, a.c_str());
        }
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        int r = access(p, R_OK | X_OK) == 0;
        printf("  %-10s %s\n            folder, %s\n", what, a.c_str(), r ? "readable" : "NOT READABLE");
        DIR *d = r ? opendir(p) : NULL;
        struct dirent *e;
        int n = 0;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            std::string f = std::string(p) + "/" + e->d_name;
            struct stat fs;
            if (n++ < 24 && stat(f.c_str(), &fs) == 0)
                printf("              %-40s %10lld bytes%s\n", e->d_name, (long long)fs.st_size,
                       access(f.c_str(), R_OK) == 0 ? "" : "  NOT READABLE");
        }
        if (d) closedir(d);
        if (n > 24) printf("              ... and %d more\n", n - 24);
        if (r && n == 0) printf("              (empty)\n");
        return;
    }
    int r = access(p, R_OK) == 0, w = access(p, W_OK) == 0;
    const char *kind = "";
    if (r && strstr(what, ".db")) {       /* an SQLite file says so in its first 16 bytes */
        char h[16] = {0};
        FILE *f = fopen(p, "rb");
        if (f) {
            size_t n = fread(h, 1, sizeof h, f);
            fclose(f);
            kind = (n == 16 && !memcmp(h, "SQLite format 3", 16)) ? ", SQLite"
                 : st.st_size == 0 ? ", empty" : ", NOT an SQLite database";
        }
    }
    printf("  %-10s %s\n            %lld bytes%s, %s%s\n", what, a.c_str(), (long long)st.st_size, kind,
           r ? "readable" : "NOT READABLE",
           is_userdb ? (w ? ", writable" : ", NOT WRITABLE (progress could not be saved)") : "");
}

static int diag(const char *content, const char *user, const char *font, const char *models) {
    struct utsname u;
    printf("hanzi --diag\n");
    if (uname(&u) == 0) printf("system: %s %s %s\n", u.sysname, u.release, u.machine);
    /* the one running, and the one built against: older than that, and the
     * loader refuses the binary before any of this could print */
    printf("glibc: %s (%s), built against %d.%d\n", gnu_get_libc_version(),
           gnu_get_libc_release(), __GLIBC__, __GLIBC_MINOR__);
    static char exe[PATH_MAX], cwd[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    printf("binary: %s\nworking folder: %s\nbackend: %s\n", n > 0 ? exe : "?",
           getcwd(cwd, sizeof cwd) ? cwd : "?", surf_name());
    surf_diag();
    printf("files:\n");
    diag_file("content.db", content, 0);
    diag_file("user.db", user, 1);
    diag_file("font", font, 0);
    diag_file("models", models, 0);
    extern char **environ;
    printf("HANZI_* in the environment:\n");
    int any = 0;
    for (char **e = environ; *e; e++)
        if (!strncmp(*e, "HANZI_", 6)) { printf("  %s\n", *e); any = 1; }
    if (!any) printf("  (none)\n");
    return 0;
}

void app_run(int nopen, int window_given);
void app_shot(int nopen, const char *dir);
int  app_selftest(void);

#ifndef DEFAULT_W
#define DEFAULT_W 1264
#endif
#ifndef DEFAULT_H
#define DEFAULT_H 1680
#endif

int main(int argc, char **argv) {
    setvbuf(stderr, NULL, _IONBF, 0);     /* the log must survive a hard kill */
    install_fatal_handlers();
    int w = DEFAULT_W, h = DEFAULT_H, nopen = 0, size_given = 0;
    const char *content = "content.db", *user = "user.db";
    const char *font = "LXGWWenKaiLite-Regular.ttf";
    const char *shotdir = NULL;
    const char *kobodb = NULL;
    const char *plecoin = NULL, *plecoout = NULL;
    int selftest = 0, user_given = 0, want_diag = 0, winmode = -1;
    const char *models = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &w, &h); size_given = 1;
        } else if (!strcmp(argv[i], "--clara")) {
            w = 1072; h = 1448; nopen = 1; size_given = 1;
        } else if (!strcmp(argv[i], "--no-pen")) {
            nopen = 1;
        } else if (!strcmp(argv[i], "--content") && i + 1 < argc) {
            content = argv[++i];
        } else if (!strcmp(argv[i], "--user") && i + 1 < argc) {
            user = argv[++i]; user_given = 1;
        } else if (!strcmp(argv[i], "--font") && i + 1 < argc) {
            font = argv[++i];
        } else if (!strcmp(argv[i], "--selftest")) {
            selftest = 1;
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shotdir = argv[++i];
        } else if (!strcmp(argv[i], "--import-kobo") && i + 1 < argc) {
            kobodb = argv[++i];
        } else if (!strcmp(argv[i], "--import-pleco") && i + 1 < argc) {
            plecoin = argv[++i];
        } else if (!strcmp(argv[i], "--export-pleco") && i + 1 < argc) {
            plecoout = argv[++i];
        } else if (!strcmp(argv[i], "--diag")) {
            want_diag = 1;
        } else if (!strcmp(argv[i], "--models") && i + 1 < argc) {
            models = argv[++i];
        } else if (!strcmp(argv[i], "--window") && i + 1 < argc &&
                   (winmode = surf_window_mode_parse(argv[i + 1])) >= 0) {
            i++;
        } else {
            fprintf(stderr,
              "usage: %s [--size WxH] [--clara] [--no-pen]\n"
              "          [--content content.db] [--user user.db] [--font LXGWWenKaiLite-Regular.ttf]\n"
              "          [--import-kobo /path/to/KoboReader.sqlite]\n"
              "          [--import-pleco DIR] [--export-pleco DIR]\n"
              "          [--window window|full|left|right] [--models DIR]\n"
              "          [--diag]   what this machine has, for a report; starts nothing\n",
              argv[0]);
            return 2;
        }
    }

    /* The OCR models: --models, else HANZI_MODELS_DIR, else models/ beside
     * content.db (the Kobo's folder, and the AppImage's share/hanzi). The
     * app reads HANZI_MODELS_DIR, which is set here either way (after
     * --diag, which reports the environment as it was given). */
    static std::string models_dir;
    if (models) models_dir = models;
    else if (getenv("HANZI_MODELS_DIR") && *getenv("HANZI_MODELS_DIR")) models_dir = getenv("HANZI_MODELS_DIR");
    else {
        const char *sl = strrchr(content, '/');
        models_dir = sl ? std::string(content, (size_t)(sl - content)) + "/models" : "models";
        if (sl == content) models_dir = "/models";
    }
    /* --diag first: its list of HANZI_* is what the user set, not this
     * default */
    if (want_diag) return diag(content, user, font, models_dir.c_str());
    setenv("HANZI_MODELS_DIR", models_dir.c_str(), 1);

    /* The selftest and the shot harness tick decks, answer cards and change
     * settings in whatever user.db is open, and --user defaults to the real
     * one beside the binary. Without --user they get a throwaway one (as the
     * AppImage gives them); a --user file that already holds anything is
     * refused, since it may be someone's progress. */
    static char tmpdir[64], tmpuser[96];
    struct stat st;
    if ((selftest || shotdir) && !user_given) {
        const char *t = getenv("TMPDIR");
        snprintf(tmpdir, sizeof tmpdir, "%s/hanzi-XXXXXX",
                 t && *t && strlen(t) < 40 ? t : "/tmp");
        if (!mkdtemp(tmpdir)) { perror(tmpdir); return 1; }
        snprintf(tmpuser, sizeof tmpuser, "%s/user.db", tmpdir);
        user = tmpuser;
    } else if ((selftest || shotdir) && stat(user, &st) == 0 && st.st_size > 0) {
        fprintf(stderr, "%s holds progress: --selftest and --shot need a "
                        "--user file that does not exist yet\n", user);
        return 2;
    }

    if (!size_given) {                 /* let the device tell us its panel size */
        int pw, ph;
        if (surf_probe(&pw, &ph) == 0 && pw > 0 && ph > 0) { w = pw; h = ph; }
    }

    if (gfx_init(w, h) != 0) { fprintf(stderr, "gfx_init failed (%dx%d)\n", w, h); return 1; }
    fprintf(stderr, "app: buffer %dx%d ok\n", w, h);
    if (gfx_font_load(font) != 0) return 1;
    fprintf(stderr, "app: font %s ok\n", font);
    /* and for a PDF's Chinese that the file names but does not carry (read
     * only if a page asks for it) */
    pdf_set_cjk_font(font);
    if (db_open(content, user) != 0) { fprintf(stderr, "db: %s\n", db_err()); return 1; }

    if (plecoin) {
        int nf = 0, nl = 0;
        int n = db_import_pleco_dir(plecoin, &nf, &nl);
        if (n < 0) fprintf(stderr, "cannot read %s\n", plecoin);
        else printf("imported %d words in %d lists from %d file(s)\n", n, nl, nf);
        db_close(); gfx_free();
        return n < 0 ? 1 : 0;
    }

    if (plecoout) {
        int nf = 0;
        int n = db_export_pleco(plecoout, &nf);
        if (n < 0) fprintf(stderr, "cannot write to %s\n", plecoout);
        else printf("exported %d words to %d file(s) in %s\n", n, nf, plecoout);
        db_close(); gfx_free();
        return n < 0 ? 1 : 0;
    }

    if (kobodb) {                  /* import from a mounted device, then exit */
        int nw = 0, nh = 0;
        int n = db_import_kobo(kobodb, &nw, &nh);
        if (n < 0) fprintf(stderr, "import failed: %s\n", db_err());
        else printf("imported %d lookups, %d highlights (%d total)\n", nw, nh, n);
        db_close();
        gfx_free();
        return n < 0 ? 1 : 0;
    }

    if (selftest || shotdir) {
        int rc = 0;
        if (selftest) rc = app_selftest();
        else app_shot(nopen, shotdir); /* headless: render each screen to a .pgm */
        db_close(); gfx_free();
        if (tmpuser[0]) {              /* the throwaway progress file, and its dir */
            char j[128];
            snprintf(j, sizeof j, "%s-journal", tmpuser);
            unlink(tmpuser); unlink(j); rmdir(tmpdir);
        }
        return rc;
    }

    if (winmode >= 0) surf_set_window_mode(winmode);   /* applied as it opens */
    if (surf_open(w, h, "hanzi") != 0) { fprintf(stderr, "surface failed\n"); return 1; }

    if (!nopen && !surf_has_pen()) {
        fprintf(stderr, "no stylus found -- write cards will ask you to "
                        "picture the character instead\n");
        nopen = 1;
    }

    fprintf(stderr, "app: entering main loop\n");
    app_run(nopen, winmode >= 0);
    fprintf(stderr, "app: left main loop cleanly\n");

    /* Hand the screen back blank. Otherwise the app's last frame stays on the
     * panel until something else happens to repaint it, which looks exactly
     * like a crash. */
    gfx_clear(0xFF);
    surf_present(rc(0, 0, gfx_w, gfx_h), RF_FULL);

    surf_close();
    db_close();
    gfx_free();
    return 0;
}
