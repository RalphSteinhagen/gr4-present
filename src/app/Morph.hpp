#ifndef GR4_PRESENT_MORPH_HPP
#define GR4_PRESENT_MORPH_HPP

#include <imgui.h>

#include <span>
#include <string>

namespace gr::present {

/// what a slide drew of one block, or of one line of a code block, as a range of its draw list's vertices
struct DrawnPiece {
    std::string key;
    int         vertexFrom = 0;
    int         vertexTo   = 0;
    bool        line       = false; // a code line, which moves on its own when the same line is on both slides
};

/// positions for `transition: morph` at `eased` (0 to 1), both slides drawn into `vertices`; colours are the caller's
/// pieces with the same key move between their boxes; a code line on both slides moves apart from its block
void morphPositions(std::span<ImDrawVert> vertices, std::span<const DrawnPiece> leaving, std::span<const DrawnPiece> arriving, float eased);

} // namespace gr::present

#endif // GR4_PRESENT_MORPH_HPP
