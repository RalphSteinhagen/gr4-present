#include "SwipeNavigation.hpp"

#include <cmath>

namespace gr::present {

namespace {
constexpr float kMinimumTravelViewportShare = 0.08f;
constexpr float kHorizontalToVerticalRatio  = 1.5f;
constexpr auto  kSlowestFlick               = std::chrono::milliseconds{900};
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

} // namespace gr::present
