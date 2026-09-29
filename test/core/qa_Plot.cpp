#include <boost/ut.hpp>

#include <gr4-present/Plot.hpp>

#include <cmath>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

using namespace gr::present;
using namespace boost::ut;

namespace {

[[nodiscard]] std::vector<std::string> linesOf(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t              start = 0UZ;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        lines.emplace_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1UZ;
    }
    return lines;
}

// Ground truth: single-pole low-pass |H(f)| = 1 / sqrt(1 + (f/fc)^2) with fc = 100, evaluated by hand in decibels
constexpr double kGainAt10   = -0.04321373783; // 20 log10(1 / sqrt(1 + 0.01))
constexpr double kGainAt100  = -3.01029995664; // the corner, exactly -10 log10(2)
constexpr double kGainAt1000 = -20.04321373783;

} // namespace

const suite<"Plot"> plotTests = [] {
    "a header field is split from its value, and a trailing comment is not part of it"_test = [] {
        expect(eq(fieldOf("type: line").first, std::string_view{"type"}));
        expect(eq(fieldOf("type: line").second, std::string_view{"line"}));
        expect(eq(fieldOf("type: line   # line | scatter | bar").second, std::string_view{"line"})) << "the comment leaked into the value";
        expect(eq(fieldOf("source: data/a#b.csv").second, std::string_view{"data/a#b.csv"})) << "a hash without whitespace before it is content";
        expect(fieldOf("# a whole-line comment").first.empty());
        expect(fieldOf("no colon here").first.empty());
    };

    "a block carries its header and its own rows"_test = [] {
        const Plot plot = parsePlot(linesOf("type: scatter\nxlabel: f / Hz\nylabel: gain / dB\nlogx: true\n---\nfrequency,gain\n10,-0.04321373783\n100,-3.01029995664\n1000,-20.04321373783"));
        expect(plot.problem.empty()) << plot.problem;
        expect(plot.kind == PlotKind::scatter);
        expect(eq(plot.xLabel, std::string{"f / Hz"}));
        expect(eq(plot.yLabel, std::string{"gain / dB"}));
        expect(plot.logX);
        expect(!plot.logY);
        expect(plot.drawable());

        expect(eq(plot.x.size(), 3UZ));
        expect(eq(plot.series.size(), 1UZ));
        expect(eq(plot.series.front().name, std::string{"gain"})) << "the series is named by its column";
        expect(std::abs(plot.x.at(1UZ) - 100.0) < 1e-9);
        expect(std::abs(plot.series.front().values.at(0UZ) - kGainAt10) < 1e-9);
        expect(std::abs(plot.series.front().values.at(1UZ) - kGainAt100) < 1e-9);
        expect(std::abs(plot.series.front().values.at(2UZ) - kGainAt1000) < 1e-9);
    };

    "the first column is x unless another is named"_test = [] {
        const Plot implicit = parsePlot(linesOf("---\nfrequency,gain,phase\n10,1,2\n100,3,4"));
        expect(implicit.problem.empty()) << implicit.problem;
        expect(eq(implicit.series.size(), 2UZ)) << "every column but x becomes a series";
        expect(eq(implicit.x.at(0UZ), 10.0));

        const Plot named = parsePlot(linesOf("x: phase\n---\nfrequency,gain,phase\n10,1,2\n100,3,4"));
        expect(named.problem.empty()) << named.problem;
        expect(eq(named.x.at(0UZ), 2.0)) << "x was taken from the named column";
        expect(eq(named.series.size(), 2UZ));
        expect(eq(named.series.front().name, std::string{"frequency"}));
    };

    "`y` chooses the series and their order"_test = [] {
        const Plot plot = parsePlot(linesOf("y: phase, gain\n---\nfrequency,gain,phase\n10,1,2\n100,3,4"));
        expect(plot.problem.empty()) << plot.problem;
        expect(eq(plot.series.size(), 2UZ));
        expect(eq(plot.series.at(0UZ).name, std::string{"phase"})) << "the order asked for is the order drawn";
        expect(eq(plot.series.at(1UZ).name, std::string{"gain"}));
        expect(eq(plot.series.at(0UZ).values.at(0UZ), 2.0));
    };

    "a `source` stands in for the rows"_test = [] {
        const Plot plot = parsePlot(linesOf("type: bar\nsource: data/bode.csv"));
        expect(plot.problem.empty()) << plot.problem;
        expect(eq(plot.source, std::string{"data/bode.csv"}));
        expect(!plot.drawable()) << "nothing can be drawn until the caller has read the file";

        Plot filled = plot;
        readCsv(filled, "frequency,gain\n10,-0.04321373783\n100,-3.01029995664\n");
        expect(filled.drawable());
        expect(eq(filled.series.front().values.size(), 2UZ));
        expect(filled.kind == PlotKind::bar) << "reading the file must not disturb the header";
    };

    "every series is as long as x, and a gap is a hole rather than a zero"_test = [] {
        const Plot plot = parsePlot(linesOf("---\nt,a,b\n0,1,2\n1,3,\n2,5,6"));
        expect(plot.problem.empty()) << plot.problem;
        expect(eq(plot.x.size(), 3UZ));
        for (const PlotSeries& series : plot.series) {
            expect(eq(series.values.size(), plot.x.size())) << series.name << " is a different length from x";
        }
        expect(std::isnan(plot.series.at(1UZ).values.at(1UZ))) << "a missing cell must not read as zero";
        expect(eq(plot.series.at(1UZ).values.at(2UZ), 6.0));
    };

    "a block that cannot be understood says so instead of drawing"_test = [] {
        expect(!parsePlot(linesOf("type: line")).problem.empty()) << "no rows and no source";
        expect(!parsePlot(linesOf("colour: red\n---\nt,a\n0,1")).problem.empty()) << "an unknown field is a mistake, not something to ignore";
        expect(!parsePlot(linesOf("x: missing\n---\nt,a\n0,1")).problem.empty()) << "x names a column that is not there";
        expect(!parsePlot(linesOf("y: missing\n---\nt,a\n0,1")).problem.empty());
        expect(!parsePlot(linesOf("---\nt,a")).problem.empty()) << "a header row with no data below it";
        expect(!parsePlot(linesOf("---\nt,a\nnot a number,1")).problem.empty()) << "x must be numeric";
        expect(!parsePlot(linesOf("---\nt\n0\n1")).problem.empty()) << "one column is x and nothing else";
    };

    "bars are beside each other unless the block asks for them piled up"_test = [] {
        expect(!parsePlot(linesOf("type: bar\n---\nt,a,b\n0,1,2")).stacked) << "grouped is the default";
        expect(parsePlot(linesOf("type: bar\nstacked: true\n---\nt,a,b\n0,1,2")).stacked);
        expect(parsePlot(linesOf("type: bar\nstacked: true\n---\nt,a,b\n0,1,2")).problem.empty());
    };

    "a y2 series is read against the right axis, and the rest stay on the left"_test = [] {
        const std::vector<std::string> lines{"x: f", "y2: phase", "y2label: phase / degrees", "logy2: false", "---", "f,gain,phase", "1,0,-1", "10,-3,-45"};
        const Plot                     plot = parsePlot(lines);
        expect(plot.problem.empty()) << plot.problem;
        expect(eq(plot.series.size(), 2UZ));
        if (plot.series.size() != 2UZ) {
            return;
        }
        expect(eq(plot.series[0].name, std::string{"gain"}) && !plot.series[0].right) << "unnamed columns default to the left axis";
        expect(eq(plot.series[1].name, std::string{"phase"}) && plot.series[1].right);
        expect(eq(plot.y2Label, std::string{"phase / degrees"}));
        expect(eq(plot.series[1].values[1], -45.0));
    };

    "a y2 column that does not exist is named"_test = [] {
        const std::vector<std::string> lines{"y2: nothing", "---", "x,a", "1,2"};
        expect(parsePlot(lines).problem.contains("nothing"));
    };

    "an unrecognised type falls back to a line rather than failing"_test = [] {
        const Plot plot = parsePlot(linesOf("type: hexbin\n---\nt,a\n0,1\n1,2"));
        expect(plot.problem.empty());
        expect(plot.kind == PlotKind::line);
    };

    // Ground truth: P. S. Heckbert, "Nice numbers for graph labels", Graphics Gems, 1990, with the fractions worked out by hand
    "a nice number is 1, 2, 5 or 10 times a power of ten, the nearest or the next one up"_test = [] {
        expect(approx(niceNumber(0.37, true), 0.5, 1e-12)) << "3.7 below 7 rounds to 5";
        expect(approx(niceNumber(1.4, true), 1.0, 1e-12)) << "1.4 below 1.5 rounds to 1";
        expect(approx(niceNumber(2.9, true), 2.0, 1e-12)) << "2.9 below 3 rounds to 2";
        expect(approx(niceNumber(86.0, true), 100.0, 1e-9)) << "8.6 rounds to 10";
        expect(approx(niceNumber(12.0, false), 20.0, 1e-9)) << "1.2 goes up to 2";
        expect(approx(niceNumber(3.0, false), 5.0, 1e-12)) << "3 goes up to 5";
        expect(approx(niceNumber(1.0, false), 1.0, 1e-12)) << "1 stays";
    };

    "a linear axis ends on nice numbers with about six ticks"_test = [] {
        const PlotAxis            axis  = axisFor(0.0, 9.3, false); // 9.3 / 5 = 1.86 rounds to a step of 2
        const std::vector<double> ticks = axis.ticks() | std::ranges::to<std::vector>();
        expect(approx(axis.low, 0.0, 1e-12) && approx(axis.high, 10.0, 1e-12) && approx(axis.step, 2.0, 1e-12));
        expect(eq(ticks.size(), 6UZ) && approx(ticks.back(), 10.0, 1e-12)) << "0, 2, 4, 6, 8, 10";
        expect(eq(tickLabel(axis, 4.0), std::string{"4"})) << "a whole step, no decimals";
    };

    "a flat series is widened by a tenth of its value either way, and by one at zero"_test = [] {
        const PlotAxis five = axisFor(5.0, 5.0, false); // 4.5..5.5, 1 / 5 = 0.2
        expect(approx(five.step, 0.2, 1e-12) && approx(five.low, 4.4, 1e-9) && approx(five.high, 5.6, 1e-9));
        expect(eq(tickLabel(five, 4.4), std::string{"4.4"})) << "one decimal for a step of 0.2";
        const PlotAxis zero = axisFor(0.0, 0.0, false); // -1..1, 2 / 5 = 0.4 rounds to 0.5
        expect(approx(zero.low, -1.0, 1e-12) && approx(zero.high, 1.0, 1e-12) && approx(zero.step, 0.5, 1e-12));
    };

    "a logarithmic axis spans whole decades of the positive part"_test = [] {
        const PlotAxis decades = axisFor(1.0, 1000.0, true);
        expect(approx(decades.low, 0.0, 1e-12) && approx(decades.high, 3.0, 1e-12) && approx(decades.step, 1.0, 1e-12));
        expect(eq(tickLabel(decades, 0.0), std::string{"1"}) && eq(tickLabel(decades, 3.0), std::string{"1000"}));
        const PlotAxis within = axisFor(2.0, 2.0, true); // inside one decade: 1..10
        expect(approx(within.low, 0.0, 1e-12) && approx(within.high, 1.0, 1e-12));
        const PlotAxis nothingPositive = axisFor(-5.0, 0.0, true); // falls back to 1..10
        expect(approx(nothingPositive.low, 0.0, 1e-12) && approx(nothingPositive.high, 1.0, 1e-12));
    };

    "a value projects to 0 at the low end, 1 at the high end, and to the middle of a zero span"_test = [] {
        expect(approx(PlotAxis{.low = 0.0, .high = 10.0, .step = 2.0, .logarithmic = false}.project(5.0), 0.5, 1e-12));
        expect(approx(PlotAxis{.low = 0.0, .high = 10.0, .step = 2.0, .logarithmic = false}.project(10.0), 1.0, 1e-12));
        expect(approx(PlotAxis{.low = 0.0, .high = 3.0, .step = 1.0, .logarithmic = true}.project(100.0), 2.0 / 3.0, 1e-12)) << "the second of three decades";
        expect(approx(PlotAxis{.low = 1.0, .high = 1.0, .step = 1.0, .logarithmic = false}.project(7.0), 0.5, 1e-12));
    };

    // Ground truth: the ranges each plot below must span, worked out by hand from its points
    "the ranges cover finite points only, and only positive values on a logarithmic axis"_test = [] {
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
        constexpr double kInf = std::numeric_limits<double>::infinity();
        Plot             plot;
        plot.x                                 = {0.0, 1.0, 2.0, kNaN};
        plot.series                            = {PlotSeries{.name = "a", .values = {1.0, -2.0, kInf, 5.0}, .right = false}};
        const std::optional<PlotRanges> linear = plotRangesOf(plot);
        expect(linear.has_value() && approx(linear->lowX, 0.0, 1e-12) && approx(linear->highX, 2.0, 1e-12)) << "the NaN x and its 5 are left out";
        expect(linear.has_value() && approx(linear->lowY, -2.0, 1e-12) && approx(linear->highY, 1.0, 1e-12)) << "the infinite value is left out";
        plot.logY                                   = true;
        plot.series                                 = {PlotSeries{.name = "a", .values = {0.0, 10.0, 100.0, 1.0}, .right = false}};
        const std::optional<PlotRanges> logarithmic = plotRangesOf(plot);
        expect(logarithmic.has_value() && approx(logarithmic->lowY, 10.0, 1e-12) && approx(logarithmic->highY, 100.0, 1e-12)) << "0 has no logarithm";
    };

    "a plot with every series on the right mirrors its range on the left"_test = [] {
        Plot plot;
        plot.x                                 = {0.0, 1.0};
        plot.series                            = {PlotSeries{.name = "b", .values = {3.0, 4.0}, .right = true}};
        const std::optional<PlotRanges> ranges = plotRangesOf(plot);
        expect(ranges.has_value() && ranges->hasRight);
        expect(ranges.has_value() && approx(ranges->lowY, 3.0, 1e-12) && approx(ranges->highY, 4.0, 1e-12) && approx(ranges->lowY2, 3.0, 1e-12) && approx(ranges->highY2, 4.0, 1e-12));
    };

    "bars reach zero, and stacked bars the sums of their positive and of their negative parts"_test = [] {
        Plot plot;
        plot.kind                                = PlotKind::bar;
        plot.x                                   = {0.0, 1.0};
        plot.series                              = {PlotSeries{.name = "a", .values = {2.0, 5.0}, .right = false}};
        const std::optional<PlotRanges> positive = plotRangesOf(plot);
        expect(positive.has_value() && approx(positive->lowY, 0.0, 1e-12) && approx(positive->highY, 5.0, 1e-12));
        plot.stacked                            = true;
        plot.series                             = {PlotSeries{.name = "a", .values = {1.0, 2.0}, .right = false}, PlotSeries{.name = "b", .values = {3.0, -4.0}, .right = false}};
        const std::optional<PlotRanges> stacked = plotRangesOf(plot); // above: 4 and 2; below: 0 and -4
        expect(stacked.has_value() && approx(stacked->lowY, -4.0, 1e-12) && approx(stacked->highY, 4.0, 1e-12));
    };

    "a plot without a drawable point has no ranges"_test = [] {
        Plot plot;
        expect(!plotRangesOf(plot).has_value()) << "no points at all";
        plot.x      = {std::numeric_limits<double>::quiet_NaN()};
        plot.series = {PlotSeries{.name = "a", .values = {1.0}, .right = false}};
        expect(!plotRangesOf(plot).has_value()) << "only a NaN x";
    };
};

int main() { return 0; }
