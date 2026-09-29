#include "PlotView.hpp"

#include "Canvas.hpp"
#include "Fonts.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace gr::present {

namespace {

constexpr int   kTargetTicks   = 6;
constexpr float kTickLength    = 0.35f; // of a line height
constexpr float kMarkerRadius  = 0.3f;
constexpr float kBarGapShare   = 0.25f; // of the space one bar has to itself
constexpr float kLegendPadding = 0.4f;

/// Heckbert's rule: the nearest of 1, 2, 5 or 10 times a power of ten, so a tick lands on a number a reader expects
[[nodiscard]] double niceNumber(double range, bool round) noexcept {
    const double exponent = std::floor(std::log10(range));
    const double fraction = range / std::pow(10.0, exponent);
    const double nice     = round ? (fraction < 1.5 ? 1.0 : fraction < 3.0 ? 2.0 : fraction < 7.0 ? 5.0 : 10.0) : (fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0);
    return nice * std::pow(10.0, exponent);
}

struct Axis {
    double low         = 0.0;
    double high        = 1.0;
    double step        = 1.0;
    bool   logarithmic = false;

    /// 0 at the low end, 1 at the high end
    [[nodiscard]] double project(double value) const noexcept {
        const double span = high - low;
        if (span <= 0.0) {
            return 0.5;
        }
        return ((logarithmic ? std::log10(value) : value) - low) / span;
    }
};

[[nodiscard]] Axis axisFor(double low, double high, bool logarithmic) noexcept {
    if (logarithmic) {
        // decades, so every tick is a power of ten and the labels stay short
        const double bottom = std::floor(std::log10(low > 0.0 ? low : 1.0));
        const double top    = std::ceil(std::log10(high > 0.0 ? high : 10.0));
        return Axis{.low = bottom, .high = top > bottom ? top : bottom + 1.0, .step = 1.0, .logarithmic = true};
    }
    if (!(high > low)) { // a flat series still needs a frame to sit in
        const double pad = std::abs(low) > 0.0 ? std::abs(low) * 0.1 : 1.0;
        low -= pad;
        high += pad;
    }
    const double step = niceNumber((high - low) / (kTargetTicks - 1), true);
    return Axis{.low = std::floor(low / step) * step, .high = std::ceil(high / step) * step, .step = step, .logarithmic = false};
}

[[nodiscard]] std::string tickLabel(const Axis& axis, double tick) {
    if (axis.logarithmic) {
        return std::format("{:g}", std::pow(10.0, tick));
    }
    const int decimals = std::clamp(static_cast<int>(-std::floor(std::log10(axis.step))), 0, 6);
    return std::format("{:.{}f}", tick, decimals);
}

[[nodiscard]] bool finite(double value) noexcept { return std::isfinite(value); }

} // namespace

void drawPlot(Canvas canvas, const Theme& theme, const Plot& plot, const Rectangle& box, float size) {
    ImFont* const font       = ImGui::GetFont();
    const float   lineHeight = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "M").y;

    if (!plot.drawable()) {
        if (canvas) {
            canvas.rect(ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height}, dimmed(theme.text, 0.35f), 4.0f);
            const std::string message = plot.problem.empty() ? std::string{"this plot has no data"} : plot.problem;
            canvas.text(font, size, ImVec2{box.x + lineHeight * 0.5f, box.y + lineHeight * 0.5f}, dimmed(theme.text, 0.7f), message.c_str());
        }
        return;
    }

    double lowX   = std::numeric_limits<double>::max();
    double highX  = std::numeric_limits<double>::lowest();
    double lowY   = std::numeric_limits<double>::max();
    double highY  = std::numeric_limits<double>::lowest();
    double lowY2  = std::numeric_limits<double>::max(); // the second axis, on the right
    double highY2 = std::numeric_limits<double>::lowest();
    for (std::size_t point = 0UZ; point < plot.x.size(); ++point) {
        const double here = plot.x[point];
        if (!finite(here) || (plot.logX && here <= 0.0)) {
            continue;
        }
        lowX  = std::min(lowX, here);
        highX = std::max(highX, here);
        for (const PlotSeries& series : plot.series) {
            const double value = series.values[point];
            if (!finite(value) || ((series.right ? plot.logY2 : plot.logY) && value <= 0.0)) {
                continue;
            }
            (series.right ? lowY2 : lowY)   = std::min(series.right ? lowY2 : lowY, value);
            (series.right ? highY2 : highY) = std::max(series.right ? highY2 : highY, value);
        }
    }
    const bool hasRight = lowY2 <= highY2;
    if (lowX > highX || (lowY > highY && !hasRight)) {
        return;
    }
    if (lowY > highY) { // every series on the right: the left axis shows the same range rather than none
        lowY  = lowY2;
        highY = highY2;
    }
    // a bar starts at zero, or the bars all float above the axis and their heights mean nothing
    if (plot.kind == PlotKind::bar && !plot.logY) {
        lowY  = std::min(lowY, 0.0);
        highY = std::max(highY, 0.0);
    }
    // stacked bars reach as high as their series add up to, not as high as the tallest of them
    const bool stacked = plot.kind == PlotKind::bar && plot.stacked;
    if (stacked) {
        for (std::size_t point = 0UZ; point < plot.x.size(); ++point) {
            double above = 0.0;
            double below = 0.0;
            for (const PlotSeries& series : plot.series) {
                const double value = series.values[point];
                if (finite(value) && !series.right) {
                    (value >= 0.0 ? above : below) += value;
                }
            }
            lowY  = std::min(lowY, below);
            highY = std::max(highY, above);
        }
    }

    const Axis horizontal = axisFor(lowX, highX, plot.logX);
    const Axis vertical   = axisFor(lowY, highY, plot.logY);
    const Axis secondary  = hasRight ? axisFor(lowY2, highY2, plot.logY2) : vertical;

    float widestLabel = 0.0f;
    for (double tick = vertical.low; tick <= vertical.high + vertical.step * 0.5; tick += vertical.step) {
        const std::string label = tickLabel(vertical, tick);
        widestLabel             = std::max(widestLabel, font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x);
    }
    float widestRight = 0.0f;
    for (double tick = secondary.low; hasRight && tick <= secondary.high + secondary.step * 0.5; tick += secondary.step) {
        const std::string label = tickLabel(secondary, tick);
        widestRight             = std::max(widestRight, font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x);
    }
    const bool axisLabels = !plot.yLabel.empty() || (hasRight && !plot.y2Label.empty());

    // The y label is written above the axis rather than turned on its side, so it needs a line to itself or it
    // lands on the topmost tick label; a title above that needs another. Both together were overlapping.
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

    // grid first, so every series is drawn over it
    const ImU32 gridColour = dimmed(theme.text, kGridAlpha);
    const ImU32 axisColour = theme.text; // as the live charts draw theirs
    for (double tick = vertical.low; tick <= vertical.high + vertical.step * 0.5; tick += vertical.step) {
        const float y = atY(vertical.logarithmic ? std::pow(10.0, tick) : tick);
        canvas.line(ImVec2{left, y}, ImVec2{rightX, y}, gridColour);
        const std::string label = tickLabel(vertical, tick);
        const float       width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x;
        canvas.text(font, size, ImVec2{left - width - lineHeight * 0.35f, y - lineHeight * 0.5f}, axisColour, label.c_str());
    }
    for (double tick = horizontal.low; tick <= horizontal.high + horizontal.step * 0.5; tick += horizontal.step) {
        const float x = atX(horizontal.logarithmic ? std::pow(10.0, tick) : tick);
        canvas.line(ImVec2{x, top}, ImVec2{x, bottom}, gridColour);
        canvas.line(ImVec2{x, bottom}, ImVec2{x, bottom + lineHeight * kTickLength}, axisColour);
        const std::string label = tickLabel(horizontal, tick);
        const float       width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str()).x;
        canvas.text(font, size, ImVec2{x - width * 0.5f, bottom + lineHeight * 0.45f}, axisColour, label.c_str());
    }
    canvas.line(ImVec2{left, bottom}, ImVec2{rightX, bottom}, axisColour);
    canvas.line(ImVec2{left, top}, ImVec2{left, bottom}, axisColour);
    if (hasRight) {
        canvas.line(ImVec2{rightX, top}, ImVec2{rightX, bottom}, axisColour);
        for (double tick = secondary.low; tick <= secondary.high + secondary.step * 0.5; tick += secondary.step) {
            const float       y     = atY2(secondary.logarithmic ? std::pow(10.0, tick) : tick);
            const std::string label = tickLabel(secondary, tick);
            canvas.line(ImVec2{rightX, y}, ImVec2{rightX + lineHeight * kTickLength, y}, axisColour);
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
        // above the axis rather than turned on its side: a draw list has no rotated text, and a rotated glyph run
        // assembled by hand would be blurred by the atlas' own hinting
        canvas.text(font, size, ImVec2{box.x, top - lineHeight * 1.45f}, theme.text, plot.yLabel.c_str());
    }

    canvas.list->PushClipRect(ImVec2{left, top - lineHeight * 0.2f}, ImVec2{rightX, bottom}, true);
    const std::size_t count = plot.series.size();
    const std::size_t bars  = std::max<std::size_t>(static_cast<std::size_t>(std::ranges::count(plot.series, false, &PlotSeries::right)), 1UZ); // the left series stand side by side
    // where the next stacked bar at each x starts, which is the top of everything already piled there
    std::vector<double> piled(plot.x.size(), 0.0);
    for (std::size_t index = 0UZ; index < count; ++index) {
        const PlotSeries& series = plot.series[index];
        const ImU32       colour = kSeriesColours[index % kSeriesColours.size()];
        // a bar chart's right-hand series is drawn as a line, the usual way to set a rate against counts
        const PlotKind kind    = series.right && plot.kind == PlotKind::bar ? PlotKind::line : plot.kind;
        const auto     project = [&](double value) { return series.right ? atY2(value) : atY(value); };

        std::vector<ImVec2> run; // one contiguous stretch of finite points; a gap ends it rather than bridging it
        const auto          flush = [&] {
            if (run.size() > 1UZ && (kind == PlotKind::line || kind == PlotKind::stairs)) {
                canvas.polyline(run, colour, ImDrawFlags_None, kSeriesWidth);
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
                    run.push_back(ImVec2{position.x, run.back().y}); // hold the previous level until the new x
                }
                run.push_back(position);
                break;
            case PlotKind::line: run.push_back(position); break;
            case PlotKind::scatter: canvas.filledCircle(position, lineHeight * kMarkerRadius, colour); break;
            case PlotKind::bar: {
                // Grouped, the series share the space one x has and stand side by side; stacked, each x has one
                // bar and a series is a segment of it, starting where the series before it stopped.
                const float whole = (rightX - left) / static_cast<float>(std::max(plot.x.size(), 1UZ));
                const float slot  = stacked ? whole : whole / static_cast<float>(bars);
                const float width = slot * (1.0f - kBarGapShare);
                if (stacked) {
                    const float base = atY(piled[point]);
                    const float head = atY(piled[point] + value);
                    piled[point] += value;
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
    canvas.list->PopClipRect();

    if (count > 1UZ) {
        float widest = 0.0f;
        for (const PlotSeries& series : plot.series) {
            widest = std::max(widest, font->CalcTextSizeA(size, FLT_MAX, 0.0f, (series.right ? series.name + " (right)" : series.name).c_str()).x);
        }
        const float swatch    = lineHeight * 0.5f;
        const float panelLeft = rightX - widest - swatch - lineHeight * 1.1f;
        // a series that ends high runs underneath the legend, so the legend sits on the background rather than on it
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
