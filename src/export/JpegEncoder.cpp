#include "JpegEncoder.hpp"

// static, so this copy of stb_image_write cannot meet another linked into the same program, as ImGui's test engine
// links one
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace gr::present {

std::vector<std::uint8_t> encodeJpeg(std::span<const std::uint8_t> rgb, std::uint32_t width, std::uint32_t height, int quality) {
    std::vector<std::uint8_t> encoded;
    const auto                append = [](void* context, void* data, int size) {
        auto*       out   = static_cast<std::vector<std::uint8_t>*>(context);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        out->insert(out->end(), bytes, bytes + size);
    };
    if (rgb.size() < static_cast<std::size_t>(width) * height * 3UZ || stbi_write_jpg_to_func(append, &encoded, static_cast<int>(width), static_cast<int>(height), 3, rgb.data(), quality) == 0) {
        return {};
    }
    return encoded;
}

} // namespace gr::present
