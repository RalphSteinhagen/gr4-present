#ifndef GR4_PRESENT_CHART_LEGEND_HPP
#define GR4_PRESENT_CHART_LEGEND_HPP

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace gr::present {

/// where a live region's dashboard puts the legend its charts share; `none` leaves each chart to show its own
enum class ChartLegend { bottom, top, left, right, none };

inline constexpr std::array<std::pair<ChartLegend, std::string_view>, 5> kChartLegendNames{{
    {ChartLegend::bottom, "bottom"},
    {ChartLegend::top, "top"},
    {ChartLegend::left, "left"},
    {ChartLegend::right, "right"},
    {ChartLegend::none, "none"},
}};

[[nodiscard]] constexpr std::optional<ChartLegend> parseChartLegend(std::string_view legendName) noexcept {
    const auto entry = std::ranges::find(kChartLegendNames, legendName, &std::pair<ChartLegend, std::string_view>::second);
    return entry == kChartLegendNames.cend() ? std::nullopt : std::optional{entry->first};
}

} // namespace gr::present

#endif // GR4_PRESENT_CHART_LEGEND_HPP
