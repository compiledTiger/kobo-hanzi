/* Replays the frame patterns this device actually produces, taken from the
 * raw event log, and asserts the behaviour each pen bug had. */
#include "../src/backend/mtstate.h"
#include <cstdio>
#include <vector>
#include <cstring>

using namespace mt;

static int bad = 0;
static void chk(bool cond, const char *what) {
    if (!cond) { printf("    FAIL: %s\n", what); bad = 1; }
}

struct Rec { Action a; bool pen; int x, y; };

class Runner {
public:
    void ev(int type, int code, int value) {
        input_event e{};
        e.type = (unsigned short)type;
        e.code = (unsigned short)code;
        e.value = value;
        int n = t_.feed(e);
        for (int i = 0; i < n; i++) {
            const Out &o = t_.out(i);
            got.push_back({o.action, o.pen, o.x, o.y});
        }
    }
    /* One frame exactly as the panel sends it: slot data then SYN_REPORT. */
    void frame(int id, int tool, int x, int y, int pressure) {
        ev(EV_ABS, ABS_MT_TRACKING_ID, id);
        ev(EV_ABS, ABS_MT_TOOL_TYPE, tool);
        ev(EV_ABS, ABS_MT_POSITION_X, x);
        ev(EV_ABS, ABS_MT_POSITION_Y, y);
        ev(EV_ABS, ABS_MT_PRESSURE, pressure);
        ev(EV_SYN, SYN_REPORT, 0);
    }
    void lift() {
        ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
        ev(EV_KEY, BTN_TOUCH, 0);
        ev(EV_SYN, SYN_REPORT, 0);
    }
    int count(Action a) const {
        int n = 0;
        for (auto &r : got) if (r.a == a) n++;
        return n;
    }
    std::vector<Rec> got;
    Tracker t_;
};

int main(void) {
    /* 1. A finger tap. The panel repeats the tracking id in every frame, which
     *    is what made one press count as two. */
    {
        Runner r;
        r.ev(EV_KEY, BTN_TOUCH, 1);
        for (int i = 0; i < 4; i++) r.frame(36, TOOL_FINGER, 500, 600, 40);
        r.lift();
        printf("    finger tap: %d down, %d up\n", r.count(Action::Down), r.count(Action::Up));
        chk(r.count(Action::Down) == 1, "a tap is one press, not two");
        chk(r.count(Action::Up) == 1, "a tap is one release");
    }

    /* 2. Two taps in a row must be two presses, not three or one. */
    {
        Runner r;
        for (int k = 0; k < 2; k++) {
            r.ev(EV_KEY, BTN_TOUCH, 1);
            for (int i = 0; i < 3; i++) r.frame(36 + k, TOOL_FINGER, 400, 400, 40);
            r.lift();
        }
        printf("    two taps: %d down, %d up\n", r.count(Action::Down), r.count(Action::Up));
        chk(r.count(Action::Down) == 2, "two taps give two presses");
    }

    /* 3. A pen approaching, hovering, touching, lifting, hovering, leaving.
     *    Only the touching part may draw. */
    {
        Runner r;
        r.ev(EV_KEY, BTN_TOOL_PEN, 1);                   /* in proximity */
        for (int i = 0; i < 3; i++) r.frame(7, TOOL_PEN, 300 + i * 10, 300, 0);   /* hovering */
        int after_hover_down = r.count(Action::Down);
        for (int i = 0; i < 5; i++) r.frame(7, TOOL_PEN, 400 + i * 10, 400, 900); /* writing */
        int after_write_down = r.count(Action::Down);
        int moves = r.count(Action::Move);
        for (int i = 0; i < 3; i++) r.frame(7, TOOL_PEN, 500 + i * 10, 500, 0);   /* lifted, still near */
        int ups = r.count(Action::Up);
        int moves_after = r.count(Action::Move);
        r.ev(EV_KEY, BTN_TOOL_PEN, 0);                   /* pen leaves */
        r.ev(EV_SYN, SYN_REPORT, 0);

        printf("    pen: hover->%d down, touch->%d down, %d moves, lift->%d up,"
               " %d moves after lifting\n",
               after_hover_down, after_write_down, moves, ups, moves_after - moves);
        chk(after_hover_down == 0, "hovering before contact does not draw");
        chk(after_write_down == 1, "touching down draws exactly once");
        chk(moves >= 3, "moving while in contact produces moves");
        chk(ups == 1, "lifting ends the stroke");
        chk(moves_after == moves, "hovering after lifting does not draw");
    }

    /* 4. The barrel button must not end a stroke. */
    {
        Runner r;
        for (int i = 0; i < 3; i++) r.frame(9, TOOL_PEN, 200 + i * 5, 200, 800);
        r.ev(EV_KEY, BTN_STYLUS, 1);
        r.ev(EV_SYN, SYN_REPORT, 0);
        r.ev(EV_KEY, BTN_STYLUS, 0);
        r.ev(EV_SYN, SYN_REPORT, 0);
        for (int i = 0; i < 3; i++) r.frame(9, TOOL_PEN, 260 + i * 5, 200, 800);
        printf("    barrel button pressed mid-stroke: %d up events\n", r.count(Action::Up));
        chk(r.count(Action::Up) == 0, "the barrel button does not end a stroke");
        chk(r.count(Action::Down) == 1, "and does not start a second one");
    }

    /* 5. A finger reporting no pressure at all must still work. */
    {
        Runner r;
        r.ev(EV_KEY, BTN_TOUCH, 1);
        for (int i = 0; i < 3; i++) r.frame(2, TOOL_FINGER, 100 + i * 20, 150, 0);
        r.lift();
        printf("    finger with zero pressure: %d down, %d move\n",
               r.count(Action::Down), r.count(Action::Move));
        chk(r.count(Action::Down) == 1, "zero pressure from a finger is still contact");
        chk(r.count(Action::Move) >= 1, "and it still moves");
    }

    /* 6. The order of pressure and coordinates within a frame must not
     *    matter. The panel is free to send either first, and the hovering
     *    pen is exactly the case where guessing wrong writes before the nib
     *    has touched the glass. */
    {
        Runner r;
        for (int i = 0; i < 3; i++) {          /* hovering, pressure first */
            r.ev(EV_KEY, BTN_TOOL_PEN, 1);
            r.ev(EV_ABS, ABS_MT_TRACKING_ID, 11);
            r.ev(EV_ABS, ABS_MT_TOOL_TYPE, TOOL_PEN);
            r.ev(EV_ABS, ABS_MT_PRESSURE, 0);
            r.ev(EV_ABS, ABS_MT_POSITION_X, 300 + i);
            r.ev(EV_ABS, ABS_MT_POSITION_Y, 400);
            r.ev(EV_SYN, SYN_REPORT, 0);
        }
        printf("    hover, pressure before coordinates: %d down\n", r.count(Action::Down));
        chk(r.count(Action::Down) == 0, "a hovering pen never opens a contact");

        r.frame(11, TOOL_PEN, 300, 400, 900);   /* now it touches */
        chk(r.count(Action::Down) == 1, "and it does once pressure arrives");
    }

    /* 7. One contact replaced by another with no -1 in between is a release
     *    and a fresh press, not one stroke dragged across the gap. */
    {
        Runner r;
        r.frame(20, TOOL_FINGER, 100, 100, 50);
        r.frame(21, TOOL_FINGER, 900, 900, 50);
        printf("    tracking id swapped in place: %d down, %d up, %d move\n",
               r.count(Action::Down), r.count(Action::Up), r.count(Action::Move));
        chk(r.count(Action::Down) == 2, "the second id is a new press");
        chk(r.count(Action::Up) == 1,   "and the first one was released");
        chk(r.count(Action::Move) == 0, "never a move between two contacts");
    }

    printf(bad ? "    -> FAILED\n" : "    -> input state machine behaves\n");
    return bad;
}
