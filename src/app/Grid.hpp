#ifndef GR4_PRESENT_GRID_HPP
#define GR4_PRESENT_GRID_HPP

#include "Layout.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * Slide layouts written as rows of boxes, `(cell, share)` in `[rows]:weight`:
 *
 *     [(pre-amble)], [(code_cpp), (code_python), (code_yaml)]:6, [(post)]
 *
 * Unsized cells divide what the sized ones leave; an unsized row of prose takes the height its words need.
 */

/// one box of a row, before it is placed
struct GridCell {
    std::string name;
    float       share = 0.0f; // of the row's width; zero means "divide what the sized cells leave"

    bool operator==(const GridCell&) const = default;
};

struct GridRow {
    std::vector<GridCell> cells;
    float                 share = 0.0f; // of the height the title and footer leave; zero means "work it out"

    bool operator==(const GridRow&) const = default;
};

/// the rows a `grid:` value describes; empty when the value cannot be read
/// `problems`, when given, collects what could not be read: an unclosed bracket, a number that is none, a row whose
/// cells claim more than all of it
[[nodiscard]] std::vector<GridRow> gridRowsOf(std::string_view spec, std::vector<std::string>* problems = nullptr);

/// the layout a slide gets when its author named none: one box for everything, called `content`
inline constexpr std::string_view kDefaultGrid = "[(content)]";

/**
 * The same rows, with any row whose cells would be too narrow to read broken into one row per cell.
 *
 * Three columns of code on a phone are three columns of nothing: at 600 px each box is about 110 px wide and every
 * line wraps mid-token. A row that cannot give each of its cells a readable measure is therefore laid out down the
 * slide instead of across it, in source order, each cell keeping the row's own share of the height so the group
 * grows as it stacks. Applied before the rows are measured, so the words are measured at the width they will have.
 */
[[nodiscard]] std::vector<GridRow> stackedWhenNarrow(std::span<const GridRow> rows, float width, float height);

/**
 * `rows` placed in a box `width` by `height`, with the slide's chrome around them.
 *
 * `filled` names the boxes that have something in them; one nobody filled collapses and gives its share to the
 * rest of its row, and a row whose boxes are all empty gives its height to the other rows. `selfSizing` names
 * the rows that should take only the height their content needs -- a row of prose, measured by the caller,
 * because the grid knows nothing about text.
 */
[[nodiscard]] Layout gridLayoutOf(std::span<const GridRow> rows, float width, float height, std::span<const std::string> filled, std::span<const float> selfSizing = {});

/**
 * A layout from boxes the author placed themselves, as fractions of the slide.
 *
 * The escape hatch under the notation: rows of equal boxes cover what a deck usually wants and nothing else, and
 * an author who wants a box somewhere the notation cannot put it should not have to draw an SVG to get it. The
 * fractions are the same shape a `:::regions` block uses for a photograph, because it is the same idea.
 *
 * `title` and `footer` are added at their usual places unless the author named them, since they are the slide's
 * chrome rather than content and most authors will not want to place them by hand.
 */
[[nodiscard]] Layout declaredLayoutOf(std::span<const Area> fractions, float width, float height);

/**
 * Whether a section asks for two layouts at once.
 *
 * A drawing and a grid are alternatives, not layers: a master's boxes are part of the picture, and a grid is for
 * when nobody drew one. Naming both is the author contradicting themselves, and the viewer says so rather than
 * silently picking -- the drawing wins, because it is the thing on the screen.
 *
 * A raster source is not a master. It carries no boxes, so a grid over a photograph is a perfectly sensible
 * thing to ask for and is not reported.
 */
[[nodiscard]] bool contradictsMaster(std::string_view source, std::string_view grid, bool placedBoxes) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_GRID_HPP
