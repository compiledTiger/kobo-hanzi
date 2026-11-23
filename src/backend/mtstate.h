/* mtstate -- multitouch protocol B state machine.
 *
 * Kept apart from the framebuffer backend so it can be tested on a desktop
 * against recorded event streams. Every pen bug this app has had came from
 * reading a protocol B panel as though it were protocol A, and the only way
 * to be sure that is fixed is to replay the frames and assert the output.
 */
#ifndef MTSTATE_H
#define MTSTATE_H

#include <linux/input.h>

namespace mt {

inline constexpr int MAX_SLOTS   = 8;
inline constexpr int TOOL_FINGER = 0;
inline constexpr int TOOL_PEN    = 1;

enum class Action { None, Down, Move, Up };

struct Out {
    Action action = Action::None;
    int    slot   = 0;
    int    x      = 0;      /* raw device coordinates */
    int    y      = 0;
    bool   pen    = false;
};

struct Slot {
    int  id      = -1;      /* tracking id; -1 means no contact */
    int  prev_id = -1;
    int  x = 0, y = 0;
    int  tool    = TOOL_FINGER;
    bool moved   = false;
    bool has_pos = false;
    /* Set when this frame carried zero pressure, and applied at SYN_REPORT
     * rather than immediately: whether the pressure arrives before or after
     * the coordinates in a frame is not something to depend on. */
    bool zero_press = false;
};

/* Feed it raw events; it emits at most MAX_SLOTS*2 actions per frame.
 *
 * Contact is the tracking id, never BTN_TOUCH: the panel repeats the id in
 * every frame, so treating its presence as a fresh press made one tap count
 * as two. Hover is dropped by KOReader's rule -- zero pressure *and* a pen
 * tool -- because pressure alone breaks panels that never report it and the
 * tool alone discards real contact. */
class Tracker {
public:
    /* Returns the number of actions produced by this event (non-zero only on
     * SYN_REPORT), readable via out(i). */
    int feed(const input_event &ev);
    const Out &out(int i) const { return out_[i]; }

private:
    void end_frame();
    void emit(int slot, Action a, const Slot &sl);

    Slot slots_[MAX_SLOTS];
    int  cur_ = 0;
    /* Panels that report tracking ids must not also get the protocol A
     * "a position means contact" fallback, or a hovering pen re-opens the
     * contact its zero pressure just closed. */
    bool saw_tracking_ = false;
    Out  out_[MAX_SLOTS * 2];
    int  nout_ = 0;
};

}  // namespace mt

#endif
