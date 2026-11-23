/* Desktop backend. The mouse stands in for the stylus so the whole app can be
 * driven on a laptop; refresh modes are accepted and ignored.
 *
 * The page is always the device's buffer, scaled by SDL to the window
 * (SDL_RenderSetLogicalSize), aspect kept, grey bars where the shapes
 * differ. The window itself is one of four modes (surf.h, WIN_*): an
 * ordinary window, borderless full screen, or the left or right half of the
 * screen -- by --window, by settings, and F11 for full screen and back. */
#include "surf.h"
#include "rot.h"
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <link.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static SDL_Window   *s_win = NULL;
static SDL_Renderer *s_ren = NULL;
static SDL_Texture  *s_tex = NULL;
static int s_w, s_h, s_down = 0;     /* s_w x s_h: the texture's size, the buffer's */
static uint8_t *s_rgb = NULL;
static int s_win_w = 0, s_win_h = 0; /* WIN_WINDOW's size, worked out at open */
static int s_mode = WIN_WINDOW;      /* as last asked for */
static int s_unfull = WIN_WINDOW;    /* what F11 goes back to */
static int s_rot = 0;
static const Uint8 BAR = 0x50;       /* the letterbox: grey, not SDL's black */

const char *surf_name(void) { return "sdl"; }

int surf_probe(int *w, int *h) { (void)w; (void)h; return -1; }
/* The desktop stands in for the Libra, which has them -- except at the
 * Clara's size (--clara), where it stands in for the Clara, which has none:
 * its renders showed marks for keys it does not have. (The size as opened:
 * a rotated buffer is still the same device.) */
int surf_has_pagekeys(void) {
    int bw, bh;
    rot_buf_size(s_rot, gfx_w, gfx_h, &bw, &bh);
    return !(bw == 1072 && bh == 1448);
}

int surf_has_pen(void)  { return 1; }   /* the mouse stands in for the pen */
int surf_pen_seen(void) { return 1; }
int surf_reinit(void)   { return 0; }
int surf_suspend(void)  { return 0; }   /* no sleep on the desktop */
int surf_charging(void) { return 0; }
void surf_wake_log(void) {}
void surf_flush_input(void) {}
int  surf_light_get(void)      { return -1; }
void surf_light_set(int level) { (void)level; }

/* The streaming texture and its RGB copy at the buffer's size, and the
 * logical size that scales and letterboxes it. */
static int make_texture(int w, int h) {
    if (s_tex) SDL_DestroyTexture(s_tex);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
    free(s_rgb);
    s_rgb = (uint8_t *)malloc((size_t)w * h * 3);
    if (!s_tex || !s_rgb) {
        fprintf(stderr, "sdl: texture %dx%d: %s\n", w, h, s_tex ? "out of memory" : SDL_GetError());
        return -1;
    }
    s_w = w; s_h = h;
    SDL_RenderSetLogicalSize(s_ren, w, h);
    return 0;
}

static void show(void) {
    SDL_SetRenderDrawColor(s_ren, BAR, BAR, BAR, 255);
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
}

static int native_wayland(void) {
    const char *d = SDL_GetCurrentVideoDriver();
    return d && !strcmp(d, "wayland");
}

/* Put the open window into mode m. 0 done, 1 done but not placed. */
static int apply_mode(int m) {
    int disp = SDL_GetWindowDisplayIndex(s_win);
    if (disp < 0) disp = 0;
    if (m == WIN_FULL) {
        if (SDL_SetWindowFullscreen(s_win, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            fprintf(stderr, "sdl: full screen refused: %s\n", SDL_GetError());
            return 1;
        }
        return 0;
    }
    SDL_SetWindowFullscreen(s_win, 0);
    SDL_RestoreWindow(s_win);             /* a maximised window ignores a size */
    SDL_SetWindowBordered(s_win, SDL_TRUE);
    if (m == WIN_WINDOW) {
        SDL_SetWindowSize(s_win, s_win_w, s_win_h);
        SDL_SetWindowPosition(s_win, SDL_WINDOWPOS_CENTERED_DISPLAY(disp),
                              SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
        return 0;
    }
    /* a half: the usable area (no panels or docks), less the frame the
     * window manager adds, which X11 learns only once the window is mapped */
    SDL_Rect ub;
    if (SDL_GetDisplayUsableBounds(disp, &ub) != 0 &&
        SDL_GetDisplayBounds(disp, &ub) != 0) {
        fprintf(stderr, "sdl: no display bounds: %s\n", SDL_GetError());
        return 1;
    }
    int t = 0, l = 0, b = 0, r = 0;
    for (int tries = 0; tries < 10; tries++) {
        if (SDL_GetWindowBordersSize(s_win, &t, &l, &b, &r) != 0) { t = l = b = r = 0; break; }
        if (t > 0 || native_wayland()) break;
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    int half = ub.w / 2;
    int x = ub.x + (m == WIN_RIGHT ? ub.w - half : 0);
    int w = half - l - r, h = ub.h - t - b;
    if (w < 100) w = 100;
    if (h < 100) h = 100;
    SDL_SetWindowSize(s_win, w, h);
    if (native_wayland()) {
        fprintf(stderr, "sdl: native Wayland cannot place a window: sized to half the screen,"
                        " but the compositor decides where it goes (SDL_VIDEODRIVER=x11 runs it"
                        " through XWayland, which can)\n");
        return 1;
    }
    SDL_SetWindowPosition(s_win, x + l, ub.y + t);
    return 0;
}

int surf_set_window_mode(int m) {
    if (m < 0 || m >= WIN_NMODES) return -1;
    if (m == WIN_FULL && s_mode != WIN_FULL) s_unfull = s_mode;
    s_mode = m;
    if (!s_win) return 0;                 /* applied by surf_open */
    int rc = apply_mode(m);
    fprintf(stderr, "sdl: window mode %s%s\n", surf_window_mode_name(m),
            rc ? " (in part)" : "");
    if (s_tex) show();
    return rc;
}

int surf_window_mode(void) { return s_mode; }

int surf_can_rotate(void) { return 0; }   /* Linux: the half view grows instead */
int surf_rotation(void)   { return s_rot; }

/* Not offered here, but it works: the buffer is re-made at the turned size
 * and shown as it is, landscape, letterboxed in the window. Headless (the
 * harness) only the buffer changes. */
int surf_set_rotation(int q) {
    if (q < 0 || q > 3) return -1;
    int bw, bh, nw, nh;
    rot_buf_size(s_rot, gfx_w, gfx_h, &bw, &bh);   /* the size as opened */
    rot_buf_size(q, bw, bh, &nw, &nh);
    if (gfx_init(nw, nh) != 0) return -1;
    s_rot = q;
    if (s_ren && make_texture(nw, nh) != 0) return -1;
    return 0;
}

/* SDL's stand-ins for a display: they always come up, so when X11 and
 * Wayland both fail (no libX11, no libwayland-client, ssh without X
 * forwarding, Wayland without XWayland) SDL_Init succeeds on one of them
 * and the app would run with no window at all. A headless driver counts
 * only when SDL_VIDEODRIVER asked for it by name. */
static int headless_driver(const char *d) {
    return d && (!strcmp(d, "offscreen") || !strcmp(d, "dummy") || !strcmp(d, "evdev"));
}
static int driver_asked(const char *d) {
    const char *v = getenv("SDL_VIDEODRIVER");
    size_t n = strlen(d);
    while (v && *v) {                     /* a comma-separated list */
        const char *e = strchr(v, ',');
        size_t l = e ? (size_t)(e - v) : strlen(v);
        if (l == n && !strncasecmp(v, d, n)) return 1;
        v = e ? e + 1 : NULL;
    }
    return 0;
}

int surf_open(int w, int h, const char *title) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        fprintf(stderr, "sdl: no display could be opened -- run with --diag to see which"
                        " display libraries and video drivers were found\n");
        return -1;
    }
    const char *drv = SDL_GetCurrentVideoDriver();
    if (headless_driver(drv) && !driver_asked(drv)) {
        fprintf(stderr, "sdl: no display could be opened: SDL fell back to its '%s' driver,"
                        " which would run the app with no window -- run with --diag to see"
                        " which display libraries and video drivers were found\n", drv);
        SDL_Quit();
        return -1;
    }
    s_w = w; s_h = h;
    /* The window fits the screen it opens on -- the Libra's page is 1680
     * pixels tall, more than most monitors -- and can be resized; what is
     * drawn stays the device's page, scaled. HANZI_SCALE sets it. */
    float sc = 0.5f;
    SDL_Rect ub;
    if (SDL_GetDisplayUsableBounds(0, &ub) == 0 && ub.h > 200) {
        sc = (float)(ub.h - 60) / (float)h;
        float sw = (float)(ub.w - 40) / (float)w;       /* a portrait monitor */
        if (sw < sc) sc = sw;
    }
    if (sc > 1.0f) sc = 1.0f;
    const char *e = getenv("HANZI_SCALE");
    if (e) sc = (float)atof(e);
    if (sc <= 0.05f) sc = 0.5f;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");   /* smooth text when scaled down */

    s_win_w = (int)(w * sc); s_win_h = (int)(h * sc);
    s_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             s_win_w, s_win_h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!s_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return -1; }
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
    if (!s_ren) s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    if (!s_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return -1; }
    SDL_RendererInfo ri;
    fprintf(stderr, "sdl: video %s, renderer %s, window %dx%d (scale %.2f)\n",
            SDL_GetCurrentVideoDriver(), SDL_GetRendererInfo(s_ren, &ri) == 0 ? ri.name : "?",
            s_win_w, s_win_h, sc);
    if (make_texture(w, h) != 0) return -1;
    if (s_mode != WIN_WINDOW) {
        int rc = apply_mode(s_mode);
        fprintf(stderr, "sdl: window mode %s%s\n", surf_window_mode_name(s_mode),
                rc ? " (in part)" : "");
    }
    return 0;
}

void surf_close(void) {
    free(s_rgb); s_rgb = NULL;
    if (s_tex) SDL_DestroyTexture(s_tex);
    if (s_ren) SDL_DestroyRenderer(s_ren);
    if (s_win) SDL_DestroyWindow(s_win);
    s_tex = NULL; s_ren = NULL; s_win = NULL;
    SDL_Quit();
}

void surf_present(rect_t r, int mode) {
    (void)r; (void)mode;
    if (!s_ren) return;
    /* the buffer was re-made at another size (a rotation, a re-init) */
    if ((gfx_w != s_w || gfx_h != s_h) && make_texture(gfx_w, gfx_h) != 0) return;
    for (size_t i = 0, n = (size_t)s_w * s_h; i < n; i++) {
        uint8_t v = gfx_fb[i];
        s_rgb[i*3] = s_rgb[i*3+1] = s_rgb[i*3+2] = v;
    }
    SDL_UpdateTexture(s_tex, NULL, s_rgb, s_w * 3);
    show();
}

static int on_page(int x, int y) { return x >= 0 && y >= 0 && x < s_w && y < s_h; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int surf_poll(ev_t *e, int wait_ms) {
    SDL_Event ev;
    int got = wait_ms > 0 ? SDL_WaitEventTimeout(&ev, wait_ms) : SDL_PollEvent(&ev);
    if (!got) return 0;
    e->pen = 1; e->key = 0;
    switch (ev.type) {
    case SDL_QUIT:
        e->type = EV_QUIT; return 1;
    /* SDL gives the page's coordinates, but unclamped: a click in a bar is
     * off the page. A press there is nothing; a stroke that strays into one
     * is held at the page's edge, and its lift ends it there. */
    case SDL_MOUSEBUTTONDOWN:
        if (!on_page(ev.button.x, ev.button.y)) return 0;
        s_down = 1; e->type = EV_DOWN; e->x = ev.button.x; e->y = ev.button.y; return 1;
    case SDL_MOUSEBUTTONUP:
        if (!s_down) return 0;
        s_down = 0; e->type = EV_UP;
        e->x = clampi(ev.button.x, 0, s_w - 1); e->y = clampi(ev.button.y, 0, s_h - 1);
        return 1;
    case SDL_MOUSEMOTION:
        if (!s_down) return 0;
        e->type = EV_MOVE;
        e->x = clampi(ev.motion.x, 0, s_w - 1); e->y = clampi(ev.motion.y, 0, s_h - 1);
        return 1;
    case SDL_TEXTINPUT: {
        /* typed text, ASCII only: search is by pinyin, English or tone number */
        unsigned char c = (unsigned char)ev.text.text[0];
        if (c < 32 || c > 126) return 0;
        e->type = EV_KEY; e->key = c; return 1;
    }
    case SDL_WINDOWEVENT:
        /* resized, full screen, uncovered: the last frame again, scaled --
         * the texture always holds the whole of it */
        if (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
            ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            show();
        return 0;
    case SDL_KEYDOWN: {
        SDL_Keycode k = ev.key.keysym.sym;
        int ctrl = ev.key.keysym.mod & KMOD_CTRL, shift = ev.key.keysym.mod & KMOD_SHIFT;
        if (ctrl && k == SDLK_q) { e->type = EV_QUIT; return 1; }
        /* a key held down repeats; the page keys and Enter act once per
         * press, as the Kobo's buttons do -- holding Right graded card after
         * card "good" */
        if (ev.key.repeat && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_PAGEUP ||
                              k == SDLK_PAGEDOWN || k == SDLK_RETURN || k == SDLK_KP_ENTER ||
                              k == SDLK_F11))
            return 0;
        if (k == SDLK_F11) {             /* full screen and back to what it was */
            surf_set_window_mode(s_mode == WIN_FULL ? s_unfull : WIN_FULL);
            return 0;
        }
        /* the page keys, as the Kobo's: back and forward through whatever
         * the screen has an order to */
        if (k == SDLK_LEFT || k == SDLK_PAGEUP) { e->type = EV_PAGE; e->key = -1; return 1; }
        if (k == SDLK_RIGHT || k == SDLK_PAGEDOWN) { e->type = EV_PAGE; e->key = 1; return 1; }
        e->type = EV_KEY;
        if (k == SDLK_BACKSPACE) e->key = 8;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) e->key = 13;
        else if (k == SDLK_ESCAPE) e->key = 27;
        else if (k == SDLK_TAB) e->key = shift ? K_BACKTAB : K_TAB;
        else if (k == SDLK_UP) e->key = K_UP;
        else if (k == SDLK_DOWN) e->key = K_DOWN;
        else return 0;
        return 1;
    }
    }
    return 0;
}

/* ---- --diag --------------------------------------------------------------
 * What a "it does not start" report needs: which display libraries this
 * machine has (the AppImage's SDL loads X11, Wayland and GL at run time,
 * from the machine), which of SDL's video drivers come up, and which one it
 * would use. */
static void diag_lib(const char *soname, const char *what) {
    void *h = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char *err = dlerror();
        printf("  %-26s MISSING  (%s) -- %s\n", soname, what, err ? err : "?");
        return;
    }
    struct link_map *lm = NULL;
    const char *path = (dlinfo(h, RTLD_DI_LINKMAP, &lm) == 0 && lm && lm->l_name && *lm->l_name)
                       ? lm->l_name : "?";
    printf("  %-26s ok       %s\n", soname, path);
    dlclose(h);
}

void surf_diag(void) {
    SDL_version c, l;
    SDL_VERSION(&c);
    SDL_GetVersion(&l);
    printf("SDL: built against %d.%d.%d, running %d.%d.%d (%s)\n",
           c.major, c.minor, c.patch, l.major, l.minor, l.patch, SDL_GetRevision());

    printf("display libraries (what SDL loads from this machine):\n");
    diag_lib("libX11.so.6",            "X11");
    diag_lib("libXext.so.6",           "X11");
    diag_lib("libXrandr.so.2",         "X11, optional");
    diag_lib("libXcursor.so.1",        "X11, optional");
    diag_lib("libXi.so.6",             "X11, optional");
    diag_lib("libxkbcommon.so.0",      "Wayland keyboard");
    diag_lib("libwayland-client.so.0", "Wayland");
    diag_lib("libwayland-egl.so.1",    "Wayland");
    diag_lib("libwayland-cursor.so.0", "Wayland");
    diag_lib("libdecor-0.so.0",        "Wayland window frames, optional");
    diag_lib("libGL.so.1",             "OpenGL renderer, optional");
    diag_lib("libEGL.so.1",            "EGL: Wayland, optional on X11");

    const char *envs[] = { "SDL_VIDEODRIVER", "SDL_RENDER_DRIVER", "XDG_SESSION_TYPE",
                           "DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR" };
    printf("session:\n");
    for (const char *k : envs) {
        const char *v = getenv(k);
        printf("  %-18s %s\n", k, v ? v : "(unset)");
    }

    int nd = SDL_GetNumVideoDrivers();
    printf("SDL video drivers built in (%d), each tried:\n", nd);
    for (int i = 0; i < nd; i++) {
        const char *d = SDL_GetVideoDriver(i);
        if (!strcmp(d, "dummy") || !strcmp(d, "offscreen") || !strcmp(d, "evdev")) {
            printf("  %-10s (headless stand-in, not tried)\n", d);
            continue;
        }
        if (SDL_VideoInit(d) == 0) {
            printf("  %-10s ok, %d display(s)\n", d, SDL_GetNumVideoDisplays());
            SDL_VideoQuit();
        } else {
            printf("  %-10s failed: %s\n", d, SDL_GetError());
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init: FAILED: %s\n"
               "  -> the app cannot open a window here. Under Wayland, check the Wayland and"
               " EGL lines above; under X11, libX11 and DISPLAY.\n", SDL_GetError());
        return;
    }
    const char *drv = SDL_GetCurrentVideoDriver();
    printf("SDL would use: %s\n", drv ? drv : "?");
    if (headless_driver(drv) && !driver_asked(drv)) {
        printf("  -> NO REAL DISPLAY: that is a headless stand-in, so the app would run with"
               " no window; it refuses to start instead. Under Wayland, check the Wayland and"
               " EGL lines above; under X11, libX11 and DISPLAY.\n");
        SDL_Quit();
        return;
    }
    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        SDL_Rect b, ub;
        SDL_GetDisplayBounds(i, &b);
        if (SDL_GetDisplayUsableBounds(i, &ub) != 0) ub = b;
        printf("  display %d: %dx%d at %d,%d, usable %dx%d at %d,%d\n",
               i, b.w, b.h, b.x, b.y, ub.w, ub.h, ub.x, ub.y);
    }
    printf("  window placement (--window left|right): %s\n",
           native_wayland() ? "NOT possible under native Wayland (SDL_VIDEODRIVER=x11 uses XWayland)"
                            : "possible");
    int nr = SDL_GetNumRenderDrivers();
    printf("  render drivers:");
    for (int i = 0; i < nr; i++) {
        SDL_RendererInfo ri;
        if (SDL_GetRenderDriverInfo(i, &ri) == 0) printf(" %s", ri.name);
    }
    printf("\n");
    /* the renderer the app would get, from a window never shown */
    SDL_Window *w = SDL_CreateWindow("hanzi --diag", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
    if (!w) {
        printf("  a window: FAILED: %s\n", SDL_GetError());
    } else {
        SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
        const char *how = "accelerated";
        if (!r) {
            printf("  accelerated renderer failed: %s\n", SDL_GetError());
            r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
            how = "software";
        }
        SDL_RendererInfo ri;
        if (r && SDL_GetRendererInfo(r, &ri) == 0) printf("  renderer: %s (%s)\n", ri.name, how);
        else printf("  renderer: FAILED: %s\n", SDL_GetError());
        if (r) SDL_DestroyRenderer(r);
        SDL_DestroyWindow(w);
    }
    SDL_Quit();
}
