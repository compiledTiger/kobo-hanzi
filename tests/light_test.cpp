/* The frontlight after a lock or a sleep comes back as it was, off included.
 *
 * Only the hardware runs the sleep path, so this replays its sequences
 * against light_wake_level: read the light, blank it, and on any way back
 * (a touch, power on a refused suspend, a real wake) put back the one level
 * saved. The device log that prompted it: "light: saving 50 before sleep /
 * light: set 0 (was 0) / ... set 50 (was 0)" -- the light was off, and came
 * back at 50. */
#include "../src/core/light.h"
#include <stdio.h>

static int bad = 0;
static void chk(int cond, const char *what) {
    if (!cond) { printf("    FAIL: %s\n", what); bad = 1; }
}

/* The app's sequence, with a light that reads `dev` before the lock.
 * Returns the level it is set to on waking. */
static int lock_and_wake(int dev, int stored) {
    int save = light_wake_level(dev, -1, stored);   /* read, then blanked */
    return save;                              /* every wake path sets this */
}

/* Locked on the charger (suspend refused), then unplugged: it goes to sleep
 * from the lock, when the light already reads 0. */
static int charger_lock_then_sleep(int dev, int stored) {
    int save = light_wake_level(dev, -1, stored);
    dev = 0;                                  /* the lock blanked it */
    int held = save;                          /* unplugged: carried over */
    save = light_wake_level(dev, held, stored);
    dev = 0;                                  /* blanked again, asleep */
    dev = save;                               /* woken */
    return dev;
}

int main(void) {
    /* whatever it was, it comes back: every level, with any stored setting */
    static const int STORED[] = { -1, 0, 20, 50, 100 };
    int all = 1;
    for (int lv = 0; lv <= 100; lv++)
        for (int st : STORED)
            if (lock_and_wake(lv, st) != lv || charger_lock_then_sleep(lv, st) != lv) all = 0;
    chk(all, "every level 0..100 comes back as it was, whatever is stored");
    printf("    light off before the lock, 50 stored: back at %d\n", lock_and_wake(0, 50));
    chk(lock_and_wake(0, 50) == 0, "a light that was off stays off (it came back at 50)");
    chk(lock_and_wake(0, -1) == 0, "off with nothing stored stays off (it came back at 20)");
    chk(lock_and_wake(37, 50) == 37, "a level is not replaced by the stored one");

    /* a lock carried into sleep keeps the lock's level, not the 0 it reads */
    chk(charger_lock_then_sleep(42, 50) == 42, "charger lock then sleep: the level before the lock");
    chk(charger_lock_then_sleep(0, 50) == 0, "charger lock then sleep: off before the lock stays off");
    chk(light_wake_level(0, 42, 50) == 42, "a held level wins over the 0 read after blanking");
    chk(light_wake_level(0, 0, 50) == 0, "a held 0 is a level too");

    /* only a light that cannot be read is guessed at, and never as 0 */
    chk(light_wake_level(-1, -1, 35) == 35, "unreadable: the level last set in settings");
    chk(light_wake_level(-1, -1, -1) == 20, "unreadable, nothing stored: 20");
    chk(light_wake_level(-1, -1, 0) == 20, "unreadable, 0 stored: 20, not dark");

    printf(bad ? "    -> FAILED\n" : "    -> the light comes back as it was\n");
    return bad;
}
