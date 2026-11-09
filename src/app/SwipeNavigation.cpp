#include "SwipeNavigation.hpp"

#include <algorithm>
#include <cmath>

namespace gr::present {

namespace {
constexpr float kMinimumTravelViewportShare = 0.08f;
constexpr float kHorizontalToVerticalRatio  = 1.5f;
constexpr auto  kSlowestFlick               = std::chrono::milliseconds{900};

constexpr std::uint64_t kStaleNanoseconds = 5'000'000'000U;
} // namespace

SwipeAction swipeActionOf(const Flick& flick, float viewportWidth) noexcept {
    if (viewportWidth <= 0.0f || flick.duration > kSlowestFlick) {
        return SwipeAction::none;
    }
    const float dx = flick.toX - flick.fromX;
    const float dy = flick.toY - flick.fromY;
    if (std::abs(dx) < kMinimumTravelViewportShare * viewportWidth || std::abs(dx) < kHorizontalToVerticalRatio * std::abs(dy)) {
        return SwipeAction::none;
    }
    return dx < 0.0f ? SwipeAction::next : SwipeAction::previous;
}

void track(Fingers& fingers, const FingerEvent& event, float viewportWidth, float viewportHeight) {
    const float x     = event.x * viewportWidth;
    const float y     = event.y * viewportHeight;
    const auto  known = std::ranges::find(fingers.onGlass, event.finger, &Fingers::Finger::id);
    if (event.change == FingerChange::down) {
        if (known == fingers.onGlass.end()) {
            fingers.onGlass.push_back(Fingers::Finger{.id = event.finger, .downX = x, .downY = y, .x = x, .y = y, .downAt = event.nanoseconds, .lastAt = event.nanoseconds});
        }
        fingers.twoAtOnce = fingers.twoAtOnce || fingers.onGlass.size() > 1UZ;
        return;
    }
    if (known == fingers.onGlass.end()) {
        return;
    }
    known->x      = x;
    known->y      = y;
    known->lastAt = event.nanoseconds;
    if (event.change == FingerChange::motion) {
        return;
    }
    if (event.change == FingerChange::up && fingers.onGlass.size() == 1UZ && !fingers.twoAtOnce) {
        const auto held = std::chrono::nanoseconds{event.nanoseconds - std::min(known->downAt, event.nanoseconds)};
        fingers.lifted  = Flick{.fromX = known->downX, .fromY = known->downY, .toX = x, .toY = y, .duration = std::chrono::duration_cast<std::chrono::milliseconds>(held)};
    }
    fingers.onGlass.erase(known);
    fingers.twoAtOnce = fingers.twoAtOnce && !fingers.onGlass.empty();
}

void releaseStale(Fingers& fingers, std::uint64_t nowNanoseconds) {
    if (fingers.onGlass.empty() || std::ranges::any_of(fingers.onGlass, [nowNanoseconds](const Fingers::Finger& finger) { return nowNanoseconds < finger.lastAt + kStaleNanoseconds; })) {
        return;
    }
    fingers.onGlass.clear();
    fingers.twoAtOnce = false;
}

} // namespace gr::present
