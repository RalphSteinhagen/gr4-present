#ifndef GR4_PRESENT_GRID_HPP
#define GR4_PRESENT_GRID_HPP

#include "Layout.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// slide layouts written as rows of boxes, `(cell, share)` in `[rows]:weight`, e.g.
/// `[(pre-amble)], [(code_cpp), (code_python), (code_yaml)]:6, [(post)]`; unsized cells divide the remainder
struct GridCell {
    std::string name;
    float       share = 0.0f; // of the row's width; zero divides what the sized cells leave

    bool operator==(const GridCell&) const = default;
};

struct GridRow {
    std::vector<GridCell> cells;
    float                 share = 0.0f; // of the height the title and footer leave; zero is derived

    bool operator==(const GridRow&) const = default;
};

/// empty when the value cannot be read; `problems`, when given, collects what could not be read
[[nodiscard]] std::vector<GridRow> gridRowsOf(std::string_view spec, std::vector<std::string>* problems = nullptr);

inline constexpr std::string_view kDefaultGrid = "[(content)]";

/// breaks any row whose cells would be too narrow to read (three code columns at 600 px wrap mid-token) into one row
/// per cell, each keeping the row's share of the height; apply before measuring so words are measured at final width
[[nodiscard]] std::vector<GridRow> stackedWhenNarrow(std::span<const GridRow> rows, float width, float height);

/// `rows` placed in a `width` by `height` box with the slide's chrome around them; unfilled boxes collapse into their
/// row, an all-empty row into the other rows; `selfSizing` holds per-row content heights measured by the caller
[[nodiscard]] Layout gridLayoutOf(std::span<const GridRow> rows, float width, float height, std::span<const std::string> filled, std::span<const float> selfSizing = {});

/// a layout from boxes the author placed as fractions of the slide; `title` and `footer` are added unless named
[[nodiscard]] Layout declaredLayoutOf(std::span<const Area> fractions, float width, float height);

/// rows (`grid:`) over a drawn master divide `box`, its `content` box, in the drawing's units; unfilled cells collapse
/// as in `gridLayoutOf`, and each cell stands on the box's backdrop
[[nodiscard]] std::vector<Area> gridAreasIn(std::span<const GridRow> rows, const Area& box, std::span<const std::string> filled);

} // namespace gr::present

#endif // GR4_PRESENT_GRID_HPP
