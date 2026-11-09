#include <boost/ut.hpp>

#include "SwipeNavigation.hpp"

using namespace boost::ut;
using namespace gr::present;

namespace {

constexpr float kWidth = 800.0f;

[[nodiscard]] Flick flick(float fromX, float toX, float fromY = 400.0f, float toY = 400.0f, int milliseconds = 200) { return Flick{.fromX = fromX, .fromY = fromY, .toX = toX, .toY = toY, .duration = std::chrono::milliseconds{milliseconds}}; }

} // namespace

// Ground truth: what a gesture means to a person -- drag left is next, short is a tap, vertical scrolls, slow rests
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

// Ground truth: SDL's finger events as a browser sends them -- positions in fractions of the window, times in nanoseconds
const suite<"Fingers"> fingerTests = [] {
    constexpr std::uint64_t kMs     = 1'000'000U;
    const auto              touched = [](Fingers& fingers, FingerChange change, std::uint64_t finger, float x, float y, std::uint64_t ms) { track(fingers, FingerEvent{.change = change, .finger = finger, .x = x, .y = y, .nanoseconds = ms * kMs}, 800.0f, 600.0f); };

    "one finger lifted is a flick in viewport pixels, from where it touched down to where it lifted"_test = [&] {
        Fingers fingers;
        touched(fingers, FingerChange::down, 7U, 0.75f, 0.5f, 100U);
        touched(fingers, FingerChange::motion, 7U, 0.5f, 0.5f, 150U);
        expect(eq(fingers.onGlass.size(), 1UZ) && !fingers.lifted.has_value()) << "still on the glass";
        touched(fingers, FingerChange::up, 7U, 0.25f, 0.5f, 300U);
        expect(fatal(fingers.lifted.has_value()));
        expect(eq(fingers.lifted->fromX, 600.0f) && eq(fingers.lifted->toX, 200.0f) && eq(fingers.lifted->fromY, 300.0f));
        expect(eq(fingers.lifted->duration.count(), 200)) << "held 200 ms";
        expect(fingers.onGlass.empty());
        expect(swipeActionOf(*fingers.lifted, 800.0f) == SwipeAction::next);
    };

    "the lift that ends a pinch is no flick, nor is any lift until the glass is empty again"_test = [&] {
        Fingers fingers;
        touched(fingers, FingerChange::down, 1U, 0.4f, 0.5f, 0U);
        touched(fingers, FingerChange::down, 2U, 0.6f, 0.5f, 10U);
        expect(eq(fingers.onGlass.size(), 2UZ));
        touched(fingers, FingerChange::up, 2U, 0.9f, 0.5f, 100U);
        touched(fingers, FingerChange::up, 1U, 0.1f, 0.5f, 120U);
        expect(!fingers.lifted.has_value() && fingers.onGlass.empty());
        touched(fingers, FingerChange::down, 3U, 0.75f, 0.5f, 500U);
        touched(fingers, FingerChange::up, 3U, 0.25f, 0.5f, 600U);
        expect(fingers.lifted.has_value()) << "the next lone finger flicks again";
    };

    "a cancelled finger leaves the glass without a flick"_test = [&] {
        Fingers fingers;
        touched(fingers, FingerChange::down, 4U, 0.75f, 0.5f, 0U);
        touched(fingers, FingerChange::cancelled, 4U, 0.25f, 0.5f, 100U);
        expect(fingers.onGlass.empty() && !fingers.lifted.has_value());
    };

    "a lift or motion of a finger never seen touching down is ignored"_test = [&] {
        Fingers fingers;
        touched(fingers, FingerChange::motion, 5U, 0.5f, 0.5f, 0U);
        touched(fingers, FingerChange::up, 5U, 0.5f, 0.5f, 10U);
        expect(fingers.onGlass.empty() && !fingers.lifted.has_value());
    };

    "a finger silent for 5 s while none moves is let go, as browsers lose a lift"_test = [&] {
        Fingers fingers;
        touched(fingers, FingerChange::down, 6U, 0.5f, 0.5f, 1000U);
        releaseStale(fingers, 5900U * kMs);
        expect(eq(fingers.onGlass.size(), 1UZ)) << "4.9 s is still a finger resting";
        releaseStale(fingers, 6100U * kMs);
        expect(fingers.onGlass.empty() && !fingers.lifted.has_value()) << "past 5 s it is gone, and no flick";
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
