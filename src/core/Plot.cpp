#include <gr4-present/Plot.hpp>

#include <gr4-present/Number.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>

#include <gnuradio-4.0/TriggerMatcher.hpp> // trim

namespace gr::present {

namespace {

using gr::trigger::detail::trim;

[[nodiscard]] bool isTrue(std::string_view value) noexcept { return value == "true" || value == "yes" || value == "1" || value == "on"; }

[[nodiscard]] std::vector<std::string> splitOn(std::string_view text, char separator) {
    std::vector<std::string> parts;
    for (const auto part : text | std::views::split(separator)) {
        parts.emplace_back(trim(std::string_view{part}));
    }
    return parts;
}

[[nodiscard]] PlotKind kindOf(std::string_view name) noexcept {
    if (name == "scatter") {
        return PlotKind::scatter;
    }
    if (name == "bar") {
        return PlotKind::bar;
    }
    if (name == "stairs") {
        return PlotKind::stairs;
    }
    return PlotKind::line;
}

} // namespace

std::pair<std::string_view, std::string_view> fieldOf(std::string_view line) noexcept {
    line = trim(line);
    if (line.starts_with('#')) {
        return {};
    }
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        return {};
    }
    std::string_view value = line.substr(colon + 1UZ);
    // a comment may follow a value, but only after whitespace, so a URL or a ratio keeps its hash
    if (const auto hash = value.find(" #"); hash != std::string_view::npos) {
        value = value.substr(0UZ, hash);
    }
    return {trim(line.substr(0UZ, colon)), trim(value)};
}

Plot parsePlot(std::span<const std::string> lines) {
    Plot        plot;
    std::size_t index = 0UZ;
    for (; index < lines.size(); ++index) {
        const std::string_view line = trim(lines[index]);
        if (line == "---") {
            ++index;
            break;
        }
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        const auto [key, value] = fieldOf(line);
        if (key == "type") {
            plot.kind = kindOf(value);
        } else if (key == "source") {
            plot.source = value;
        } else if (key == "x") {
            plot.xColumn = value;
        } else if (key == "y") {
            plot.yColumns = splitOn(value, ',');
        } else if (key == "y2") {
            plot.y2Columns = splitOn(value, ',');
        } else if (key == "y2label") {
            plot.y2Label = value;
        } else if (key == "logy2") {
            plot.logY2 = isTrue(value);
        } else if (key == "xlabel") {
            plot.xLabel = value;
        } else if (key == "ylabel") {
            plot.yLabel = value;
        } else if (key == "title") {
            plot.title = value;
        } else if (key == "logx") {
            plot.logX = isTrue(value);
        } else if (key == "logy") {
            plot.logY = isTrue(value);
        } else if (key == "stacked") {
            plot.stacked = isTrue(value);
        } else if (key.empty()) {
            plot.problem = "a plot header needs `key: value` lines, and `" + std::string{line} + "` is neither";
            return plot;
        } else {
            plot.problem = "unknown plot field `" + std::string{key} + "`";
            return plot;
        }
    }

    std::string rows;
    for (; index < lines.size(); ++index) {
        rows.append(lines[index]).push_back('\n');
    }
    if (!trim(rows).empty()) {
        readCsv(plot, rows);
    } else if (plot.source.empty()) {
        plot.problem = "a plot needs either rows of its own or a `source:`";
    }
    return plot;
}

void readCsv(Plot& plot, std::string_view csv) {
    std::vector<std::string_view> lines;
    for (const auto line : csv | std::views::split('\n')) {
        std::string_view text{line};
        if (text.ends_with('\r')) {
            text.remove_suffix(1UZ);
        }
        if (!trim(text).empty()) {
            lines.push_back(text);
        }
    }
    if (lines.size() < 2UZ) {
        plot.problem = "a plot needs a header row and at least one row of data";
        return;
    }

    const std::vector<std::string> names = splitOn(lines.front(), ',');

    // the x column is the one named, else the first; every other column is a series unless `y:` chose some
    std::size_t xAt = 0UZ;
    if (!plot.xColumn.empty()) {
        const auto found = std::ranges::find(names, plot.xColumn);
        if (found == names.end()) {
            plot.problem = "no column named `" + plot.xColumn + "`";
            return;
        }
        xAt = static_cast<std::size_t>(found - names.begin());
    }

    std::vector<std::size_t> chosen;
    std::vector<bool>        onRight;
    const auto               columnOf = [&](const std::string& wanted) -> std::optional<std::size_t> {
        const auto found = std::ranges::find(names, wanted);
        if (found == names.end()) {
            plot.problem = "no column named `" + wanted + "`";
            return std::nullopt;
        }
        return static_cast<std::size_t>(found - names.begin());
    };
    std::vector<std::size_t> right;
    for (const std::string& wanted : plot.y2Columns) {
        const auto column = columnOf(wanted);
        if (!column.has_value()) {
            return;
        }
        right.push_back(*column);
    }
    if (plot.yColumns.empty()) {
        for (std::size_t column = 0UZ; column < names.size(); ++column) {
            if (column != xAt && std::ranges::find(right, column) == right.end()) {
                chosen.push_back(column);
            }
        }
    } else {
        for (const std::string& wanted : plot.yColumns) {
            const auto column = columnOf(wanted);
            if (!column.has_value()) {
                return;
            }
            chosen.push_back(*column);
        }
    }
    onRight.assign(chosen.size(), false);
    chosen.insert(chosen.end(), right.begin(), right.end());
    onRight.resize(chosen.size(), true);
    if (chosen.empty()) {
        plot.problem = "a plot needs at least one column besides its x";
        return;
    }

    plot.x.clear();
    plot.series.clear();
    for (std::size_t index = 0UZ; index < chosen.size(); ++index) {
        plot.series.push_back(PlotSeries{.name = names[chosen[index]], .values = {}, .right = onRight[index]});
    }

    for (const std::string_view row : lines | std::views::drop(1UZ)) {
        const std::vector<std::string> cells = splitOn(row, ',');
        const auto                     value = [&cells](std::size_t column) { return column < cells.size() ? parseNumber<double>(trim(cells[column])) : std::nullopt; };

        const std::optional<double> here = value(xAt);
        if (!here.has_value()) {
            plot.problem = "row `" + std::string{trim(row)} + "` has no number in its x column";
            return;
        }
        plot.x.push_back(*here);
        for (std::size_t series = 0UZ; series < chosen.size(); ++series) {
            // a gap is a hole in the line rather than a zero, which would be a measurement that never happened
            plot.series[series].values.push_back(value(chosen[series]).value_or(std::numeric_limits<double>::quiet_NaN()));
        }
    }
}

} // namespace gr::present
