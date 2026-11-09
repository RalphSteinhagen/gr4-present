#ifndef GR4_PRESENT_SWIPE_NAVIGATION_HPP
#define GR4_PRESENT_SWIPE_NAVIGATION_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

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

enum class FingerChange { down, motion, up, cancelled };

/// a finger event as SDL reports it: the position in fractions of the window, the time in nanoseconds
struct FingerEvent {
    FingerChange  change      = FingerChange::down;
    std::uint64_t finger      = 0U;
    float         x           = 0.0f;
    float         y           = 0.0f;
    std::uint64_t nanoseconds = 0U;
};

/// the fingers on the glass in viewport pixels, and the flick a finger made alone, taken once by the frame after it
struct Fingers {
    struct Finger {
        std::uint64_t id;
        float         downX;
        float         downY;
        float         x;
        float         y;
        std::uint64_t downAt;
        std::uint64_t lastAt;
    };
    std::vector<Finger>  onGlass;
    std::optional<Flick> lifted;
    bool                 twoAtOnce = false; // since the glass was last empty: the lift ending a pinch is no flick
};

void track(Fingers& fingers, const FingerEvent& event, float viewportWidth, float viewportHeight);

/// lets go of a finger whose lift never arrived: silent for 5 s while no other finger moved, as browsers lose a lift
void releaseStale(Fingers& fingers, std::uint64_t nowNanoseconds);

} // namespace gr::present

#endif // GR4_PRESENT_SWIPE_NAVIGATION_HPP
