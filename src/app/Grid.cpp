#include "Grid.hpp"

#include "Fonts.hpp"
#include "SvgImage.hpp"

#include <gr4-present/Number.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <numeric>
#include <ranges>
#include <utility>

#include <gnuradio-4.0/TriggerMatcher.hpp> // trim

namespace gr::present {

namespace {

// chrome in ems, not height fractions: type follows the viewport diagonal, so chrome keeps its proportion to the words
constexpr float kTitleBandEms     = 2.6f;
constexpr float kSubtitleEms      = 1.7f; // title size over 1.5 at the same line spacing
constexpr float kSubtitleGapEms   = 0.3f;
constexpr float kFooterDropEms    = 1.0f;
constexpr float kFooterBandEms    = 2.1f;
constexpr float kMarginEms        = 2.6f;
constexpr float kGutterEms        = 1.4f;
constexpr float kMarginWidthShare = 0.08f;
// about 25 characters of prose; ems rather than pixels because the type is sized from the viewport diagonal
constexpr float kMinCellEms           = 12.0f;
constexpr float kSelfSizedHeightShare = 0.45f; // of the usable height: a self-sizing row must not swallow the slide

using gr::trigger::detail::trim;

[[nodiscard]] std::vector<std::string_view> cellsIn(std::string_view row) {
    std::vector<std::string_view> cells;
    for (std::size_t at = row.find('('); at != std::string_view::npos; at = row.find('(', at + 1UZ)) {
        const auto close = row.find(')', at);
        if (close == std::string_view::npos) {
            break;
        }
        cells.push_back(row.substr(at + 1UZ, close - at - 1UZ));
        at = close;
    }
    return cells;
}

/// `name` or `name, 0.32`, the number being the cell's share of its row
[[nodiscard]] GridCell cellOf(std::string_view text, std::vector<std::string>* problems) {
    const auto comma = text.find(',');
    if (comma == std::string_view::npos) {
        return GridCell{.name = std::string{trim(text)}, .share = 0.0f};
    }
    const std::string_view number = trim(text.substr(comma + 1UZ));
    const auto             parsed = parseNumber<float>(number);
    const bool             whole  = parsed.has_value();
    const float            share  = parsed.value_or(0.0f);
    if (problems != nullptr && !(whole && share > 0.0f && share <= 1.0f)) {
        problems->push_back(std::format("'{}' in ({}) is no fraction of the row between 0 and 1", number, trim(text)));
    }
    return GridCell{.name = std::string{trim(text.substr(0UZ, comma))}, .share = whole && share > 0.0f ? share : 0.0f};
}

} // namespace

std::vector<GridRow> gridRowsOf(std::string_view spec, std::vector<std::string>* problems) {
    const auto report = [problems](std::string problem) {
        if (problems != nullptr) {
            problems->push_back(std::move(problem));
        }
    };
    const std::string_view text = trim(spec);
    if (text.empty()) {
        return {};
    }

    std::vector<GridRow> rows;
    GridRow              unbracketed;
    for (std::size_t at = 0UZ; at < text.size();) {
        if (text[at] == '[') {
            const auto close = text.find(']', at);
            if (close == std::string_view::npos) {
                report("a '[' is never closed, so the whole grid is ignored");
                return {}; // half a layout is worse than none
            }
            GridRow row;
            for (const std::string_view cell : cellsIn(text.substr(at + 1UZ, close - at - 1UZ))) {
                row.cells.push_back(cellOf(cell, problems));
            }
            if (const float claimed = std::ranges::fold_left(row.cells | std::views::transform(&GridCell::share), 0.0f, std::plus{}); claimed > 1.0f + 1e-4f) {
                report(std::format("the cells of row {} claim {:.2f} of it, more than all of it; a cell's number is a fraction of its row, a row's `:N` its weight", rows.size() + 1UZ, claimed));
            }
            std::size_t after = text.find_first_not_of(" \t", close + 1UZ);
            if (after != std::string_view::npos && text[after] == ':') {
                const auto       end    = text.find_first_of(",[", after);
                std::string_view number = trim(text.substr(after + 1UZ, end == std::string_view::npos ? std::string_view::npos : end - after - 1UZ));
                const float      share  = parseNumber<float>(number).value_or(0.0f);
                row.share               = share > 0.0f ? share : 0.0f;
                if (row.share <= 0.0f) {
                    report(std::format("':{}' after row {} is no weight", number, rows.size() + 1UZ));
                }
                after = end;
            }
            rows.push_back(std::move(row));
            at = after == std::string_view::npos ? text.size() : after;
            continue;
        }
        if (text[at] == '(') {
            const auto close = text.find(')', at);
            if (close == std::string_view::npos) {
                report("a '(' is never closed, so the whole grid is ignored");
                return {};
            }
            unbracketed.cells.push_back(cellOf(text.substr(at + 1UZ, close - at - 1UZ), problems));
            at = close + 1UZ;
            continue;
        }
        if (text[at] != ',' && text[at] != ' ' && text[at] != '\t') {
            const auto next = text.find_first_of("[(", at);
            report(std::format("'{}' is neither a row nor a cell, and is ignored", trim(text.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at))));
            at = next == std::string_view::npos ? text.size() : next;
            continue;
        }
        ++at;
    }

    if (!unbracketed.cells.empty()) {
        rows.insert(rows.begin(), std::move(unbracketed));
    }
    return rows;
}

std::vector<GridRow> stackedWhenNarrow(std::span<const GridRow> rows, float width, float height) {
    std::vector<GridRow> stacked;
    if (width <= 0.0f || height <= 0.0f) {
        stacked.assign(rows.begin(), rows.end());
        return stacked;
    }
    const float body   = Fonts::slideBodySize(width, height);
    const float margin = std::min(body * kMarginEms, width * kMarginWidthShare);
    const float gutter = body * kGutterEms;

    for (const GridRow& row : rows) {
        const float available = width - 2.0f * margin - gutter * static_cast<float>(row.cells.size() - 1UZ);
        float       stated    = 0.0f;
        float       unsized   = 0.0f;
        for (const GridCell& cell : row.cells) {
            stated += cell.share;
            unsized += cell.share > 0.0f ? 0.0f : 1.0f;
        }
        const float loose     = std::max(1.0f - stated, 0.0f);
        float       narrowest = available;
        for (const GridCell& cell : row.cells) {
            narrowest = std::min(narrowest, available * (cell.share > 0.0f ? cell.share : (unsized > 0.0f ? loose / unsized : 0.0f)));
        }

        if (row.cells.size() < 2UZ || narrowest >= body * kMinCellEms) {
            stacked.push_back(row);
            continue;
        }
        // each cell keeps the row's share, so three stacked boxes get three rows' worth of height
        for (const GridCell& cell : row.cells) {
            stacked.push_back(GridRow{.cells = {GridCell{.name = cell.name, .share = 0.0f}}, .share = row.share});
        }
    }
    return stacked;
}

Layout gridLayoutOf(std::span<const GridRow> rows, float width, float height, std::span<const std::string> filled, std::span<const float> selfSizing) {
    Layout layout{.width = width, .height = height, .areas = {}, .generated = true};
    if (width <= 0.0f || height <= 0.0f) {
        return layout;
    }

    const float body   = Fonts::slideBodySize(width, height);
    const float margin = std::min(body * kMarginEms, width * kMarginWidthShare);
    const float gutter = body * kGutterEms;
    const float band   = body * kTitleBandEms;
    const float footer = body * kFooterBandEms;

    // the title area includes the subtitle a `<br>` starts
    layout.areas.push_back(Area{.id = "title", .kind = {}, .step = 0, .x = margin, .y = body * kTitleTopEms, .width = width - 2.0f * margin, .height = band + body * kSubtitleEms});
    layout.areas.push_back(Area{.id = "footer", .kind = {}, .step = 0, .x = margin, .y = height - footer + body * kFooterDropEms, .width = width - 2.0f * margin, .height = footer * 0.7f});

    // unnamed boxes are addressed by their reading-order position across the whole grid
    struct Kept {
        GridRow row;
        float   wantedHeight = 0.0f;
    };
    std::vector<Kept> kept;
    std::size_t       position = 0UZ;
    std::size_t       index    = 0UZ;
    for (const GridRow& row : rows) {
        GridRow surviving{.cells = {}, .share = row.share};
        for (const GridCell& cell : row.cells) {
            ++position;
            const std::string name = cell.name.empty() ? std::to_string(position) : cell.name;
            if (filled.empty() || std::ranges::find(filled, name) != filled.end()) {
                surviving.cells.push_back(GridCell{.name = name, .share = cell.share});
            }
        }
        const float wantedHeight = index < selfSizing.size() ? selfSizing[index] : 0.0f;
        ++index;
        if (!surviving.cells.empty()) {
            kept.push_back(Kept{.row = std::move(surviving), .wantedHeight = row.share > 0.0f ? 0.0f : wantedHeight});
        }
    }
    if (kept.empty()) {
        return layout; // an empty grid asks for a title and nothing else
    }

    const float top    = body * (kTitleTopEms + kTitleBandEms + kSubtitleEms + kSubtitleGapEms);
    const float usable = height - footer - top - gutter - gutter * static_cast<float>(kept.size() - 1UZ);

    // self-sizing rows take their content height, capped; the rest divide what is left by share, equally if unstated
    const auto weightOf = [](const Kept& row) {
        if (row.wantedHeight > 0.0f) {
            return 0.0f;
        }
        return row.row.share > 0.0f ? row.row.share : 1.0f;
    };
    float taken  = 0.0f;
    float shares = 0.0f;
    for (const Kept& row : kept) {
        taken += std::min(row.wantedHeight, usable * kSelfSizedHeightShare);
        shares += weightOf(row);
    }
    const float spare = std::max(usable - taken, 0.0f);

    float y = top;
    for (const Kept& each : kept) {
        const float selfSized = std::min(each.wantedHeight, usable * kSelfSizedHeightShare);
        const float divided   = shares > 0.0f ? spare * weightOf(each) / shares : spare;
        const float rowHeight = each.wantedHeight > 0.0f ? selfSized : divided;
        const float available = width - 2.0f * margin - gutter * static_cast<float>(each.row.cells.size() - 1UZ);

        float stated  = 0.0f;
        float unsized = 0.0f;
        for (const GridCell& cell : each.row.cells) {
            stated += cell.share;
            unsized += cell.share > 0.0f ? 0.0f : 1.0f;
        }
        const float loose = std::max(1.0f - stated, 0.0f);

        float x = margin;
        for (const GridCell& cell : each.row.cells) {
            const float cellWidth = available * (cell.share > 0.0f ? cell.share : loose / unsized); // never by zero: the loop above counted this cell
            layout.areas.push_back(Area{.id = cell.name, .kind = {}, .step = 0, .x = x, .y = y, .width = cellWidth, .height = rowHeight});
            x += cellWidth + gutter;
        }
        y += rowHeight + gutter;
    }
    return layout;
}

Layout declaredLayoutOf(std::span<const Area> fractions, float width, float height) {
    Layout layout{.width = width, .height = height, .areas = {}, .generated = true};
    if (width <= 0.0f || height <= 0.0f) {
        return layout;
    }
    for (const Area& fraction : fractions) {
        layout.areas.push_back(Area{.id = fraction.id, .kind = fraction.kind, .step = fraction.step, .x = fraction.x * width, .y = fraction.y * height, .width = fraction.width * width, .height = fraction.height * height});
    }

    const float body   = Fonts::slideBodySize(width, height);
    const float margin = std::min(body * kMarginEms, width * kMarginWidthShare);
    if (layout.find("title") == nullptr) {
        layout.areas.push_back(Area{.id = "title", .kind = {}, .step = 0, .x = margin, .y = body * kTitleTopEms, .width = width - 2.0f * margin, .height = body * (kTitleBandEms + kSubtitleEms)});
    }
    if (layout.find("footer") == nullptr) {
        layout.areas.push_back(Area{.id = "footer", .kind = {}, .step = 0, .x = margin, .y = height - body * kFooterBandEms + body * kFooterDropEms, .width = width - 2.0f * margin, .height = body * kFooterBandEms * 0.7f});
    }
    return layout;
}

bool contradictsMaster(std::string_view source, std::string_view grid, bool placedBoxes) noexcept { return !source.empty() && isSvgReference(source) && (!grid.empty() || placedBoxes); }

} // namespace gr::present
