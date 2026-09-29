#include "ImGuiTestHarness.hpp"
#include "ScreenshotDiff.hpp"

#include "DocumentView.hpp"
#include "FigureCache.hpp"
#include "Fonts.hpp"
#include "FormulaCache.hpp"
#include "Grid.hpp"
#include "NotesOverlay.hpp"
#include "QrCode.hpp"
#include "SvgImage.hpp"
#include "Theme.hpp"
#include "VideoCache.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace boost::ut;
using namespace gr::present;
using namespace gr::present::test;

namespace {

// One document exercising every block kind, so a rendering regression in any of them moves pixels in a capture.
constexpr std::string_view kSource = R"(# Frequency domain {#intro}

A **spectrum** is the *magnitude* of the `FFT` of a windowed block of samples.

- windowing reduces spectral leakage
- overlap trades latency for variance
  - Welch's method averages the segments

1. acquire
2. window
3. transform

```cpp
const auto spectrum = fft(window(samples));
```

---

:::step

:::gr4
id: spectrum
workflow: workflows/demo.grc
widget: FFT Spectrum
:::
)";

// a section far taller than the viewport, so the scale-to-fit path is what draws it
constexpr std::string_view kOverflowing = R"(# A long section {#long}

Paragraph one of a section that cannot fit in one screen at full size.

- item 1
- item 2
- item 3
- item 4
- item 5
- item 6
- item 7
- item 8
- item 9
- item 10
- item 11
- item 12
- item 13
- item 14
- item 15
- item 16
- item 17
- item 18
- item 19
- item 20
- item 21
- item 22
- item 23
- item 24

```cpp
line1();
line2();
line3();
line4();
line5();
line6();
line7();
line8();
line9();
line10();
line11();
line12();
```

Paragraph two, after the code, still within the same section.

Paragraph three, which pushes the content well past the bottom of any sensible viewport.
)";

constexpr std::string_view kRegionInArea = R"(# A region in a named area {#in-area}

The region below asks for the sidebar by name.

:::gr4
id: sidebar-spectrum
region: sidebar
workflow: workflows/demo.grc
:::
)";

constexpr std::string_view kRegionWithBars = R"(# A region with its bars {#with-bars}

The region below asks for a toolbar above its charts and a status bar in the content area.

:::gr4
id: sidebar-spectrum
region: sidebar
workflow: workflows/demo.grc
toolbar: here
status: content
:::
)";

constexpr std::string_view kLaidOut = R"(# Laid out by an SVG {#master}

This text sits in the area the layout calls `content`, and the boxes that define the areas are hidden
so they do not show through as frames.

- the master keeps its aspect ratio and is centred
- the reading column is the area, not the viewport
)";

constexpr std::string_view kVideoShown = R"(# Video {#video}

:::video
src: media/Steamboat_Willie_(1928)_by_Walt_Disney_extract.webm
:::
)";

// real files from the package: a photograph as JPEG, the same photograph as WebP, a transparent PNG and an
// animated GIF, so the decoders are exercised on formats rather than on fixtures
constexpr std::string_view kMediaShown = R"(# Pictures {#media}

![the Hubble Ultra Deep Field, as JPEG](media/hubble-deep-field.jpg)

![a galloping horse, animated](media/horse-in-motion.gif)
)";

constexpr std::string_view kCoded = R"(# Scan this {#qr}

:::qr
url: https://ralphsteinhagen.github.io/gr4-present
label: the live demo
:::

Every link in the deck, gathered:

:::references
:::
)";

// labels out of order on purpose: the markers on the slide must read 1 then 2 by first appearance, not by
// where the definitions happen to sit
constexpr std::string_view kFootnoted = R"(# Citations {#cited}

Windowing trades resolution for leakage[^harris], and the transform itself is older than the
computers that made it practical[^cooley].

[^cooley]: J. W. Cooley and J. W. Tukey, "An algorithm for the machine calculation of complex Fourier series", Math. Comput., vol. 19, no. 90, pp. 297-301, 1965.
[^harris]: F. J. Harris, "On the use of windows for harmonic analysis with the discrete Fourier transform", Proc. IEEE, vol. 66, no. 1, pp. 51-83, 1978.
)";

// the same view with the two definitions written the other way round; the slide must come out identical
constexpr std::string_view kFootnotedSwapped = R"(# Citations {#cited}

Windowing trades resolution for leakage[^harris], and the transform itself is older than the
computers that made it practical[^cooley].

[^harris]: F. J. Harris, "On the use of windows for harmonic analysis with the discrete Fourier transform", Proc. IEEE, vol. 66, no. 1, pp. 51-83, 1978.
[^cooley]: J. W. Cooley and J. W. Tukey, "An algorithm for the machine calculation of complex Fourier series", Math. Comput., vol. 19, no. 90, pp. 297-301, 1965.
)";

// a view with notes: what is inside the directive belongs to the presenter and must never reach the slide
constexpr std::string_view kNoted = R"(# Speaker notes {#noted}

Everything on this slide is for the room.

:::notes
Say this out loud: the corner frequency is 100 Hz, and the phase is -45 degrees there.
Then pause before the next slide.
:::

Nothing above mentions the corner frequency.
)";

// no code spans and no links, so brand orange appears in this capture only if a formula fell back to its source
constexpr std::string_view kMaths = R"(# Formulas {#maths}

The identity $e^{i\pi} + 1 = 0$ sits inside the line, on the same baseline as the words around it.

$$
\int_{-\infty}^{\infty} e^{-x^2} \, dx = \sqrt{\pi}
$$

A displayed formula is centred in the column and set larger than the prose.
)";

// the same single-pole response the demo package holds, cut to three decades, so the sourced path is exercised
// without the test depending on the demo deck's own files
constexpr std::string_view kSourcedCsv = "frequency,gain\n10,-0.043\n100,-3.010\n1000,-20.043\n";

constexpr std::string_view kPlotSourced = R"(# From a file {#sourced}

```plot
type: scatter
source: measured.csv
xlabel: frequency / Hz
logx: true
```
)";

// y = x and y = 10 - x over the same x: two straight lines of opposite slope, so where each colour is drawn can be
// checked against the arithmetic rather than against a picture
constexpr std::string_view kPlotted = R"(# Plots {#plots}

```plot
type: line
xlabel: x
ylabel: y
---
x,rising,falling
0,0,10
2,2,8
4,4,6
6,6,4
8,8,2
10,10,0
```
)";

// the same table twice, differing only in the delimiter row, so alignment can be shown to change what is drawn
constexpr std::string_view kTabled = R"(# Tables {#tables}

| block | rate | note |
| :--- | ---: | :---: |
| `Decimate` | 1 / 8 | drops samples after filtering |
| `FFT` | 1 | a **windowed** transform whose description is long enough to wrap inside its column |
| `Sink` | 1 | terminal |
)";

constexpr std::string_view kTabledLeft = R"(# Tables {#tables}

| block | rate | note |
| :--- | :--- | :--- |
| `Decimate` | 1 / 8 | drops samples after filtering |
| `FFT` | 1 | a **windowed** transform whose description is long enough to wrap inside its column |
| `Sink` | 1 | terminal |
)";

// one fenced block per supported language, plus a line far wider than the column so wrapping is exercised
constexpr std::string_view kHighlighted = R"(# Highlighting {#code}

```cpp
constexpr int kMax = 42; // the cap
const auto label = std::string{"spectrum"};
const auto wide = compute(alpha, beta, gamma, delta, epsilon, zeta, eta, theta, iota, kappa, lambda);
```

```python
@property
def total(self, values: list) -> int:  # sums
    return sum(values)
```

```yaml
title: A talk   # trailing
count: 12
enabled: true
```
)";

/// within `delta` on every channel: a glyph is anti-aliased, so even its core blends towards the panel behind it
[[nodiscard]] bool near(std::uint32_t pixel, std::uint32_t colour, int delta) noexcept {
    for (unsigned shift = 0U; shift < 24U; shift += 8U) {
        const int left  = static_cast<int>((pixel >> shift) & 0xFFU);
        const int right = static_cast<int>((colour >> shift) & 0xFFU);
        if (std::abs(left - right) > delta) {
            return false;
        }
    }
    return true;
}

// Two boxes of a grid, one short and one far too long for the box it is in, so that what an option does to a box
// can be seen and measured. The text is the same in both scenarios below; only the options differ.
constexpr std::string_view kShortBox = "A short paragraph, which does not come close to filling the box it is in.\n";

// four body rows, so a striped table washes two of them and leaves two plain between and after
constexpr std::string_view kStripedTable = R"(| block | rate | note |
| :--- | ---: | :--- |
| Decimate | 1 / 8 | drops samples |
| FFT | 1 | a windowed transform |
| Sink | 1 | terminal |
| Tap | 1 | a copy for the display |
)";

constexpr std::string_view kRevealedCode = R"(# Code {#code}

```cpp
int a = 1;
// @step
int b = 2;
int c = 3;
```
)";

constexpr std::string_view kSubtitled = R"(# A title<br>its sub-title {#subtitled}

A line,<br>and a second one the author asked for.
)";

// two series a thousandfold apart: on one axis the small one would be a flat line along the bottom
constexpr std::string_view kTwoAxes = R"(# Two axes {#axes}

```plot
type: line
y: small
y2: large
---
x,small,large
0,0,10000
10,10,0
```
)";

constexpr std::string_view kBoxedPlot = R"(```plot
type: line
---
x,y
0,0
10,10
```
)";

constexpr std::string_view kBoxedFormula = R"($$\int_{-\infty}^{\infty} e^{-x^2} \, dx = \sqrt{\pi}$$)";
constexpr std::string_view kLongBox      = R"(This box is given far more words than it has room for, so that a viewer
has to decide what to do about it. It can make the words smaller until they fit, which is what it does unless the
author says otherwise, or it can keep the size the author asked for and cut off what does not fit.

A second paragraph, to make sure of it, which also runs to several lines so that the box is overrun at any window
this test is likely to be run in, on a desktop and on a telephone held upright alike.

- and a list
- with several items
- so the box is comfortably overrun
- whatever the window happens to be
- and a few more
- because a box half the width of the slide
- holds a surprising number of short lines
- before it runs out of room
- so the list goes on
- for a while yet

A third paragraph, longer again, to leave no doubt that this box cannot hold what has been put into it at the size
its author asked for, and that something has to give: either the words get smaller or they are cut off.

A closing paragraph that will not be seen when the box is told not to shrink.
)";

// A table whose last column holds one word that no column of this table can be wide enough for. Breaking lines
// cannot help -- there is nowhere in the word to break -- so either that cell is set smaller or it runs off the
// slide, which is the case the precedence list calls a cell shrinking its own font.
constexpr std::string_view kTabledUnbreakable = R"(# Long words {#long}

| block | rate | symbol | note |
| :--- | ---: | :--- | :--- |
| `Decimate` | 1 / 8 | `gr::blocks::Decimate<std::complex<float>>::ResamplingIsEnabledHere` | drops samples after filtering and never before, which is a long enough note to take its own share of the width |
| `FFT` | 1 | `gr::blocks::fft::FastFourierTransform<float>` | a windowed transform, described at similar length so that both of these columns are asking for more than they can have |
)";

// far more than any slide holds even at the floor size: the body must stop at its edge and say so
const std::string kFarTooMuch = [] {
    std::string text = "# Far too much {#far}\n\n";
    for (int paragraph = 0; paragraph < 14; ++paragraph) {
        text += std::string{kLongBox} + "\n";
    }
    return text;
}();

// words set at their own size and face, beside the body they sit in
constexpr std::string_view kSpans = "# Spans {#spans}\n\nbody [double]{size=200%} [nine]{size=9pt} [mmmm]{font=mono} end\n";

// one paragraph that arrives at step 1, the way each fixture below says, frozen half-way through
[[nodiscard]] std::string arriving(std::string_view how) { return std::format("# Arrives {{#arrives}}\n\nalready here\n\n:::step {{{} dur=1}}\n\nThese words arrive on a line of their own.\n", how); }
const std::string         kArriveDone      = arriving("in=fade");
const std::string         kArriveGrow      = arriving("in=grow");
const std::string         kArriveRiseLeft  = arriving("in=rise from=left");
const std::string         kArriveWipe      = arriving("in=wipe");
const std::string         kArriveWipeAbove = arriving("in=wipe from=above");

struct Scenario {
    std::string      name;
    ColourScheme     scheme;
    int              step;
    std::string_view source      = kSource;
    bool             useMaster   = false;
    BoxOptions       options     = {};    // given to both boxes of a `useBoxes` scenario
    bool             useBoxes    = false; // a two-box grid rather than the master or the reading column
    float            opacity     = 1.0f;
    std::string      anchor      = {};    // the master element the camera frames; empty shows the whole master
    bool             useDrawing  = false; // the facility drawing, which has no box for words, so its section is captioned
    bool             withNotes   = false; // also draws the presenter panel over the slide
    double           clock       = 0.0;   // seconds an animation has been running, for a deterministic frame
    std::string_view leftBox     = {};    // what a `useBoxes` scenario puts in its left box; empty is a short paragraph
    std::string_view slideSize   = {};    // `size:` of the slide's layout; empty is the deck's body
    float            stepSeconds = -1.0f; // since the step was reached; negative has it arrived already
};

// immortal for the same reason the recorded results are: the suite below iterates it from a static destructor
const std::vector<Scenario>& kScenarios = *new std::vector<Scenario>{
    {.name = "document-dark-step0", .scheme = ColourScheme::dark, .step = 0},
    {.name = "document-light-step0", .scheme = ColourScheme::light, .step = 0},
    {.name = "document-dark-step1", .scheme = ColourScheme::dark, .step = 1},
    {.name = "document-overflow", .scheme = ColourScheme::dark, .step = 0, .source = kOverflowing},
    {.name = "document-far-too-much", .scheme = ColourScheme::dark, .step = 0, .source = kFarTooMuch},
    {.name = "document-drawing-bare", .scheme = ColourScheme::dark, .step = 0, .source = "", .anchor = "ring", .useDrawing = true},
    {.name = "document-layout", .scheme = ColourScheme::dark, .step = 0, .source = kLaidOut, .useMaster = true},
    {.name = "document-half-faded", .scheme = ColourScheme::dark, .step = 0, .source = kLaidOut, .useMaster = true, .opacity = 0.5f},
    {.name = "document-camera-title", .scheme = ColourScheme::dark, .step = 0, .source = kLaidOut, .useMaster = true, .anchor = "title"},
    {.name = "document-region-in-area", .scheme = ColourScheme::dark, .step = 0, .source = kRegionInArea, .useMaster = true},
    {.name = "document-region-bars", .scheme = ColourScheme::dark, .step = 0, .source = kRegionWithBars, .useMaster = true},
    {.name = "document-code-dark", .scheme = ColourScheme::dark, .step = 0, .source = kHighlighted},
    {.name = "document-code-light", .scheme = ColourScheme::light, .step = 0, .source = kHighlighted},
    {.name = "document-table", .scheme = ColourScheme::dark, .step = 0, .source = kTabled},
    {.name = "document-table-left", .scheme = ColourScheme::dark, .step = 0, .source = kTabledLeft},
    {.name = "document-plot", .scheme = ColourScheme::dark, .step = 0, .source = kPlotted},
    {.name = "document-plot-sourced", .scheme = ColourScheme::dark, .step = 0, .source = kPlotSourced},
    {.name = "document-maths", .scheme = ColourScheme::dark, .step = 0, .source = kMaths},
    {.name = "document-video-early", .scheme = ColourScheme::dark, .step = 0, .source = kVideoShown},
    {.name = "document-video-later", .scheme = ColourScheme::dark, .step = 0, .source = kVideoShown, .clock = 10.0},
    {.name = "document-media-early", .scheme = ColourScheme::dark, .step = 0, .source = kMediaShown},
    {.name = "document-media-later", .scheme = ColourScheme::dark, .step = 0, .source = kMediaShown, .clock = 0.45},
    {.name = "document-qr", .scheme = ColourScheme::dark, .step = 0, .source = kCoded},
    {.name = "document-footnotes", .scheme = ColourScheme::dark, .step = 0, .source = kFootnoted},
    {.name = "document-footnotes-swapped", .scheme = ColourScheme::dark, .step = 0, .source = kFootnotedSwapped},
    {.name = "document-notes-hidden", .scheme = ColourScheme::dark, .step = 0, .source = kNoted},
    {.name = "document-notes-shown", .scheme = ColourScheme::dark, .step = 0, .source = kNoted, .withNotes = true},
    {.name = "document-table-long-words", .scheme = ColourScheme::dark, .step = 0, .source = kTabledUnbreakable},
    {.name = "document-boxes-plain", .scheme = ColourScheme::dark, .step = 0, .useBoxes = true},
    {.name = "document-boxes-fixed", .scheme = ColourScheme::dark, .step = 0, .options = BoxOptions{.shrink = false, .centre = true, .striped = false}, .useBoxes = true},
    {.name = "document-boxes-bottom", .scheme = ColourScheme::dark, .step = 0, .options = BoxOptions{.bottom = true}, .useBoxes = true},
    {.name = "document-table-striped", .scheme = ColourScheme::dark, .step = 0, .options = BoxOptions{.striped = true}, .useBoxes = true, .leftBox = kStripedTable},
    {.name = "document-plot-boxed", .scheme = ColourScheme::dark, .step = 0, .useBoxes = true, .leftBox = kBoxedPlot},
    {.name = "document-formula-boxed", .scheme = ColourScheme::dark, .step = 0, .useBoxes = true, .leftBox = kBoxedFormula},
    {.name = "document-formula-left", .scheme = ColourScheme::dark, .step = 0, .options = BoxOptions{.left = true}, .useBoxes = true, .leftBox = kBoxedFormula},
    {.name = "document-plot-two-axes", .scheme = ColourScheme::dark, .step = 0, .source = kTwoAxes},
    {.name = "document-subtitle", .scheme = ColourScheme::dark, .step = 0, .source = kSubtitled},
    {.name = "document-code-step0", .scheme = ColourScheme::dark, .step = 0, .source = kRevealedCode},
    {.name = "document-code-step1", .scheme = ColourScheme::dark, .step = 1, .source = kRevealedCode},
    {.name = "document-size-below-floor", .scheme = ColourScheme::dark, .step = 0, .source = kFarTooMuch, .slideSize = "10pt"},
    {.name = "document-size-above-floor", .scheme = ColourScheme::dark, .step = 0, .source = kFarTooMuch, .slideSize = "24pt"},
    {.name = "document-spans", .scheme = ColourScheme::dark, .step = 0, .source = kSpans},
    {.name = "document-arrive-done", .scheme = ColourScheme::dark, .step = 1, .source = kArriveDone},
    {.name = "document-arrive-grow", .scheme = ColourScheme::dark, .step = 1, .source = kArriveGrow, .stepSeconds = 0.5f},
    {.name = "document-arrive-rise-left", .scheme = ColourScheme::dark, .step = 1, .source = kArriveRiseLeft, .stepSeconds = 0.5f},
    {.name = "document-arrive-wipe", .scheme = ColourScheme::dark, .step = 1, .source = kArriveWipe, .stepSeconds = 0.5f},
    {.name = "document-arrive-wipe-above", .scheme = ColourScheme::dark, .step = 1, .source = kArriveWipeAbove, .stepSeconds = 0.5f},
};

bool        gEngineSucceeded = false; // trivially destructible, so it is still readable when the suite runs
std::size_t gFormulaRasters  = 0UZ;   // likewise: how many times the formula cache had to call the engine

/**
 * What the scenarios recorded, kept alive past the end of the program on purpose.
 *
 * boost.ut runs a suite from a static destructor. A plain global here is torn down before that runs -- the order
 * depends on the standard library, and under libc++ it is torn down first -- so the assertions below would read an
 * empty container and report that nothing was ever laid out. That is exactly what happened, and only under Clang.
 * These are allocated once and never freed, which costs a few hundred bytes at exit and makes the results readable
 * whenever the suite happens to run.
 */
template<typename T, int kWhich>
[[nodiscard]] T& recorded() {
    static T* values = new T{}; // the tag keeps two containers of one type from becoming the same container
    return *values;
}

auto& gAppliedScale  = recorded<std::map<std::string, float>, 0>();
auto& gPlacedRegions = recorded<std::vector<DocumentView::PlacedRegion>, 1>();
auto& gRegionsInArea = recorded<std::vector<DocumentView::PlacedRegion>, 2>();           // what each layout settled on
auto& gMissingGlyphs = recorded<std::vector<std::string>, 3>();                          // characters the deck draws as boxes
auto& gClipped       = recorded<std::map<std::string, std::vector<std::string>>, 4>();   // boxes each scenario had to cut
auto& gLeftBox       = recorded<std::map<std::string, Rectangle>, 5>();                  // where a `useBoxes` scenario's left box was
auto& gRegionBars    = recorded<std::map<std::string, DocumentView::RegionBoxes>, 20>(); // what a region was given, by its id
auto& gBarsRegion    = recorded<std::vector<DocumentView::PlacedRegion>, 21>();          // and where it was placed
auto& gHeights       = recorded<std::map<std::string, float>, 6>();                      // `heightOf` for the cases its test names
auto& gRecordings    = recorded<std::map<std::string, PageRecording>, 7>();              // what each scenario drew, as an exported page would get it
auto& gCopied        = recorded<std::map<std::string, std::string>, 8>();                // everything a reader could select, copied
auto& gRuns          = recorded<std::map<std::string, std::vector<TextRun>>, 22>();      // the words each scenario drew, and where

/// Checks, once, that every character the shipped deck uses has a glyph in the face it will be drawn with. It runs
/// here rather than in the suite because the suite runs after the ImGui context and its atlas are gone.
void recordMissingGlyphs() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    std::ifstream     file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/talk.md", std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const Fonts&      fonts = Fonts::instance();
    if (source.empty() || fonts.face == nullptr || fonts.mono == nullptr) {
        gMissingGlyphs.emplace_back("the deck or the faces could not be read");
        return;
    }

    std::set<std::pair<unsigned int, bool>> seen; // {codepoint, monospace}, so each is demanded only of its own face
    const auto                              scan = [&seen](std::string_view text, bool monospace) {
        for (std::size_t at = 0UZ; at < text.size();) {
            unsigned int codepoint = 0U;
            const int    length    = ImTextCharFromUtf8(&codepoint, text.data() + at, text.data() + text.size());
            at += length <= 0 ? 1UZ : static_cast<std::size_t>(length);
            if (codepoint >= 0x80U) {
                seen.emplace(codepoint, monospace);
            }
        }
    };

    for (const Block& block : parseMarkdown(source).blocks) {
        for (const InlineSpan& span : block.spans) {
            scan(span.text, span.kind == InlineKind::code);
        }
        for (const std::vector<InlineSpan>& cell : block.cells) {
            for (const InlineSpan& span : cell) {
                scan(span.text, span.kind == InlineKind::code);
            }
        }
        if (block.kind == BlockKind::codeBlock) {
            for (const std::string& line : block.lines) {
                scan(line, true);
            }
        }
    }

    for (const auto& [codepoint, monospace] : seen) {
        ImFont* face = monospace ? fonts.mono : fonts.face;
        if (!face->IsGlyphInFont(static_cast<ImWchar>(codepoint))) {
            gMissingGlyphs.push_back(std::format("U+{:04X} in the {} face", codepoint, monospace ? "monospace" : "body"));
        }
    }
}

void drawScenario(const Scenario& scenario) {
    Fonts::instance().load();
    recordMissingGlyphs();

    const Theme          theme    = themeFor(scenario.scheme);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, ImGui::ColorConvertFloat4ToU32(theme.backgroundColour()));

    static std::vector<std::uint8_t> masterSource;
    static Layout                    master;
    static FigureCache               masterFigures;
    if (scenario.useMaster && masterSource.empty()) {
        std::ifstream file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/figures/master.svg", std::ios::binary);
        masterSource.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        master                 = layoutOfSvg(masterSource).value_or(Layout{});
        masterFigures.bytesFor = [](std::string_view) { return std::span<const std::uint8_t>{masterSource}; };
    }

    static std::vector<std::uint8_t> drawingSource;
    static Layout                    drawing;
    static FigureCache               drawingFigures;
    if (scenario.useDrawing && drawingSource.empty()) {
        std::ifstream file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/figures/facility.svg", std::ios::binary);
        drawingSource.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        drawing                 = layoutOfSvg(drawingSource).value_or(Layout{});
        drawingFigures.bytesFor = [](std::string_view) { return std::span<const std::uint8_t>{drawingSource}; };
    }

    // the package's own media, read straight from disk: these scenarios are about decoding real files
    static std::map<std::string, std::vector<std::uint8_t>, std::less<>> mediaBytes;
    static FigureCache                                                   mediaFigures;
    mediaFigures.bytesFor = [](std::string_view reference) {
        if (const auto known = mediaBytes.find(reference); known != mediaBytes.end()) {
            return std::span<const std::uint8_t>{known->second};
        }
        std::ifstream file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/" + std::string{reference}, std::ios::binary);
        const auto&   stored = mediaBytes.emplace(std::string{reference}, std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}).first->second;
        return std::span<const std::uint8_t>{stored};
    };
    mediaFigures.clock = scenario.clock;

    DocumentView view{.document = parseMarkdown(scenario.source), .step = scenario.step, .imageFor = {}, .placedRegions = {}};
    view.dataFor = [](std::string_view) { return kSourcedCsv; };
    static FormulaCache formulas;
    view.formulaFor = [](std::string_view latex, float pixels, bool display) { return &formulas.get(latex, pixels, display); };
    gFormulaRasters = formulas.rasterised;
    view.imageFor   = [](std::string_view reference) { return mediaFigures.get(reference, 640U); };
    static VideoCache clips;
    clips.bytesFor = mediaFigures.bytesFor;
    clips.clock    = scenario.clock;
    view.videoFor  = [](std::string_view reference, bool autoplay, bool loop, bool sound) { return clips.get(reference, autoplay, loop, sound); };
    static QrCache codes;
    view.qrFor      = [](std::string_view text, float pixels) { return &codes.get(text, pixels); };
    view.references = {{"the manual", "docs/manual.md"}, {"the live demo", "https://ralphsteinhagen.github.io/gr4-present"}};
    static Layout boxes; // outlives the draw, since the view keeps a pointer to it
    if (scenario.useBoxes) {
        const ImGuiViewport& screen = *ImGui::GetMainViewport();
        boxes                       = gridLayoutOf(gridRowsOf("[(left), (right)]"), screen.Size.x, screen.Size.y, std::array<std::string, 2UZ>{"left", "right"});
        view.layout                 = &boxes;
        view.document               = parseMarkdown("# Boxes {#boxes}\n");
        if (const Area* left = boxes.find("left"); left != nullptr) {
            gLeftBox[scenario.name] = Rectangle{.x = left->x, .y = left->y, .width = left->width, .height = left->height};
        }
        view.areaDocuments = {AreaDocument{.id = "left", .contents = parseMarkdown(std::string{scenario.leftBox.empty() ? kShortBox : scenario.leftBox}), .options = scenario.options}, //
            AreaDocument{.id = "right", .contents = parseMarkdown(std::string{kLongBox}), .options = scenario.options}};
    }
    if (scenario.useMaster) {
        view.layout         = &master;
        view.footerText     = "gr4-present \xe2\x80\xa2 a talk";
        view.opacity        = scenario.opacity;
        view.layoutBackdrop = masterFigures.get("figures/master.svg", static_cast<std::uint32_t>(ImGui::GetMainViewport()->Size.x), hiddenAt(master, scenario.step));
        if (const Area* framed = master.find(scenario.anchor); framed != nullptr) {
            view.cameraFrame = Rectangle{.x = framed->x, .y = framed->y, .width = framed->width, .height = framed->height};
        }
    }
    if (scenario.useDrawing) {
        view.layout         = &drawing;
        view.layoutBackdrop = drawingFigures.get("figures/facility.svg", static_cast<std::uint32_t>(ImGui::GetMainViewport()->Size.x), hiddenAt(drawing, 0));
        if (const Area* framed = drawing.find(scenario.anchor); framed != nullptr) {
            view.cameraFrame = Rectangle{.x = framed->x, .y = framed->y, .width = framed->width, .height = framed->height};
        }
    }
    if (scenario.name == "document-region-bars") {
        view.regionFor = [](std::string_view id, const DocumentView::RegionBoxes& given) {
            gRegionBars.insert_or_assign(std::string{id}, given);
            return false;
        };
    }
    PageRecording& recording = gRecordings[scenario.name];
    recording                = {}; // the frame drawn last is the one kept
    view.recording           = &recording;
    view.layoutSource        = scenario.useMaster ? "figures/master.svg" : (scenario.useDrawing ? "figures/facility.svg" : "");
    view.slideSize           = scenario.slideSize.empty() ? std::nullopt : parseTypeSize(scenario.slideSize);
    view.stepSeconds         = scenario.stepSeconds;
    view.draw(theme);
    gRuns[scenario.name] = view.textRuns.runs;
    if (scenario.name == "document-spans") {
        gHeights["nine points"] = Fonts::pointsToPixels(9.0f, ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y);
    }
    if (scenario.name == "document-dark-step0") {
        // measured here because measuring needs the atlas, which is gone by the time the suite runs
        const float    body    = Fonts::slideBodySize(ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y);
        const Document one     = parseMarkdown(std::string{kShortBox});
        const Document two     = parseMarkdown(std::string{kShortBox} + "\n" + std::string{kShortBox});
        gHeights["one wide"]   = view.heightOf(theme, one, 900.0f, body, 720.0f, BoxOptions{});
        gHeights["one narrow"] = view.heightOf(theme, one, 300.0f, body, 720.0f, BoxOptions{});
        gHeights["two wide"]   = view.heightOf(theme, two, 900.0f, body, 720.0f, BoxOptions{});
        gHeights["line"]       = body;
    }
    gAppliedScale[scenario.name] = view.appliedScale;
    gCopied[scenario.name]       = view.textRuns.runs.empty() ? std::string{} : copiedText(view.textRuns.runs, 0UZ, view.textRuns.runs.size() - 1UZ);
    gClipped[scenario.name].clear();
    for (const DocumentView::Overrun& overrun : view.clipped) {
        gClipped[scenario.name].push_back(overrun.id);
    }
    if (scenario.withNotes) {
        static NotesOverlay overlay;
        overlay.visible = true;
        overlay.draw(theme, notesOf(view.document), "3 / 12", "spectrum");
    }
    // each fixture keeps its own regions: one asserts placement in the prose, the other placement in a named area
    if (!view.placedRegions.empty()) {
        (scenario.name == "document-region-in-area" ? gRegionsInArea : (scenario.name == "document-region-bars" ? gBarsRegion : gPlacedRegions)) = view.placedRegions;
    }
}

} // namespace

/// the first and last column carrying anything but the background, below the row `under`
[[nodiscard]] std::pair<int, int> inkColumns(const Screenshot& shot, int under) {
    const std::uint32_t background = shot.at(2, 2);
    int                 first      = shot.width;
    int                 last       = -1;
    for (int x = 0; x < shot.width; ++x) {
        for (int y = under; y < shot.height; ++y) {
            if (shot.at(x, y) != background) {
                first = std::min(first, x);
                last  = std::max(last, x);
                break;
            }
        }
    }
    return {first, last};
}

/// the first and last row carrying anything but the background, within the columns [from, to] and below `under`
[[nodiscard]] std::pair<int, int> inkRows(const Screenshot& shot, int from, int to, int under = 0) {
    const std::uint32_t background = shot.at(2, 2); // a corner the slide never draws in
    int                 first      = shot.height;
    int                 last       = -1;
    for (int y = under; y < shot.height; ++y) {
        for (int x = std::max(0, from); x <= std::min(to, shot.width - 1); ++x) {
            if (shot.at(x, y) != background) {
                first = std::min(first, y);
                last  = std::max(last, y);
                break;
            }
        }
    }
    return {first, last};
}

const boost::ut::suite<"DocumentView"> documentViewTests = [] {
    "the ImGui test engine ran every document scenario"_test = [] { expect(gEngineSucceeded); };

    // What an exported page is built from: every scenario is drawn with a recording, and the screenshots above
    // prove the recording changes nothing on screen. The ground truth is each scenario's own Markdown.
    "a recorded slide keeps its words as text, in the faces they were set in"_test = [] {
        const PageRecording& page = gRecordings["document-layout"];
        std::string          body;
        float                titleSize   = 0.0f;
        float                bodySize    = 0.0f;
        bool                 monoContent = false;
        for (const RecordedPrimitive& primitive : page.primitives) {
            if (const auto* text = std::get_if<RecordedText>(&primitive)) {
                body += text->text + " ";
                if (text->text.contains("Laid")) {
                    titleSize = text->size;
                }
                if (text->text.contains("sits")) {
                    bodySize = text->size;
                }
                monoContent = monoContent || (text->text == "content" && text->face == RecordedFace::mono);
            }
        }
        // a line is recorded in runs, a word or a few at a time, so the words are compared with their spacing evened out
        std::string spaced;
        for (const char letter : body) {
            if (letter != ' ' || (!spaced.empty() && spaced.back() != ' ')) {
                spaced += letter;
            }
        }
        body = spaced;
        for (const std::string_view words : {"Laid out by an SVG", "the reading column is the area", "gr4-present \xe2\x80\xa2 a talk"}) {
            expect(body.contains(words)) << "missing from the recording: " << words;
        }
        expect(gt(titleSize, bodySize * 1.5f) && gt(bodySize, 0.0f)) << "the title is set larger than the prose: " << titleSize << " against " << bodySize;
        expect(monoContent) << "the code span `content` is recorded in the mono face";
    };

    "a recorded picture says what it shows rather than which texture it was"_test = [] {
        const auto images = [](std::string_view scenario) {
            std::vector<RecordedImage> found;
            for (const RecordedPrimitive& primitive : gRecordings[std::string{scenario}].primitives) {
                if (const auto* image = std::get_if<RecordedImage>(&primitive)) {
                    found.push_back(*image);
                }
            }
            return found;
        };
        const auto has = [](const std::vector<RecordedImage>& found, RecordedImageKind kind, std::string_view source) { return std::ranges::any_of(found, [&](const RecordedImage& image) { return image.kind == kind && image.source == source && image.at.width > 0.0f; }); };
        expect(has(images("document-layout"), RecordedImageKind::master, "figures/master.svg")) << "the master is recorded by its reference";
        expect(has(images("document-maths"), RecordedImageKind::formula, "e^{i\\pi} + 1 = 0")) << "an inline formula is recorded by its LaTeX";
        expect(has(images("document-maths"), RecordedImageKind::formula, "\\int_{-\\infty}^{\\infty} e^{-x^2} \\, dx = \\sqrt{\\pi}")) << "a displayed formula is recorded by its LaTeX";
        expect(has(images("document-qr"), RecordedImageKind::qrCode, "https://ralphsteinhagen.github.io/gr4-present")) << "a QR code is recorded by the address it encodes";
    };

    "a recorded plot keeps its series as lines, not as pixels"_test = [] {
        std::size_t series = 0UZ;
        std::size_t rules  = 0UZ;
        for (const RecordedPrimitive& primitive : gRecordings["document-plot"].primitives) {
            if (const auto* shape = std::get_if<RecordedShape>(&primitive)) {
                series += shape->kind == RecordedShapeKind::polyline && shape->points.size() >= 10UZ ? 1UZ : 0UZ; // five or more points
                rules += shape->kind == RecordedShapeKind::line ? 1UZ : 0UZ;
            }
        }
        expect(eq(series, 2UZ)) << "the two series, rising and falling, each one polyline";
        expect(ge(rules, 4UZ)) << "grid and axes are lines";
    };

    "a reveal step hides the blocks that come after it"_test = [] {
        DocumentView early{.document = parseMarkdown(kSource), .step = 0, .imageFor = {}, .placedRegions = {}};
        const auto&  blocks = early.document.blocks;
        const auto   hidden = std::ranges::count_if(blocks, [](const Block& block) { return block.step > 0; });
        expect(hidden > 0_i) << "the fixture must contain a staged block for this test to mean anything";
        expect(eq(early.document.stepCount(), 2));
    };

    "a region naming an area is placed there rather than in the prose"_test = [] {
        const auto sidebar = std::ranges::find(gRegionsInArea, std::string{"sidebar-spectrum"}, &DocumentView::PlacedRegion::id);
        expect(sidebar != gRegionsInArea.end()) << "the region that asked for an area was not placed";
        if (sidebar == gRegionsInArea.end()) {
            return;
        }
        // the sidebar box sits in the right-hand half of the master, which a region following the prose would not
        const float centre = (sidebar->min.x + sidebar->max.x) * 0.5f;
        expect(gt(centre, 640.0f)) << "the region did not land in the sidebar, its centre is at " << centre;
        expect(gt(sidebar->max.y - sidebar->min.y, 100.0f)) << "the region should fill the area's height";
    };

    "a region's toolbar takes a strip above its charts and its status bar the area it names"_test = [] {
        const auto given  = gRegionBars.find("sidebar-spectrum");
        const auto placed = std::ranges::find(gBarsRegion, std::string{"sidebar-spectrum"}, &DocumentView::PlacedRegion::id);
        expect(given != gRegionBars.end() && placed != gBarsRegion.end()) << "the region with bars was not drawn";
        if (given == gRegionBars.end() || placed == gBarsRegion.end()) {
            return;
        }
        const auto& [charts, toolbar, status] = given->second;
        expect(toolbar.has_value() && status.has_value()) << "both bars were asked for";
        if (!toolbar || !status) {
            return;
        }
        const ImVec2 area = placed->min;
        const auto   near = [](float left, float right) { return std::abs(left - right) < 0.5f; }; // the same edge, mapped twice
        expect(near(toolbar->y, area.y) && near(toolbar->x, area.x) && near(toolbar->width, placed->max.x - area.x)) << "the toolbar spans the top of the sidebar";
        expect(gt(toolbar->height, 0.0f) && lt(toolbar->height, 0.2f * (placed->max.y - area.y))) << "and is a thin strip of it, " << toolbar->height << " px";
        expect(near(charts.y, toolbar->y + toolbar->height) && near(charts.y + charts.height, placed->max.y)) << "the charts take the rest of the sidebar";
        expect(lt(status->x + status->width * 0.5f, area.x)) << "the status bar is in the content area, left of the sidebar, not in the sidebar";
    };

    "a live region reports the rectangle it occupies, so the binder can fill it"_test = [] {
        DocumentView view{.document = parseMarkdown(kSource), .step = 1, .imageFor = {}, .placedRegions = {}};
        // the rectangle only exists once something has laid it out, which the engine scenarios above did
        expect(!gPlacedRegions.empty()) << "no region was placed by any scenario";
        const auto region = std::ranges::find(gPlacedRegions, std::string{"spectrum"}, &DocumentView::PlacedRegion::id);
        expect(region != gPlacedRegions.end()) << "the fixture's live region was not recorded";
        if (region != gPlacedRegions.end()) {
            expect(gt(region->max.x, region->min.x)) << "the region has no width";
            expect(gt(region->max.y, region->min.y)) << "the region has no height";
        }
    };

    "a live region reports where it was placed, so the binder can find it"_test = [] {
        // the document has to outlive the pointer into it; a temporary here dangles, which libstdc++ happened to
        // tolerate and libc++ did not
        const Document document = parseMarkdown(kSource);
        expect(!document.blocks.empty());
        const Block* region = document.withId("spectrum");
        expect(region != nullptr) << "the fixture declares a live region with a stable id";
        if (region != nullptr) {
            expect(region->kind == BlockKind::directive);
            expect(eq(region->field("widget"), std::string_view{"FFT Spectrum"}));
        }
    };

    "a table cell sets its own text smaller when one word will not fit its column"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-table-long-words.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }
        // the longest symbol is far wider than any column of this table, and there is nowhere in it to break a
        // line: unless that cell is set smaller it runs past the slide, and the ink reaches the window's edge
        const auto [firstInk, lastInk] = inkColumns(*shot, shot->height / 4);
        expect(lt(lastInk, shot->width - 20)) << "the table's ink reaches column " << lastInk << " of " << shot->width;
        expect(gt(firstInk, 20)) << "the table has no left margin, so this measures the wrong thing";
    };

    // Ground truth is each scenario's Markdown source above, read by hand under spec D's rules: words as written,
    // a wrapped line joined by a space, a new block, a `<br>` or a code line by a line break, a table cell by a tab,
    // and a formula as its LaTeX between its dollars.
    "what a reader copies from a slide is the text it was written as"_test = [] {
        const auto copies = [](std::string_view scenario, std::string_view expected) {
            const std::string& copied = gCopied[std::string{scenario}];
            expect(copied.contains(expected)) << scenario << ": expected\n" << expected << "\nin\n" << copied;
        };
        copies("document-maths", "Formulas\nThe identity $e^{i\\pi} + 1 = 0$ sits inside the line, on the same baseline as the words around it.\n$$\\int_{-\\infty}^{\\infty} e^{-x^2} \\, dx = \\sqrt{\\pi}$$\nA displayed formula is centred in the column and set larger than the prose.");
        copies("document-subtitle", "A title\nits sub-title\nA line,\nand a second one the author asked for.");
        copies("document-table", "block\trate\tnote\nDecimate\t1 / 8\tdrops samples after filtering\nFFT\t1\ta windowed transform whose description is long enough to wrap inside its column\nSink\t1\tterminal");
        // the third line is far wider than the column, so it wraps on screen; copied, it is the one line it was written as
        copies("document-code-dark", "constexpr int kMax = 42; // the cap\nconst auto label = std::string{\"spectrum\"};\nconst auto wide = compute(alpha, beta, gamma, delta, epsilon, zeta, eta, theta, iota, kappa, lambda);\n");
        copies("document-code-dark", "@property\ndef total(self, values: list) -> int:  # sums\n    return sum(values)\n");
    };

    "a box told not to shrink keeps its size, cuts what will not fit and says so"_test = [] {
        expect(gClipped["document-boxes-plain"].empty()) << "a box that may shrink was reported as clipped";
        const std::vector<std::string>& fixed = gClipped["document-boxes-fixed"];
        expect(std::ranges::find(fixed, std::string{"right"}) != fixed.end()) << "the overrun box was not reported";
        expect(std::ranges::find(fixed, std::string{"left"}) == fixed.end()) << "a box whose content fits was reported";

        // and it really was cut: the words stop at the box, rather than running past it as the unshrunk ones would
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-boxes-fixed.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }
        const int bottom = inkRows(*shot, shot->width / 2, shot->width - 1, shot->height / 4).second;
        expect(lt(bottom, static_cast<int>(static_cast<float>(shot->height) * 0.90f))) << "ink at row " << bottom << " has run past the box";
    };

    "content sits at the top of its box unless the box asks for the middle"_test = [] {
        const auto plain   = readScreenshot(Harness::captureDirectory() / "document-boxes-plain.png");
        const auto centred = readScreenshot(Harness::captureDirectory() / "document-boxes-fixed.png");
        expect(plain.has_value() && centred.has_value());
        if (!plain.has_value() || !centred.has_value()) {
            return;
        }
        // The same short paragraph in the same box, with and without `{centre}`: it must start lower with it. Read
        // below the title band, which is above both boxes and in neither.
        const int under = plain->height / 8; // below the 36 pt title, which ends well above it
        const int wasAt = inkRows(*plain, 0, plain->width / 2 - 1, under).first;
        const int nowAt = inkRows(*centred, 0, centred->width / 2 - 1, under).first;
        expect(gt(nowAt, wasAt + 20)) << "centred content starts at row " << nowAt << ", top-aligned at " << wasAt;
    };

    "a displayed formula is centred in its box, or at its left edge when the box asks"_test = [] {
        const auto centred = readScreenshot(Harness::captureDirectory() / "document-formula-boxed.png");
        const auto flush   = readScreenshot(Harness::captureDirectory() / "document-formula-left.png");
        expect(centred.has_value() && flush.has_value());
        if (!centred.has_value() || !flush.has_value()) {
            return;
        }
        // The same formula, narrower than its box, in the left one: centred, its first ink lies well inside the box;
        // at the left edge, it starts where the box's prose would. Read below the title band.
        const int under = centred->height / 8;
        const int wasAt = inkColumns(*centred, under).first;
        const int nowAt = inkColumns(*flush, under).first;
        expect(lt(nowAt, wasAt - 20)) << "the left-aligned formula starts at column " << nowAt << ", the centred one at " << wasAt;
    };

    "content sits at the foot of its box when the box asks for it"_test = [] {
        const auto plain  = readScreenshot(Harness::captureDirectory() / "document-boxes-plain.png");
        const auto footed = readScreenshot(Harness::captureDirectory() / "document-boxes-bottom.png");
        expect(plain.has_value() && footed.has_value());
        if (!plain.has_value() || !footed.has_value()) {
            return;
        }
        const int under = plain->height / 8; // below the 36 pt title, which ends well above it
        const int wasAt = inkRows(*plain, 0, plain->width / 2 - 1, under).second;
        const int nowAt = inkRows(*footed, 0, footed->width / 2 - 1, under).second;
        expect(gt(nowAt, wasAt + 20)) << "bottom-aligned content ends at row " << nowAt << ", top-aligned at " << wasAt;
    };

    "a fence's braces say how its box behaves, and an absent option changes nothing"_test = [] {
        const auto optioned = [](std::string_view fence) { return boxOptionsOf(parseMarkdown(std::string{fence} + "\nprose\n:::\n").blocks.front()); };
        expect(optioned(":::plain") == BoxOptions{}) << "a box with no braces is not the default one";
        expect(!optioned(":::a {shrink=off}").shrink);
        expect(!optioned(":::a {shrink=false}").shrink) << "`false` must say the same thing as `off`";
        expect(optioned(":::a {shrink=on}").shrink);
        expect(optioned(":::a {centre}").centre) << "a flag written on its own is on";
        expect(optioned(":::a {striped}").striped);
        expect(!optioned(":::a {centre=no}").centre);
        expect(optioned(":::plain").notes) << "a box lists the notes it cites unless told not to";
        expect(!optioned(":::a {notes=off}").notes);
        expect(optioned(":::a {notes=on}").notes);
        expect(!optioned(":::plain").bottom);
        expect(optioned(":::a {bottom}").bottom) << "a flag written on its own is on";
        expect(!optioned(":::a {bottom=no}").bottom);
        expect(optioned(":::a {bottom notes=off}") == BoxOptions{.notes = false, .bottom = true});
        expect(optioned(":::a {shrink=off centre striped}") == BoxOptions{.shrink = false, .centre = true, .striped = true});
        expect(optioned(":::a {left}").left) << "a flag written on its own is on";
        expect(!optioned(":::plain").left) << "a displayed formula is centred unless asked";
    };

    "a box states its own type size and face, and every notes option lists what it should"_test = [] {
        const auto optioned = [](std::string_view fence) { return boxOptionsOf(parseMarkdown(std::string{fence} + "\nprose\n:::\n").blocks.front()); };
        expect(optioned(":::a {size=8}").size == TypeSize{.unit = TypeSize::Unit::points, .value = 8.0f});
        expect(optioned(":::a {size=13.5pt}").size == TypeSize{.unit = TypeSize::Unit::points, .value = 13.5f}) << "pt is points, as a bare number is";
        expect(optioned(":::a {size=80%}").size == TypeSize{.unit = TypeSize::Unit::relative, .value = 0.8f}) << "a share of the slide's body";
        expect(!optioned(":::a").size.has_value()) << "no size is the slide's own";
        expect(!optioned(":::a {size=x}").size.has_value()) << "an unreadable size is no size";
        expect(eq(optioned(":::a {font=hand}").font, std::string{"hand"}));
        expect(optioned(":::a").font.empty()) << "no face is the slide's own";
        expect(optioned(":::a {notes=all}").slideNotes) << "`all` lists every note the slide cites";
        expect(optioned(":::a {notes=all}").notes) << "and is not `off`";
        expect(!optioned(":::a {notes=on}").slideNotes);
        expect(!optioned(":::a").slideNotes);
    };

    "a section that fits is drawn at full size"_test = [] { expect(eq(gAppliedScale["document-dark-step0"], 1.0f)) << "a fitting section must not be shrunk"; };

    "a section too tall for the view is scaled down rather than clipped"_test = [] {
        const float scale = gAppliedScale["document-overflow"];
        expect(lt(scale, 1.0f)) << "the overflowing fixture was not scaled, so this path is untested";
        expect(ge(scale, DocumentView{}.minimumScale)) << "scaling must stop at the floor rather than shrink without limit";
    };

    "a drawing under a caption stops at the room the title and the words leave it"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-drawing-bare.png");
        expect(shot.has_value());
        if (shot.has_value()) {
            const auto [first, last] = inkRows(*shot, 0, shot->width - 1);
            expect(ge(first, shot->height / 14)) << "the drawing reaches row " << first << " of " << shot->height << ", into the title band";
            expect(lt(last, shot->height - shot->height / 14)) << "the drawing reaches row " << last << " of " << shot->height << ", into the caption band";
        }
    };

    // Ground truth: the anchor the scenario frames, `ring` in figures/facility.svg, starts at y 240 and the circle's
    // topmost magnet at y 286, inside it, so the magnet's orange is drawn whole, clear of the room's top edge. A camera
    // kept to the window rather than to the room slid the drawing up behind the title and cut it at that edge.
    "a frame near the top of a drawing under a caption is shown whole, not slid behind the title"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-drawing-bare.png");
        expect(fatal(shot.has_value()));
        // the first row with at least `share` of its pixels as wanted
        const auto rowOf = [&shot](auto isWanted, float share) {
            for (int y = 0; y < shot->height; ++y) {
                int count = 0;
                for (int x = 0; x < shot->width; ++x) {
                    count += isWanted(shot->at(x, y)) ? 1 : 0;
                }
                if (static_cast<float>(count) >= share * static_cast<float>(shot->width)) {
                    return y;
                }
            }
            return shot->height;
        };
        const auto red      = [](std::uint32_t pixel) { return static_cast<int>(pixel & 0xFFU); };
        const auto green    = [](std::uint32_t pixel) { return static_cast<int>((pixel >> 8U) & 0xFFU); };
        const auto blue     = [](std::uint32_t pixel) { return static_cast<int>((pixel >> 16U) & 0xFFU); };
        const auto backdrop = [&](std::uint32_t pixel) { return std::abs(red(pixel) - 15) <= 3 && std::abs(green(pixel) - 15) <= 3 && std::abs(blue(pixel) - 19) <= 3; }; // the drawing's #0f0f13
        const auto orange   = [&](std::uint32_t pixel) { return red(pixel) > 180 && green(pixel) > 70 && green(pixel) < 150 && blue(pixel) < 90; };                       // its strokes, #F36F21
        const int  room     = rowOf(backdrop, 0.5f);
        const int  stroke   = rowOf(orange, 1.0f / static_cast<float>(shot->width));
        expect(gt(stroke, room + 4)) << "the topmost stroke is at row " << stroke << ", the room starts at row " << room;
    };

    "the smallest body type is 12 pt on the deck's 18 pt body"_test = [] { expect(eq(DocumentView{}.minimumScale, 12.0f / 18.0f)); };

    // Ground truth: the rule as the deck's author was given it -- the 12 pt floor bounds what auto-shrink does,
    // never a size the author set
    "a slide set below the floor is never shrunk, and what does not fit is cut off and named"_test = [] {
        expect(eq(gAppliedScale["document-size-below-floor"], 1.0f)) << "10 pt was shrunk although the author set it";
        const std::vector<std::string>& reported = gClipped["document-size-below-floor"];
        expect(std::ranges::contains(reported, std::string{"content"})) << "the overrunning body was not reported";
    };

    "a slide set above the floor shrinks to the floor and no further"_test = [] { expect(std::abs(gAppliedScale["document-size-above-floor"] - 12.0f / 24.0f) < 1e-4f) << "24 pt shrank to a scale of " << gAppliedScale["document-size-above-floor"] << " rather than to 12 pt"; };

    "a span sets its words at its own size and in its own face"_test = [] {
        const std::vector<TextRun>& runs   = gRuns["document-spans"];
        const auto                  areaOf = [&runs](std::string_view word) {
            const auto found = std::ranges::find_if(runs, [word](const TextRun& run) { return run.text.starts_with(word); });
            return found == runs.end() ? Rectangle{} : found->area;
        };
        const Rectangle body = areaOf("body");
        expect(gt(body.height, 0.0f)) << "the body's words were not drawn";
        expect(std::abs(areaOf("double").height - 2.0f * body.height) < 0.01f) << "200 % of " << body.height << " px is " << areaOf("double").height;
        expect(std::abs(areaOf("nine").height - gHeights["nine points"]) < 0.01f) << "9 pt is " << gHeights["nine points"] << " px, not " << areaOf("nine").height;
        // Liberation Mono advances every glyph by 1229 units, and ImGui sets a face so that its ascent and descent,
        // 1705 and 615 units, span the size asked for (stb_truetype's ScaleForPixelHeight)
        const auto mono = std::ranges::find_if(runs, [](const TextRun& run) { return run.text.starts_with("mmmm"); });
        expect(mono != runs.end()) << "the monospace span was not drawn";
        if (mono != runs.end()) {
            const float advances = static_cast<float>(mono->text.size());
            expect(std::abs(mono->area.width - advances * 1229.0f / (1705.0f + 615.0f) * body.height) < 1.0f) << "'" << mono->text << "' in mono is " << mono->area.width << " px wide";
        }
    };

    // Ground truth: where the paragraph is once it has arrived, measured on the screen; half-way through a 1 s reveal
    // the smoothstep has eased it half-way, so `grow` has it at 0.95 of its size about its centre, `rise from=left`
    // to the left of where it ends and on the same rows, and `wipe` cut at half its width, or from above at half
    // its height
    "half-way through, each reveal has its words half-way into place"_test = [] {
        const auto inkOf = [](std::string_view name) {
            const std::vector<TextRun>& runs = gRuns[std::string{name}];
            const auto                  word = std::ranges::find_if(runs, [](const TextRun& run) { return run.text.starts_with("These"); });
            const auto                  shot = readScreenshot(Harness::captureDirectory() / std::format("{}.png", name));
            struct Ink {
                int left = -1, right = -1, top = -1, bottom = -1;
            };
            if (word == runs.end() || !shot.has_value()) {
                return Ink{};
            }
            const int under          = static_cast<int>(word->area.y) - 4;
            const auto [left, right] = inkColumns(*shot, under);
            const auto [top, bottom] = inkRows(*shot, 0, shot->width - 1, under);
            return Ink{.left = left, .right = right, .top = top, .bottom = bottom};
        };
        const auto done = inkOf("document-arrive-done");
        expect(done.right > done.left && done.bottom > done.top) << "the arrived paragraph has no ink";
        const float doneWidth  = static_cast<float>(done.right - done.left);
        const float doneCentre = static_cast<float>(done.right + done.left) * 0.5f;

        const auto grow = inkOf("document-arrive-grow");
        expect(std::abs(static_cast<float>(grow.right - grow.left) - 0.95f * doneWidth) <= 3.0f) << "grow: " << (grow.right - grow.left) << " px wide, 0.95 of " << doneWidth;
        expect(std::abs(static_cast<float>(grow.right + grow.left) * 0.5f - doneCentre) <= 2.0f) << "grow: about its own centre";

        const auto rise = inkOf("document-arrive-rise-left");
        expect(lt(rise.left, done.left - 2)) << "rise from=left: starts left of where it ends, " << rise.left << " against " << done.left;
        expect(std::abs(rise.top - done.top) <= 1 && std::abs(rise.bottom - done.bottom) <= 1) << "and on the same rows";

        const auto wipe = inkOf("document-arrive-wipe");
        expect(std::abs(wipe.left - done.left) <= 1) << "wipe: starts at the left edge";
        expect(std::abs(static_cast<float>(wipe.right) - (static_cast<float>(done.left) + 0.5f * doneWidth)) <= 3.0f) << "wipe: cut at half its width, at " << wipe.right;

        const auto above = inkOf("document-arrive-wipe-above");
        expect(above.bottom < done.bottom - 2 && std::abs(above.top - done.top) <= 1) << "wipe from=above: shows its top part only, rows " << above.top << ".." << above.bottom;
    };

    "a body that does not fit even at the floor is cut off at its edge and named"_test = [] {
        expect(eq(gAppliedScale["document-far-too-much"], DocumentView{}.minimumScale)) << "the fixture did not reach the floor, so this path is untested";
        const std::vector<std::string>& reported = gClipped["document-far-too-much"];
        expect(std::ranges::find(reported, std::string{"content"}) != reported.end()) << "the overrunning body was not reported";
        expect(gClipped["document-overflow"].empty() || gAppliedScale["document-overflow"] <= DocumentView{}.minimumScale) << "a body that fitted above the floor was reported as clipped";

        const auto shot = readScreenshot(Harness::captureDirectory() / "document-far-too-much.png");
        expect(shot.has_value());
        if (shot.has_value()) {
            const int bottom = inkRows(*shot, 0, shot->width - 1, shot->height / 8).second;
            expect(lt(bottom, static_cast<int>(static_cast<float>(shot->height) * 0.97f))) << "ink at row " << bottom << " runs to the window's edge";
        }
    };

    "a master layout reports named areas the renderer can place content into"_test = [] {
        std::ifstream                   file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/figures/master.svg", std::ios::binary);
        const std::vector<std::uint8_t> source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto                      parsed = layoutOfSvg(source);
        expect(parsed.has_value()) << "the demo layout did not parse";
        if (parsed.has_value()) {
            expect(parsed->find("content") != nullptr) << "the renderer places body content into the area named content";
            expect(parsed->find("title") != nullptr);
        }
    };

    "a layout with a title area takes the heading out of the body flow"_test = [] {
        std::ifstream                   file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/figures/master.svg", std::ios::binary);
        const std::vector<std::uint8_t> source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto                      parsed = layoutOfSvg(source);
        expect(parsed.has_value());
        if (!parsed.has_value()) {
            return;
        }
        // the three areas the renderer routes to; without a title area the heading stays in the body column
        expect(parsed->find("title") != nullptr) << "the heading is drawn here when this area exists";
        expect(parsed->find("content") != nullptr);
        expect(parsed->find("footer") != nullptr) << "the footer text is drawn here";
    };

    // Ground truth is Solarized's published hex values, not anything this renderer computed: if the accents stop
    // reaching the canvas the capture still matches its own reference, so sameness alone would not catch it.
    "a fenced block carries its panel and its language's accents"_test = [] {
        struct Expectation {
            std::string   scenario;
            std::uint32_t panel;
        };
        const std::array<Expectation, 2UZ> schemes{Expectation{.scenario = "document-code-dark", .panel = IM_COL32(0x00, 0x2B, 0x36, 0xFF)}, Expectation{.scenario = "document-code-light", .panel = IM_COL32(0xFD, 0xF6, 0xE3, 0xFF)}};

        const std::array<std::pair<std::string_view, std::uint32_t>, 5UZ> accents{
            std::pair{"keyword (green)", IM_COL32(0x85, 0x99, 0x00, 0xFF)},   //
            std::pair{"type (yellow)", IM_COL32(0xB5, 0x89, 0x00, 0xFF)},     //
            std::pair{"number (magenta)", IM_COL32(0xD3, 0x36, 0x82, 0xFF)},  //
            std::pair{"string (cyan)", IM_COL32(0x2A, 0xA1, 0x98, 0xFF)},     //
            std::pair{"directive (orange)", IM_COL32(0xCB, 0x4B, 0x16, 0xFF)} //
        };

        for (const Expectation& scheme : schemes) {
            const auto shot = readScreenshot(Harness::captureDirectory() / (scheme.scenario + ".png"));
            expect(shot.has_value()) << scheme.scenario << " was not captured";
            if (!shot.has_value()) {
                continue;
            }

            std::size_t                  panelPixels = 0UZ;
            std::array<std::size_t, 5UZ> found{};
            for (int y = 0; y < shot->height; ++y) {
                for (int x = 0; x < shot->width; ++x) {
                    const std::uint32_t pixel = shot->at(x, y);
                    panelPixels += pixel == scheme.panel ? 1UZ : 0UZ;
                    // an accent needs a tolerance, a flat panel does not
                    for (std::size_t index = 0UZ; index < accents.size(); ++index) {
                        found[index] += near(pixel, accents[index].second, 24) ? 1UZ : 0UZ;
                    }
                }
            }

            expect(gt(panelPixels, 5000UZ)) << scheme.scenario << ": the code panel is not painted in Solarized's background";
            for (std::size_t index = 0UZ; index < accents.size(); ++index) {
                expect(gt(found[index], 3UZ)) << scheme.scenario << ": no pixel carries the " << accents[index].first << " accent";
            }
        }
    };

    // Ground truth is GFM's rule that the delimiter row decides placement: the two captures share every character
    // and differ only in their colons, so identical pixels would mean alignment was parsed and then ignored.
    "a column's alignment changes where its cells are drawn"_test = [] {
        const std::filesystem::path aligned = Harness::captureDirectory() / "document-table.png";
        const std::filesystem::path flat    = Harness::captureDirectory() / "document-table-left.png";
        expect(std::filesystem::exists(aligned) && std::filesystem::exists(flat));
        if (!std::filesystem::exists(aligned) || !std::filesystem::exists(flat)) {
            return;
        }
        const ComparisonResult result = compareScreenshot(aligned, flat);
        expect(!result.matches) << "right- and centre-aligned columns drew exactly where left-aligned ones did";
    };

    // Ground truth is the arithmetic of the two series: one is y = x and the other y = 10 - x over the same x, so
    // the first must be drawn rising and the second falling, whatever the axes turn out to be.
    "a plotted series is drawn where its numbers say"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-plot.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }

        // the mean row of one colour's ink in each screen column, which is where that curve sits at that x
        const auto trace = [&shot](std::uint32_t colour) {
            std::map<int, std::pair<double, std::size_t>> perColumn;
            for (int y = 0; y < shot->height; ++y) {
                for (int x = 0; x < shot->width; ++x) {
                    if (near(shot->at(x, y), colour, 32)) {
                        auto& [sum, count] = perColumn[x];
                        sum += y;
                        ++count;
                    }
                }
            }
            return perColumn;
        };

        const auto sampleAt = [](const std::map<int, std::pair<double, std::size_t>>& curve, double fraction) {
            const int  first  = curve.begin()->first;
            const int  last   = std::prev(curve.end())->first;
            const int  wanted = first + static_cast<int>(fraction * (last - first));
            const auto at     = curve.lower_bound(wanted);
            return at->second.first / static_cast<double>(at->second.second);
        };

        const auto rising  = trace(IM_COL32(0xFD, 0xB3, 0x42, 0xFF)); // the first series takes the brand colour
        const auto falling = trace(IM_COL32(0x26, 0x8B, 0xD2, 0xFF));
        expect(gt(rising.size(), 50UZ)) << "the first series was not drawn";
        expect(gt(falling.size(), 50UZ)) << "the second series was not drawn";
        if (rising.empty() || falling.empty()) {
            return;
        }

        // sampled short of the right-hand edge, where the legend carries the same two colours
        const double risingLeft   = sampleAt(rising, 0.15);
        const double risingRight  = sampleAt(rising, 0.60);
        const double fallingLeft  = sampleAt(falling, 0.15);
        const double fallingRight = sampleAt(falling, 0.60);

        // screen rows grow downwards, so a rising curve moves to a smaller row as x grows
        expect(lt(risingRight, risingLeft - 20.0)) << "y = x was not drawn rising: " << risingLeft << " then " << risingRight;
        expect(gt(fallingRight, fallingLeft + 20.0)) << "y = 10 - x was not drawn falling: " << fallingLeft << " then " << fallingRight;
        // the two series sum to 10 at every x, so their screen rows sum to the same constant at every x as well,
        // which holds only if the mapping from value to row is linear and the same for both
        expect(lt(std::abs((risingLeft + fallingLeft) - (risingRight + fallingRight)), 20.0)) << "the value-to-row mapping is not linear: " << (risingLeft + fallingLeft) << " against " << (risingRight + fallingRight);
    };

    // The renderer falls back to drawing a formula's LaTeX source in the brand colour when it cannot rasterise it.
    // That slide carries no code spans and no links, so brand orange in the capture means exactly one thing.
    "a formula is typeset, not printed as its own source"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-maths.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }
        std::size_t fallback = 0UZ;
        std::size_t ink      = 0UZ;
        for (int y = 0; y < shot->height; ++y) {
            for (int x = 0; x < shot->width; ++x) {
                const std::uint32_t pixel = shot->at(x, y);
                fallback += near(pixel, IM_COL32(0xFD, 0xB3, 0x42, 0xFF), 24) ? 1UZ : 0UZ;
                ink += (pixel & 0x00FFFFFFU) != 0U ? 1UZ : 0UZ;
            }
        }
        expect(gt(ink, 5000UZ)) << "the slide is blank";
        expect(lt(fallback, 20UZ)) << "the LaTeX source was drawn instead of the formula";
    };

    // The same view twice, with the presenter panel hidden and shown. The slide itself must be identical in both,
    // and the bottom of the frame must change: notes belong to the presenter, never to the room.
    "the presenter panel covers the foot of the slide and changes nothing above it"_test = [] {
        const auto hidden = readScreenshot(Harness::captureDirectory() / "document-notes-hidden.png");
        const auto shown  = readScreenshot(Harness::captureDirectory() / "document-notes-shown.png");
        expect(hidden.has_value() && shown.has_value());
        if (!hidden.has_value() || !shown.has_value()) {
            return;
        }
        expect(eq(hidden->height, shown->height));

        const int     band   = shown->height * 2 / 3; // the panel occupies the bottom third
        std::size_t   inBand = 0UZ;
        std::set<int> touchedColumns;
        for (int y = 0; y < shown->height; ++y) {
            for (int x = 0; x < shown->width; ++x) {
                if (hidden->at(x, y) == shown->at(x, y)) {
                    continue;
                }
                if (y >= band) {
                    ++inBand;
                } else {
                    touchedColumns.insert(x);
                }
            }
        }
        expect(gt(inBand, 10000UZ)) << "the presenter panel did not draw";

        // Above the band the criterion is how wide the change is, not whether there is one. The panel bakes glyphs
        // at a size the slide does not use, and repacking the font atlas moves the odd glyph stem by a pixel, which
        // shows up as one or two full-height columns. Notes leaking onto the slide would be sentences, and a
        // sentence touches hundreds of columns.
        expect(lt(touchedColumns.size(), 8UZ)) << "the change above the panel is as wide as text, so the notes leaked onto the slide: " << touchedColumns.size() << " columns";
    };

    // A slide's markers must read 1, 2, 3 down the page whatever the author's labels are and wherever the
    // definitions sit, so the same view with its two definitions written the other way round must come out
    // pixel for pixel the same. Numbering by definition order would swap both the markers and the notes.
    "footnotes are numbered by first appearance, not by where they are defined"_test = [] {
        const std::filesystem::path written = Harness::captureDirectory() / "document-footnotes.png";
        const std::filesystem::path swapped = Harness::captureDirectory() / "document-footnotes-swapped.png";
        expect(std::filesystem::exists(written) && std::filesystem::exists(swapped));
        if (!std::filesystem::exists(written) || !std::filesystem::exists(swapped)) {
            return;
        }
        const ComparisonResult result = compareScreenshot(written, swapped);
        expect(result.matches) << "reordering the definitions changed the slide: " << result.message;
    };

    // The same view drawn at two clock readings. The JPEG is one frame and cannot change; the GIF has eleven and
    // must, so the two captures have to differ, and differ only where the horse is.
    "an animated image shows a different frame as the clock moves"_test = [] {
        const auto early = readScreenshot(Harness::captureDirectory() / "document-media-early.png");
        const auto later = readScreenshot(Harness::captureDirectory() / "document-media-later.png");
        expect(early.has_value() && later.has_value());
        if (!early.has_value() || !later.has_value()) {
            return;
        }

        std::size_t differing     = 0UZ;
        std::size_t belowTheStill = 0UZ;
        for (int y = 0; y < early->height; ++y) {
            for (int x = 0; x < early->width; ++x) {
                if (early->at(x, y) == later->at(x, y)) {
                    continue;
                }
                ++differing;
                belowTheStill += y > early->height / 3 ? 1UZ : 0UZ;
            }
        }
        expect(gt(differing, 1000UZ)) << "the animation did not advance between the two clock readings";
        // the photograph is one frame and cannot change; a handful of pixels in the heading move when the atlas is
        // repacked, so the test is that essentially all of the change is where the animation is
        expect(gt(static_cast<double>(belowTheStill) / static_cast<double>(differing), 0.95)) << "too much of the change is above the animation, so the frame index is being applied to the wrong image";
    };

    // The same view at two clock readings, two seconds apart in a four-second clip: the picture must move on, and
    // the progress bar under it must have grown, which it can only do if the clip's own length was read.
    "a clip advances and its progress bar follows"_test = [] {
        const auto early = readScreenshot(Harness::captureDirectory() / "document-video-early.png");
        const auto later = readScreenshot(Harness::captureDirectory() / "document-video-later.png");
        expect(early.has_value() && later.has_value());
        if (!early.has_value() || !later.has_value()) {
            return;
        }

        std::size_t differing = 0UZ;
        for (int y = 0; y < early->height; ++y) {
            for (int x = 0; x < early->width; ++x) {
                differing += early->at(x, y) != later->at(x, y) ? 1UZ : 0UZ;
            }
        }
        expect(gt(differing, 50000UZ)) << "ten seconds apart and the clip did not move";

        // the bar is the brand colour; ten seconds into twenty-one, it must be far longer than at the start
        const auto barWidth = [](const Screenshot& shot) {
            std::size_t widest = 0UZ;
            for (int y = 0; y < shot.height; ++y) {
                std::size_t run = 0UZ;
                for (int x = 0; x < shot.width; ++x) {
                    run    = near(shot.at(x, y), IM_COL32(0xFD, 0xB3, 0x42, 0xFF), 24) ? run + 1UZ : 0UZ;
                    widest = std::max(widest, run);
                }
            }
            return widest;
        };
        const std::size_t atStart = barWidth(*early);
        const std::size_t atTwo   = barWidth(*later);
        expect(gt(atTwo, atStart + 100UZ)) << "the progress bar did not grow: " << atStart << " then " << atTwo;
    };

    // A formula is rasterised by parsing TeX and filling glyph outlines, which is far too slow to repeat every
    // frame. The maths scenario holds two formulas and the harness draws it repeatedly; each is laid out once to
    // measure and once to paint, so four rasters is the ceiling. If the cache key ever varies with something that
    // moves from frame to frame, this climbs without bound and the viewer stops responding to anything.
    "a formula is rasterised a fixed number of times, however often it is drawn"_test = [] {
        expect(gt(gFormulaRasters, 0UZ)) << "no formula was rasterised at all, so this proves nothing";
        expect(le(gFormulaRasters, 4UZ)) << "the formula cache missed " << gFormulaRasters << " times for two formulas";
    };

    // Ground truth is typography, not a screenshot: a line of roughly 45 to 90 characters is the range every
    // manual of style gives, and a phone turned on its side must not change how big the text looks. The four
    // sizes are a desktop window, a phone either way up and a tablet.
    // Two slides have already shipped a character the face they are drawn in does not have, and a missing glyph
    // draws as a box: an ellipsis inside a code span, and the element-of sign on the very slide claiming the subset
    // carries it. The scan runs in `drawScenario`, where the atlas is alive; the assertion only reads the result.
    "every character the deck uses exists in the face it is drawn with"_test = [] { expect(gMissingGlyphs.empty()) << "characters the deck draws as boxes: " << std::ranges::fold_left(gMissingGlyphs, std::string{}, [](std::string all, const std::string& one) { return all.empty() ? one : all + "; " + one; }); };

    // The author asked for a striped table to wash alternate rows in a semi-transparent light grey and the header in a
    // stronger grey. Measured as bands of one flat colour spanning most of the box: header first and brightest, the
    // washes neutral grey, and plain rows left as the background between them.
    "a striped table washes its header strongest and every second row lighter, in neutral grey"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-table-striped.png");
        expect(shot.has_value() && gLeftBox.contains("document-table-striped"));
        if (!shot.has_value() || !gLeftBox.contains("document-table-striped")) {
            return;
        }
        const Rectangle     box        = gLeftBox["document-table-striped"];
        const std::uint32_t background = shot->at(2, 2);
        struct Band {
            int           top;
            int           bottom;
            std::uint32_t colour;
        };
        std::vector<Band> bands;
        int               plainRowsBetween = 0;
        for (int y = static_cast<int>(box.y); y < static_cast<int>(box.y + box.height) && y < shot->height; ++y) {
            // the colour most of the row is: under a wash that is the wash, since text covers a minority of a row
            std::map<std::uint32_t, int> counts;
            for (int x = static_cast<int>(box.x); x < static_cast<int>(box.x + box.width) && x < shot->width; ++x) {
                ++counts[shot->at(x, y)];
            }
            const std::uint32_t colour = std::ranges::max_element(counts, {}, &std::pair<const std::uint32_t, int>::second)->first;
            const bool          washed = colour != background;
            if (!washed) {
                plainRowsBetween += bands.empty() ? 0 : 1;
                continue;
            }
            if (!bands.empty() && bands.back().bottom == y - 1 && bands.back().colour == colour) {
                bands.back().bottom = y;
            } else {
                bands.push_back(Band{.top = y, .bottom = y, .colour = colour});
            }
        }
        std::erase_if(bands, [](const Band& band) { return band.bottom - band.top < 2; }); // the rule under the header
        const auto channel   = [](std::uint32_t colour, int shift) { return static_cast<int>((colour >> shift) & 0xFFU); };
        const auto lightness = [&](std::uint32_t colour) { return channel(colour, 0) + channel(colour, 8) + channel(colour, 16); };
        expect(eq(bands.size(), 3UZ)) << "a header and two washed rows of four, found " << bands.size() << " bands";
        if (bands.size() < 2UZ) {
            return;
        }
        for (const Band& band : bands) {
            expect(le(std::abs(channel(band.colour, 0) - channel(band.colour, 8)), 2) && le(std::abs(channel(band.colour, 8) - channel(band.colour, 16)), 2)) << "a wash is grey, not tinted: " << std::hex << band.colour;
        }
        expect(gt(lightness(bands.front().colour), lightness(bands[1].colour))) << "the header is the stronger grey";
        expect(gt(lightness(bands[1].colour), lightness(background))) << "a washed row is lighter than the slide";
        expect(gt(plainRowsBetween, 0)) << "plain rows stay the background between the washes";
    };

    // Charts scale to the room they are given in both directions (the author's general rule for plots).
    "a plot in a box fills the box's width and reaches its foot"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-plot-boxed.png");
        expect(shot.has_value() && gLeftBox.contains("document-plot-boxed"));
        if (!shot.has_value() || !gLeftBox.contains("document-plot-boxed")) {
            return;
        }
        const Rectangle box            = gLeftBox["document-plot-boxed"];
        const auto [firstRow, lastRow] = inkRows(*shot, static_cast<int>(box.x), static_cast<int>(box.x + box.width) - 1, static_cast<int>(box.y));
        const auto [firstCol, lastCol] = inkColumns(*shot, static_cast<int>(box.y));
        const float boxBottom          = box.y + box.height;
        expect(gt(static_cast<float>(lastRow), boxBottom - box.height * 0.2f)) << "the plot stops at row " << lastRow << " of a box ending at " << boxBottom;
        expect(lt(static_cast<float>(firstCol), box.x + box.width * 0.1f)) << "the plot starts at column " << firstCol << " of a box from " << box.x;
        expect(firstRow >= 0);
    };

    // Each axis is scaled to its own series: y = x on the left rises from the foot of the plot to its top and
    // y = 10000 - 1000 x on the right falls over the same height, where a shared axis would leave the small one flat
    // along the bottom.
    "a series on the second axis is scaled to that axis, not to the first"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-plot-two-axes.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }
        const auto rowsOf = [&shot](std::uint32_t colour) {
            int top    = shot->height;
            int bottom = -1;
            for (int y = 0; y < shot->height; ++y) {
                for (int x = shot->width / 4; x < shot->width * 3 / 4; ++x) { // clear of the legend at the right
                    if (near(shot->at(x, y), colour, 32)) {
                        top    = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            }
            return std::pair{top, bottom};
        };
        const auto [smallTop, smallBottom] = rowsOf(IM_COL32(0xFD, 0xB3, 0x42, 0xFF));
        const auto [largeTop, largeBottom] = rowsOf(IM_COL32(0x26, 0x8B, 0xD2, 0xFF));
        expect(gt(smallBottom - smallTop, shot->height / 4)) << "the left series spans rows " << smallTop << ".." << smallBottom;
        expect(gt(largeBottom - largeTop, shot->height / 4)) << "the right series spans rows " << largeTop << ".." << largeBottom;
        expect(lt(std::abs((smallBottom - smallTop) - (largeBottom - largeTop)), 12)) << "both rise over the same height";
    };

    // The author's rule: text after `<br>` in a title is its sub-title, 20 pt under a 36 pt title, on its own line;
    // in prose a `<br>` only breaks the line. Measured as bands of ink rows down the left of the slide.
    "a title's <br> starts a sub-title about 20/36 of its size, and prose breaks where it says"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-subtitle.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }
        const std::uint32_t              background = shot->at(2, 2);
        std::vector<std::pair<int, int>> bands; // runs of rows carrying ink
        for (int y = 0; y < shot->height; ++y) {
            bool ink = false;
            for (int x = 0; x < shot->width && !ink; ++x) {
                ink = shot->at(x, y) != background;
            }
            if (ink && (bands.empty() || bands.back().second != y - 1)) {
                bands.emplace_back(y, y);
            } else if (ink) {
                bands.back().second = y;
            }
        }
        expect(ge(bands.size(), 4UZ)) << "title, sub-title and two prose lines, found " << bands.size() << " bands";
        if (bands.size() < 4UZ) {
            return;
        }
        const float title    = static_cast<float>(bands[0].second - bands[0].first + 1);
        const float subtitle = static_cast<float>(bands[1].second - bands[1].first + 1);
        expect(lt(std::abs(subtitle / title - 20.0f / 36.0f), 0.15f)) << "the sub-title is " << subtitle << " px of ink against the title's " << title;
        expect(lt(std::abs(static_cast<float>(bands[2].second - bands[2].first) - static_cast<float>(bands[3].second - bands[3].first)), 6.0f)) << "the two prose lines are one size";
    };

    // `// @step` in code: the first line shows at once and the next two with the following step, and the panel keeps
    // its size throughout so nothing below it moves.
    "code revealed in steps shows one line, then three, in a panel of one size"_test = [] {
        const auto before = readScreenshot(Harness::captureDirectory() / "document-code-step0.png");
        const auto after  = readScreenshot(Harness::captureDirectory() / "document-code-step1.png");
        expect(before.has_value() && after.has_value());
        if (!before.has_value() || !after.has_value()) {
            return;
        }
        // rows of the panel that carry a glyph: a text line is several rows, so lines are counted as runs of them
        const auto panelAndLines = [](const Screenshot& shot) {
            const std::uint32_t background = shot.at(2, 2);
            int                 top = -1, bottom = -1, lines = 0;
            bool                inLine = false;
            for (int y = 0; y < shot.height; ++y) {
                const std::uint32_t edge = shot.at(shot.width - 80, y); // inside the panel, right of any short line
                if (edge == background) {
                    continue;
                }
                top        = top < 0 ? y : top;
                bottom     = y;
                bool glyph = false;
                for (int x = 60; x < shot.width / 2 && !glyph; ++x) {
                    glyph = shot.at(x, y) != edge;
                }
                lines += glyph && !inLine ? 1 : 0;
                inLine = glyph;
            }
            return std::tuple{top, bottom, lines};
        };
        const auto [top0, bottom0, lines0] = panelAndLines(*before);
        const auto [top1, bottom1, lines1] = panelAndLines(*after);
        expect(eq(lines0, 1)) << "before the step, one line of code";
        expect(eq(lines1, 3)) << "after it, three";
        expect(eq(top0, top1) && eq(bottom0, bottom1)) << "the panel is rows " << top0 << ".." << bottom0 << " and then " << top1 << ".." << bottom1;
    };

    // A clip alone on its slide is the thing on the slide: it sits in the middle of the width, not against an edge.
    "a clip alone on its slide is centred across it"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-video-early.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }
        const auto [first, last] = inkColumns(*shot, shot->height / 3); // below the heading, which starts at the left
        const float middle       = (static_cast<float>(first) + static_cast<float>(last)) * 0.5f;
        expect(lt(std::abs(middle - static_cast<float>(shot->width) * 0.5f), 3.0f)) << "the clip spans columns " << first << ".." << last << " of " << shot->width;
    };

    "a box measures taller when it is narrower or holds more, and a short line is about one line"_test = [] {
        expect(gHeights.contains("one wide")) << "the measurement was not recorded";
        if (!gHeights.contains("one wide")) {
            return;
        }
        expect(gt(gHeights["one narrow"], gHeights["one wide"])) << "a narrower box wraps the same words onto more lines";
        expect(gt(gHeights["two wide"], gHeights["one wide"] + gHeights["line"] * 0.9f)) << "a second paragraph adds at least a line";
        expect(lt(gHeights["one wide"], gHeights["line"] * 4.0f)) << "one short line measures " << gHeights["one wide"] << " px at a " << gHeights["line"] << " px body";
    };

    // The page's geometry, measured on the plain reading column: the title and the prose under it start on one left
    // edge, the column is centred (a full-width code panel has equal margins either side), and the title sits close
    // to the top, as the author asked when the title margin was halved twice.
    "the title and the prose share a left edge, the column is centred, and the title is near the top"_test = [] {
        const auto shot = readScreenshot(Harness::captureDirectory() / "document-dark-step0.png");
        expect(shot.has_value());
        if (!shot.has_value()) {
            return;
        }
        const std::uint32_t background  = shot->at(2, 2);
        const auto          firstInkRow = [&](int from) {
            for (int y = from; y < shot->height; ++y) {
                for (int x = 0; x < shot->width; ++x) {
                    if (shot->at(x, y) != background) {
                        return y;
                    }
                }
            }
            return -1;
        };
        const auto leftmostInRows = [&](int from, int to) {
            for (int x = 0; x < shot->width; ++x) {
                for (int y = from; y < to; ++y) {
                    if (shot->at(x, y) != background) {
                        return x;
                    }
                }
            }
            return -1;
        };
        const int titleTop = firstInkRow(0);
        int       gap      = titleTop;
        while (gap < shot->height && firstInkRow(gap) == gap) {
            ++gap; // the title's own rows
        }
        const int proseTop = firstInkRow(gap);
        expect(titleTop >= 0 && proseTop > titleTop);
        expect(lt(titleTop, static_cast<int>(static_cast<float>(shot->height) * 0.05f))) << "the title's ink starts at row " << titleTop;
        const int titleLeft = leftmostInRows(titleTop, gap);
        const int proseLeft = leftmostInRows(proseTop, proseTop + 8);
        expect(le(std::abs(titleLeft - proseLeft), 1)) << "title from column " << titleLeft << ", prose from column " << proseLeft;

        // the code panel is the one wide flat band in the capture; its two margins are the column's
        for (int y = proseTop; y < shot->height; ++y) {
            const std::uint32_t middle = shot->at(shot->width / 2, y);
            if (middle == background) {
                continue;
            }
            int left = shot->width / 2;
            while (left > 0 && shot->at(left - 1, y) == middle) {
                --left;
            }
            int right = shot->width / 2;
            while (right + 1 < shot->width && shot->at(right + 1, y) == middle) {
                ++right;
            }
            if (right - left < shot->width / 2) {
                continue; // a glyph, not the panel
            }
            const int rightMargin = shot->width - 1 - right;
            expect(le(std::abs(left - rightMargin), 2)) << "the panel's margins are " << left << " and " << rightMargin << " px";
            return;
        }
        expect(false) << "no code panel found";
    };

    "text is sized so a line stays readable at any shape of window"_test = [] {
        struct Window {
            std::string name;
            float       width;
            float       height;
        };
        for (const Window& window : {Window{.name = "desktop", .width = 1280.0f, .height = 800.0f}, Window{.name = "phone portrait", .width = 390.0f, .height = 844.0f}, //
                 Window{.name = "phone landscape", .width = 844.0f, .height = 390.0f}, Window{.name = "tablet portrait", .width = 768.0f, .height = 1024.0f}}) {
            const float body   = Fonts::slideBodySize(window.width, window.height);
            const float column = DocumentView::columnWidth(window.width, body);

            expect(gt(body, 11.0f)) << window.name << ": body text of " << body << " px is too small to read";
            expect(lt(column, window.width)) << window.name << ": the column is wider than the window";
            // A margin on each side, and nothing else: the measure is no longer capped, so a line runs the width
            // of its box. A cap of about sixty-five characters was asserted here until the deck's author asked
            // for the width instead; a column narrower than the margins can explain is still a fault.
            expect(gt(column, window.width * 0.8f)) << window.name << ": the column keeps only " << 100.0f * column / window.width << "% of the width";
            // Liberation Sans averages about half an em per character, so the measure in characters is twice the column in ems
            const float characters = 2.0f * column / body;
            expect(gt(characters, 40.0f)) << window.name << ": only " << characters << " characters a line";
        }

        // the same device turned over must not resize the type, which sizing from the height alone did
        const float portrait  = Fonts::slideBodySize(390.0f, 844.0f);
        const float landscape = Fonts::slideBodySize(844.0f, 390.0f);
        expect(lt(std::abs(portrait - landscape), 0.01f)) << "rotating the device changed the text size: " << portrait << " against " << landscape;

        // and a bigger window still means bigger text
        expect(gt(Fonts::slideBodySize(1920.0f, 1080.0f), Fonts::slideBodySize(1280.0f, 800.0f)));
    };

    for (const Scenario& scenario : kScenarios) {
        const std::filesystem::path captured  = Harness::captureDirectory() / (scenario.name + ".png");
        const std::filesystem::path reference = Harness::referenceDirectory() / (scenario.name + ".png");

        boost::ut::test("'" + scenario.name + "' matches its reference") = [captured, reference] {
            expect(std::filesystem::exists(captured)) << "no screenshot was captured";
            if (!std::filesystem::exists(captured)) {
                return;
            }
            if (updatingReferences()) {
                std::filesystem::create_directories(reference.parent_path());
                std::filesystem::copy_file(captured, reference, std::filesystem::copy_options::overwrite_existing);
                return;
            }
            const ComparisonResult result = compareScreenshot(captured, reference);
            expect(result.matches) << result.message;
        };
    }
};

int main() {
    Harness harness;
    for (const Scenario& scenario : kScenarios) {
        harness.addTest(
            scenario.name,                                              //
            [&scenario](ImGuiTestContext*) { drawScenario(scenario); }, //
            [&scenario](ImGuiTestContext* context) { Harness::captureTo(context, scenario.name); });
    }
    gEngineSucceeded = harness.run();
    return 0;
}
