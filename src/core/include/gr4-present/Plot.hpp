#ifndef GR4_PRESENT_PLOT_HPP
#define GR4_PRESENT_PLOT_HPP

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

enum class PlotKind : std::uint8_t { line, scatter, bar, stairs };

struct PlotSeries {
    std::string         name;
    std::vector<double> values;        // one per x, and always the same length as the plot's x
    bool                right = false; // drawn against the second y axis, on the right

    bool operator==(const PlotSeries&) const = default;
};

/**
 * A ```plot block: a `key: value` header, then `---`, then CSV rows; or a `source:` naming a CSV in the package
 * and no rows at all.
 *
 * The header borrows the `:::` directive's field syntax rather than inventing a second one, so an author who has
 * written a live region already knows how to write a plot.
 */
struct Plot {
    PlotKind                 kind = PlotKind::line;
    std::string              source; // package-relative CSV; empty when the rows came with the block
    std::string              xLabel;
    std::string              yLabel;
    std::string              y2Label; // the second y axis, on the right
    std::string              title;
    bool                     logX    = false;
    bool                     logY    = false;
    bool                     logY2   = false;
    bool                     stacked = false; // bars: one bar per x, the series piled on each other rather than beside
    std::string              xColumn;         // the column to take x from; empty means the first
    std::vector<std::string> yColumns;        // the series to plot, in the order given; empty means every other column
    std::vector<std::string> y2Columns;       // series against the second y axis, on the right

    std::vector<double>     x;
    std::vector<PlotSeries> series;

    std::string problem; // why the block could not be drawn, shown on the slide instead of a plot

    [[nodiscard]] bool drawable() const noexcept { return problem.empty() && !series.empty() && !x.empty(); }
};

/// splits `key: value`, dropping a ` #` comment; the key is empty when the line carries no colon
[[nodiscard]] std::pair<std::string_view, std::string_view> fieldOf(std::string_view line) noexcept;

/// the header, and the rows after `---` when the block carries them
[[nodiscard]] Plot parsePlot(std::span<const std::string> lines);

/// fills `x` and `series` from CSV text, honouring the `x:` and `y:` the header asked for
void readCsv(Plot& plot, std::string_view csv);

} // namespace gr::present

#endif // GR4_PRESENT_PLOT_HPP
