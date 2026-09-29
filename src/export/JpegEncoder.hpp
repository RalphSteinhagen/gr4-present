#ifndef GR4_PRESENT_EXPORT_JPEG_ENCODER_HPP
#define GR4_PRESENT_EXPORT_JPEG_ENCODER_HPP

#include <cstdint>
#include <span>
#include <vector>

namespace gr::present {

/// RGB pixels, top row first, as a baseline JPEG at `quality` (1 to 100); empty if they could not be encoded
[[nodiscard]] std::vector<std::uint8_t> encodeJpeg(std::span<const std::uint8_t> rgb, std::uint32_t width, std::uint32_t height, int quality);

} // namespace gr::present

#endif // GR4_PRESENT_EXPORT_JPEG_ENCODER_HPP
