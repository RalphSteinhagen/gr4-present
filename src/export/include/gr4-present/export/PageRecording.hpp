#ifndef GR4_PRESENT_EXPORT_PAGE_RECORDING_HPP
#define GR4_PRESENT_EXPORT_PAGE_RECORDING_HPP

#include <gr4-present/RegionGeometry.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gr::present {

/// what one slide drew, in the terms a PDF page needs: text as text in a named face, shapes as shapes, each picture
/// by what it shows (a figure's reference, a formula's source); positions are window pixels, colours 0xAABBGGRR as
/// ImGui packs them
enum class RecordedFace { body, bold, italic, boldItalic, mono, icons, other };

struct RecordedText {
    std::string   text; // UTF-8
    RecordedFace  face     = RecordedFace::body;
    float         size     = 0.0f; // pixels, the line height ImGui sets it at
    float         x        = 0.0f; // top left of the line, as ImGui places text
    float         y        = 0.0f;
    std::uint32_t colour   = 0U;
    Rectangle     clip     = {};
    std::string   deckFace = {}; // a face the deck brought, by its name in index.yml; empty is `face`
};

enum class RecordedShapeKind { line, polyline, closedPolyline, rect, filledRect, filledTriangle, filledCircle };

struct RecordedShape {
    RecordedShapeKind  kind = RecordedShapeKind::line;
    std::vector<float> points; // x, y pairs: two corners for a rectangle, the centre and a point on it for a circle
    std::uint32_t      colour    = 0U;
    float              thickness = 1.0f; // of a stroke, in pixels
    float              rounding  = 0.0f; // of a rectangle's corners, in pixels
    Rectangle          clip      = {};
};

enum class RecordedImageKind { figure, master, picture, formula, video, qrCode, chart };

/// pixels a page embeds as a picture, RGBA, top row first
struct RecordedPixels {
    std::uint32_t             width  = 0U;
    std::uint32_t             height = 0U;
    std::vector<std::uint8_t> rgba;
};

/// turns an OpenGL read-back, bottom row first, into the top-row-first picture a page embeds; the framebuffer's alpha
/// is not the picture's, so every pixel becomes opaque
inline void uprightOpaqueFromReadBack(RecordedPixels& pixels) noexcept {
    const std::size_t stride   = static_cast<std::size_t>(pixels.width) * 4UZ;
    const auto        rowBegin = [&pixels, stride](std::size_t row) { return pixels.rgba.begin() + static_cast<std::ptrdiff_t>(row * stride); };
    for (std::size_t row = 0UZ; row < pixels.height / 2U; ++row) {
        std::swap_ranges(rowBegin(row), rowBegin(row + 1UZ), rowBegin(pixels.height - 1UZ - row));
    }
    for (std::size_t alpha = 3UZ; alpha < pixels.rgba.size(); alpha += 4UZ) {
        pixels.rgba[alpha] = 255U;
    }
}

struct RecordedImage {
    RecordedImageKind        kind = RecordedImageKind::figure;
    std::string              source;                                                              // a package reference, a formula's LaTeX, or a QR code's text
    Rectangle                at;                                                                  // where it was drawn
    Rectangle                uv          = {.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f}; // the part of it, as fractions
    std::uint32_t            tint        = 0xFFFFFFFFU;
    Rectangle                clip        = {};
    float                    size        = 0.0f;  // a formula's type size in pixels, which its layout depends on
    bool                     display     = false; // a formula set in TeX's display style
    std::vector<std::string> hidden      = {};    // ids of a drawing's elements the slide drew it without: boxes and later steps
    RecordedPixels           pixels      = {};    // read back from the frame for a clip or chart, which have no source
    float                    turnDegrees = 0.0f;  // degrees the view turned it, anticlockwise as seen, about (pivotX, pivotY)
    float                    pivotX      = 0.0f;
    float                    pivotY      = 0.0f;
};

/// words that lead somewhere: an address, or `#view` for another page of the same deck
struct RecordedLink {
    std::string target;
    Rectangle   at;
};

using RecordedPrimitive = std::variant<RecordedText, RecordedShape, RecordedImage, RecordedLink>;

/// one outline of a drawing: moves, lines, cubic curves and closes, filled, stroked or both
struct VectorPath {
    enum class Step : std::uint8_t { move, line, cubic, close };
    std::vector<Step>  steps;
    std::vector<float> points;           // x, y pairs: one for a move or a line, three for a cubic, none for a close
    std::uint32_t      fill        = 0U; // 0xAABBGGRR; a zero alpha is no fill
    std::uint32_t      stroke      = 0U; // likewise: a zero alpha is no stroke
    float              strokeWidth = 0.0f;
    bool               evenOdd     = false; // the fill rule; non-zero winding otherwise
};

/// a drawing's words, placed as a drawing places them: on a baseline, anchored at its start, middle or end
struct DrawingText {
    enum class Anchor : std::uint8_t { start, middle, end };
    std::string   text;
    RecordedFace  face         = RecordedFace::body;
    float         em           = 0.0f; // the font size, in the drawing's units
    float         x            = 0.0f;
    float         y            = 0.0f; // the baseline
    Anchor        anchor       = Anchor::start;
    std::uint32_t colour       = 0xFF000000U;
    float         angleDegrees = 0.0f; // degrees its baseline is turned, anticlockwise as seen, by the drawing's transforms
};

/// what a picture shows, as outlines in its own pixels, so a page can draw it as vectors where the screen drew a
/// texture
struct VectorDrawing {
    float                    width  = 0.0f; // the extent the outlines are in, the same as the texture's
    float                    height = 0.0f;
    std::vector<VectorPath>  paths;
    std::vector<DrawingText> texts; // words a drawing carries as text, in the same coordinates
};

struct PageRecording {
    std::vector<RecordedPrimitive> primitives;  // in the order they were drawn, which is the order they stack in
    std::string                    viewId = {}; // the section the page shows, which a `#view` link and a bookmark name
    std::string                    title  = {}; // its heading, for the bookmark; empty on a page that continues a section
    std::string                    notes  = {}; // the presenter's words for it, attached to the page as a comment
};

} // namespace gr::present

#endif // GR4_PRESENT_EXPORT_PAGE_RECORDING_HPP
