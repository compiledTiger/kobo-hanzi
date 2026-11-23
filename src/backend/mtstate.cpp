#include "mtstate.h"
#include <algorithm>

namespace mt {

int Tracker::feed(const input_event &ev) {
    nout_ = 0;
    Slot &sl = slots_[std::clamp(cur_, 0, MAX_SLOTS - 1)];

    switch (ev.type) {
    case EV_ABS:
        switch (ev.code) {
        case ABS_MT_SLOT:
            cur_ = std::clamp(ev.value, 0, MAX_SLOTS - 1);
            break;
        case ABS_MT_TRACKING_ID:
            saw_tracking_ = true;
            sl.id = ev.value;
            if (ev.value < 0) sl.has_pos = false;
            break;
        case ABS_MT_TOOL_TYPE:
            sl.tool = ev.value;
            break;
        case ABS_X:
        case ABS_MT_POSITION_X:
            if (sl.has_pos && sl.x != ev.value) sl.moved = true;
            sl.x = ev.value;
            sl.has_pos = true;
            if (sl.id < 0 && !saw_tracking_) sl.id = 0;   /* protocol A only */
            break;
        case ABS_Y:
        case ABS_MT_POSITION_Y:
            if (sl.has_pos && sl.y != ev.value) sl.moved = true;
            sl.y = ev.value;
            sl.has_pos = true;
            if (sl.id < 0 && !saw_tracking_) sl.id = 0;   /* protocol A only */
            break;
        case ABS_PRESSURE:
        case ABS_MT_PRESSURE:
            /* Hover: the pen keeps reporting a position with the contact
             * still open. Zero pressure from a pen ends it -- but record it
             * and decide at SYN_REPORT, so it does not matter whether the
             * pressure came before or after the coordinates in the frame. */
            sl.zero_press = (ev.value == 0);
            break;
        default:
            break;
        }
        break;

    case EV_KEY:
        switch (ev.code) {
        case BTN_TOUCH:
            /* Only decisive for release, and only on panels without slots. */
            if (!ev.value) sl.id = -1;
            break;
        case BTN_TOOL_PEN:
            sl.tool = ev.value ? TOOL_PEN : TOOL_FINGER;
            if (!ev.value) sl.id = -1;          /* left proximity entirely */
            break;
        case BTN_TOOL_RUBBER:
            if (ev.value) sl.tool = TOOL_PEN;
            break;
        default:
            /* BTN_STYLUS and BTN_STYLUS2 are barrel buttons. They say nothing
             * about contact, and treating them as such ended strokes. */
            break;
        }
        break;

    case EV_SYN:
        if (ev.code == SYN_REPORT) end_frame();
        break;

    default:
        break;
    }
    return nout_;
}

void Tracker::emit(int slot, Action a, const Slot &sl) {
    if (nout_ >= MAX_SLOTS * 2) return;
    Out &o = out_[nout_++];
    o.action = a;
    o.slot   = slot;
    o.x      = sl.x;
    o.y      = sl.y;
    o.pen    = sl.tool == TOOL_PEN;
}

void Tracker::end_frame() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot &sl = slots_[i];
        /* A pen hovering is not in contact however its tracking id reads. */
        if (sl.zero_press && sl.tool == TOOL_PEN) sl.id = -1;

        const bool was = sl.prev_id >= 0;
        const bool now = sl.id >= 0;
        /* One contact replaced by another without an intervening -1: that is
         * a release and a fresh press, not a move across the gap. */
        const bool swapped = was && now && sl.id != sl.prev_id;

        if (swapped) {
            emit(i, Action::Up, sl);
            if (sl.has_pos) emit(i, Action::Down, sl);
        } else if (!was && now && sl.has_pos) emit(i, Action::Down, sl);
        else if (was && !now)                 emit(i, Action::Up, sl);
        else if (was && now && sl.moved)      emit(i, Action::Move, sl);

        sl.prev_id    = sl.id;
        sl.moved      = false;
        sl.zero_press = false;
    }
}

}  // namespace mt
