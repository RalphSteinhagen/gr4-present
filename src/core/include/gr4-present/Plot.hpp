#ifndef GR4_PRESENT_PLOT_HPP
#define GR4_PRESENT_PLOT_HPP

#include <cmath>
#include <cstdint>
#include <optional>
#include <ranges>
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

/// a ```plot block: a `key: value` header (the field syntax of a `:::` directive), then `---` and CSV rows, or a
/// `source:`
/// naming a CSV in the package and no rows
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

/// a plot axis: its ends and tick spacing in data units, or in decades when logarithmic
struct PlotAxis {
    double low         = 0.0;
    double high        = 1.0;
    double step        = 1.0;
    bool   logarithmic = false;

    /// 0 at the low end, 1 at the high end; a zero span puts every value in the middle
    [[nodiscard]] double project(double value) const noexcept {
        const double span = high - low;
        if (span <= 0.0) {
            return 0.5;
        }
        return ((logarithmic ? std::log10(value) : value) - low) / span;
    }

    /// `low`, `low + step`, … up to `high`, each computed from its index so rounding does not accumulate
    [[nodiscard]] auto ticks() const noexcept {
        const int count = static_cast<int>(std::floor((high - low) / step + 0.5)) + 1;
        return std::views::iota(0, count) | std::views::transform([low = low, step = step](int index) { return low + index * step; });
    }
};

/// what a plot's axes must cover: finite values only, positive only on a logarithmic axis; the left axis mirrors the right when every
/// series is on the right; bars reach zero, and stacked bars the sums of their positive and of their negative parts
struct PlotRanges {
    double lowX     = 0.0;
    double highX    = 0.0;
    double lowY     = 0.0;
    double highY    = 0.0;
    double lowY2    = 0.0;
    double highY2   = 0.0;
    bool   hasRight = false; // some series is drawn against the second y axis
};

/// nothing when no point of the plot can be drawn
[[nodiscard]] std::optional<PlotRanges> plotRangesOf(const Plot& plot) noexcept;

/// Heckbert's rule [Graphics Gems, 1990]: the nearest (`round`) or the next of 1, 2, 5 or 10 times a power of ten, so a tick lands on a
/// number a reader expects
[[nodiscard]] double niceNumber(double range, bool round) noexcept;

/// an axis over `low`..`high` on nice numbers with about six ticks; a flat range is widened by a tenth of its value (by 1 at zero),
/// a logarithmic one spans whole decades of its positive part
[[nodiscard]] PlotAxis axisFor(double low, double high, bool logarithmic) noexcept;

/// a tick's label: the power of ten on a logarithmic axis, else as many decimals as the step needs
[[nodiscard]] std::string tickLabel(const PlotAxis& axis, double tick);

/// splits `key: value`, dropping a ` #` comment; the key is empty when the line carries no colon
[[nodiscard]] std::pair<std::string_view, std::string_view> fieldOf(std::string_view line) noexcept;

/// the header, and the rows after `---` when the block carries them
[[nodiscard]] Plot parsePlot(std::span<const std::string> lines);

/// fills `x` and `series` from CSV text, honouring the `x:` and `y:` the header asked for
void readCsv(Plot& plot, std::string_view csv);

} // namespace gr::present

#endif // GR4_PRESENT_PLOT_HPP
