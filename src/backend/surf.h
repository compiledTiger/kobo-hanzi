/* surf.h -- platform surface: present pixels, read input.
 * Two implementations: surf_sdl.c (desktop) and surf_fb.c (Kobo). */
#ifndef SURF_H
#define SURF_H
#include "../core/gfx.h"

enum { RF_FULL, RF_PART, RF_FAST };   /* GC16-ish, DU/GL16-ish, A2-ish */
enum { EV_NONE, EV_DOWN, EV_MOVE, EV_UP, EV_KEY, EV_QUIT, EV_POWER, EV_PAGE };

typedef struct {
    int type;
    int x, y;
    int pen;      /* 1 = stylus, 0 = finger/mouse */
    int key;      /* EV_KEY: ASCII, or 8 backspace, 13 enter, 27 escape, or
                   * one of the K_ codes below
                   * EV_PAGE: -1 for the back button, +1 for the forward one */
} ev_t;

/* EV_KEY codes past ASCII. Only the desktop sends EV_KEY at all: the Kobo
 * has no keyboard, only its page buttons (EV_PAGE). */
enum { K_TAB = 9, K_UP = 0x101, K_DOWN, K_BACKTAB };

/* Fill in the real panel size if the platform knows it (Kobo); -1 otherwise. */
int  surf_probe(int *w, int *h);
/* 1 when a stylus digitiser was found. Clara Colour has none, so the write
 * card falls back to "picture it, then check". */
int  surf_has_pen(void);
/* Whether this device has the page-turn buttons. The interface marks which
 * of them presses which grade, and a mark for a key that is not there is
 * just clutter -- the Clara has none. */
int  surf_has_pagekeys(void);
/* 1 once a stylus has actually been in contact. */
int  surf_pen_seen(void);
/* Re-establish the framebuffer after the device has slept. Returns 1 if the
 * state actually changed and the caller should redraw everything. */
int  surf_reinit(void);
/* Put the device to sleep, blocking until it wakes. Returns 0 if the platform
 * cannot suspend, in which case the caller should stay on its sleep screen. */
/* 1: slept and woke. 0: the kernel refused, the caller stays locked.
 * -1: the power button was pressed on the way down, so it never slept. */
int  surf_suspend(void);
/* Whether the device is on a charger (and so must not suspend). 0 on the desktop. */
int  surf_charging(void);
/* After a wake, once the screen is back: log the kernel's account of the
 * suspend. */
void surf_wake_log(void);
/* Discard input that queued while asleep or waking. */
void surf_flush_input(void);
/* The reading light. Returns the current level, or -1 if there is no light or
 * it cannot be read; surf_light_set restores what was read. Blanking it is
 * the difference between a sleeping device and a flat battery by morning. */
int  surf_light_get(void);
void surf_light_set(int level);
int  surf_open(int w, int h, const char *title);
void surf_close(void);
void surf_present(rect_t r, int mode);
int  surf_poll(ev_t *e, int wait_ms);   /* 1 if an event was filled in */
const char *surf_name(void);

/* ---- rotation (the Kobo's landscape "flip" view) ------------------------
 * Done in software: the panel stays portrait, the app draws into a buffer of
 * swapped size, the backend turns it on the way to the panel, turns the
 * refresh rectangles with it and maps touch back (src/backend/rot.h has the
 * arithmetic, tests/rot_test.cpp holds it).
 *
 * surf_set_rotation(q): q quarter turns clockwise, 0 (portrait, as opened)
 * or 1; 3 is the other way round. On success it has re-made gfx_fb at the
 * new size (gfx_w x gfx_h swapped for an odd q, cleared white) and returns
 * 0; the caller must take its sizes from gfx_w/gfx_h again and redraw the
 * whole screen. -1: refused, nothing changed. Works headless too (--shot),
 * where only the buffer changes.
 * surf_can_rotate: whether the app should offer it. 1 on the Kobo; 0 on the
 * desktop, which has a real window (there the half view grows instead; a
 * rotation asked for anyway is shown as is, landscape). */
int  surf_can_rotate(void);
int  surf_set_rotation(int quarter_turns);
int  surf_rotation(void);

/* ---- window modes (desktop) ---------------------------------------------
 *   WIN_WINDOW  an ordinary window sized to the screen, the page drawn
 *               pixel for pixel at scale 1 (as it always was)
 *   WIN_FULL    borderless at the desktop's size, the page scaled to fit
 *               with its aspect kept, grey bars either side
 *   WIN_LEFT/WIN_RIGHT  the left or right half of the usable desktop, full
 *               height, scaled the same way. Placing a window needs X11
 *               (XWayland included); under native Wayland it is sized but
 *               the compositor places it, and the log says so.
 * surf_set_window_mode may be called before surf_open (the --window flag)
 * and at any time after (settings). Returns 0 when done as asked, 1 when
 * only partly (not placed: Wayland), -1 where there are no windows (Kobo)
 * or the mode is unknown. surf_window_mode returns the mode last asked for,
 * for the app to persist, or -1 on the Kobo.
 * A press in the bars is dropped; a drag that wanders into them is held
 * at the page's edge. */
enum { WIN_WINDOW = 0, WIN_FULL, WIN_LEFT, WIN_RIGHT, WIN_NMODES };
int  surf_set_window_mode(int mode);
int  surf_window_mode(void);
/* "window", "full", "left", "right" <-> WIN_*; -1 / NULL for anything else */
static inline int surf_window_mode_parse(const char *s) {
    static const char *const n[] = { "window", "full", "left", "right" };
    for (int i = 0; s && i < WIN_NMODES; i++) {
        const char *a = s, *b = n[i];
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}
static inline const char *surf_window_mode_name(int m) {
    static const char *const n[] = { "window", "full", "left", "right" };
    return m >= 0 && m < WIN_NMODES ? n[m] : (const char *)0;
}

/* --diag: print what the backend can see of the machine -- the display
 * libraries and video drivers on the desktop, the panel on the Kobo. */
void surf_diag(void);

#endif
