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

// The slide's chrome, in multiples of the body size rather than fractions of the height.
//
// A fraction of the height is not a constant shape: the same deck turned upright gave a one-line heading a band
// half as deep again as it had lying down, because the height grew and the type did not. The type is sized from
// the viewport's diagonal, so measuring the chrome in ems keeps it the same relative to the words whichever way
// up the device is -- which is what adjusting to the aspect ratio has to mean.
// Enough for a heading of two lines with air around it. The title is 36 pt against an 18 pt body, so one line is
// 36/18 = 2 ems and two are 4, the rest being the air above and below. Derived from the ladder rather than
// left at the 5.4 a 2.34 em title needed, which reserved about a fifth of a 720-pixel view for a heading that is
// usually one line, and squeezed every row beneath it.
constexpr float kTitleBandEms   = 2.6f; // one line of title and a little air
constexpr float kTitleTopEms    = 0.2f; // a quarter of the air above the title that there was, and a quarter of it below the footer
constexpr float kSubtitleEms    = 1.7f; // a line of subtitle, which is the title's size over 1.5 at the same line spacing
constexpr float kSubtitleGapEms = 0.3f; // and a small margin under it, before the first content
constexpr float kFooterDropEms  = 1.0f;
constexpr float kFooterBandEms  = 2.1f;
constexpr float kMarginEms      = 2.6f; // outside the grid
constexpr float kGutterEms      = 1.4f; // between its boxes
constexpr float kMarginShare    = 0.08f;
// Twelve ems is about twenty-five characters of prose, and it is what separates the two cases this has to tell
// apart: three code boxes across a 1280 px slide are 14.8 ems each and read perfectly well, while the same three
// on a 600 px one are 9.1 ems and wrap mid-token. Measured in ems rather than pixels because the type is sized
// from the viewport's diagonal, so a threshold in pixels would mean something different on every device.
constexpr float kMinCellEms       = 12.0f;
constexpr float kSelfSizedCeiling = 0.45f; // of the room: a row that sizes itself must not swallow the slide // but never more than this of the width, so a narrow slide keeps its room

using gr::trigger::detail::trim;

/// the `(...)` groups inside a row, as the text between each pair of brackets
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

/// `name` or `name, 0.32`; the number, when present, is the cell's share of its row
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
    GridRow              loose; // cells written without brackets, which is allowed when the grid is one row
    for (std::size_t at = 0UZ; at < text.size();) {
        if (text[at] == '[') {
            const auto close = text.find(']', at);
            if (close == std::string_view::npos) {
                report("a '[' is never closed, so the whole grid is ignored");
                return {}; // an unclosed row is a typo, and half a layout is worse than none
            }
            GridRow row;
            for (const std::string_view cell : cellsIn(text.substr(at + 1UZ, close - at - 1UZ))) {
                row.cells.push_back(cellOf(cell, problems));
            }
            // a cell's number is a fraction of its row, so together they can claim all of it and no more
            if (const float claimed = std::ranges::fold_left(row.cells | std::views::transform(&GridCell::share), 0.0f, std::plus{}); claimed > 1.0f + 1e-4f) {
                report(std::format("the cells of row {} claim {:.2f} of it, more than all of it; a cell's number is a fraction of its row, a row's `:N` its weight", rows.size() + 1UZ, claimed));
            }
            // a share after the bracket is the row's own, `[(a), (b)]:6`
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
            loose.cells.push_back(cellOf(text.substr(at + 1UZ, close - at - 1UZ), problems));
            at = close + 1UZ;
            continue;
        }
        if (text[at] != ',' && text[at] != ' ' && text[at] != '\t') {
            const auto next = text.find_first_of("[(", at);
            report(std::format("'{}' is neither a row nor a cell, and is ignored", trim(text.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at))));
            at = next == std::string_view::npos ? text.size() : next;
            continue;
        }
        ++at; // commas and the spaces between things
    }

    if (!loose.cells.empty()) {
        // brackets may be left off when the grid is one row, which is the common case and reads better without
        rows.insert(rows.begin(), std::move(loose));
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
    const float margin = std::min(body * kMarginEms, width * kMarginShare);
    const float gutter = body * kGutterEms;

    for (const GridRow& row : rows) {
        // the widths the layout would give this row's cells, by the same division it makes
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
        // Each cell becomes a row of its own, keeping the share the row stated: a row that stacks takes as many
        // shares as it has cells, so three code boxes get three rows' worth of height rather than one row's.
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
    const float margin = std::min(body * kMarginEms, width * kMarginShare);
    const float gutter = body * kGutterEms;
    const float band   = body * kTitleBandEms;
    const float footer = body * kFooterBandEms;

    // the band holds the subtitle a `<br>` starts as well: the rows below already leave it the room, and a band of the
    // title's line alone clipped it
    layout.areas.push_back(Area{.id = "title", .kind = {}, .step = 0, .x = margin, .y = body * kTitleTopEms, .width = width - 2.0f * margin, .height = band + body * kSubtitleEms});
    layout.areas.push_back(Area{.id = "footer", .kind = {}, .step = 0, .x = margin, .y = height - footer + body * kFooterDropEms, .width = width - 2.0f * margin, .height = footer * 0.7f});

    // positions are counted across the whole grid in reading order, so an unnamed box is addressed by where it is
    struct Kept {
        GridRow row;
        float   wanted = 0.0f; // the height this row asked to be, when it is one that sizes itself
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
        const float wanted = index < selfSizing.size() ? selfSizing[index] : 0.0f;
        ++index;
        if (!surviving.cells.empty()) {
            kept.push_back(Kept{.row = std::move(surviving), .wanted = row.share > 0.0f ? 0.0f : wanted});
        }
    }
    if (kept.empty()) {
        return layout; // a section with a title and nothing else, which an empty grid asks for deliberately
    }

    const float top    = body * (kTitleTopEms + kTitleBandEms + kSubtitleEms + kSubtitleGapEms);
    const float usable = height - footer - top - gutter - gutter * static_cast<float>(kept.size() - 1UZ);

    // A row that sizes itself takes the height its content needs, capped so it cannot swallow the slide. What is
    // left is divided between the rest, by their stated shares where they have them and equally where they do not.
    float taken  = 0.0f;
    float shares = 0.0f;
    for (const Kept& row : kept) {
        taken += std::min(row.wanted, usable * kSelfSizedCeiling);
        shares += row.wanted > 0.0f ? 0.0f : (row.row.share > 0.0f ? row.row.share : 1.0f);
    }
    const float spare = std::max(usable - taken, 0.0f);

    float y = top;
    for (const Kept& each : kept) {
        const float rowHeight = each.wanted > 0.0f ? std::min(each.wanted, usable * kSelfSizedCeiling) : (shares > 0.0f ? spare * (each.row.share > 0.0f ? each.row.share : 1.0f) / shares : spare);
        const float available = width - 2.0f * margin - gutter * static_cast<float>(each.row.cells.size() - 1UZ);

        // cells that stated a share take it; the rest divide what is left, equally
        float stated  = 0.0f;
        float unsized = 0.0f;
        for (const GridCell& cell : each.row.cells) {
            stated += cell.share;
            unsized += cell.share > 0.0f ? 0.0f : 1.0f;
        }
        const float loose = std::max(1.0f - stated, 0.0f);

        float x = margin;
        for (const GridCell& cell : each.row.cells) {
            const float cellWidth = available * (cell.share > 0.0f ? cell.share : (unsized > 0.0f ? loose / unsized : 0.0f));
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
    const float margin = std::min(body * kMarginEms, width * kMarginShare);
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
