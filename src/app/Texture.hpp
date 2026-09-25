#ifndef GR4_PRESENT_TEXTURE_HPP
#define GR4_PRESENT_TEXTURE_HPP

#include "Theme.hpp"

#include <span>

namespace gr::present {

class Texture {
public:
    ImTextureID id     = 0;
    float       width  = 0.0f; // texels; the raster is cropped to the drawing, so it is not square
    float       height = 0.0f;

    Texture()                          = default;
    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept { swap(other); }
    Texture& operator=(Texture&& other) noexcept {
        swap(other);
        return *this;
    }
    ~Texture() { release(); }

    [[nodiscard]] static Texture load(std::span<const unsigned char> encodedPng);
    [[nodiscard]] static Texture loadLogo(ColourScheme scheme);
    void                         release() noexcept;
    void                         swap(Texture& other) noexcept;

    [[nodiscard]] ImVec2 scaledToWidth(float boxWidth) const noexcept { return width > 0.0f ? ImVec2{boxWidth, boxWidth * height / width} : ImVec2{}; }
};

} // namespace gr::present

#endif // GR4_PRESENT_TEXTURE_HPP
