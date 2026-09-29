#include <boost/ut.hpp>

#include "SwipeNavigation.hpp"

using namespace boost::ut;
using namespace gr::present;

namespace {

constexpr float kWidth = 800.0f;

[[nodiscard]] Flick flick(float fromX, float toX, float fromY = 400.0f, float toY = 400.0f, int milliseconds = 200) { return Flick{.fromX = fromX, .fromY = fromY, .toX = toX, .toY = toY, .duration = std::chrono::milliseconds{milliseconds}}; }

} // namespace

// Ground truth is what the gesture means to a person holding the device, not what the code does: dragging leftwards
// turns to the next view, as it does in every reader; a short touch is a tap and must not turn anything; a mostly
// vertical drag is a scroll; and a slow drag is somebody resting a finger on the screen.
const suite<"SwipeNavigation"> swipeTests = [] {
    "dragging leftwards brings the next view in from the right"_test = [] {
        expect(swipeActionOf(flick(600.0f, 200.0f), kWidth) == SwipeAction::next);
        expect(swipeActionOf(flick(200.0f, 600.0f), kWidth) == SwipeAction::previous);
    };

    "a tap turns nothing"_test = [] {
        expect(swipeActionOf(flick(400.0f, 400.0f), kWidth) == SwipeAction::none);
        expect(swipeActionOf(flick(400.0f, 404.0f), kWidth) == SwipeAction::none) << "a few pixels of wobble is still a tap";
    };

    "the travel needed scales with the viewport, not with pixels"_test = [] {
        // 80 px is a tenth of an 800 px window and a fiftieth of a 4000 px one: a gesture on one, nothing on the other
        expect(swipeActionOf(flick(400.0f, 320.0f), 800.0f) == SwipeAction::next);
        expect(swipeActionOf(flick(400.0f, 320.0f), 4000.0f) == SwipeAction::none);
    };

    "a mostly vertical drag is a scroll, not a page turn"_test = [] {
        expect(swipeActionOf(flick(600.0f, 400.0f, 100.0f, 700.0f), kWidth) == SwipeAction::none) << "200 across and 600 down is a scroll";
        expect(swipeActionOf(flick(600.0f, 200.0f, 400.0f, 450.0f), kWidth) == SwipeAction::next) << "400 across and 50 down is a swipe";
    };

    "a slow drag is a finger resting, not a flick"_test = [] {
        expect(swipeActionOf(flick(600.0f, 200.0f, 400.0f, 400.0f, 200), kWidth) == SwipeAction::next);
        expect(swipeActionOf(flick(600.0f, 200.0f, 400.0f, 400.0f, 4000), kWidth) == SwipeAction::none);
    };

    "a viewport with no width cannot be swiped"_test = [] { expect(swipeActionOf(flick(600.0f, 200.0f), 0.0f) == SwipeAction::none); };
};

int main() { return 0; }
