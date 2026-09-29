#ifndef GR4_PRESENT_LAYOUT_HPP
#define GR4_PRESENT_LAYOUT_HPP

#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

inline constexpr float kTitleTopEms = 0.2f; // generated grids and the drawn title agree on where a title starts

/// a named area of a slide layout; bounds are in the layout's own units
struct Area {
    std::string id;
    std::string kind;              // markdown, image, svg or gr4; empty for a bare anchor
    int         step          = 0; // shares the section's reveal counter
    float       x             = 0.0f;
    float       y             = 0.0f;
    float       width         = 0.0f;
    float       height        = 0.0f;
    float       backdropLight = -1.0f; // master brightness under the area, 0 to 1; negative if transparent

    bool operator==(const Area&) const = default;
};

struct Layout {
    float             width  = 0.0f; // in the layout's own units
    float             height = 0.0f;
    std::vector<Area> areas;

    /// fitted to the viewport rather than drawn: no proportions to letterbox into, type sized from the screen
    bool generated = false;

    [[nodiscard]] const Area* find(std::string_view id) const noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_LAYOUT_HPP
