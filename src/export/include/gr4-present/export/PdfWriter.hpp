#ifndef GR4_PRESENT_EXPORT_PDF_WRITER_HPP
#define GR4_PRESENT_EXPORT_PDF_WRITER_HPP

#include <gr4-present/export/PageRecording.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gr::present {

/// a face the recordings name, as the TrueType bytes the viewer drew it from; embedded with a map back to Unicode
struct PdfFont {
    RecordedFace                  face = RecordedFace::body;
    std::span<const std::uint8_t> ttf;
    std::string                   deckFace = {}; // set for a face the deck brought: the name its recordings carry
};

/// what the pages are, apart from their content
struct PdfDocument {
    std::string          title;
    std::string          author;
    float                pageWidth      = 960.0f;  // points: 13.33 in, PowerPoint's widescreen slide
    float                pageHeight     = 540.0f;  // points: 7.5 in
    float                recordedWidth  = 1920.0f; // pixels of the frame the pages were recorded in
    float                recordedHeight = 1080.0f;
    std::vector<PdfFont> fonts          = {}; // the body face first: any face without its own bytes is set in it

    /// a picture as outlines, where the viewer can make them -- a formula, a drawing; nothing leaves it to later steps
    std::function<std::optional<VectorDrawing>(const RecordedImage&)> vectorFor = {};

    /// a picture's own pixels, uncropped, where it is not outlines and was not read back from the frame
    std::function<std::optional<RecordedPixels>(const RecordedImage&)> pixelsFor = {};
};

/**
 * One PDF page per recording, in libharu: text is text in the embedded faces, so it can be searched and copied, and
 * shapes and the pictures `vectorFor` can outline are paths, and anything else is embedded as the
 * picture it is: a photograph as JPEG, anything with sharp edges or transparency losslessly. Errors are returned, never thrown.
 */
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> writePdf(const PdfDocument& document, std::span<const PageRecording> pages);

} // namespace gr::present

#endif // GR4_PRESENT_EXPORT_PDF_WRITER_HPP
