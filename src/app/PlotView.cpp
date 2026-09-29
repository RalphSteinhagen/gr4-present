#include "PlotView.hpp"

#include "Canvas.hpp"
#include "Fonts.hpp"
#include "ScopedWindow.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace gr::present {

namespace {

constexpr float kTickLengthLineHeights = 0.35f;
constexpr float kMarkerRadius          = 0.3f;
constexpr float kBarGapSlotShare       = 0.25f;
constexpr float kLegendPadding         = 0.4f;

[[nodiscard]] bool finite(double value) noexcept { return std::isfinite(value); }

} // namespace

void drawPlot(Canvas canvas, const Theme& theme, const Plot& plot, const Rectangle& box, float size) {
    ImFont* const font       = ImGui::GetFont();
    const float   lineHeight = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "M").y;

    if (!plot.drawable()) {
        if (canvas) {
            canvas.rect(lowCorner(box), highCorner(box), dimmed(theme.text, 0.35f), 4.0f);
            const std::string message = plot.problem.empty() ? std::string{"this plot has no data"} : plot.problem;
            canvas.text(font, size, ImVec2{box.x + lineHeight * 0.5f, box.y + lineHeight * 0.5f}, dimmed(theme.text, 0.7f), message.c_str());
        }
        return;
    }

    const std::optional<PlotRanges> ranges = plotRangesOf(plot);
    if (!ranges) {
        return;
    }
    const bool hasRight = ranges->hasRight;
    const bool stacked  = plot.kind == PlotKind::bar && plot.stacked;

    const PlotAxis horizontal = axisFor(ranges->lowX, ranges->highX, plot.logX);
    const PlotAxis vertical   = axisFor(ranges->lowY, ranges->highY, plot.logY);
    const PlotAxis secondary  = hasRight ? axisFor(ranges->lowY2, ranges->highY2, plot.logY2) : vertical;

    float widestLabel = 0.0f;
    for (const double tick : vertical.ticks()) {
        const std::string label = tickLabel(vertical, tick);
        widestLabel             = std::max(widestLabel, font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x);
    }
    float widestRight = 0.0f;
    if (hasRight) {
        for (const double tick : secondary.ticks()) {
            const std::string label = tickLabel(secondary, tick);
            widestRight             = std::max(widestRight, font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x);
        }
    }
    const bool axisLabels = !plot.yLabel.empty() || (hasRight && !plot.y2Label.empty());

    // the y label sits above the axis on a line of its own, with the title above that
    const float top    = box.y + lineHeight * (plot.title.empty() ? (axisLabels ? 1.9f : 0.6f) : (axisLabels ? 3.1f : 1.8f));
    const float bottom = box.y + box.height - lineHeight * (plot.xLabel.empty() ? 1.6f : 2.8f);
    const float left   = box.x + widestLabel + lineHeight * 0.8f;
    const float rightX = box.x + box.width - lineHeight * 0.6f - (hasRight ? widestRight + lineHeight * 0.8f : 0.0f);
    if (rightX <= left || bottom <= top) {
        return;
    }

    const auto atX  = [&](double value) { return left + static_cast<float>(horizontal.project(value)) * (rightX - left); };
    const auto atY  = [&](double value) { return bottom - static_cast<float>(vertical.project(value)) * (bottom - top); };
    const auto atY2 = [&](double value) { return bottom - static_cast<float>(secondary.project(value)) * (bottom - top); };

    if (!canvas) {
        return;
    }

    if (!plot.title.empty()) {
        canvas.text(font, size, ImVec2{box.x, box.y + lineHeight * 0.3f}, theme.text, plot.title.c_str());
    }

    const ImU32 gridColour = dimmed(theme.text, kGridAlpha);
    const ImU32 axisColour = theme.text;
    for (const double tick : vertical.ticks()) {
        const float y = atY(vertical.logarithmic ? std::pow(10.0, tick) : tick);
        canvas.line(ImVec2{left, y}, ImVec2{rightX, y}, gridColour);
        const std::string label = tickLabel(vertical, tick);
        const float       width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x;
        canvas.text(font, size, ImVec2{left - width - lineHeight * 0.35f, y - lineHeight * 0.5f}, axisColour, label.c_str());
    }
    for (const double tick : horizontal.ticks()) {
        const float x = atX(horizontal.logarithmic ? std::pow(10.0, tick) : tick);
        canvas.line(ImVec2{x, top}, ImVec2{x, bottom}, gridColour);
        canvas.line(ImVec2{x, bottom}, ImVec2{x, bottom + lineHeight * kTickLengthLineHeights}, axisColour);
        const std::string label = tickLabel(horizontal, tick);
        const float       width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x;
        canvas.text(font, size, ImVec2{x - width * 0.5f, bottom + lineHeight * 0.45f}, axisColour, label.c_str());
    }
    canvas.line(ImVec2{left, bottom}, ImVec2{rightX, bottom}, axisColour);
    canvas.line(ImVec2{left, top}, ImVec2{left, bottom}, axisColour);
    if (hasRight) {
        canvas.line(ImVec2{rightX, top}, ImVec2{rightX, bottom}, axisColour);
        for (const double tick : secondary.ticks()) {
            const float       y     = atY2(secondary.logarithmic ? std::pow(10.0, tick) : tick);
            const std::string label = tickLabel(secondary, tick);
            canvas.line(ImVec2{rightX, y}, ImVec2{rightX + lineHeight * kTickLengthLineHeights, y}, axisColour);
            canvas.text(font, size, ImVec2{rightX + lineHeight * 0.45f, y - lineHeight * 0.5f}, axisColour, label.c_str());
        }
        if (!plot.y2Label.empty()) {
            const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, plot.y2Label.c_str()).x;
            canvas.text(font, size, ImVec2{box.x + box.width - width, top - lineHeight * 1.45f}, theme.text, plot.y2Label.c_str());
        }
    }

    if (!plot.xLabel.empty()) {
        const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, plot.xLabel.c_str()).x;
        canvas.text(font, size, ImVec2{(left + rightX - width) * 0.5f, bottom + lineHeight * 1.5f}, theme.text, plot.xLabel.c_str());
    }
    if (!plot.yLabel.empty()) {
        // a draw list has no rotated text, and a hand-assembled rotated glyph run blurs under the atlas' hinting
        canvas.text(font, size, ImVec2{box.x, top - lineHeight * 1.45f}, theme.text, plot.yLabel.c_str());
    }

    const std::size_t count = plot.series.size();
    {
        const ScopedClipRect inPlot{canvas.list, ImVec2{left, top - lineHeight * 0.2f}, ImVec2{rightX, bottom}};
        const std::size_t    bars = std::max<std::size_t>(static_cast<std::size_t>(std::ranges::count(plot.series, false, &PlotSeries::right)), 1UZ);
        std::vector<double>  pileTop(plot.x.size(), 0.0);
        for (std::size_t index = 0UZ; index < count; ++index) {
            const PlotSeries& series = plot.series[index];
            const ImU32       colour = kSeriesColours[index % kSeriesColours.size()];
            // a bar chart draws its right-hand series as a line, to set a rate against counts
            const PlotKind kind    = series.right && plot.kind == PlotKind::bar ? PlotKind::line : plot.kind;
            const auto     project = [&](double value) { return series.right ? atY2(value) : atY(value); };

            std::vector<ImVec2> run;
            const auto          flush = [&] {
                if (run.size() > 1UZ && (kind == PlotKind::line || kind == PlotKind::stairs)) {
                    canvas.polyline(run, colour, ImDrawFlags_None, kSeriesWidthPixels);
                }
                run.clear();
            };

            for (std::size_t point = 0UZ; point < plot.x.size(); ++point) {
                const double here  = plot.x[point];
                const double value = series.values[point];
                if (!finite(here) || !finite(value) || (plot.logX && here <= 0.0) || ((series.right ? plot.logY2 : plot.logY) && value <= 0.0)) {
                    flush();
                    continue;
                }
                const ImVec2 position{atX(here), project(value)};
                switch (kind) {
                case PlotKind::stairs:
                    if (!run.empty()) {
                        run.push_back(ImVec2{position.x, run.back().y});
                    }
                    run.push_back(position);
                    break;
                case PlotKind::line: run.push_back(position); break;
                case PlotKind::scatter: canvas.filledCircle(position, lineHeight * kMarkerRadius, colour); break;
                case PlotKind::bar: {
                    const float whole = (rightX - left) / static_cast<float>(std::max(plot.x.size(), 1UZ));
                    const float slot  = stacked ? whole : whole / static_cast<float>(bars);
                    const float width = slot * (1.0f - kBarGapSlotShare);
                    if (stacked) {
                        const float base = atY(pileTop[point]);
                        const float head = atY(pileTop[point] + value);
                        pileTop[point] += value;
                        canvas.filledRect(ImVec2{position.x - width * 0.5f, std::min(base, head)}, ImVec2{position.x + width * 0.5f, std::max(base, head)}, colour);
                        break;
                    }
                    const float centre = position.x + (static_cast<float>(index) - static_cast<float>(bars - 1UZ) * 0.5f) * slot;
                    canvas.filledRect(ImVec2{centre - width * 0.5f, std::min(position.y, atY(0.0))}, ImVec2{centre + width * 0.5f, std::max(position.y, atY(0.0))}, colour);
                    break;
                }
                }
            }
            flush();
        }
    }

    if (count > 1UZ) {
        float widest = 0.0f;
        for (const PlotSeries& series : plot.series) {
            widest = std::max(widest, font->CalcTextSizeA(size, FLT_MAX, 0.0f, (series.right ? series.name + " (right)" : series.name).c_str()).x);
        }
        const float swatch    = lineHeight * 0.5f;
        const float panelLeft = rightX - widest - swatch - lineHeight * 1.1f;
        // opaque panel: series that end high run beneath the legend
        canvas.filledRect(ImVec2{panelLeft, top + lineHeight * 0.2f}, ImVec2{rightX - lineHeight * 0.2f, top + lineHeight * (kLegendPadding + static_cast<float>(count))}, dimmed(theme.background, 0.85f), 3.0f);

        float y = top + lineHeight * kLegendPadding;
        for (std::size_t index = 0UZ; index < count; ++index) {
            const std::string name = plot.series[index].right ? plot.series[index].name + " (right)" : plot.series[index].name;
            const float       x    = panelLeft + lineHeight * 0.4f;
            canvas.filledRect(ImVec2{x, y + lineHeight * 0.3f}, ImVec2{x + swatch, y + lineHeight * 0.3f + swatch * 0.5f}, kSeriesColours[index % kSeriesColours.size()]);
            canvas.text(font, size, ImVec2{x + swatch + lineHeight * 0.3f, y}, theme.text, name.c_str());
            y += lineHeight;
        }
    }
}

} // namespace gr::present
