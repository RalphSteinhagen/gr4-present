#include <boost/ut.hpp>

#include "FileContents.hpp"

#include "SvgImage.hpp"

#include <gr4-present/export/PageRecording.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

// a layout fixture the tests own, so they assert the renderer's behaviour rather than the demo deck's look
constexpr std::string_view kLayoutSource = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 960 540" width="960" height="540">
  <rect x="0" y="0" width="960" height="540" fill="#101014"/>
  <rect id="title"   data-present="markdown" x="40"  y="32"  width="880" height="72"  fill="none" stroke="#F36F21" stroke-width="2"/>
  <rect id="content" data-present="markdown" x="40"  y="136" width="520" height="316" fill="none" stroke="#F36F21" stroke-width="2"/>
  <rect id="sidebar" data-present="gr4"      x="592" y="136" width="328" height="316" fill="none" stroke="#F36F21" stroke-width="2"/>
  <rect id="footer"  data-present="markdown" x="40"  y="476" width="880" height="32"  fill="none" stroke="#808080" stroke-width="1"/>
</svg>)svg";

constexpr std::string_view kStagedSource = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 960 540" width="960" height="540">
  <rect id="content" data-present="markdown" x="40" y="40" width="880" height="200" fill="none"/>
  <rect id="stage-one"   data-step="1" x="60"  y="320" width="220" height="120" fill="none" stroke="#F36F21" stroke-width="4"/>
  <rect id="stage-two"   data-step="2" x="370" y="320" width="220" height="120" fill="none" stroke="#F36F21" stroke-width="4"/>
  <rect id="stage-three" data-step="3" x="680" y="320" width="220" height="120" fill="none" stroke="#F36F21" stroke-width="4"/>
</svg>)svg";

[[nodiscard]] std::span<const std::uint8_t> bytesOf(std::string_view text) noexcept { return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}; }

[[nodiscard]] std::vector<std::uint8_t> readFile(const std::string& path) { return fileContents<std::vector<std::uint8_t>>(path); }

} // namespace

// Ground truth: Inkscape -- assets/ holds the SVG sources and the PNGs it rasterised from them
const suite<"SvgImage"> svgImageTests = [] {
    "element ids are found in the source, and attribute lookalikes are not"_test = [] {
        const auto names = svgElementNames(R"svg(<svg><rect id="one" data-present="markdown"/><g clip-path="url(#one)"><rect id="two"/></g><rect data-id="no"/></svg>)svg");
        expect(eq(names.size(), 2UZ)) << "clip-path and data-id must not be mistaken for an id attribute";
        expect(eq(names.at(0UZ).id, std::string{"one"}));
        expect(eq(names.at(0UZ).kind, std::string{"markdown"}));
        expect(eq(names.at(1UZ).id, std::string{"two"}));
        expect(names.at(1UZ).kind.empty()) << "an element without data-present is an anchor, not an area";
    };

    "data-step is read, and the backdrop hides what has not been revealed"_test = [] {
        const auto layout = layoutOfSvg(bytesOf(kStagedSource));
        expect(layout.has_value());
        if (!layout.has_value()) {
            return;
        }
        const Area* second = layout->find("stage-two");
        expect(second != nullptr);
        if (second != nullptr) {
            expect(eq(second->step, 2)) << "data-step was not read";
        }

        const auto atZero  = hiddenAt(*layout, 0);
        const auto atTwo   = hiddenAt(*layout, 2);
        const auto atThree = hiddenAt(*layout, 3);
        const auto hides   = [](const std::vector<std::string>& ids, std::string_view id) { return std::ranges::find(ids, id) != ids.end(); };

        expect(hides(atZero, "stage-one")) << "a group at step 1 must be hidden at step 0";
        expect(!hides(atTwo, "stage-one")) << "a group at step 1 must be visible once step 2 is reached";
        expect(hides(atTwo, "stage-three")) << "a group at step 3 must still be hidden at step 2";
        expect(!hides(atThree, "stage-three"));
        expect(hides(atThree, "content")) << "an area that holds content is always hidden, whatever the step";
        expect(lt(atThree.size(), atZero.size())) << "advancing must reveal, not conceal";
    };

    // Ground truth: the fills the master paints under each area -- a near-black band, a white band, and nothing
    "a layout knows how bright the master is under each area, and says nothing where the master paints nothing"_test = [] {
        constexpr std::string_view kBands = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 300" width="400" height="300">
  <rect x="0" y="0" width="400" height="100" fill="#0f0f13"/>
  <rect x="0" y="100" width="400" height="100" fill="#ffffff"/>
  <rect id="onDark" data-present="image" x="20" y="20" width="100" height="60" fill="none"/>
  <rect id="onLight" data-present="image" x="20" y="120" width="100" height="60" fill="none"/>
  <rect id="onNothing" data-present="image" x="20" y="220" width="100" height="60" fill="none"/>
</svg>)";
        const auto                 layout = layoutOfSvg(bytesOf(kBands));
        expect(fatal(layout.has_value()));
        const Area* dark    = layout->find("onDark");
        const Area* light   = layout->find("onLight");
        const Area* nothing = layout->find("onNothing");
        expect(fatal(dark != nullptr && light != nullptr && nothing != nullptr));
        expect(dark->backdropLight >= 0.0f && dark->backdropLight < 0.1f) << dark->backdropLight;
        expect(light->backdropLight > 0.9f) << light->backdropLight;
        expect(nothing->backdropLight < 0.0f) << "transparent: the scheme decides there";
    };

    "a layout reports its named areas with the boxes they occupy"_test = [] {
        const auto layout = layoutOfSvg(bytesOf(kLayoutSource));
        expect(layout.has_value());
        if (!layout.has_value()) {
            return;
        }
        expect(eq(layout->width, 960.0f));
        expect(eq(layout->height, 540.0f));

        const Area* content = layout->find("content");
        expect(content != nullptr) << "the content area was not found";
        if (content != nullptr) {
            expect(eq(content->kind, std::string{"markdown"}));
            expect(std::abs(content->x - 40.0f) < 6.0f) << "x includes the stroke width: " << content->x;
            expect(std::abs(content->width - 520.0f) < 12.0f) << "width includes the stroke on both sides: " << content->width;
        }

        const Area* sidebar = layout->find("sidebar");
        expect(sidebar != nullptr);
        if (sidebar != nullptr) {
            expect(eq(sidebar->kind, std::string{"gr4"})) << "a live-region area is typed by data-present";
        }
        expect(layout->find("absent") == nullptr);
    };

    "an area inside a transformed group reports where it actually sits"_test = [] {
        constexpr std::string_view          kSource = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200" width="400" height="200">
  <g transform="translate(40,32)"><rect id="moved" data-present="markdown" x="0" y="0" width="100" height="50" fill="none"/></g>
  <rect id="still" data-present="markdown" x="200" y="100" width="100" height="50" fill="none"/>
</svg>)svg";
        const std::span<const std::uint8_t> bytes{reinterpret_cast<const std::uint8_t*>(kSource.data()), kSource.size()};
        const auto                          layout = layoutOfSvg(bytes);
        expect(layout.has_value());
        if (!layout.has_value()) {
            return;
        }
        const Area* moved = layout->find("moved");
        expect(moved != nullptr);
        if (moved != nullptr) {
            // Ground truth: the rect at 0,0 in a group translated by (40,32); an untransformed box would report 0,0
            expect(std::abs(moved->x - 40.0f) < 1.0f) << "the group transform was not applied, got x=" << moved->x;
            expect(std::abs(moved->y - 32.0f) < 1.0f) << "the group transform was not applied, got y=" << moved->y;
        }
        const Area* still = layout->find("still");
        expect(still != nullptr);
        if (still != nullptr) {
            expect(std::abs(still->x - 200.0f) < 1.0f) << "an untransformed element must be reported where it is";
        }
    };

    "a link in a drawing reports where it leads and the box it covers"_test = [] {
        // Ground truth: the rect at (100,50) sized 200x100 in a 1000x500 drawing, the second moved by (500,250)
        constexpr std::string_view          kSource = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 500" width="1000" height="500">
  <a href="#branch-a"><rect x="100" y="50" width="200" height="100" fill="#888"/></a>
  <g transform="translate(500,250)"><a href="https://example.org/"><rect x="0" y="0" width="100" height="50" fill="#888"/></a></g>
  <g id="plain"><rect x="0" y="400" width="10" height="10"/></g>
</svg>)svg";
        const std::span<const std::uint8_t> bytes{reinterpret_cast<const std::uint8_t*>(kSource.data()), kSource.size()};
        const std::vector<SvgLink>          links = linksOfSvg(bytes);
        expect(eq(links.size(), 2UZ)) << "two links, and a plain group is not one";
        if (links.size() != 2UZ) {
            return;
        }
        const auto near = [](float got, float wanted) { return std::abs(got - wanted) < 0.005f; };
        expect(eq(links[0].target, std::string{"#branch-a"}));
        expect(near(links[0].area.x, 0.1f) && near(links[0].area.y, 0.1f) && near(links[0].area.width, 0.2f) && near(links[0].area.height, 0.2f)) << links[0].area.x << "," << links[0].area.y << " " << links[0].area.width << "x" << links[0].area.height;
        expect(eq(links[1].target, std::string{"https://example.org/"}));
        expect(near(links[1].area.x, 0.5f) && near(links[1].area.y, 0.5f) && near(links[1].area.width, 0.1f) && near(links[1].area.height, 0.1f)) << "the enclosing transform applies: " << links[1].area.x << "," << links[1].area.y;
        expect(linksOfSvg(bytesOf("not an svg")).empty());
    };

    "hiding an area keeps it out of the raster"_test = [] {
        // native width, so the two-pixel strokes are not lost to anti-aliasing before they can be counted
        const auto                     withBoxes = rasteriseSvg(bytesOf(kLayoutSource), 960U);
        const std::vector<std::string> hidden{"title", "content", "sidebar", "footer"};
        const auto                     withoutBoxes = rasteriseSvg(bytesOf(kLayoutSource), 960U, hidden);
        expect(withBoxes.has_value() && withoutBoxes.has_value());
        if (!withBoxes.has_value() || !withoutBoxes.has_value()) {
            return;
        }
        const auto strokePixels = [](const RasterImage& image) {
            std::size_t orange = 0UZ;
            for (std::size_t pixel = 0UZ; pixel < image.rgba.size(); pixel += 4UZ) {
                orange += image.rgba[pixel] > 120U && image.rgba[pixel] > image.rgba[pixel + 2UZ] + 50U ? 1UZ : 0UZ; // warm against the dark backdrop
            }
            return orange;
        };
        expect(gt(strokePixels(*withBoxes), 100UZ)) << "the placeholder strokes should be visible when nothing is hidden";
        expect(lt(strokePixels(*withoutBoxes), strokePixels(*withBoxes) / 10UZ)) << "hiding the areas did not remove their strokes";
    };

    "the embedded faces reach lunasvg, which is the only way a browser draws text"_test = [] {
        // lunasvg loads system fonts, so natively labels survive regardless; a browser has only the embedded faces
        expect(svgTextHasAFont()) << "lunasvg refused the embedded faces, so every <text> element vanishes in a browser";
    };

    "a text element in a drawing actually draws"_test = [] {
        constexpr std::string_view kWithText    = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 60" width="200" height="60"><rect x="0" y="0" width="200" height="60" fill="#000000"/><text x="10" y="40" font-family="sans-serif" font-size="32" fill="#FFFFFF">Filter</text></svg>)svg";
        constexpr std::string_view kWithoutText = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 60" width="200" height="60"><rect x="0" y="0" width="200" height="60" fill="#000000"/></svg>)svg";

        const auto inked = [](std::string_view source) {
            const auto image = rasteriseSvg(bytesOf(source), 400U);
            expect(image.has_value());
            std::size_t light = 0UZ;
            if (image.has_value()) {
                for (std::size_t pixel = 0UZ; pixel < image->rgba.size(); pixel += 4UZ) {
                    light += image->rgba[pixel] > 128U ? 1UZ : 0UZ;
                }
            }
            return light;
        };

        expect(eq(inked(kWithoutText), 0UZ)) << "the control drawing is not blank, so this proves nothing";
        expect(gt(inked(kWithText), 200UZ)) << "the text drew no pixels at all, so no font reached lunasvg";
    };

    "the demo deck's own drawing keeps its labels"_test = [] {
        const auto chain = readFile(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/figures/chain.svg");
        expect(!chain.empty()) << "the deck's signal chain is missing";
        const auto image = rasteriseSvg(chain, 800U);
        expect(image.has_value());
        if (!image.has_value()) {
            return;
        }
        // the drawing is boxes, arrows and five labels; without the labels it loses a visible share of its ink
        std::size_t inked = 0UZ;
        for (std::size_t pixel = 3UZ; pixel < image->rgba.size(); pixel += 4UZ) {
            inked += image->rgba[pixel] > 32U ? 1UZ : 0UZ;
        }
        expect(gt(inked, 4000UZ)) << "the drawing rendered nearly empty: " << inked << " pixels carry anything";
    };

    "an SVG reference is recognised by its extension"_test = [] {
        expect(isSvgReference("figures/system.svg"));
        expect(isSvgReference("FIGURE.SVG"));
        expect(!isSvgReference("figures/system.png"));
        expect(!isSvgReference("svg"));
        expect(!isSvgReference(""));
    };

    "a malformed document does not rasterise"_test = [] {
        const std::string               garbage = "this is not an SVG";
        const std::vector<std::uint8_t> bytes{garbage.begin(), garbage.end()};
        expect(!rasteriseSvg(bytes, 64U).has_value());
        expect(!rasteriseSvg({}, 64U).has_value());
    };

    "rasterising preserves the source aspect ratio"_test = [] {
        const auto source = readFile(std::string{GR4_PRESENT_ASSET_DIRECTORY} + "/GSI_FAIR_Logo.svg");
        expect(!source.empty()) << "the logo asset is missing";
        const auto image = rasteriseSvg(source, 400U);
        expect(image.has_value()) << "the project's own logo did not rasterise";
        if (!image.has_value()) {
            return;
        }
        expect(eq(image->width, 400U));
        expect(image->height > 0U);
        expect(eq(image->rgba.size(), static_cast<std::size_t>(image->width) * image->height * 4UZ));

        const auto larger = rasteriseSvg(source, 800U);
        expect(larger.has_value());
        if (larger.has_value()) {
            const double ratio      = static_cast<double>(image->height) / static_cast<double>(image->width);
            const double ratioLarge = static_cast<double>(larger->height) / static_cast<double>(larger->width);
            expect(std::abs(ratio - ratioLarge) < 0.01) << "the aspect ratio must not depend on the raster size";
        }
    };

    "the logo's glyphs actually render, not just its background"_test = [] {
        const auto source = readFile(std::string{GR4_PRESENT_ASSET_DIRECTORY} + "/GSI_FAIR_Logo.svg");
        const auto image  = rasteriseSvg(source, 800U);
        expect(image.has_value());
        if (!image.has_value()) {
            return;
        }

        // white background, near-black glyphs: dark pixels tell rendered paths from a background-only render
        std::size_t dark = 0UZ;
        for (std::size_t pixel = 0UZ; pixel < image->rgba.size(); pixel += 4UZ) {
            dark += image->rgba[pixel] < 128U && image->rgba[pixel + 1UZ] < 128U && image->rgba[pixel + 2UZ] < 128U ? 1UZ : 0UZ;
        }
        const double inked = static_cast<double>(dark) / (static_cast<double>(image->width) * image->height);
        expect(inked > 0.01) << "no glyphs were drawn: only the background rect rendered";
        expect(inked < 0.50) << "the raster is mostly dark, so the fills are wrong";

        const auto corner = image->rgba.begin();
        expect(corner[0] > 200U && corner[1] > 200U && corner[2] > 200U) << "the background rect did not render white";
        expect(eq(corner[3], std::uint8_t{255})) << "the background rect is not opaque";
    };

    // Ground truth: hand arithmetic -- the viewBox halves coordinates, a group translate moves children, `style` counts
    // Ground truth: SVG's number lists (viewBox, points): whitespace or commas between numbers, a sign or a second point starts the next one
    "a number list reads every number, whatever separates them, and stops where one is not a number"_test = [] {
        expect(numbersOf("0 0 100 50") == std::vector<float>{0.0f, 0.0f, 100.0f, 50.0f});
        expect(numbersOf("0,0,100,50") == std::vector<float>{0.0f, 0.0f, 100.0f, 50.0f}) << "commas";
        expect(numbersOf("  1 , 2\t3\n4 ") == std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f}) << "mixed whitespace and commas";
        expect(numbersOf("1-2") == std::vector<float>{1.0f, -2.0f}) << "a minus starts the next number";
        expect(numbersOf(".5.5") == std::vector<float>{0.5f, 0.5f}) << "a second point starts the next number";
        expect(numbersOf("1e2,2.5E-1") == std::vector<float>{100.0f, 0.25f}) << "exponents";
        expect(numbersOf("1 2 x 3") == std::vector<float>{1.0f, 2.0f}) << "the list ends at what is not a number";
        expect(numbersOf("").empty() && numbersOf(" , ").empty());
        expect(numbersOf("+1,+2.5 3+4") == std::vector<float>{1.0f, 2.5f, 3.0f, 4.0f}) << "a plus sign, as SVG allows, also starts the next number";
    };

    "a drawing's shapes and words come out as outlines where the drawing puts them"_test = [] {
        constexpr std::string_view     kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 400 200">
  <title>not drawn</title>
  <rect x="20" y="40" width="100" height="60" fill="#ff0000"/>
  <g transform="translate(200,0)" style="fill:#0000ff;stroke:none"><circle cx="50" cy="50" r="20"/></g>
  <rect id="box" x="0" y="0" width="400" height="200" fill="#123456"/>
  <line x1="0" y1="190" x2="400" y2="190" stroke="#000000" stroke-width="4"/>
  <polygon points="10,10 30,10 20,30" fill="#ffffff"/>
  <text x="300" y="150" font-size="20" text-anchor="middle" fill="#00ff00">Hi &amp; bye</text>
</svg>)svg";
        const auto                     bytes    = std::span{reinterpret_cast<const std::uint8_t*>(kDrawing.data()), kDrawing.size()};
        const std::vector<std::string> hidden{"box"};
        const auto                     drawing = outlineSvg(bytes, hidden);
        expect(drawing.has_value()) << (drawing.has_value() ? std::string{} : drawing.error());
        if (!drawing.has_value()) {
            return;
        }
        expect(eq(drawing->width, 200.0f) && eq(drawing->height, 100.0f)) << "the drawing's own extent";
        const auto boundsOf = [](const VectorPath& path) {
            std::array<float, 4> bounds{1e9f, 1e9f, -1e9f, -1e9f};
            for (std::size_t at = 0UZ; at + 1UZ < path.points.size(); at += 2UZ) {
                bounds = {std::min(bounds[0], path.points[at]), std::min(bounds[1], path.points[at + 1UZ]), std::max(bounds[2], path.points[at]), std::max(bounds[3], path.points[at + 1UZ])};
            }
            return bounds;
        };
        const auto near = [](const std::array<float, 4>& got, const std::array<float, 4>& want) { return std::ranges::all_of(std::views::iota(0, 4), [&](int i) { return std::abs(got[static_cast<std::size_t>(i)] - want[static_cast<std::size_t>(i)]) < 0.01f; }); };
        expect(eq(drawing->paths.size(), 4UZ)) << "rect, circle, line, polygon; the hidden box is left out";
        if (drawing->paths.size() == 4UZ) {
            const VectorPath& red = drawing->paths[0];
            expect(eq(red.fill, 0xFF0000FFU) && near(boundsOf(red), {10.0f, 20.0f, 60.0f, 50.0f})) << "the red rectangle, halved";
            const VectorPath& blue = drawing->paths[1];
            expect(eq(blue.fill, 0xFFFF0000U) && eq(blue.stroke, 0U) && near(boundsOf(blue), {115.0f, 15.0f, 135.0f, 35.0f})) << "the circle, moved by its group and coloured by its group's style";
            const VectorPath& line = drawing->paths[2];
            expect(eq(line.fill, 0U) && eq(line.stroke, 0xFF000000U) && std::abs(line.strokeWidth - 2.0f) < 0.01f) << "a line strokes, at half its width";
            const VectorPath& triangle = drawing->paths[3];
            expect(!triangle.steps.empty() && triangle.steps.back() == VectorPath::Step::close) << "a polygon closes";
        }
        expect(eq(drawing->texts.size(), 1UZ)) << "the title is not drawn, the text is";
        if (drawing->texts.size() == 1UZ) {
            const DrawingText& words = drawing->texts.front();
            expect(words.text == "Hi & bye") << words.text;
            expect(std::abs(words.x - 150.0f) < 0.01f && std::abs(words.y - 75.0f) < 0.01f && std::abs(words.em - 10.0f) < 0.01f) << "on its baseline, halved";
            expect(words.anchor == DrawingText::Anchor::middle && eq(words.colour, 0xFF00FF00U));
        }
    };

    // Ground truth: RFC 3629 -- one byte below U+0080, two below U+0800, three below U+10000, four above
    "a numeric character reference reads as the character it names, at every UTF-8 length"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><text x="10" y="50" font-size="10">&#65;&#xE9;&#x20AC;&#x1F600;</text></svg>)svg";
        const auto                 drawing  = outlineSvg(std::span{reinterpret_cast<const std::uint8_t*>(kDrawing.data()), kDrawing.size()}, {});
        expect(fatal(drawing.has_value() && eq(drawing->texts.size(), 1UZ)));
        expect(drawing->texts.front().text == "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80") << "A, e acute, the euro sign and U+1F600";
    };

    "a drawing with something outlines cannot reproduce is refused, saying what"_test = [] {
        for (const auto& [svg, said] : {std::pair{std::string_view{R"svg(<svg width="10" height="10"><image href="a.png" width="10" height="10"/></svg>)svg"}, std::string_view{"<image>"}}, //
                 std::pair{std::string_view{R"svg(<svg width="10" height="10"><rect width="10" height="10" fill="url(#g)"/></svg>)svg"}, std::string_view{"url(#g)"}}}) {
            const auto refused = outlineSvg(std::span{reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size()}, {});
            expect(!refused.has_value() && refused.error().contains(said)) << (refused.has_value() ? std::string{"accepted"} : refused.error());
        }
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
