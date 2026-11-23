/* surf_fb.c -- Kobo backend.
 *
 * FBInk supplies the things that are genuinely hard and device-specific: the
 * framebuffer pointer, stride/bpp, the per-model touch quirks, and the e-ink
 * refresh itself (it knows the MTK/HWTCON path used by the Clara and Libra
 * Colour). Everything else here is a plain evdev reader.
 *
 * Pen and touch orientation are read from FBInk's quirk table but can be
 * overridden at runtime without rebuilding:
 *     HANZI_PEN_SWAP=1 HANZI_PEN_MIRRORX=1 HANZI_PEN_MIRRORY=1
 *     HANZI_INPUT_DEBUG=1     log raw events to stderr
 */
#include "surf.h"
#include "../vendor/FBInk/fbink.h"
#include "mtstate.h"
#include "rot.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <algorithm>
#include <utility>
#include <vector>
#include <sys/ioctl.h>
#include <sys/stat.h>

/* Older kernel headers name the event time `time`; newer ones, and 32-bit
 * ARM with a 64-bit time_t, use these. */
#ifndef input_event_sec
#define input_event_sec  time.tv_sec
#define input_event_usec time.tv_usec
#endif

/* Set when the input scan finds the page-turn keys; the interface marks
 * which key presses which grade, and only where they exist. */
static int g_has_pagekeys = 0;

#define MAXDEV 8

/* ---- input ---------------------------------------------------------------
 * The decoding lives in mtstate, which is tested on the desktop against
 * recorded frames from this panel; here we only map coordinates and route. */

struct InputDev {
    int  fd       = -1;
    bool is_power = false;
    bool is_keys  = false;          /* page-turn buttons live here too */
    bool is_pen   = false;
    int  minx = 0, maxx = 1, miny = 0, maxy = 1;
    bool swap = false, mirx = false, miry = false;
    mt::Tracker tracker;
    long long pen_ms = 0;
    ev_t pending[mt::MAX_SLOTS * 2];
    int  npending = 0;
    int  drained  = 0;
};

static InputDev s_dev[MAXDEV];

/* ---- framebuffer state ------------------------------------------------- */

static int          s_fbfd = -1;
static FBInkConfig  s_cfg = {};
static FBInkState   s_state = {};
static unsigned char *s_fb = nullptr;
static size_t       s_fblen = 0;
static int          s_w = 0, s_h = 0, s_bpp = 0, s_stride = 0;
static int          s_ndev = 0;
static int          s_dbg = 0;
/* The flip view's quarter turns (surf_set_rotation). The panel is never
 * turned: the app's buffer is, by blit on the way out and by queue() for
 * touch on the way in (rot.h). */
static int          s_rot = 0;

const char *surf_name(void) { return "fbink"; }

/* 64-bit, as app.cpp's msec_t: `long` is 32 bits on the Kobo, and
 * tv_sec * 1000 in it wrapped after 24.8 days awake. */
static long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int envflag(const char *k, int dflt) {
    const char *v = getenv(k);
    return v ? atoi(v) : dflt;
}

static void scan_inputs(void);   /* defined with the input half below */

/* The app always runs upright (canonical rotation 0). Nickel turns the
 * framebuffer with the device -- upside down, or landscape -- and leaves it
 * so when it is killed. The app then drew turned while touch, whose axes
 * FBInk's quirk table gives for upright, was not: taps landed on the wrong
 * buttons (the Libra Colour, launched from a Nickel held upside down). So
 * the framebuffer is put upright on opening and after every wake, and
 * Nickel's rotation goes back on close.
 *
 * Held upside down is honoured all the same, in software: s_base is a half
 * turn added under the flip view's s_rot (panel_q), so picture and touch go
 * through rot.h together, and the page buttons swap with it. Landscape is
 * not a way the app is laid out, so a Nickel in landscape opens it upright. */
static uint32_t s_nickel_rota = KEEP_CURRENT_ROTATE;
static int      s_base = 0;      /* 0 upright, 2 upside down: as Nickel was held */
static int panel_q(void) { return rot_norm(s_rot + s_base); }

static int force_upright(const char *when) {   /* 1: it turned the screen */
    fbink_get_state(&s_cfg, &s_state);
    uint8_t canon = fbink_rota_native_to_canonical(s_state.current_rota);
    if (canon == 0 || canon > 3) return 0;        /* upright, or not a Kobo */
    uint32_t want = fbink_rota_canonical_to_native(0);
    if (s_nickel_rota == KEEP_CURRENT_ROTATE) {     /* the first time: at opening */
        s_nickel_rota = s_state.current_rota;
        s_base = canon == 2 ? 2 : 0;
    }
    int rc = fbink_set_fb_info(s_fbfd, want, KEEP_CURRENT_BITDEPTH,
                               KEEP_CURRENT_GRAYSCALE, &s_cfg);
    fprintf(stderr, "fb: %s: rotation %u (canonical %u) -> %u upright: rc=%d; drawn %s\n",
            when, (unsigned)s_state.current_rota, (unsigned)canon, (unsigned)want, rc,
            s_base ? "upside down" : "upright");
    fbink_get_state(&s_cfg, &s_state);
    return 1;
}

int surf_probe(int *w, int *h) {
    int fd = fbink_open();
    if (fd < 0) return -1;
    FBInkConfig cfg = {};
    if (fbink_init(fd, &cfg) < 0) { fbink_close(fd); return -1; }
    FBInkState st = {};
    fbink_get_state(&cfg, &st);
    *w = (int)st.screen_width;
    *h = (int)st.screen_height;
    fbink_close(fd);
    return 0;
}

int surf_open(int w, int h, const char *title) {
    (void)title;
    s_dbg = envflag("HANZI_INPUT_DEBUG", 0);

    s_fbfd = fbink_open();
    if (s_fbfd < 0) { fprintf(stderr, "fbink_open failed\n"); return -1; }
    s_cfg.is_quiet = true;
    if (fbink_init(s_fbfd, &s_cfg) < 0) { fprintf(stderr, "fbink_init failed\n"); return -1; }
    force_upright("open");

    s_w = (int)s_state.screen_width;
    s_h = (int)s_state.screen_height;
    s_bpp = (int)s_state.bpp;
    s_stride = (int)s_state.scanline_stride;

    fprintf(stderr, "fb: %s (%s) %dx%d %dbpp stride=%d mtk=%d\n",
            s_state.device_name, s_state.device_codename,
            s_w, s_h, s_bpp, s_stride, (int)s_state.is_mtk);
    fprintf(stderr, "fb: rota=%u boot_rota=%u quirky_landscape=%d can_rotate=%d\n",
            (unsigned)s_state.current_rota, (unsigned)s_state.ntx_boot_rota,
            (int)s_state.is_ntx_quirky_landscape, (int)s_state.can_rotate);
    if (s_state.is_ntx_quirky_landscape)
        fprintf(stderr, "fb: WARNING quirky landscape -- the screen may come out "
                        "scrambled; report this line\n");

    if (s_w != w || s_h != h)
        fprintf(stderr, "fb: WARNING app built for %dx%d, panel is %dx%d\n", w, h, s_w, s_h);

    s_fb = fbink_get_fb_pointer(s_fbfd, &s_fblen);
    if (!s_fb) { fprintf(stderr, "fbink_get_fb_pointer failed\n"); return -1; }

    scan_inputs();
    return 0;
}

void surf_close(void) {
    for (int i = 0; i < s_ndev; i++) close(s_dev[i].fd);
    if (s_fbfd >= 0) {
        if (s_nickel_rota != KEEP_CURRENT_ROTATE) {      /* give Nickel its own back */
            int rc = fbink_set_fb_info(s_fbfd, s_nickel_rota, KEEP_CURRENT_BITDEPTH,
                                       KEEP_CURRENT_GRAYSCALE, &s_cfg);
            fprintf(stderr, "fb: close: rotation back to %u: rc=%d\n",
                    (unsigned)s_nickel_rota, rc);
        }
        fbink_close(s_fbfd);
    }
}

/* After a suspend the framebuffer's state can change underneath us, and the
 * pointer we cached may no longer be the right one. FBInk's own header says to
 * call fbink_reinit for exactly this. */
int surf_reinit(void) {
    int rc = fbink_reinit(s_fbfd, &s_cfg);
    if (rc < 0) {
        fprintf(stderr, "fb: reinit failed (%d)\n", rc);
        return 0;
    }
    if (force_upright("wake")) rc = 1;                  /* counts as changed: full redraw */
    size_t len = 0;
    unsigned char *p = fbink_get_fb_pointer(s_fbfd, &len);
    int nw = (int)s_state.screen_width, nh = (int)s_state.screen_height;
    int changed = (rc != 0) || (p != s_fb)
               || ((int)s_state.scanline_stride != s_stride)
               || nw != s_w || nh != s_h;
    if (p) { s_fb = p; s_fblen = len; }
    s_stride = (int)s_state.scanline_stride;
    s_bpp    = (int)s_state.bpp;
    /* Track the panel size too: every clamp in blit and present is against
     * these, so leaving them stale after a rotation would write past the end
     * of the mapping rather than clip to it. */
    if (nw != s_w || nh != s_h)
        fprintf(stderr, "fb: panel is now %dx%d (was %dx%d)\n", nw, nh, s_w, s_h);
    s_w = nw;
    s_h = nh;
    if (changed)
        fprintf(stderr, "fb: state changed after wake (rc=%d stride=%d bpp=%d)\n",
                rc, s_stride, s_bpp);
    return changed;
}

/* Suspend, the way this kernel wants it.
 *
 * Writing "mem" to /sys/power/state on its own is refused with EPERM on a
 * Kobo -- which is exactly what the device log showed. The kernel wants
 * /sys/power/state-extended flipped to 1 first: it sets gSleep_Mode_Suspend,
 * which the NTX kernel checks all over the place to put subsystems down.
 * Nickel does 1 -> mem -> 0, KOReader does the same, and so do we.
 *
 * The sleep between the two writes is not superstition on our part; KOReader
 * carries the same wait with a note about things hanging without it.
 *
 * Blocks until something wakes the device -- on a Kobo, the power button. */
/* Reports the real errno, captured before close() can overwrite it -- the
 * previous version printed whatever errno happened to hold, which made the
 * device log say EPERM for failures that were nothing of the sort. */
static int write_sysfs(const char *path, const char *val) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "sysfs: open %s failed (%s)\n", path, strerror(errno));
        return 0;
    }
    ssize_t want = (ssize_t)strlen(val);
    ssize_t n = write(fd, val, (size_t)want);
    int err = errno;
    close(fd);
    if (n == want) return 1;
    fprintf(stderr, "sysfs: write \"%s\" to %s -> %zd (%s)\n",
            val, path, n, n < 0 ? strerror(err) : "short write");
    return 0;
}

/* First line of a sysfs file, for the log. */
static void log_sysfs(const char *what, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "%s: %s unreadable\n", what, path); return; }
    char buf[256] = "";
    if (fgets(buf, sizeof buf, f)) buf[strcspn(buf, "\r\n")] = 0;
    fclose(f);
    fprintf(stderr, "%s: %s = \"%s\"\n", what, path, buf);
}

/* Anything but "Discharging" means power is coming in from somewhere. */
static bool is_charging(void) {
    static const char *KNOWN[] = {
        "/sys/class/power_supply/bd71827_bat/status",
        "/sys/class/power_supply/bd71828_bat/status",
        "/sys/class/power_supply/mc13892_bat/status",
        NULL
    };
    char path[256];
    const char *use = NULL;
    for (int i = 0; KNOWN[i] && !use; i++)
        if (access(KNOWN[i], R_OK) == 0) use = KNOWN[i];
    if (!use) {
        DIR *d = opendir("/sys/class/power_supply");
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            snprintf(path, sizeof path,
                     "/sys/class/power_supply/%s/status", e->d_name);
            if (access(path, R_OK) == 0) { use = path; break; }
        }
        if (d) closedir(d);
    }
    if (!use) {
        fprintf(stderr, "sleep: no battery status found; assuming on battery\n");
        return false;
    }
    FILE *f = fopen(use, "r");
    if (!f) return false;
    char st[64] = "";
    if (!fgets(st, sizeof st, f)) st[0] = 0;
    fclose(f);
    st[strcspn(st, "\r\n")] = 0;
    fprintf(stderr, "sleep: %s = \"%s\"\n", use, st);
    return st[0] && strcmp(st, "Discharging") != 0;
}

/* Wait up to `ms`, listening for the power button. Returns 1 if it was
 * pressed -- which, while the device is on its way to sleep, means "no, stay
 * awake".
 *
 * Only the power button. The page keys used to count too, and the first
 * test on battery was cancelled by a key in the two seconds before
 * suspending: the page keys sit under the hand that holds the device, and
 * the power key's driver (bd71828_pwrkey.c) sends one press per push, so a
 * double-fire it was not. Nickel and KOReader listen for power alone here.
 *
 * The waits in the sleep sequence used to be plain nanosleeps. Three attempts
 * with a two-second wait either side came to twelve seconds of the screen
 * looking asleep while nothing read the buttons; every press in that time
 * queued up, and afterwards each one toggled the lock -- awake, locked,
 * another twelve seconds -- which is what "almost impossible to wake" was.
 *
 * Touch is fed to the tracker so its idea of which fingers are down stays
 * right, and otherwise dropped: a hand on the screen is not a decision. */
static int wait_for_key(int ms) {
    long long end = mono_ms() + ms;
    for (;;) {
        long long left = end - mono_ms();
        if (left <= 0) return 0;
        struct pollfd pfd[MAXDEV];
        for (int i = 0; i < s_ndev; i++) { pfd[i].fd = s_dev[i].fd; pfd[i].events = POLLIN; }
        if (poll(pfd, (nfds_t)s_ndev, (int)left) <= 0) continue;
        int pressed = 0;
        for (int i = 0; i < s_ndev; i++) {
            if (!(pfd[i].revents & POLLIN)) continue;
            InputDev &dv = s_dev[i];
            struct input_event ev;
            while (read(dv.fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (dv.is_keys) {
                    if (ev.type == EV_KEY && ev.value == 1) {
                        if (ev.code == KEY_POWER) pressed = 1;
                        else fprintf(stderr, "sleep: key %d while going to sleep;"
                                             " ignored\n", ev.code);
                    }
                    continue;
                }
                dv.tracker.feed(ev);
            }
            dv.npending = dv.drained = 0;
        }
        if (pressed) return 1;
    }
}

/* The longest of the panel's XON timers, in ms, from the device tree.
 *
 * Each time the panel powers down after an update, the e-ink PMIC driver
 * (Kobo's epd_pmic_core.c) arms a timer that switches the XON line off
 * later, and hwtcon_fb_suspend refuses while it is pending ("xon off timer
 * pending", which is what the log named). Its length comes from the device
 * tree: 2.75 s by day on every 7-inch MT8113 board in Kobo's source, 1.65 s
 * for the night-mode one. Read them rather than trust that; fall back to
 * the source's value. */
static int panel_xon_ms(void) {
    static const char *NAMES[] = { "off_xon_0_day_delay_us",
                                   "off_xon_1_delay_us",
                                   "off_xon_0_delay_us" };
    char dir[256] = "";
    FILE *pf = popen("find /proc/device-tree -maxdepth 3 -name off_xon_0_day_delay_us"
                     " 2>/dev/null | head -n 1", "r");
    if (pf) {
        if (fgets(dir, sizeof dir, pf)) {
            dir[strcspn(dir, "\r\n")] = 0;
            char *slash = strrchr(dir, '/');
            if (slash) *slash = 0; else dir[0] = 0;
        }
        pclose(pf);
    }
    int worst = 0, found = 0;
    for (const char *name : NAMES) {
        if (!dir[0]) break;
        char path[320];
        snprintf(path, sizeof path, "%s/%s", dir, name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        unsigned char b[4];
        if (fread(b, 1, 4, f) == 4) {           /* device tree cells are big-endian */
            int ms = (int)((((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) |
                            ((unsigned)b[2] << 8) | b[3]) / 1000u);
            if (ms > worst) worst = ms;
            found = 1;
        }
        fclose(f);
    }
    if (!found || worst <= 0 || worst > 10000) {
        fprintf(stderr, "sleep: panel XON delay not in the device tree; assuming 2750 ms\n");
        return 2750;
    }
    fprintf(stderr, "sleep: panel XON delay from the device tree: %d ms\n", worst);
    return worst;
}

/* The kernel's account of the last suspend, and the time the app got
 * control back -- kept for surf_wake_log, which runs once the screen is
 * back rather than in the way of it. */
static int s_wake_log = 0;
static struct timespec s_woke_at;

static void hms(char *out, size_t n, const struct timespec *t) {
    snprintf(out, n, "%02ld:%02ld:%02ld.%03ld", (long)(t->tv_sec / 3600 % 24),
             (long)(t->tv_sec / 60 % 60), (long)(t->tv_sec % 60), t->tv_nsec / 1000000L);
}

void surf_wake_log(void) {
    if (!s_wake_log) return;
    s_wake_log = 0;
    /* One line. This used to copy the kernel's account of the suspend into
     * the log with dmesg, and that took ten seconds on the device -- ten
     * seconds of the app ignoring the page it had just put back on screen.
     * The log that found it has done its work: the kernel is back within
     * 130 ms of the press. A refused suspend still gets the kernel's side. */
    struct timespec t0;
    clock_gettime(CLOCK_REALTIME, &t0);
    char a[32], b[32];
    hms(a, sizeof a, &s_woke_at);
    hms(b, sizeof b, &t0);
    fprintf(stderr, "sleep: the app had control at %s UTC, the screen was back by %s\n", a, b);
}

/* Throw away whatever input queued up while the device was asleep or on its
 * way back: a page key pressed at a sleeping screen is an attempt to wake
 * it, and the last log had nineteen of them turn nineteen pages the moment
 * the app could see them. */
void surf_flush_input(void) {
    int dropped = 0;
    for (int i = 0; i < s_ndev; i++) {
        struct input_event ev;
        while (read(s_dev[i].fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (!s_dev[i].is_keys) s_dev[i].tracker.feed(ev);   /* keep contacts straight */
            else if (ev.type == EV_KEY && ev.value == 1) dropped++;
        }
        s_dev[i].npending = s_dev[i].drained = 0;
    }
    if (dropped) fprintf(stderr, "input: dropped %d key press(es) made while waking\n", dropped);
}

int surf_charging(void) { return is_charging() ? 1 : 0; }

int surf_suspend(void) {
    /* On MTK -- which the Libra Colour is -- attempting to suspend while
     * plugged in hangs the kernel outright. KOReader refuses the request for
     * exactly this reason, and a device that needs the power button held for
     * thirty seconds is not an acceptable way to find out. */
    /* Say what the kernel offers and what the battery thinks, so a refusal
     * can be read off the log instead of guessed at. */
    log_sysfs("sleep", "/sys/power/state");
    if (is_charging()) {
        fprintf(stderr, "sleep: plugged in; not suspending (it would hang)\n");
        return 0;
    }

    /* Nickel's cycle, as KOReader copies it: flag state-extended, wait, sync,
     * ask for "mem", unflag. Only "mem" -- the "freeze" and "standby"
     * fallbacks this used to try could never work, and made the log lie.
     *
     * Kobo's source for this device (drivers/power/supply/bd71827-power.c)
     * shows why: the battery driver refuses to suspend within one second of
     * its last resume, and a suspend that fails partway *resumes* every
     * device that had already gone down -- that driver included. Each
     * fallback was tried milliseconds after the last one failed, so each hit
     * the one-second guard, and "bd71827 wait resume_jiffies" filled the log
     * while the real reason "mem" failed scrolled out of it.
     *
     * So: "mem", and if it is refused -- the panel still finishing the sleep
     * screen is the usual cause -- wait past the guard and try again. */
    /* The first attempt was refused every time on the device, and the
     * kernel named the display controller: "Device 14000000.hwtcon failed
     * to suspend". It is still busy with the sleep screen just sent to it
     * -- a full-screen GC16 on the colour panel -- and the second attempt,
     * seconds later, went through. So wait for that refresh to finish
     * before asking. Monza's wait is reliable (KOReader relies on it); the
     * retries stay for whatever else says no. */
    if (s_fbfd >= 0) {
        int wr = fbink_wait_for_complete(s_fbfd, LAST_MARKER);
        if (wr < 0) fprintf(stderr, "sleep: waiting for the panel failed (%d)\n", wr);
    }

    /* Waiting for the refresh was not enough: the next test was refused the
     * same way. Kobo's source for the display driver (hwtcon_fb.c,
     * hwtcon_fb_suspend) refuses while its panel power-off timer is still
     * pending, and that timer runs for the driver's power-down delay after
     * the last update -- 500 ms by default, but settable, and whatever Nickel
     * left it at stays. Ask the driver, and give the panel that long plus a
     * margin before asking for "mem". */
    /* And after that power-down, the XON timer (panel_xon_ms): the two run
     * back to back, so the wait is their sum plus a margin. */
    int settle_ms = 2000;
    if (s_state.is_mtk && s_fbfd >= 0) {
        int32_t delay = -2;
        static int xon_ms = -1;
        if (xon_ms < 0) xon_ms = panel_xon_ms();
        if (ioctl(s_fbfd, _IOR('F', 0x31, int32_t), &delay) == 0) {   /* HWTCON_GET_PWRDOWN_DELAY */
            fprintf(stderr, "sleep: the panel powers down %d ms after its last update\n",
                    (int)delay);
        } else {
            delay = 500;                                  /* the driver's default */
            fprintf(stderr, "sleep: could not read the panel's power-down delay (%s)\n",
                    strerror(errno));
        }
        if (delay < 0) delay = 0;          /* "never powers down": nothing to wait for */
        settle_ms = std::max(settle_ms, std::min((int)delay + xon_ms + 750, 10000));
        fprintf(stderr, "sleep: waiting %d ms for the panel before asking\n", settle_ms);
    }

    int ok = 0;
    for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
        if (attempt > 1 && wait_for_key(2000)) {   /* past the one-second guard */
            fprintf(stderr, "sleep: power pressed between attempts; staying awake\n");
            return -1;
        }
        if (!write_sysfs("/sys/power/state-extended", "1")) {
            fprintf(stderr, "sleep: state-extended refused; not suspending\n");
            return 0;
        }
        /* KOReader: "I have traumatic memories of things going awry if we
         * don't sleep between the two writes". So do we, now. */
        if (wait_for_key(settle_ms)) {
            write_sysfs("/sys/power/state-extended", "0");
            fprintf(stderr, "sleep: power pressed while going to sleep; staying awake\n");
            return -1;
        }
        sync();

        /* Again, right before asking: the charger was looked at once, four
         * seconds and maybe two attempts ago, and a cable plugged in since
         * would have "mem" hang the kernel -- the thirty-second power hold. */
        if (is_charging()) {
            write_sysfs("/sys/power/state-extended", "0");
            fprintf(stderr, "sleep: plugged in while going to sleep; not suspending\n");
            return 0;
        }

        fprintf(stderr, "sleep: asking for \"mem\" (attempt %d)\n", attempt);
        /* The watchdog stays off only for the write that sleeps: it blocks
         * for as long as the device is asleep, and an alarm left running
         * would go off the moment it woke. Everything around it -- the
         * waits, dmesg, the next attempt -- stays covered. */
        unsigned left = alarm(0);
        ok = write_sysfs("/sys/power/state", "mem");
        alarm(left ? left : 60);
        /* Execution resumes here, on wake or on refusal. */
        write_sysfs("/sys/power/state-extended", "0");
        if (ok) {
            /* How long the kernel took to bring the devices back: when a wake
             * feels slow, this says whether it was the kernel or us. */
            fprintf(stderr, "sleep: woke\n");
            fflush(stderr);
            /* Nothing else here. Collecting the kernel's account of the
             * suspend used to happen right here, before the app drew
             * anything -- six programs started on a device seconds out of
             * suspend -- and the next log showed the kernel back 130 ms
             * after the press and the app 12 seconds after that. The
             * screen comes first; surf_wake_log() tells the story after. */
            s_wake_log = 1;
            clock_gettime(CLOCK_REALTIME, &s_woke_at);
            break;
        }
        /* What the kernel said about this attempt, and only this attempt:
         * from its "suspend entry" to the end. The driver that refused is
         * named in there. */
        fflush(stderr);
        int rc = system("dmesg 2>/dev/null"
                        " | awk '/PM: suspend entry/{b=\"\"} {b=b $0 \"\\n\"} END{printf \"%s\", b}'"
                        " | grep -E 'fail|error|abort|busy|Busy|refus|wait|returns|wakeup|wake up|PM:|HWTCON|pending|still on'"
                        " | tail -n 40 | sed 's/^/sleep: kernel: /' >&2");
        (void)rc;
    }
    if (!ok) fprintf(stderr, "sleep: refused three times; staying locked\n");
    return ok;
}

/* ---- the reading light -------------------------------------------------- *
 * Suspending the SoC does not turn the frontlight off, so a device "asleep"
 * with the light on is just a device running its battery down behind a blank
 * screen. Nickel turns it off; so must we.
 *
 * KOReader names this node for the Libra Colour (codename monza); the others
 * cover its cousins, and failing all of them any single backlight class
 * device will do. */
static const char *light_path(void) {
    static const char *cached = NULL;
    static bool looked = false;
    if (looked) return cached;
    looked = true;

    static const char *KNOWN[] = {
        "/sys/class/backlight/mxc_msp430.0/brightness",
        "/sys/class/backlight/lm3630a_led/brightness",
        "/sys/class/backlight/backlight_fl/brightness",
        "/sys/class/backlight/tlc5947_bl/brightness",
        NULL
    };
    for (int i = 0; KNOWN[i]; i++)
        if (access(KNOWN[i], R_OK | W_OK) == 0) { cached = KNOWN[i]; break; }

    if (!cached) {                       /* anything in the class will do */
        static char found[256];
        DIR *d = opendir("/sys/class/backlight");
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            snprintf(found, sizeof found,
                     "/sys/class/backlight/%s/brightness", e->d_name);
            if (access(found, R_OK | W_OK) == 0) { cached = found; break; }
        }
        if (d) closedir(d);
    }
    fprintf(stderr, "light: %s\n", cached ? cached : "none found");
    return cached;
}

int surf_light_get(void) {
    const char *p = light_path();
    if (!p) return -1;
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static void light_write(const char *p, int level) {
    FILE *f = fopen(p, "w");
    if (!f) { fprintf(stderr, "light: cannot write %s\n", p); return; }
    fprintf(f, "%d\n", level);
    fclose(f);
}

void surf_light_set(int level) {
    const char *p = light_path();
    if (!p || level < 0) return;
    /* Force a transition. After a resume the driver can believe it is still
     * at the old level while the LED is actually dark, and writing the same
     * number back is then a no-op -- which is what left the light off. */
    int cur = surf_light_get();
    if (cur == level && level != 0) light_write(p, 0);
    light_write(p, level);
    fprintf(stderr, "light: set %d (was %d)\n", level, cur);
}

/* ---- present ----------------------------------------------------------- */

/* The buffer rectangle r onto the panel; returns the panel rectangle it
 * wrote, which is the one to refresh (empty: nothing to do).
 *
 * Clipped twice: to the buffer, so a rectangle drawn past its edge (ink
 * dragged off the page) is never a read past the end of gfx_fb, and to the
 * panel, so nothing is written past the mapping. The two used to be the
 * same size and only the panel was checked; the flip view's buffer is the
 * panel turned, and a panel that is not the size the app opened at (it
 * warns) is no longer a read out of bounds either. */
static rect_t blit(rect_t r) {
    r = rot_clip(r, gfx_w, gfx_h);
    if (r.w <= 0 || r.h <= 0) return r;
    /* the rotation is of the buffer as it is: pw x ph its portrait size */
    int pw, ph, q = panel_q();
    rot_buf_size(q, gfx_w, gfx_h, &pw, &ph);
    rect_t pr = rot_clip(rot_rect_to_panel(q, pw, ph, r), s_w, s_h);
    if (pr.w <= 0 || pr.h <= 0) return pr;

    static std::vector<uint8_t> row;      /* one turned panel row */
    if (q && (int)row.size() < pr.w) row.resize((size_t)pr.w);
    for (int y = 0; y < pr.h; y++) {
        int py = pr.y + y;
        unsigned char *dst = s_fb + (size_t)py * s_stride;
        const uint8_t *src;
        if (!q) {
            src = &gfx_fb[(size_t)py * gfx_w + pr.x];
        } else if (s_bpp == 8) {          /* straight into the panel's row */
            rot_row(q, pw, ph, gfx_fb, gfx_w, pr.x, py, pr.w, dst + pr.x);
            continue;
        } else {
            rot_row(q, pw, ph, gfx_fb, gfx_w, pr.x, py, pr.w, row.data());
            src = row.data();
        }
        if (s_bpp == 8) {
            memcpy(dst + pr.x, src, (size_t)pr.w);
        } else if (s_bpp == 16) {
            uint16_t *d16 = (uint16_t *)(dst) + pr.x;
            for (int x = 0; x < pr.w; x++) {
                uint8_t v = src[x];
                d16[x] = (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
            }
        } else {                      /* 32bpp BGRA */
            uint8_t *d32 = dst + (size_t)pr.x * 4;
            for (int x = 0; x < pr.w; x++) {
                uint8_t v = src[x];
                d32[x*4+0] = v; d32[x*4+1] = v; d32[x*4+2] = v; d32[x*4+3] = 0xFF;
            }
        }
    }
    return pr;
}

/* Waveform choice is what decides whether the app feels pleasant.
 *   ink      DU4 is asked for (A2 may not be honoured here, see below)
 *   ordinary DU4 is asked for; FBInk has no DU4 on the MediaTek Kobos and
 *            uses GL16, which does not flash. REAGL and dotted grey lines
 *            were tried in its place and changed nothing: the flash was the
 *            periodic GC16, now gone (see redraw in app.cpp).
 *   full     GC16 flashes the panel black first; only the sleep screen
 *            and the blank on exit use it
 * Override without rebuilding: HANZI_WFM_PART=du4|du|gl16|gc16fast|gc16 */
/* The waveform used for ink while the pen is moving. A2 is the fastest but is
 * specified as B&W-to-B&W, and on the MTK/HWTCON path a partial A2 update may
 * simply not be honoured -- which looks exactly like the app ignoring the pen.
 * Override with HANZI_WFM_INK=a2|du|du4|gl16|gc16fast. */
static int ink_wfm(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    const char *v = getenv("HANZI_WFM_INK");
    if      (v && !strcmp(v, "a2"))       cached = WFM_A2;
    else if (v && !strcmp(v, "du"))       cached = WFM_DU;
    else if (v && !strcmp(v, "gl16"))     cached = WFM_GL16;
    else if (v && !strcmp(v, "gc16fast")) cached = WFM_GC16_FAST;
    else                                  cached = WFM_DU4;   /* never flashes */
    return cached;
}

static int part_wfm(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    const char *v = getenv("HANZI_WFM_PART");
    if      (v && !strcmp(v, "du"))       cached = WFM_DU;
    else if (v && !strcmp(v, "gl16"))     cached = WFM_GL16;
    else if (v && !strcmp(v, "gc16fast")) cached = WFM_GC16_FAST;
    else if (v && !strcmp(v, "gc16"))     cached = WFM_GC16;
    else                                  cached = WFM_DU4;
    return cached;
}

void surf_present(rect_t r, int mode) {
    r = blit(r);                      /* now the panel's rectangle */
    if (r.w <= 0 || r.h <= 0) return;
    FBInkConfig cfg = s_cfg;
    switch (mode) {
    case RF_FAST: cfg.wfm_mode = (WFM_MODE_INDEX_T)ink_wfm(); cfg.no_refresh = false; break;
    case RF_PART: cfg.wfm_mode = (WFM_MODE_INDEX_T)part_wfm(); cfg.no_refresh = false; break;
    default:      cfg.wfm_mode = WFM_GC16; cfg.no_refresh = false; break;
    }
    int rc = fbink_refresh(s_fbfd, (uint32_t)r.y, (uint32_t)r.x,
                           (uint32_t)r.w, (uint32_t)r.h, &cfg);
    if (rc < 0) {
        static int complained = 0;
        if (!complained++)
            fprintf(stderr, "fb: refresh rejected (rc=%d wfm=%d rect=%d,%d %dx%d)\n",
                    rc, (int)cfg.wfm_mode, r.x, r.y, r.w, r.h);
    }
}


static bool has_bit(const unsigned long *arr, int bit) {
    return (arr[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
}

/* Open every usable input node. Three kinds matter:
 *   - the panel, which carries position and, on this device, the stylus too
 *   - a key-only node carrying KEY_POWER (event0 on Kobo)
 * An accelerometer also reports ABS_X/ABS_Y, so a pointer must additionally
 * report a contact button or multitouch slots. Every node is logged, accepted
 * or not, because working out why one was skipped from a device with no shell
 * is otherwise guesswork. */
static void scan_inputs(void) {
    DIR *d = opendir("/dev/input");
    if (!d) { fprintf(stderr, "input: cannot open /dev/input\n"); return; }
    struct dirent *e;
    while ((e = readdir(d)) && s_ndev < MAXDEV) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) { fprintf(stderr, "input: %s cannot open\n", path); continue; }

        unsigned long keybits[(KEY_MAX + 8*sizeof(long)) / (8*sizeof(long))] = {};
        unsigned long absbits[(ABS_MAX + 8*sizeof(long)) / (8*sizeof(long))] = {};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits);
        char name[128] = "?";
        ioctl(fd, EVIOCGNAME(sizeof name), name);

        bool has_xy    = has_bit(absbits, ABS_X) && has_bit(absbits, ABS_Y);
        bool has_mt    = has_bit(absbits, ABS_MT_POSITION_X);
        bool is_pen    = has_bit(keybits, BTN_TOOL_PEN) || has_bit(keybits, BTN_STYLUS);
        bool is_touch  = has_bit(keybits, BTN_TOUCH) || has_bit(keybits, BTN_TOOL_FINGER);
        bool has_power = has_bit(keybits, KEY_POWER);
        /* The Libra's page-turn buttons report these two; KOReader maps the
         * same codes (193 back, 194 forward) for every Kobo that has keys. */
        bool has_pages = has_bit(keybits, KEY_F23) || has_bit(keybits, KEY_F24);
        if (has_pages) g_has_pagekeys = 1;
        bool pointer   = (has_xy || has_mt) && (is_touch || is_pen || has_mt);

        if (pointer) {
            InputDev &dv = s_dev[s_ndev];
            dv = InputDev{};
            dv.fd = fd;
            dv.is_pen = is_pen;
            struct input_absinfo ai;
            int ax = has_mt ? ABS_MT_POSITION_X : ABS_X;
            int ay = has_mt ? ABS_MT_POSITION_Y : ABS_Y;
            if (ioctl(fd, EVIOCGABS(ax), &ai) == 0) { dv.minx = ai.minimum; dv.maxx = ai.maximum; }
            if (ioctl(fd, EVIOCGABS(ay), &ai) == 0) { dv.miny = ai.minimum; dv.maxy = ai.maximum; }
            if (dv.maxx <= dv.minx) dv.maxx = dv.minx + 1;
            if (dv.maxy <= dv.miny) dv.maxy = dv.miny + 1;

            long rx = (long)dv.maxx - dv.minx, ry = (long)dv.maxy - dv.miny;
            if (rx < 16 || rx > 65535 || ry < 16 || ry > 65535) {
                fprintf(stderr, "input: %s  %-26s skipped, axes %ld x %ld\n",
                        path, name, rx, ry);
                close(fd);
                continue;
            }
            dv.swap = envflag("HANZI_PEN_SWAP",    s_state.touch_swap_axes) != 0;
            dv.mirx = envflag("HANZI_PEN_MIRRORX", s_state.touch_mirror_x) != 0;
            dv.miry = envflag("HANZI_PEN_MIRRORY", s_state.touch_mirror_y) != 0;
            fprintf(stderr, "input: %s  %-26s %s  x[%d..%d] y[%d..%d] swap=%d mx=%d my=%d\n",
                    path, name, is_pen ? "panel+pen" : "panel",
                    dv.minx, dv.maxx, dv.miny, dv.maxy,
                    (int)dv.swap, (int)dv.mirx, (int)dv.miry);
            s_ndev++;
            continue;
        }
        if (has_power || has_pages) {
            InputDev &dv = s_dev[s_ndev];
            dv = InputDev{};
            dv.fd = fd;
            dv.is_power = has_power;
            dv.is_keys  = true;
            fprintf(stderr, "input: %s  %-26s keys%s%s\n", path, name,
                    has_power ? " POWER" : "", has_pages ? " PAGE" : "");
            s_ndev++;
            continue;
        }
        fprintf(stderr, "input: %s  %-26s ignored (xy=%d mt=%d touch=%d pen=%d "
                        "power=%d pages=%d)\n",
                path, name, (int)has_xy, (int)has_mt, (int)is_touch, (int)is_pen,
                (int)has_power, (int)has_pages);
        close(fd);
    }
    closedir(d);
    if (s_ndev == 0) fprintf(stderr, "input: no usable devices found\n");
}

int surf_has_pagekeys(void) { return g_has_pagekeys; }

/* ---- rotation, window modes, --diag ------------------------------------ */

/* Any panel can show the flip view: it is done in software. */
int surf_can_rotate(void) { return 1; }
int surf_rotation(void)   { return s_rot; }

int surf_set_rotation(int q) {
    if (q < 0 || q > 3) return -1;
    int bw, bh, nw, nh;
    rot_buf_size(s_rot, gfx_w, gfx_h, &bw, &bh);   /* the buffer as opened */
    rot_buf_size(q, bw, bh, &nw, &nh);
    if (gfx_init(nw, nh) != 0) {
        fprintf(stderr, "fb: rotation %d: no memory for a %dx%d buffer\n", q, nw, nh);
        return -1;
    }
    if (q != s_rot) fprintf(stderr, "fb: rotation %d -> %d, buffer %dx%d\n", s_rot, q, nw, nh);
    s_rot = q;
    return 0;
}

/* The Kobo is all screen: there are no windows to arrange. */
int surf_set_window_mode(int mode) { (void)mode; return -1; }
int surf_window_mode(void) { return -1; }

void surf_diag(void) {
    int fd = fbink_open();
    if (fd < 0) { printf("framebuffer: cannot open (fbink_open failed)\n"); return; }
    FBInkConfig cfg = {};
    cfg.is_quiet = true;
    if (fbink_init(fd, &cfg) < 0) {
        printf("framebuffer: fbink_init failed\n");
        fbink_close(fd);
        return;
    }
    FBInkState st = {};
    fbink_get_state(&cfg, &st);
    printf("framebuffer: %s (%s) %ux%u %ubpp stride=%u mtk=%d\n",
           st.device_name, st.device_codename, (unsigned)st.screen_width,
           (unsigned)st.screen_height, (unsigned)st.bpp, (unsigned)st.scanline_stride,
           (int)st.is_mtk);
    printf("  rota=%u boot_rota=%u can_rotate=%d touch swap=%d mirror x=%d y=%d\n",
           (unsigned)st.current_rota, (unsigned)st.ntx_boot_rota, (int)st.can_rotate,
           (int)st.touch_swap_axes, (int)st.touch_mirror_x, (int)st.touch_mirror_y);
    fbink_close(fd);
    DIR *d = opendir("/dev/input");
    int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) n += strncmp(e->d_name, "event", 5) == 0;
    if (d) closedir(d);
    printf("input: %d event node(s) in /dev/input\n", n);
}

int surf_has_pen(void) {
    for (int i = 0; i < s_ndev; i++) if (s_dev[i].is_pen) return 1;
    return 0;
}

/* True only while the stylus is actually in use: palm rejection is about a
 * hand resting during writing, not about a pen having once been touched. */
static constexpr long long PEN_ACTIVE_MS = 1500;

int surf_pen_seen(void) {
    long long now = mono_ms();
    for (int i = 0; i < s_ndev; i++)
        if (s_dev[i].pen_ms && now - s_dev[i].pen_ms < PEN_ACTIVE_MS) return 1;
    return 0;
}

static void map_xy(const InputDev &dv, int rx, int ry, int *ox, int *oy) {
    int minx = dv.minx, maxx = dv.maxx, miny = dv.miny, maxy = dv.maxy;
    if (dv.swap) {
        std::swap(rx, ry);
        std::swap(minx, miny);
        std::swap(maxx, maxy);
    }
    long dx = (long)maxx - minx, dy = (long)maxy - miny;
    if (dx <= 0) dx = 1;
    if (dy <= 0) dy = 1;
    int x = (int)(((long)rx - minx) * (s_w - 1) / dx);
    int y = (int)(((long)ry - miny) * (s_h - 1) / dy);
    if (dv.mirx) x = s_w - 1 - x;
    if (dv.miry) y = s_h - 1 - y;
    *ox = std::clamp(x, 0, s_w - 1);
    *oy = std::clamp(y, 0, s_h - 1);
}

static void queue(InputDev &dv, int type, int rx, int ry, bool pen) {
    if (dv.npending >= (int)(sizeof dv.pending / sizeof *dv.pending)) return;
    ev_t &e = dv.pending[dv.npending++];
    e.type = type;
    e.pen  = pen ? 1 : 0;
    e.key  = 0;
    map_xy(dv, rx, ry, &e.x, &e.y);
    if (int q = panel_q()) {          /* the panel's point, back into the turned buffer */
        int pw, ph, bx, by;
        rot_buf_size(q, gfx_w, gfx_h, &pw, &ph);
        rot_to_buf(q, pw, ph, e.x, e.y, &bx, &by);
        e.x = std::clamp(bx, 0, gfx_w - 1);
        e.y = std::clamp(by, 0, gfx_h - 1);
    }
}

int surf_poll(ev_t *e, int wait_ms) {
    /* Anything decoded from the previous frame comes out first. */
    for (int i = 0; i < s_ndev; i++) {
        InputDev &dv = s_dev[i];
        if (dv.drained < dv.npending) {
            *e = dv.pending[dv.drained++];
            if (s_dbg && e->type != EV_MOVE)
                fprintf(stderr, "ev: %s %d,%d pen=%d\n",
                        e->type == EV_DOWN ? "DOWN" : "UP  ", e->x, e->y, e->pen);
            return 1;
        }
        dv.drained = dv.npending = 0;
    }

    struct pollfd pfd[MAXDEV];
    for (int i = 0; i < s_ndev; i++) { pfd[i].fd = s_dev[i].fd; pfd[i].events = POLLIN; }
    if (poll(pfd, (nfds_t)s_ndev, wait_ms) <= 0) return 0;

    for (int i = 0; i < s_ndev; i++) {
        if (!(pfd[i].revents & POLLIN)) continue;
        InputDev &dv = s_dev[i];
        struct input_event ev;
        while (read(dv.fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (dv.is_keys) {
                if (ev.type == EV_KEY && ev.value == 1) {
                    if (ev.code == KEY_POWER) {
                        e->type = EV_POWER; e->x = e->y = 0; e->pen = 0;
                        /* When the press happened, by the kernel's clock, and
                         * when it reached us: a press that sat queued while
                         * the device was still resuming shows up as a gap. */
                        struct timespec now;
                        clock_gettime(CLOCK_REALTIME, &now);
                        long lag = (now.tv_sec - ev.input_event_sec) * 1000L
                                 + (now.tv_nsec / 1000000L - ev.input_event_usec / 1000L);
                        fprintf(stderr, "ev: POWER pressed %02ld:%02ld:%02ld.%03ld,"
                                        " read %ld ms later\n",
                                (long)(ev.input_event_sec / 3600 % 24), (long)(ev.input_event_sec / 60 % 60),
                                (long)(ev.input_event_sec % 60), (long)(ev.input_event_usec / 1000), lag);
                        return 1;
                    }
                    /* Which button is "forward" depends on how the device is
                     * held, so it is overridable without a rebuild. The
                     * default here is upper = next, lower = previous;
                     * HANZI_PAGE_SWAP=1 gives Kobo's own default back.
                     * Upside down the buttons trade places, so they trade
                     * meanings too: upper stays next, as Nickel does. That
                     * is the picture's turn on the glass (panel_q), so it
                     * holds whether Nickel was upside down or the flip view
                     * turned it; both together are upright again. */
                    if (ev.code == KEY_F23 || ev.code == KEY_F24) {
                        int swap = (envflag("HANZI_PAGE_SWAP", 0) != 0) != (panel_q() == 2);
                        int fwd = swap ? KEY_F24 : KEY_F23;
                        e->type = EV_PAGE;
                        e->key  = (ev.code == fwd) ? 1 : -1;
                        e->x = e->y = 0; e->pen = 0;
                        fprintf(stderr, "ev: PAGE %s (code %d)\n",
                                e->key > 0 ? "fwd" : "back", ev.code);
                        return 1;
                    }
                }
                continue;
            }
            int n = dv.tracker.feed(ev);
            for (int k = 0; k < n; k++) {
                const mt::Out &o = dv.tracker.out(k);
                if (o.pen) dv.pen_ms = mono_ms();
                switch (o.action) {
                case mt::Action::Down: queue(dv, EV_DOWN, o.x, o.y, o.pen); break;
                case mt::Action::Move: queue(dv, EV_MOVE, o.x, o.y, o.pen); break;
                case mt::Action::Up:   queue(dv, EV_UP,   o.x, o.y, o.pen); break;
                default: break;
                }
            }
            if (dv.npending) {
                dv.drained = 1;
                *e = dv.pending[0];
                if (s_dbg && e->type != EV_MOVE)
                    fprintf(stderr, "ev: %s %d,%d pen=%d\n",
                            e->type == EV_DOWN ? "DOWN" : "UP  ", e->x, e->y, e->pen);
                return 1;
            }
        }
    }
    return 0;
}
