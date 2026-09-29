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

/**
 * What a finger's flick means for navigation.
 *
 * A presentation is swiped the way a book is turned: dragging leftwards pulls the next view in from the right.
 * Three things separate a page turn from everything else a finger does on a slide — it travels far enough to be
 * deliberate, it is more horizontal than vertical so a scroll is not mistaken for it, and it is quick enough that
 * resting a finger and moving it slowly is not a gesture at all.
 *
 * The distance is a share of the viewport rather than a pixel count, because the same physical gesture covers very
 * different pixel counts on a phone and on a projector.
 */
[[nodiscard]] SwipeAction swipeActionOf(const Flick& flick, float viewportWidth) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_SWIPE_NAVIGATION_HPP
