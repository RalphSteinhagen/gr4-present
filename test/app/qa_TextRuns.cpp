#include <boost/ut.hpp>

#include "TextRuns.hpp"

#include <string>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] Rectangle at(float x, float y) { return Rectangle{.x = x, .y = y, .width = 40.0f, .height = 20.0f}; }

} // namespace

// Ground truth is the rule spec D states for copied text, applied by hand to runs laid out here: words as drawn, a
// wrapped line joined by a space, a new cell by a tab and a new block or code line by a line break, nothing before
// the first word and no space at the end of a line.
const suite<"TextRuns"> textRunTests = [] {
    "words drawn with their spaces copy as they read"_test = [] {
        TextRuns frame;
        frame.add("Gauss' ", at(0, 0));
        frame.add("law", at(40, 0));
        expect(eq(copiedText(frame.runs, 0UZ, 1UZ), std::string{"Gauss' law"}));
    };

    "a wrapped line joins with one space, and a new block starts a new line"_test = [] {
        TextRuns frame;
        frame.add("first ", at(0, 0));
        frame.breakWith(" ");
        frame.add("wrapped", at(0, 20));
        frame.breakWith("\n");
        frame.add("next ", at(0, 60));
        frame.add("block ", at(40, 60));
        expect(eq(copiedText(frame.runs, 0UZ, 3UZ), std::string{"first wrapped\nnext block"}));
    };

    "the strongest break asked for since the last run is the one it carries"_test = [] {
        TextRuns frame;
        frame.add("cell", at(0, 0));
        frame.breakWith(" ");
        frame.breakWith("\n");
        frame.breakWith("\t");
        frame.add("row", at(0, 20));
        expect(eq(frame.runs[1].separator, std::string{"\n"}));
        frame.breakWith("\t");
        frame.add("next cell", at(40, 20));
        expect(eq(copiedText(frame.runs, 0UZ, 2UZ), std::string{"cell\nrow\tnext cell"}));
    };

    "a selection made backwards copies the same text, and the first run takes no separator"_test = [] {
        TextRuns frame;
        frame.add("a", at(0, 0));
        frame.breakWith("\n");
        frame.add("b", at(0, 20));
        expect(eq(copiedText(frame.runs, 1UZ, 0UZ), std::string{"a\nb"}));
        expect(eq(copiedText(frame.runs, 1UZ, 1UZ), std::string{"b"})) << "one run on its own has nothing before it";
    };

    "a code line that wraps on screen copies as one line"_test = [] {
        TextRuns frame;
        frame.add("return fft(window(in), tru", at(0, 0));
        frame.add("e);", at(10, 20)); // the hanging indent: drawn below, but no break asked for
        frame.breakWith("\n");
        frame.add("}", at(0, 40));
        expect(eq(copiedText(frame.runs, 0UZ, 2UZ), std::string{"return fft(window(in), true);\n}"}));
    };

    "a code line keeps the spaces drawn as tokens of their own, indentation included"_test = [] {
        TextRuns frame;
        frame.add("    ", at(0, 0));
        frame.add("double", at(20, 0));
        frame.add(" ", at(60, 0));
        frame.add("start", at(70, 0));
        frame.breakWith("\n");
        frame.add("};", at(0, 20));
        expect(eq(copiedText(frame.runs, 0UZ, 4UZ), std::string{"    double start\n};"}));
        frame.add("", at(0, 40));
        expect(eq(frame.runs.size(), 5UZ)) << "nothing drawn is not a run";
    };

    "the run under a point, and the nearest when the point is off the text"_test = [] {
        TextRuns frame;
        frame.add("left", at(0, 0));
        frame.add("right", at(100, 0));
        expect(runAt(frame.runs, 10.0f, 10.0f) == std::optional{0UZ});
        expect(runAt(frame.runs, 70.0f, 10.0f) == std::nullopt) << "the gap between the words holds neither";
        expect(runNearest(frame.runs, 95.0f, 10.0f) == std::optional{1UZ});
        expect(runNearest(std::span<const TextRun>{}, 0.0f, 0.0f) == std::nullopt);
        expect(copiedText(std::span<const TextRun>{}, 0UZ, 3UZ).empty());
    };
};

int main() { return 0; }
