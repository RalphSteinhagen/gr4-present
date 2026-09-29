#include <boost/ut.hpp>

#include <gr4-present/Plot.hpp>

#include <cmath>
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

// A single-pole low-pass, |H(f)| = 1 / sqrt(1 + (f/fc)^2) with fc = 100, evaluated by hand in decibels. The plot
// code is never asked what these should be: they are what the transfer function says.
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
};

int main() { return 0; }
