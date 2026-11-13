// A region's toolbar holds its workflow's controls, and they set the graph's blocks while it runs: on `live-graph`,
// workflows/modulated-sines.grc, a carrier times a modulation plus noise, with its spectrum: A1 dragged to its bottom
// leaves sine 1, the carrier, a flat line while the rest goes on moving.
//
// Run against the native viewer under Xvfb and driven with XTest, because a browser's synthesised clicks do not reliably
// reach an ImGui widget: the viewer takes them for a click on the slide. Ground truth is the screen and what a sine of
// amplitude zero is: sine 1 is drawn in the colour its workflow gives it, 0x00C800, and spans many rows while A1 is
// 1 V and one or two once it is 0. The toolbar draws no play, pause or stop: the deck runs the graph itself.
//
// The sliders are found in the toolbar, in the workflow's order f1, A1, f2, A2, noise, as the spans of inked columns
// wider than a label or a button, each toolbar row taken whole: along one pixel row, a slider's hollow knob splits it.

#include "NativeDisplay.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <thread>

using namespace boost::ut;
using namespace std::chrono_literals;

namespace {
using gr::present::e2e::Box;
using gr::present::e2e::Pixels;
using gr::present::e2e::Screen;

constexpr Box kCharts{.x = 150, .y = 120, .width = 410, .height = 193}; // the time chart's plot on `live-graph`
constexpr Box kBand{.x = 614, .y = 528, .width = 602, .height = 70};    // where the toolbar sits on `live-graph`
constexpr Box kTitle{.x = 62, .y = 5, .width = 600, .height = 45};      // the slide's title, to tell slides apart

struct Slider {
    int from;
    int to;
    int y;
};

/// the spec: a chart has stalled when no pixel changes for two seconds. Compared at several moments within them,
/// because the sines repeat every second (12 Hz and 5 Hz), so two pictures exactly two seconds apart can match while it
/// runs
[[nodiscard]] bool changing(const Screen& screen) {
    const Pixels before = screen.pixels(kCharts, 3);
    for (const auto pause : {700ms, 600ms, 700ms}) {
        std::this_thread::sleep_for(pause);
        if (!std::ranges::equal(screen.pixels(kCharts, 3), before)) {
            return true;
        }
    }
    return false;
}

/// how many rows of the time chart hold sine 1's green, whatever its anti-aliasing
[[nodiscard]] std::size_t sineOneRows(const Screen& screen) {
    const auto green = [](std::uint32_t pixel) { return ((pixel >> 8U) & 0xFFU) > 150U && (pixel >> 16U) < 80U && (pixel & 0xFFU) < 80U; };
    return static_cast<std::size_t>(std::ranges::count_if(screen.pixels(kCharts), [&green](const auto& row) { return std::ranges::any_of(row, green); }));
}

/// the sliders across the toolbar: its rows of controls are separated by background rows, and in each the columns inked
/// on any of its rows are spans at least 50 px wide, aimed at the middle of the toolbar row, the background being each
/// pixel row's last pixel
[[nodiscard]] std::vector<Slider> slidersIn(const Screen& screen) {
    const Pixels        rows    = screen.pixels(kBand);
    const auto          inkedAt = [&rows](std::size_t row, std::size_t column) { return rows[row][column] != rows[row].back(); };
    const auto          blank   = [&rows, &inkedAt](std::size_t row) { return std::ranges::none_of(std::views::iota(0UZ, rows[row].size()), [&](std::size_t column) { return inkedAt(row, column); }); };
    std::vector<Slider> found;
    for (std::size_t index = 0UZ; index < rows.size(); ++index) {
        const std::size_t first = index;
        while (index < rows.size() && !blank(index)) {
            ++index;
        }
        if (index == first) {
            continue;
        }
        const int  middle = kBand.y + static_cast<int>(first + (index - first) / 2UZ);
        const auto inked  = [&](std::size_t column) { return std::ranges::any_of(std::views::iota(first, index), [&](std::size_t row) { return inkedAt(row, column); }); };
        int        start  = -1;
        for (std::size_t column = 0UZ; column <= rows[first].size(); ++column) {
            const int  x     = static_cast<int>(column);
            const bool inkOn = column < rows[first].size() && inked(column);
            if (inkOn && start < 0) {
                start = x;
            } else if (!inkOn && start >= 0) {
                if (x - start >= 50) {
                    found.push_back({.from = kBand.x + start, .to = kBand.x + x - 1, .y = middle});
                }
                start = -1;
            }
        }
    }
    return found;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return 2; // usage: qa_NativeToolbar <gr4-present executable>
    }
    const std::filesystem::path viewerUnderTest = std::filesystem::absolute(argv[1]);

    "the toolbar's A1 control flattens the carrier while the rest goes on"_test = [&viewerUnderTest] {
        const gr::present::e2e::VirtualDisplay display;
        expect(!display.name().empty()) << "Xvfb did not start";
        gr::present::e2e::ViewerProcess viewer{viewerUnderTest, display.name(), {"--windowed", "--view", "live-graph"}};
        std::this_thread::sleep_for(10s);
        Screen screen{display.name()};

        expect(changing(screen)) << "the charts on `live-graph` run";
        const std::vector<Slider> sliders = slidersIn(screen);
        std::string               where;
        for (const Slider& slider : sliders) {
            where += std::format("({}, {}, {}) ", slider.from, slider.to, slider.y);
        }
        expect(fatal(eq(sliders.size(), 5UZ))) << "its toolbar shows the five controls f1, A1, f2, A2 and noise, at " << where;
        const int lead = sliders[0].from - kBand.x;
        expect(le(lead, 40)) << "and no play, pause or stop before them: f1's slider starts " << lead << " px into the toolbar";
        const std::size_t spread = sineOneRows(screen);
        expect(gt(spread, 30UZ)) << "sine 1 at 1 V spans the chart, " << spread << " rows";

        const Pixels title = screen.pixels(kTitle, 4);
        screen.click(sliders[1].from + 1, sliders[1].y); // the bottom of A1's range: 0 V
        std::this_thread::sleep_for(2s);                 // the chart's history is a few seconds long, and the old samples have to scroll out
        for (auto deadline = std::chrono::steady_clock::now() + 8s; sineOneRows(screen) > 3UZ && std::chrono::steady_clock::now() < deadline;) {
            std::this_thread::sleep_for(500ms);
        }
        const std::size_t flat = sineOneRows(screen);
        expect(le(flat, 3UZ)) << "with A1 at its minimum, sine 1 is a flat line, " << flat << " rows";
        expect(changing(screen)) << "while the rest goes on moving";
        expect(std::ranges::equal(screen.pixels(kTitle, 4), title)) << "and the click stayed in the toolbar: the slide is the same";
        expect(viewer.quit()) << "the viewer quits when asked";
    };

    return 0;
}
