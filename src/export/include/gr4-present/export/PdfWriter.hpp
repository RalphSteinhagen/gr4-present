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

struct PdfDocument {
    std::string          title;
    std::string          author;
    float                pageWidthPoints     = 960.0f;  // points: 13.33 in, PowerPoint's widescreen slide
    float                pageHeightPoints    = 540.0f;  // points: 7.5 in
    float                recordedWidthPixels = 1920.0f; // pixels of the frame the pages were recorded in
    std::vector<PdfFont> fonts               = {};      // the body face first: any face without its own bytes is set in it
    float                imageDpi            = 0.0f;    // above zero, no picture keeps more pixels per inch than this where drawn

    /// a picture as outlines, where the viewer can make them (a formula, a drawing); null where it cannot; a drawing
    /// stays where it is until `writePdf` returns
    std::function<const VectorDrawing*(const RecordedImage&)> vectorFor = {};

    /// a picture's own pixels, uncropped, where it is not outlines and was not read back from the frame
    std::function<std::optional<RecordedPixels>(const RecordedImage&)> pixelsFor = {};
};

/// one PDF page per recording, in libharu: text stays searchable text in embedded faces, shapes and outlined pictures
/// are paths, any other picture embeds as JPEG (photographs, and opaque pictures that deflate poorly) or losslessly,
/// averaged down to `imageDpi` where it has more pixels (a QR code excepted); errors are returned, never thrown
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> writePdf(const PdfDocument& document, std::span<const PageRecording> pages);

} // namespace gr::present

#endif // GR4_PRESENT_EXPORT_PDF_WRITER_HPP
