#include <boost/ut.hpp>

#include "FileContents.hpp"

#include "SvgImage.hpp"

#include <gr4-present/export/PageRecording.hpp>

#include <plutovg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
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

/// the x (`axis` 0) or y (`axis` 1) of each x, y pair; std::views::stride is absent from the libc++ some toolchains ship
[[nodiscard]] std::vector<float> coordinatesOf(std::span<const float> points, std::size_t axis) {
    std::vector<float> along;
    for (std::size_t at = axis; at < points.size(); at += 2UZ) {
        along.push_back(points[at]);
    }
    return along;
}

/// the RGBA of `picture` at fractions `fx`, `fy` of its width and height
[[nodiscard]] std::array<std::uint8_t, 4UZ> pixelOf(const VectorPicture& picture, float fx, float fy) {
    const std::size_t at = (static_cast<std::size_t>(fy * static_cast<float>(picture.pixels.height - 1U)) * picture.pixels.width + static_cast<std::size_t>(fx * static_cast<float>(picture.pixels.width - 1U))) * 4UZ;
    return std::array{picture.pixels.rgba[at], picture.pixels.rgba[at + 1UZ], picture.pixels.rgba[at + 2UZ], picture.pixels.rgba[at + 3UZ]};
}

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

    // a line drawn right to left and upwards
    "a line drawn backwards reports the box it covers"_test = [] {
        constexpr std::string_view kSource = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="960" height="540">
  <line id="g-meter-sep-1" x1="475.74" y1="379.08" x2="403.13" y2="350.59" stroke="#ffffff" stroke-width="2"/>
</svg>)svg";
        const auto                 layout  = layoutOfSvg(bytesOf(kSource));
        expect(fatal(layout.has_value()));
        const Area* line = layout->find("g-meter-sep-1");
        expect(fatal(line != nullptr));
        // ground truth: its two ends; the margin is the stroke's, at most the miter limit's 4 times its width
        expect(line->x <= 403.13f && line->x + line->width >= 475.74f && line->width < 72.61f + 17.0f) << "across both ends: " << line->x << " + " << line->width;
        expect(line->y <= 350.59f && line->y + line->height >= 379.08f && line->height < 28.49f + 17.0f) << "down both ends: " << line->y << " + " << line->height;
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

    // Ground truth: |det| of a rotation is 1, of a scale by (sx, sy) |sx sy|; a shear keeps area, a translation moves only
    "a transform scales lengths by the square root of its determinant's magnitude"_test = [] {
        const float                cosine = std::cos(0.5f);
        const float                sine   = std::sin(0.5f);
        constexpr plutovg_matrix_t kIdentity{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        expect(eq(uniformScaleOf(kIdentity), 1.0f));
        expect(std::abs(uniformScaleOf(plutovg_matrix_t{cosine, sine, -sine, cosine, 30.0f, -7.0f}) - 1.0f) < 1e-6f) << "a rotation and a translation";
        expect(eq(uniformScaleOf(plutovg_matrix_t{2.0f, 0.0f, 0.0f, 8.0f, 0.0f, 0.0f}), 4.0f)) << "a scale by 2 and 8";
        expect(eq(uniformScaleOf(plutovg_matrix_t{-3.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f}), 3.0f)) << "a mirror image";
        expect(eq(uniformScaleOf(plutovg_matrix_t{1.0f, 0.0f, 5.0f, 1.0f, 0.0f, 0.0f}), 1.0f)) << "a shear";
        expect(std::abs(uniformScaleOf(plutovg_matrix_t{3.0f * cosine, 3.0f * sine, -3.0f * sine, 3.0f * cosine, 0.0f, 0.0f}) - 3.0f) < 1e-5f) << "a rotation scaled by 3";
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

    // Ground truth: SVG's own rule, a group's transform applies to what it holds, inside a clipPath or marker as anywhere
    "a group inside a clip or a marker moves what it holds"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <defs><clipPath id="moved"><g transform="translate(50 0)"><rect x="0" y="0" width="10" height="10"/></g></clipPath>
  <marker id="dot" markerWidth="20" markerHeight="20" markerUnits="userSpaceOnUse"><g transform="translate(5 0)" fill="#ff0000"><rect x="0" y="0" width="2" height="2"/></g></marker></defs>
  <rect x="0" y="0" width="100" height="100" fill="#0000ff" clip-path="url(#moved)"/>
  <path d="M20,80 L40,80" stroke="#000000" marker-end="url(#dot)"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value() && eq(drawing->clips.size(), 1UZ))) << (drawing.has_value() ? std::string{} : drawing.error());
        const auto [low, high] = std::ranges::minmax(coordinatesOf(drawing->clips[0].points, 0UZ));
        expect(std::abs(low - 50.0f) < 0.01f && std::abs(high - 60.0f) < 0.01f) << "the clip from x 50 to 60: " << low << " to " << high;
        expect(fatal(eq(drawing->paths.size(), 3UZ))) << "the clipped square, the line and its marker";
        const VectorPath& dot = drawing->paths[2];
        expect(eq(dot.fill, 0xFF0000FFU)) << "in the group's red";
        const auto [left, right] = std::ranges::minmax(coordinatesOf(dot.points, 0UZ));
        expect(std::abs(left - 45.0f) < 0.01f && std::abs(right - 47.0f) < 0.01f) << "moved 5 along from the line's end at 40: " << left << " to " << right;
    };

    // Ground truth: the same drawing without the prefix, which XML says it is
    "a drawing that spells out SVG's namespace as svg: outlines as the same drawing without it"_test = [] {
        constexpr std::string_view kPlain = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <defs><clipPath id="half"><rect x="0" y="0" width="50" height="100"/></clipPath><mask id="round"><circle cx="50" cy="50" r="20" fill="#ffffff"/></mask></defs>
  <rect x="0" y="0" width="100" height="100" fill="#0000ff" clip-path="url(#half)"/>
  <g mask="url(#round)"><rect x="0" y="0" width="100" height="100" fill="#ff0000"/></g>
</svg>)svg";
        std::string                prefixed{kPlain};
        for (const auto& [from, to] : {std::pair{std::string_view{"</"}, std::string_view{"</svg:"}}, std::pair{std::string_view{"<"}, std::string_view{"<svg:"}}}) {
            for (std::size_t at = prefixed.find(from); at != std::string::npos; at = prefixed.find(from, at + to.size())) {
                if (to == "<svg:" && prefixed.substr(at, 2UZ) == "</") {
                    continue;
                }
                prefixed.replace(at, from.size(), to);
            }
        }
        prefixed.replace(prefixed.find("xmlns="), 6UZ, "xmlns:svg=");
        const auto plain = outlineSvg(bytesOf(kPlain), {});
        const auto spelt = outlineSvg(bytesOf(prefixed), {});
        expect(fatal(plain.has_value() && spelt.has_value())) << (spelt.has_value() ? std::string{} : spelt.error()) << "\n" << prefixed;
        expect(eq(spelt->paths.size(), plain->paths.size()) && eq(spelt->clips.size(), plain->clips.size())) << "the clipped square and the masked group's picture";
        expect(spelt->paths.size() == 2UZ && spelt->paths[1].picture.has_value() && plain->paths[1].picture.has_value() && spelt->paths[1].picture->at.width == plain->paths[1].picture->at.width) << "the picture covers what the mask lets through, not the group's start tag alone";
    };

    // matplotlib defines its clips after the drawing that uses them, Inkscape before; both name them by id
    "a clipped shape is outlined with its clip in the drawing's coordinates, wherever the clip is defined"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 100 50">
  <g transform="translate(10 5)"><rect x="0" y="0" width="80" height="40" fill="#0000ff" clip-path="url(#half)"/></g>
  <rect x="0" y="0" width="5" height="5" fill="#ff0000"/>
  <defs><clipPath id="half"><rect x="0" y="0" width="40" height="40"/></clipPath></defs>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value())) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(fatal(eq(drawing->paths.size(), 2UZ) && eq(drawing->clips.size(), 1UZ)));
        expect(drawing->paths[0].clips == std::vector<std::uint32_t>{0U}) << "the blue rectangle is clipped";
        expect(drawing->paths[1].clips.empty()) << "the red one, outside the group, is not";
        // the clip's rectangle in the user space of the shape that names it: moved by the group, scaled by the viewBox
        const std::vector<float>& corners = drawing->clips[0].points;
        expect(fatal(corners.size() >= 4UZ));
        const auto [low, high] = std::ranges::minmax(coordinatesOf(corners, 0UZ));
        expect(std::abs(low - 20.0f) < 0.01f && std::abs(high - 100.0f) < 0.01f) << "from x 20 to 100, not 0 to 80";
    };

    // a logo or an eye placed in a master as a drawing of its own; Ground truth: lunasvg's placement of the same element
    "a nested svg places, fits and cuts what it draws as its viewport says"_test = [] {
        for (const std::string_view aspect : {std::string_view{""}, std::string_view{R"( preserveAspectRatio="none")"}, std::string_view{R"( preserveAspectRatio="xMaxYMin slice")"}}) {
            const std::string source  = std::format(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 400 200">
  <rect x="0" y="0" width="5" height="5" fill="#00ff00"/>
  <svg x="20" y="40" width="80" height="80" viewBox="0 0 400 200"{}><rect id="inner" x="0" y="0" width="400" height="200" fill="#ff0000"/></svg>
</svg>)svg",
                 aspect);
            const auto        drawing = outlineSvg(bytesOf(source), {});
            const auto        layout  = layoutOfSvg(bytesOf(source));
            expect(fatal(drawing.has_value() && layout.has_value())) << (drawing.has_value() ? std::string{} : drawing.error());
            expect(fatal(eq(drawing->paths.size(), 2UZ) && eq(drawing->clips.size(), 1UZ))) << aspect;
            expect(drawing->paths[0].clips.empty() && drawing->paths[1].clips == std::vector<std::uint32_t>{0U}) << "only what the viewport draws is cut to it";
            const Area* inner = layout->find("inner");
            expect(fatal(inner != nullptr));
            const std::vector<float>& points = drawing->paths[1].points;
            const auto [left, right]         = std::ranges::minmax(coordinatesOf(points, 0UZ));
            const auto [top, bottom]         = std::ranges::minmax(coordinatesOf(points, 1UZ));
            // both in the drawing's units; a slice spills past the viewport, which only the cut below holds in
            expect(std::abs(left - inner->x) < 0.01f && std::abs(right - (inner->x + inner->width)) < 0.01f) << aspect << ": x " << left << " to " << right << ", not " << inner->x << " + " << inner->width;
            expect(std::abs(top - inner->y) < 0.01f && std::abs(bottom - (inner->y + inner->height)) < 0.01f) << aspect << ": y " << top << " to " << bottom << ", not " << inner->y << " + " << inner->height;
            const auto [cutLeft, cutRight] = std::ranges::minmax(coordinatesOf(drawing->clips[0].points, 0UZ));
            expect(std::abs(cutLeft - 10.0f) < 0.01f && std::abs(cutRight - 50.0f) < 0.01f) << "cut to the viewport, x 10 to 50";
        }
    };

    // Ground truth: lunasvg's placement of the same element; a percent is of the drawing's own size
    "a nested svg's lengths in percent are of the drawing"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 400 200">
  <svg x="10%" y="20%" width="50%" height="25%" viewBox="0 0 100 100" preserveAspectRatio="none"><rect id="inner" x="0" y="0" width="100" height="100" fill="#ff0000"/></svg>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        const auto                 layout   = layoutOfSvg(bytesOf(kDrawing));
        expect(fatal(drawing.has_value() && layout.has_value() && eq(drawing->paths.size(), 1UZ)));
        const Area* inner = layout->find("inner");
        expect(fatal(inner != nullptr));
        const std::vector<float>& points = drawing->paths[0].points;
        const auto [left, right]         = std::ranges::minmax(coordinatesOf(points, 0UZ));
        const auto [top, bottom]         = std::ranges::minmax(coordinatesOf(points, 1UZ));
        expect(std::abs(left - inner->x) < 0.01f && std::abs(right - (inner->x + inner->width)) < 0.01f) << "x " << left << " to " << right << ", not " << inner->x << " + " << inner->width;
        expect(std::abs(top - inner->y) < 0.01f && std::abs(bottom - (inner->y + inner->height)) < 0.01f) << "y " << top << " to " << bottom << ", not " << inner->y << " + " << inner->height;
    };

    // a logo's horn, whose relative curves return to where it began, in decimal; Ground truth: its last end is its start
    "a closed outline whose curves come back to its start ends exactly there"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="600">
  <g transform="matrix(0.95788161,0,0,0.86643684,383.14742,133.00278)"><path d="m 498.29697,164.86245 c -28.80096,94.72673 -37.5792,63.79495 -6.87776,123.61959 -39.20064,31.51909 -43.40703,39.85083 -71.03215,100.10393 -44.82624,-94.15169 17.812,-223.75201 77.90991,-223.72352 z" style="fill:#000000;stroke:#ffffff;stroke-width:5.81914" transform="matrix(1.0439704,0,0,0.85208953,-393.73072,-29.363312)"/></g>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 1UZ)));
        const VectorPath& horn = drawing->paths.front();
        expect(fatal(horn.steps.size() == 5UZ && horn.steps.back() == VectorPath::Step::close && horn.points.size() == 20UZ));
        expect(eq(horn.points[18], horn.points[0]) && eq(horn.points[19], horn.points[1])) << "no sliver for the close to stroke: " << horn.points[18] - horn.points[0] << ", " << horn.points[19] - horn.points[1];
    };

    // Ground truth: SVG's defaults (miter joins, a limit of 4, butt caps), not a PDF's limit of 10
    "a stroke keeps its joins, caps and miter limit"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10">
  <path d="M0 0 L5 9 L9 0" fill="none" stroke="#000000"/>
  <path d="M0 0 L5 9 L9 0" style="fill:none;stroke:#000000;stroke-linejoin:round;stroke-linecap:square;stroke-miterlimit:2"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 2UZ)));
        expect(drawing->paths[0].join == VectorPath::Join::miter && drawing->paths[0].cap == VectorPath::Cap::butt && eq(drawing->paths[0].miterLimit, 4.0f)) << "SVG's defaults";
        expect(drawing->paths[1].join == VectorPath::Join::round && drawing->paths[1].cap == VectorPath::Cap::square && eq(drawing->paths[1].miterLimit, 2.0f)) << "as its style says";
    };

    "a use draws a copy of what it names where it is placed, painted as the use says"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="100" height="100">
  <defs><g id="star"><rect x="0" y="0" width="10" height="10"/></g></defs>
  <use xlink:href="#star" x="20" y="30" fill="#ff0000"/>
  <use href="#star" transform="translate(50 0)" fill="#0000ff"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 2UZ))) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(std::abs(drawing->paths[0].points[0] - 20.0f) < 0.01f && std::abs(drawing->paths[0].points[1] - 30.0f) < 0.01f) << "the first copy at (20, 30)";
        expect(eq(drawing->paths[0].fill, 0xFF0000FFU)) << "in the use's red";
        expect(std::abs(drawing->paths[1].points[0] - 50.0f) < 0.01f && eq(drawing->paths[1].fill, 0xFFFF0000U)) << "the second moved by its transform, in blue";
    };

    // Ground truth: lunasvg's box of each `<use>`
    "a use copies what it names to its own end tag, past elements whose names begin the same, and a symbol draws only where used"_test = [] {
        for (const std::string_view named : {std::string_view{R"svg(<defs><g id="star"><defs><font horiz-adv-x="10"><glyph unicode="a" d="M 0 0"></glyph></font></defs><rect x="0" y="0" width="10" height="10"/></g></defs>)svg"}, //
                 std::string_view{R"svg(<symbol id="star"><rect x="0" y="0" width="10" height="10"/></symbol>)svg"},                                                                                                                //
                 std::string_view{R"svg(<symbol id="star" viewBox="0 0 10 10"><rect x="0" y="0" width="10" height="10"/></symbol>)svg"}}) {
            const std::string source  = std::format(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">{}<use id="placed" href="#star" x="20" y="30" width="40" height="40" fill="#ff0000"/></svg>)svg", named);
            const auto        drawing = outlineSvg(bytesOf(source), {});
            const auto        layout  = layoutOfSvg(bytesOf(source));
            expect(fatal(drawing.has_value() && layout.has_value())) << named << ": " << (drawing.has_value() ? std::string{} : drawing.error());
            expect(fatal(eq(drawing->paths.size(), 1UZ))) << named;
            const Area* placed = layout->find("placed");
            expect(fatal(placed != nullptr));
            const std::vector<float>& points = drawing->paths[0].points;
            const auto [left, right]         = std::ranges::minmax(coordinatesOf(points, 0UZ));
            const auto [top, bottom]         = std::ranges::minmax(coordinatesOf(points, 1UZ));
            expect(std::abs(left - placed->x) < 0.01f && std::abs(right - (placed->x + placed->width)) < 0.01f) << named << ": x " << left << " to " << right << ", not " << placed->x << " + " << placed->width;
            expect(std::abs(top - placed->y) < 0.01f && std::abs(bottom - (placed->y + placed->height)) < 0.01f) << named << ": y " << top << " to " << bottom << ", not " << placed->y << " + " << placed->height;
        }
    };

    "an empty symbol a use names draws nothing, and the rest of the drawing is outlined"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <symbol id="nothing"/>
  <use href="#nothing" x="20" y="30"/>
  <rect x="0" y="0" width="10" height="10" fill="#00ff00"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value())) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(eq(drawing->paths.size(), 1UZ) && eq(drawing->paths[0].fill, 0xFF00FF00U)) << "the green square alone";
    };

    // Ground truth: SVG 1.1 §11.6 -- refX/refY land on the vertex, the marker in units of the stroke's width, turned
    // with the path for orient="auto"
    "a marker at a path's end is drawn as paths at its last point, turned along it, in the path's stroke colour"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <defs><marker id="arrow" orient="auto" markerWidth="4" markerHeight="4" refX="0" refY="0"><path d="M 0 0 L 1 0" fill="none" stroke="context-stroke"/></marker></defs>
  <path d="M 10 10 L 10 50" stroke="#ff0000" stroke-width="2" fill="none" marker-end="url(#arrow)"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 2UZ))) << (drawing.has_value() ? std::string{} : drawing.error());
        const VectorPath& head = drawing->paths[1];
        expect(fatal(eq(head.points.size(), 4UZ)));
        // from (10, 50) one unit along the path, turned to point down it, times the stroke's width of 2: to (10, 52)
        expect(std::abs(head.points[0] - 10.0f) < 0.01f && std::abs(head.points[1] - 50.0f) < 0.01f) << "starts at the path's end";
        expect(std::abs(head.points[2] - 10.0f) < 0.01f && std::abs(head.points[3] - 52.0f) < 0.01f) << "and points along it";
        expect(eq(head.stroke, 0xFF0000FFU)) << "in the path's red";
    };

    // Ground truth: SVG 1.1 §11.6 -- an empty marker draws nothing; the shapes after its definition are drawn once each
    "a self-closing marker draws nothing at the path, and the shapes after it once"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <defs><marker id="empty" orient="auto" markerWidth="4" markerHeight="4"/></defs>
  <path d="M 10 10 L 10 50" stroke="#ff0000" stroke-width="2" fill="none" marker-end="url(#empty)"/>
  <rect x="50" y="50" width="20" height="20" fill="#00ff00"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value())) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(eq(drawing->paths.size(), 2UZ)) << "the path and the square, nothing at the path's end";
    };

    // a 1 by 1 PNG of opaque red, and a gradient from red to blue: Ground truth is their own colours
    "an embedded bitmap and a gradient's fill stay pixels, over only what they cover, between the outlines"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <rect x="0" y="0" width="10" height="10" fill="#00ff00"/>
  <image x="10" y="10" width="20" height="20" href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg=="/>
  <defs><linearGradient id="g"><stop offset="0" stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/></linearGradient></defs>
  <rect x="50" y="0" width="50" height="100" fill="url(#g)"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 3UZ))) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(!drawing->paths[0].picture.has_value()) << "the green square stays an outline";
        expect(fatal(drawing->paths[1].picture.has_value() && drawing->paths[2].picture.has_value()));
        const VectorPicture& bitmap   = *drawing->paths[1].picture;
        const VectorPicture& gradient = *drawing->paths[2].picture;
        expect(std::abs(bitmap.at.x - 10.0f) < 0.01f && std::abs(bitmap.at.width - 20.0f) < 0.01f && bitmap.photograph) << "the bitmap over its own box";
        expect(pixelOf(bitmap, 0.5f, 0.5f)[0] > 200U && pixelOf(bitmap, 0.5f, 0.5f)[3] > 200U) << "red, opaque";
        expect(std::abs(gradient.at.x - 50.0f) < 0.01f && std::abs(gradient.at.width - 50.0f) < 0.01f && !gradient.photograph) << "the gradient over its rectangle";
        expect(pixelOf(gradient, 0.0f, 0.5f)[0] > 200U && pixelOf(gradient, 1.0f, 0.5f)[2] > 200U) << "red at its left, blue at its right";
    };

    // Ground truth: what lunasvg paints, and that the rest of the drawing keeps its outlines
    "a masked or filtered element is drawn as pixels over what it paints, and the rest stays outlines"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100">
  <rect x="0" y="0" width="10" height="10" fill="#00ff00"/>
  <mask id="round"><circle cx="50" cy="50" r="20" fill="#ffffff"/></mask>
  <g mask="url(#round)"><rect x="0" y="0" width="100" height="100" fill="#0000ff"/><rect x="5" y="5" width="2" height="2" fill="#000000"/></g>
  <filter id="soft"><feGaussianBlur stdDeviation="2"/></filter>
  <rect x="140" y="40" width="20" height="20" fill="#ff0000" filter="url(#soft)"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value())) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(fatal(eq(drawing->paths.size(), 3UZ))) << "the green square, then a picture each for the masked group and the blurred square";
        expect(!drawing->paths[0].picture.has_value()) << "the green square stays an outline";
        expect(fatal(drawing->paths[1].picture.has_value() && drawing->paths[2].picture.has_value())) << "the masked group's own shapes are not outlined as well";
        const VectorPicture& masked  = *drawing->paths[1].picture;
        const VectorPicture& blurred = *drawing->paths[2].picture;
        expect(masked.at.x >= 29.0f && masked.at.x + masked.at.width <= 71.0f && masked.at.y >= 29.0f && masked.at.height <= 42.0f) << "only where the mask lets it paint: " << masked.at.x << " + " << masked.at.width;
        expect(pixelOf(masked, 0.5f, 0.5f)[2] > 200U && pixelOf(masked, 0.5f, 0.5f)[3] > 200U) << "blue, opaque, at the mask's centre";
        expect(pixelOf(masked, 0.0f, 0.0f)[3] < 50U) << "and clear in the corner the circle leaves out";
        expect(blurred.at.x < 140.0f && blurred.at.x + blurred.at.width > 160.0f) << "the blur reaches past the square: " << blurred.at.x << " + " << blurred.at.width;
    };

    // Ground truth: a hidden id is a step not reached yet, so it draws nothing, inside a picture as anywhere else
    "a part hidden by id stays hidden inside a masked group's picture"_test = [] {
        constexpr std::string_view         kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <mask id="round"><circle cx="50" cy="50" r="40" fill="#ffffff"/></mask>
  <g mask="url(#round)"><rect x="0" y="0" width="100" height="100" fill="#0000ff"/><rect id="later" x="40" y="40" width="20" height="20" fill="#ff0000"/></g>
</svg>)svg";
        const std::array<std::string, 1UZ> hidden{"later"};
        const auto                         drawing = outlineSvg(bytesOf(kDrawing), hidden);
        expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 1UZ) && drawing->paths[0].picture.has_value()));
        const auto centre = pixelOf(*drawing->paths[0].picture, 0.5f, 0.5f);
        expect(centre[0] < 50U && centre[2] > 200U) << "blue at the centre, not the hidden red square: " << int{centre[0]} << ", " << int{centre[1]} << ", " << int{centre[2]};
    };

    // Ground truth: SVG 1.1 §11.6, a marker is drawn whatever paints the path
    "a gradient-stroked path keeps its arrowhead"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">
  <defs><linearGradient id="fade"><stop offset="0" stop-color="#0000ff"/><stop offset="1" stop-color="#00ff00"/></linearGradient></defs>
  <marker id="arrow" markerWidth="10" markerHeight="10" refX="0" refY="5" markerUnits="userSpaceOnUse" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#ff0000"/></marker>
  <path d="M10,50 L80,50" stroke="url(#fade)" stroke-width="4" fill="none" marker-end="url(#arrow)"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(fatal(drawing.has_value())) << (drawing.has_value() ? std::string{} : drawing.error());
        const auto redOutline = [](const VectorPath& path) { return !path.picture.has_value() && (path.fill & 0xFF0000FFU) == 0xFF0000FFU && (path.fill & 0x00FFFF00U) == 0U; };
        const auto redPixels  = [](const VectorPath& path) { return path.picture.has_value() && std::ranges::any_of(std::views::iota(0UZ, path.picture->pixels.rgba.size() / 4UZ), [&](std::size_t at) { return path.picture->pixels.rgba[4UZ * at] > 200U && path.picture->pixels.rgba[4UZ * at + 2UZ] < 50U && path.picture->pixels.rgba[4UZ * at + 3UZ] > 200U; }); };
        expect(std::ranges::any_of(drawing->paths, redOutline) || std::ranges::any_of(drawing->paths, redPixels)) << "the red arrowhead is drawn, as an outline or in the picture";
    };

    // Ground truth: the gradient's own colours, red at the start, blue at the end
    "a text a gradient fills is pixels in the gradient's colours, a part of a text it fills is refused"_test = [] {
        constexpr std::string_view kWhole  = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="60">
  <defs><linearGradient id="fade"><stop offset="0" stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/></linearGradient></defs>
  <text x="10" y="45" font-size="40" fill="url(#fade)">MMMMM</text>
</svg>)svg";
        const auto                 drawing = outlineSvg(bytesOf(kWhole), {});
        expect(fatal(drawing.has_value())) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(drawing->texts.empty()) << "not set as words in one colour";
        expect(fatal(eq(drawing->paths.size(), 1UZ) && drawing->paths[0].picture.has_value())) << "but a picture";
        const auto& rgba = drawing->paths[0].picture->pixels.rgba;
        const auto  some = [&rgba](auto wanted) { return std::ranges::any_of(std::views::iota(0UZ, rgba.size() / 4UZ), [&](std::size_t at) { return rgba[4UZ * at + 3UZ] > 200U && wanted(rgba[4UZ * at], rgba[4UZ * at + 2UZ]); }); };
        expect(some([](std::uint8_t red, std::uint8_t blue) { return red > 200U && blue < 80U; }) && some([](std::uint8_t red, std::uint8_t blue) { return blue > 200U && red < 80U; })) << "red letters at its start, blue ones at its end";

        constexpr std::string_view kPart = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="60">
  <defs><linearGradient id="fade"><stop offset="0" stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/></linearGradient></defs>
  <text x="10" y="45" font-size="40">MM<tspan fill="url(#fade)">MM</tspan></text>
</svg>)svg";
        expect(!outlineSvg(bytesOf(kPart), {}).has_value()) << "a tspan cannot be a picture apart from its text";
    };

    // Ground truth: SVG's filter region, in fractions of the element's box whether written in percent or as numbers
    "a filter's region in percent or fractions bounds the picture it paints"_test = [] {
        for (const std::string_view region : {std::string_view{R"(x="-50%" y="0%" width="200%" height="100%")"}, std::string_view{R"(x="-0.5" y="0" width="2" height="1")"}}) {
            const std::string source  = std::format(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100">
  <filter id="tint" {}><feColorMatrix values="0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0"/></filter>
  <rect x="40" y="20" width="40" height="30" fill="#0000ff" filter="url(#tint)"/>
</svg>)svg",
                 region);
            const auto        drawing = outlineSvg(bytesOf(source), {});
            expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 1UZ) && drawing->paths[0].picture.has_value())) << region;
            const auto& at = drawing->paths[0].picture->at;
            expect(std::abs(at.x - 20.0f) < 0.51f && std::abs(at.width - 80.0f) < 1.01f && std::abs(at.y - 20.0f) < 0.51f && std::abs(at.height - 30.0f) < 1.01f) << region << ": " << at.x << " + " << at.width << ", " << at.y << " + " << at.height;
        }
    };

    // Cairo's pattern for a JPEG with an alpha channel: a red JPEG masked by a PNG's brightness, black left, white right;
    // Ground truth: the PNG's halves
    "a JPEG Cairo masks with a PNG's brightness is pixels over its own box, clear where the PNG is black"_test = [] {
        constexpr std::string_view kJpeg      = "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAACAAQDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD50ooor8MP9Uz/2Q==";
        constexpr std::string_view kHalf      = "iVBORw0KGgoAAAANSUhEUgAAAAQAAAACCAIAAADwyuo0AAAAGUlEQVR4nGNkYGBgYGD4//8/AwMDEwMSAAA2GAMB/5uh3gAAAABJRU5ErkJggg=="; // 4 by 2, black left, white right
        const auto                 sourceWith = [&](std::string_view kBright) {
            return std::format(R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="100" height="100">
<defs>
<filter id="filter-remove-color" x="0%" y="0%" width="100%" height="100%"><feColorMatrix color-interpolation-filters="sRGB" values="0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 0 0 0 1 0"/></filter>
<filter id="filter-color-to-alpha" x="0%" y="0%" width="100%" height="100%"><feColorMatrix color-interpolation-filters="sRGB" values="0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 0.2126 0.7152 0.0722 0 0"/></filter>
<image id="source-2" x="0" y="0" width="4" height="2" xlink:href="data:image/png;base64,{1}"/>
<mask id="mask-0"><g filter="url(#filter-remove-color)"><use xlink:href="#source-2" filter="url(#filter-color-to-alpha)" transform="matrix(10, 0, 0, 10, 20, 30)"/></g></mask>
<image id="source-1" x="0" y="0" width="4" height="2" xlink:href="data:image/jpeg;base64,{0}"/>
</defs>
<g mask="url(#mask-0)"><use xlink:href="#source-1" transform="matrix(10, 0, 0, 10, 20, 30)"/></g>
</svg>)svg",
                                kJpeg, kBright);
        };
        const std::string source  = sourceWith(kHalf);
        const auto        drawing = outlineSvg(bytesOf(source), {});
        expect(fatal(drawing.has_value() && eq(drawing->paths.size(), 1UZ))) << (drawing.has_value() ? std::string{} : drawing.error());
        expect(fatal(drawing->paths.front().picture.has_value()));
        const VectorPicture* picture = &*drawing->paths.front().picture;
        expect(fatal(picture->pixels.width > 1U && picture->pixels.height > 0U));
        expect(std::abs(picture->at.x - 20.0f) < 0.01f && std::abs(picture->at.y - 30.0f) < 0.01f && std::abs(picture->at.width - 40.0f) < 0.01f && std::abs(picture->at.height - 20.0f) < 0.01f) << "over the image's own box";
        const auto alphaAt = [picture](float fx) { return picture->pixels.rgba[(static_cast<std::size_t>(picture->pixels.height / 2U) * picture->pixels.width + static_cast<std::size_t>(fx * static_cast<float>(picture->pixels.width - 1U))) * 4UZ + 3UZ]; };
        expect(alphaAt(0.1f) < 10U && alphaAt(0.9f) > 245U) << "clear on the left, opaque on the right: " << int{alphaAt(0.1f)} << ", " << int{alphaAt(0.9f)};
    };

    "an editor's own elements draw nothing and do not stop a drawing being outlined"_test = [] {
        constexpr std::string_view kDrawing = R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="10" height="10">
  <sodipodi:namedview id="view" pagecolor="#ffffff"><inkscape:grid type="xygrid"/></sodipodi:namedview>
  <namedview id="plain"/>
  <rect width="10" height="10" fill="#00ff00"/>
</svg>)svg";
        const auto                 drawing  = outlineSvg(bytesOf(kDrawing), {});
        expect(drawing.has_value() && eq(drawing->paths.size(), 1UZ)) << (drawing.has_value() ? std::string{} : drawing.error());
    };

    "a drawing with something outlines cannot reproduce is refused, saying what"_test = [] {
        for (const auto& [svg, said] : {std::pair{std::string_view{R"svg(<svg width="10" height="10"><rect width="10" height="10" clip-path="url(#nowhere)"/></svg>)svg"}, std::string_view{"clip-path"}}, //
                 std::pair{std::string_view{R"svg(<svg width="10" height="10"><clipPath id="c" clipPathUnits="objectBoundingBox"><rect width="0.5" height="1"/></clipPath><rect width="10" height="10" clip-path="url(#c)"/></svg>)svg"}, std::string_view{"clip-path"}}}) {
            const auto refused = outlineSvg(std::span{reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size()}, {});
            expect(!refused.has_value() && refused.error().contains(said)) << (refused.has_value() ? std::string{"accepted"} : refused.error());
        }
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
