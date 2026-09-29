#ifndef GR4_PRESENT_ANIMATION_HPP
#define GR4_PRESENT_ANIMATION_HPP

#include "SvgImage.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace gr::present {

/// a decoded image of one or more RGBA frames, already composited; a still picture is a one-frame animation
/// delays are in milliseconds
struct Animation {
    std::uint32_t                          width  = 0U;
    std::uint32_t                          height = 0U;
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<int>                       delaysMs;

    [[nodiscard]] bool        animated() const noexcept { return frames.size() > 1UZ; }
    [[nodiscard]] int         totalMs() const noexcept;
    [[nodiscard]] std::size_t frameAt(double seconds) const noexcept;
};

/// `seconds` since the deck was opened; the frame sequence loops
[[nodiscard]] std::size_t frameAt(std::span<const int> delaysMs, double seconds) noexcept;

/// PNG, JPEG, GIF and WebP, still or animated; the format is detected from the bytes, not the file name
[[nodiscard]] std::expected<Animation, std::string> decodeAnimation(std::span<const std::uint8_t> bytes);

} // namespace gr::present

#endif // GR4_PRESENT_ANIMATION_HPP
