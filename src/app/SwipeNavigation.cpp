#include "SwipeNavigation.hpp"

#include <cmath>

namespace gr::present {

namespace {
constexpr float kMinimumTravelShare = 0.08f;                          // of the viewport width
constexpr float kHorizontalRatio    = 1.5f;                           // how much more horizontal than vertical
constexpr auto  kSlowestFlick       = std::chrono::milliseconds{900}; // longer than this is a drag, not a flick
} // namespace

SwipeAction swipeActionOf(const Flick& flick, float viewportWidth) noexcept {
    if (viewportWidth <= 0.0f || flick.duration > kSlowestFlick) {
        return SwipeAction::none;
    }
    const float dx = flick.toX - flick.fromX;
    const float dy = flick.toY - flick.fromY;
    if (std::abs(dx) < kMinimumTravelShare * viewportWidth || std::abs(dx) < kHorizontalRatio * std::abs(dy)) {
        return SwipeAction::none;
    }
    // dragging leftwards pulls the next view in from the right, as turning a page does
    return dx < 0.0f ? SwipeAction::next : SwipeAction::previous;
}

} // namespace gr::present
