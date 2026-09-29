#ifndef GR4_PRESENT_SWIPE_NAVIGATION_HPP
#define GR4_PRESENT_SWIPE_NAVIGATION_HPP

#include <chrono>

namespace gr::present {

/// one finger's journey from touching down to lifting, in viewport pixels
struct Flick {
    float                     fromX    = 0.0f;
    float                     fromY    = 0.0f;
    float                     toX      = 0.0f;
    float                     toY      = 0.0f;
    std::chrono::milliseconds duration = std::chrono::milliseconds{0};
};

enum class SwipeAction { none, next, previous };

/// leftwards pulls the next view in from the right, as turning a page does; a page turn travels far enough, is mostly
/// horizontal and is quick; the distance is a share of the viewport, as a gesture spans different pixels per device
[[nodiscard]] SwipeAction swipeActionOf(const Flick& flick, float viewportWidth) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_SWIPE_NAVIGATION_HPP
