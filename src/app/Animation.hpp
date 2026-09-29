#ifndef GR4_PRESENT_ANIMATION_HPP
#define GR4_PRESENT_ANIMATION_HPP

#include "SvgImage.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace gr::present {

/**
 * A decoded image with one or more frames.
 *
 * A still picture is a one-frame animation, which lets the figure cache hold one kind of thing rather than two and
 * lets the renderer draw a GIF exactly as it draws a PNG.
 *
 * Frames are RGBA and already composited: a GIF frame may only be a patch of the one before it, and an animated
 * WebP frame may be blended or disposed, so a caller that had to compose them itself would be re-implementing both
 * formats. Delays are in milliseconds, as both formats store them.
 */
struct Animation {
    std::uint32_t                          width  = 0U;
    std::uint32_t                          height = 0U;
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<int>                       delaysMs;

    [[nodiscard]] bool        animated() const noexcept { return frames.size() > 1UZ; }
    [[nodiscard]] int         totalMs() const noexcept;
    [[nodiscard]] std::size_t frameAt(double seconds) const noexcept;
};

/// which frame is showing `seconds` into a loop that has been running since the deck was opened
[[nodiscard]] std::size_t frameAt(std::span<const int> delaysMs, double seconds) noexcept;

/// PNG, JPEG, GIF and WebP, still or animated; the format is taken from the bytes, never from the file name
[[nodiscard]] std::expected<Animation, std::string> decodeAnimation(std::span<const std::uint8_t> bytes);

} // namespace gr::present

#endif // GR4_PRESENT_ANIMATION_HPP
