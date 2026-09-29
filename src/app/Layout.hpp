#ifndef GR4_PRESENT_LAYOUT_HPP
#define GR4_PRESENT_LAYOUT_HPP

#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * A named area of a slide layout.
 *
 * Where the areas came from is not this type's business: an author draws them in Inkscape and gives the boxes ids,
 * or names them as fractions of a photograph, and a grid will generate them. Bounds are in the layout's own units,
 * so a consumer scales them with whatever it drew.
 */
struct Area {
    std::string id;
    std::string kind;       // what belongs in the area: markdown, image, svg, gr4; empty when it is only an anchor
    int         step   = 0; // shared with the document's own reveal counter for this section
    float       x      = 0.0f;
    float       y      = 0.0f;
    float       width  = 0.0f;
    float       height = 0.0f;

    bool operator==(const Area&) const = default;
};

struct Layout {
    float             width  = 0.0f; // the layout's own size, in its own units
    float             height = 0.0f;
    std::vector<Area> areas;

    /// generated to fit the viewport rather than drawn, so it has no proportions of its own to letterbox into and
    /// its type is sized from the screen instead of from the drawing
    bool generated = false;

    [[nodiscard]] const Area* find(std::string_view id) const noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_LAYOUT_HPP
