#ifndef GR4_PRESENT_SVG_IMAGE_HPP
#define GR4_PRESENT_SVG_IMAGE_HPP

#include "Layout.hpp"

#include <gr4-present/RegionGeometry.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

struct VectorDrawing; // what a page draws a drawing as: PageRecording.hpp, which only `outlineSvg` needs

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

/**
 * Which elements to leave out when drawing the backdrop at `step`.
 *
 * An area is hidden because content is about to be drawn into it and its box would otherwise show through as a
 * frame. A `data-step` group is hidden because its step has not arrived: the counter is the section's own, so one
 * press of Next advances the prose and the drawing together.
 */
[[nodiscard]] std::vector<std::string> hiddenAt(const Layout& layout, int step);

/**
 * A drawing as outlines, for a page that draws it as vectors rather than as the bitmap: the shapes and words of the
 * SVG basics -- rect, circle, ellipse, line, polyline, polygon, path, text -- with their transforms, presentation
 * attributes and `style` declarations, in the drawing's own width and height. The named elements are left out, as
 * `rasteriseSvg` leaves them out. A drawing that uses anything else -- an image, a gradient, a clip, a filter, a
 * marker -- is refused with what it used, and is then better shown as the picture it is.
 */
[[nodiscard]] std::expected<VectorDrawing, std::string> outlineSvg(std::span<const std::uint8_t> source, std::span<const std::string> hiddenIds);

/// ids appearing in an SVG source

/// an element's id and its `data-present` kind, read from the source: lunasvg exposes lookup by id but no traversal,
/// and discards attributes it does not recognise, so both facts have to come from the text
struct SvgElementName {
    std::string id;
    std::string kind;
    int         step = 0; // `data-step`: the reveal step at which this element first appears

    bool operator==(const SvgElementName&) const = default;
};

[[nodiscard]] std::vector<SvgElementName> svgElementNames(std::string_view source);

/// whether the embedded fallback faces reached lunasvg. A browser has no system fonts, so without them every
/// `<text>` element in every drawing silently disappears there while looking perfect on a developer machine.
[[nodiscard]] bool svgTextHasAFont();

[[nodiscard]] bool isSvgReference(std::string_view reference) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_SVG_IMAGE_HPP
