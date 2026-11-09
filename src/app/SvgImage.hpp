#ifndef GR4_PRESENT_SVG_IMAGE_HPP
#define GR4_PRESENT_SVG_IMAGE_HPP

#include "Layout.hpp"

#include <gr4-present/RegionGeometry.hpp>

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

struct VectorDrawing; // PageRecording.hpp, needed by `outlineSvg` only

struct RasterImage {
    std::uint32_t             width  = 0U;
    std::uint32_t             height = 0U;
    std::vector<std::uint8_t> rgba;
};

/// rasterises an SVG at the width it will be drawn at; std::nullopt when the source is not a document we can render
[[nodiscard]] std::optional<RasterImage> rasteriseSvg(std::span<const std::uint8_t> source, std::uint32_t targetWidth);

/// rasterises an SVG with the named elements hidden, so placeholder boxes do not show through the content put in them
[[nodiscard]] std::optional<RasterImage> rasteriseSvg(std::span<const std::uint8_t> source, std::uint32_t targetWidth, std::span<const std::string> hiddenIds);

/// the named areas of an SVG layout, with their bounding boxes after transforms
[[nodiscard]] std::optional<Layout> layoutOfSvg(std::span<const std::uint8_t> source);

/// a link drawn into a picture: where `<a href>` leads, and the box of what it wraps, in fractions of the drawing
struct SvgLink {
    std::string target;
    Rectangle   area;
};

/// every `<a href>` in a drawing; a picture with none, or one that does not parse, has none
[[nodiscard]] std::vector<SvgLink> linksOfSvg(std::span<const std::uint8_t> source);

/// ids to leave out when drawing the backdrop at `step`: areas that content is drawn into, and `data-step` groups
/// whose step has not arrived; the counter is the section's own, so one press of Next advances prose and drawing
[[nodiscard]] std::vector<std::string> hiddenAt(const Layout& layout, int step);

/// what a cache of a drawing drawn with `hiddenIds` left out knows it by
[[nodiscard]] std::string drawingKey(std::string_view reference, std::span<const std::string> hiddenIds);

/// the numbers of an SVG number list (`viewBox`, `points`), up to the first text that is not one
[[nodiscard]] std::vector<float> numbersOf(std::string_view list);

/// whether the parts outlines cannot paint (an embedded bitmap, a gradient's fill) are rasterised into the drawing,
/// as the PDF wants, or left out, as a reader of its words alone wants
enum class SvgPictures : bool { leftOut, rasterised };

/// a drawing as outlines in its own width and height, for a page that draws vectors; hidden ids are left out as in
/// `rasteriseSvg`; supports rect, circle, ellipse, line, polyline, polygon, path and text with transforms and styles,
/// clip paths (in user units), `use`, `symbol`, markers, and editors' own elements, which draw nothing; an image, a
/// shape a gradient or pattern paints, and a masked or filtered element are pictures over what they cover; anything
/// else is refused with what it used
[[nodiscard]] std::expected<VectorDrawing, std::string> outlineSvg(std::span<const std::uint8_t> source, std::span<const std::string> hiddenIds, SvgPictures pictures = SvgPictures::rasterised);

/// a drawing rasterised for a page has this many pixels per pixel it is drawn with, at most this many across; the PDF's
/// `imageDpi` averages them down
inline constexpr float kSvgOversampling   = 2.0f;
inline constexpr float kSvgMostPixelsWide = 4096.0f;

/// the factor a 2-D affine transform (`a b c d e f`, as plutovg and SVG write it) scales lengths by
template<typename TMatrix>
[[nodiscard]] float uniformScaleOf(const TMatrix& matrix) noexcept {
    return std::sqrt(std::abs(matrix.a * matrix.d - matrix.b * matrix.c));
}

/// an element's id and `data-present` kind, read from the text: lunasvg has no traversal and drops unknown attributes
struct SvgElementName {
    std::string id;
    std::string kind;
    int         step = 0; // `data-step`: the reveal step at which this element first appears

    bool operator==(const SvgElementName&) const = default;
};

[[nodiscard]] std::vector<SvgElementName> svgElementNames(std::string_view source);

/// whether the embedded fallback faces reached lunasvg; a browser has no system fonts, so `<text>` vanishes silently
[[nodiscard]] bool svgTextHasAFont();

[[nodiscard]] bool isSvgReference(std::string_view reference) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_SVG_IMAGE_HPP
