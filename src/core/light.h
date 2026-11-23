/* light.h -- which frontlight level to put back on waking.
 *
 * Only the hardware exercises the sleep path, so the decision lives here,
 * pure, where tests/light_test.cpp can run it on a desktop. */
#ifndef HANZI_LIGHT_H
#define HANZI_LIGHT_H

/* The level to restore after a lock or a sleep, decided as it begins.
 *
 *   read    the light as read just before blanking it; -1 if it cannot be
 *           read (no light, or the node refused)
 *   held    a level already saved by a lock this one carries on from -- a
 *           lock on the charger that goes to sleep once unplugged -- or -1.
 *           That lock blanked the light, so reading it now says 0.
 *   stored  the level last chosen in settings (db_light), -1 if never set
 *
 * What was read is what comes back, 0 included: a light that was off stays
 * off. It used to be "never restore 0", because waking to a dark screen left
 * no way back -- but settings, and its light row, are now one press away on
 * every screen, so a light the user had off came back at 50 for nothing.
 * Only when the level could not be read is there a guess to make, and then
 * it is the stored setting, else 20: never 0, since a light that cannot be
 * read cannot be shown or stepped in settings either. */
static inline int light_wake_level(int read, int held, int stored) {
    if (held >= 0) return held;
    if (read >= 0) return read;
    return stored > 0 ? stored : 20;
}

#endif
