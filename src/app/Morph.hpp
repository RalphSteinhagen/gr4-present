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

/**
 * The positions of `transition: morph` at `eased` (0 to 1), both slides drawn into `vertices`.
 *
 * A piece with the same key on both slides is mapped from its own box to the box between the two. Code lines are
 * matched first: a line both slides show glides from its place to its new one, and is then left out of its block's
 * move, so a block's panel and its unchanged lines travel separately; a changed line moves with its block and fades
 * with it. Colours are left to the caller.
 */
void morphPositions(std::span<ImDrawVert> vertices, std::span<const DrawnPiece> leaving, std::span<const DrawnPiece> arriving, float eased);

} // namespace gr::present

#endif // GR4_PRESENT_MORPH_HPP
