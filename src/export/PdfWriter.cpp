#include <gr4-present/export/PdfWriter.hpp>

#include "JpegEncoder.hpp"

#include <hpdf.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <utility>
#include <variant>

namespace gr::present {

namespace {

struct Failure {
    std::string message; // the first error libharu reported; later ones follow from it
};

void reportError(HPDF_STATUS error, HPDF_STATUS detail, void* userData) {
    auto* failure = static_cast<Failure*>(userData);
    if (failure->message.empty()) {
        failure->message = std::format("libharu error 0x{:04X}, detail {}", static_cast<unsigned>(error), static_cast<unsigned>(detail));
    }
}

/// ImGui packs a colour as 0xAABBGGRR
struct Colour {
    float red, green, blue, alpha;
};

[[nodiscard]] Colour colourOf(std::uint32_t packed) noexcept { return Colour{.red = static_cast<float>(packed & 0xFFU) / 255.0f, .green = static_cast<float>((packed >> 8U) & 0xFFU) / 255.0f, .blue = static_cast<float>((packed >> 16U) & 0xFFU) / 255.0f, .alpha = static_cast<float>(packed >> 24U) / 255.0f}; }

/// a loaded face, and how ImGui's pixel size relates to a PDF font size for it
struct LoadedFace {
    HPDF_Font font      = nullptr;
    float     emPerSize = 1.0f; // ImGui sizes a face by ascent minus descent; a PDF by its em
    float     ascent    = 0.8f; // of ImGui's size, from the top of the line to the baseline
};

/// the page as it is drawn: one transform from recorded pixels to points, and the graphics states alpha needs
struct PageWriter {
    HPDF_Doc                                                                  pdf;
    HPDF_Page                                                                 page;
    const std::map<RecordedFace, LoadedFace>&                                 faces;
    const std::map<std::string, LoadedFace, std::less<>>&                     deckFaces;
    std::map<int, HPDF_ExtGState>&                                            alphaStates;
    float                                                                     scale;
    float                                                                     height;
    const std::function<std::optional<VectorDrawing>(const RecordedImage&)>&  vectorFor;
    const std::function<std::optional<RecordedPixels>(const RecordedImage&)>& pixelsFor;
    std::map<std::string, HPDF_Image>&                                        images; // one embedding per picture and crop

    [[nodiscard]] float x(float pixels) const noexcept { return pixels * scale; }
    [[nodiscard]] float y(float pixels) const noexcept { return height - pixels * scale; }

    void alpha(float value) const {
        const int step      = std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
        auto [state, added] = alphaStates.try_emplace(step, nullptr);
        if (added) {
            state->second = HPDF_CreateExtGState(pdf);
            HPDF_ExtGState_SetAlphaFill(state->second, static_cast<HPDF_REAL>(step) / 255.0f);
            HPDF_ExtGState_SetAlphaStroke(state->second, static_cast<HPDF_REAL>(step) / 255.0f);
        }
        HPDF_Page_SetExtGState(page, state->second);
    }

    /// the clip a primitive was drawn under, from here until the matching `end()`
    void begin(const Rectangle& clip) const {
        HPDF_Page_GSave(page);
        if (clip.width > 0.0f && clip.height > 0.0f) {
            HPDF_Page_Rectangle(page, x(clip.x), y(clip.y + clip.height), clip.width * scale, clip.height * scale);
            HPDF_Page_Clip(page);
            HPDF_Page_EndPath(page);
        }
    }
    void end() const { HPDF_Page_GRestore(page); }

    /// what is drawn next is turned as the view turned `image`, anticlockwise as seen, about its pivot
    void turned(const RecordedImage& image) const {
        if (image.turn == 0.0f) {
            return;
        }
        const float radians = image.turn * std::numbers::pi_v<float> / 180.0f;
        const float cosine  = std::cos(radians);
        const float sine    = std::sin(radians);
        const float px      = x(image.pivotX);
        const float py      = y(image.pivotY);
        HPDF_Page_Concat(page, cosine, sine, -sine, cosine, px - px * cosine + py * sine, py - px * sine - py * cosine);
    }

    void path(const RecordedShape& shape) const {
        const std::vector<float>& p = shape.points;
        switch (shape.kind) {
        case RecordedShapeKind::line:
        case RecordedShapeKind::polyline:
        case RecordedShapeKind::closedPolyline:
        case RecordedShapeKind::filledTriangle:
            HPDF_Page_MoveTo(page, x(p[0]), y(p[1]));
            for (std::size_t index = 2UZ; index + 1UZ < p.size(); index += 2UZ) {
                HPDF_Page_LineTo(page, x(p[index]), y(p[index + 1UZ]));
            }
            if (shape.kind == RecordedShapeKind::closedPolyline || shape.kind == RecordedShapeKind::filledTriangle) {
                HPDF_Page_ClosePath(page);
            }
            break;
        case RecordedShapeKind::rect:
        case RecordedShapeKind::filledRect: rectangle(p[0], p[1], p[2], p[3], shape.rounding); break;
        case RecordedShapeKind::filledCircle: HPDF_Page_Circle(page, x(p[0]), y(p[1]), (p[2] - p[0]) * scale); break;
        }
    }

    /// a rectangle between two corners, with its corners rounded as ImGui rounds them
    void rectangle(float left, float top, float right, float bottom, float rounding) const {
        const float r = std::min({rounding, (right - left) * 0.5f, (bottom - top) * 0.5f});
        if (r <= 0.0f) {
            HPDF_Page_Rectangle(page, x(left), y(bottom), (right - left) * scale, (bottom - top) * scale);
            return;
        }
        constexpr float kArc = 0.5523f; // a quarter circle as one cubic: the control points sit this far along the tangents
        const float     k    = r * kArc;
        HPDF_Page_MoveTo(page, x(left + r), y(top));
        HPDF_Page_LineTo(page, x(right - r), y(top));
        HPDF_Page_CurveTo(page, x(right - r + k), y(top), x(right), y(top + r - k), x(right), y(top + r));
        HPDF_Page_LineTo(page, x(right), y(bottom - r));
        HPDF_Page_CurveTo(page, x(right), y(bottom - r + k), x(right - r + k), y(bottom), x(right - r), y(bottom));
        HPDF_Page_LineTo(page, x(left + r), y(bottom));
        HPDF_Page_CurveTo(page, x(left + r - k), y(bottom), x(left), y(bottom - r + k), x(left), y(bottom - r));
        HPDF_Page_LineTo(page, x(left), y(top + r));
        HPDF_Page_CurveTo(page, x(left), y(top + r - k), x(left + r - k), y(top), x(left + r), y(top));
        HPDF_Page_ClosePath(page);
    }

    void draw(const RecordedShape& shape) const {
        if (shape.points.size() < 4UZ) {
            return;
        }
        const Colour colour = colourOf(shape.colour);
        const bool   filled = shape.kind == RecordedShapeKind::filledRect || shape.kind == RecordedShapeKind::filledTriangle || shape.kind == RecordedShapeKind::filledCircle;
        begin(shape.clip);
        alpha(colour.alpha);
        if (filled) {
            HPDF_Page_SetRGBFill(page, colour.red, colour.green, colour.blue);
        } else {
            HPDF_Page_SetRGBStroke(page, colour.red, colour.green, colour.blue);
            HPDF_Page_SetLineWidth(page, std::max(shape.thickness, 0.5f) * scale);
        }
        path(shape);
        if (filled) {
            HPDF_Page_Fill(page);
        } else {
            HPDF_Page_Stroke(page);
        }
        end();
    }

    /// a drawing's words: its font size is an em, its y a baseline, and its anchor moves it by its own width
    void draw(const DrawingText& text, float shiftX, float shiftY, float span, float opacity) const {
        const auto known = faces.find(text.face);
        const auto face  = known != faces.end() ? known : faces.find(RecordedFace::body);
        if (face == faces.end() || text.text.empty() || text.em <= 0.0f) {
            return;
        }
        const Colour colour = colourOf(text.colour);
        const float  size   = text.em * span * scale;
        alpha(colour.alpha * opacity);
        HPDF_Page_SetRGBFill(page, colour.red, colour.green, colour.blue);
        HPDF_Page_SetFontAndSize(page, face->second.font, size);
        const float wide = HPDF_Page_TextWidth(page, text.text.c_str());
        const float back = text.anchor == DrawingText::Anchor::middle ? wide * 0.5f : (text.anchor == DrawingText::Anchor::end ? wide : 0.0f);
        // the baseline runs the way the drawing turned it, and an anchor moves the start back along it
        const float radians = text.angle * std::numbers::pi_v<float> / 180.0f;
        const float cosine  = std::cos(radians);
        const float sine    = std::sin(radians);
        HPDF_Page_BeginText(page);
        HPDF_Page_SetTextMatrix(page, cosine, sine, -sine, cosine, x(shiftX + text.x * span) - back * cosine, y(shiftY + text.y * span) - back * sine);
        HPDF_Page_ShowText(page, text.text.c_str());
        HPDF_Page_EndText(page);
    }

    void draw(const RecordedText& text) const {
        const auto  known  = faces.find(text.face);
        const auto  role   = known != faces.end() ? known : faces.find(RecordedFace::body);
        const auto  deck   = text.deckFace.empty() ? deckFaces.end() : deckFaces.find(text.deckFace);
        const auto* loaded = deck != deckFaces.end() ? &deck->second : (role != faces.end() ? &role->second : nullptr);
        if (loaded == nullptr || text.text.empty()) {
            return;
        }
        const Colour colour = colourOf(text.colour);
        begin(text.clip);
        alpha(colour.alpha);
        HPDF_Page_SetRGBFill(page, colour.red, colour.green, colour.blue);
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, loaded->font, text.size * loaded->emPerSize * scale);
        HPDF_Page_TextOut(page, x(text.x), y(text.y + text.size * loaded->ascent), text.text.c_str());
        HPDF_Page_EndText(page);
        end();
    }

    /// a picture the viewer could outline: its paths and words mapped from the picture's own pixels -- the part of it
    /// the slide showed -- onto where it was drawn, cut to that rectangle and to the clip it was drawn under
    [[nodiscard]] bool drawOutlines(const RecordedImage& image) const {
        if (!vectorFor) {
            return false;
        }
        const std::optional<VectorDrawing> drawing = vectorFor(image);
        if (!drawing.has_value() || drawing->width <= 0.0f || drawing->height <= 0.0f) {
            return false;
        }
        const float spanX   = image.at.width / (image.uv.width * drawing->width); // recorded pixels per drawing pixel
        const float spanY   = image.at.height / (image.uv.height * drawing->height);
        const float shiftX  = image.at.x - image.uv.x * drawing->width * spanX;
        const float shiftY  = image.at.y - image.uv.y * drawing->height * spanY;
        const float opacity = colourOf(image.tint).alpha;
        begin(image.clip);
        turned(image);
        HPDF_Page_Rectangle(page, x(image.at.x), y(image.at.y + image.at.height), image.at.width * scale, image.at.height * scale);
        HPDF_Page_Clip(page);
        HPDF_Page_EndPath(page);
        for (const VectorPath& path : drawing->paths) {
            const Colour fill   = colourOf(path.fill);
            const Colour stroke = colourOf(path.stroke);
            // libharu keeps a path's state strictly: one that does not begin with a move, or has nothing to fill, is skipped
            if ((fill.alpha <= 0.0f && stroke.alpha <= 0.0f) || path.steps.empty() || path.steps.front() != VectorPath::Step::move) {
                continue;
            }
            // the state a path is painted with is set before it begins: libharu allows nothing else inside a path
            alpha(std::max(fill.alpha, stroke.alpha) * opacity);
            if (fill.alpha > 0.0f) {
                HPDF_Page_SetRGBFill(page, fill.red, fill.green, fill.blue);
            }
            if (stroke.alpha > 0.0f) {
                HPDF_Page_SetRGBStroke(page, stroke.red, stroke.green, stroke.blue);
                HPDF_Page_SetLineWidth(page, std::max(path.strokeWidth * spanX, 0.25f) * scale);
            }
            std::size_t at   = 0UZ;
            const auto  next = [&](float& px, float& py) {
                px = x(shiftX + path.points[at] * spanX);
                py = y(shiftY + path.points[at + 1UZ] * spanY);
                at += 2UZ;
            };
            for (const VectorPath::Step step : path.steps) {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, x3 = 0.0f, y3 = 0.0f;
                switch (step) {
                case VectorPath::Step::move:
                    next(x1, y1);
                    HPDF_Page_MoveTo(page, x1, y1);
                    break;
                case VectorPath::Step::line:
                    next(x1, y1);
                    HPDF_Page_LineTo(page, x1, y1);
                    break;
                case VectorPath::Step::cubic:
                    next(x1, y1);
                    next(x2, y2);
                    next(x3, y3);
                    HPDF_Page_CurveTo(page, x1, y1, x2, y2, x3, y3);
                    break;
                case VectorPath::Step::close: HPDF_Page_ClosePath(page); break;
                }
            }
            if (fill.alpha > 0.0f && stroke.alpha > 0.0f) {
                path.evenOdd ? HPDF_Page_EofillStroke(page) : HPDF_Page_FillStroke(page);
            } else if (fill.alpha > 0.0f) {
                path.evenOdd ? HPDF_Page_Eofill(page) : HPDF_Page_Fill(page);
            } else {
                HPDF_Page_Stroke(page);
            }
        }
        for (const DrawingText& text : drawing->texts) {
            // words the slide cut off are left out too: hidden by the clip, they would still be found by a search
            const float atX = shiftX + text.x * spanX;
            const float atY = shiftY + text.y * spanY;
            if (atX >= image.at.x && atX <= image.at.x + image.at.width && atY >= image.at.y && atY <= image.at.y + image.at.height) {
                draw(text, shiftX, shiftY, spanY, opacity);
            }
        }
        end();
        return true;
    }

    /// a link is placed once every page exists, since it may lead to one not yet written
    void draw(const RecordedLink&) const {}

    /// a picture as itself: what the frame showed of it, or its own pixels cut to the part the slide showed
    void draw(const RecordedImage& image) const {
        if (image.at.width <= 0.0f || image.at.height <= 0.0f || image.uv.width <= 0.0f || image.uv.height <= 0.0f || drawOutlines(image)) {
            return;
        }
        const bool                    captured = !image.pixels.rgba.empty();
        std::optional<RecordedPixels> own      = captured || !pixelsFor ? std::nullopt : pixelsFor(image);
        const RecordedPixels*         pixels   = captured ? &image.pixels : (own.has_value() ? &*own : nullptr);
        if (pixels == nullptr || pixels->width == 0U || pixels->height == 0U) {
            return;
        }
        const Rectangle uv       = captured ? Rectangle{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f} : image.uv;
        const auto      key      = std::format("{}\n{}\n{} {} {} {}", static_cast<int>(image.kind), image.source, uv.x, uv.y, uv.width, uv.height);
        HPDF_Image      embedded = captured ? nullptr : (images.contains(key) ? images.at(key) : nullptr);
        if (embedded == nullptr) {
            embedded = embed(*pixels, uv, image.kind == RecordedImageKind::picture);
            if (embedded == nullptr) {
                return;
            }
            if (!captured) {
                images.emplace(key, embedded);
            }
        }
        begin(image.clip);
        turned(image);
        alpha(colourOf(image.tint).alpha);
        HPDF_Page_DrawImage(page, embedded, x(image.at.x), y(image.at.y + image.at.height), image.at.width * scale, image.at.height * scale);
        end();
    }

    /// the part `uv` of `pixels` as an image of this document: a photograph without transparency as JPEG, anything
    /// else losslessly, with its transparency as a soft mask
    [[nodiscard]] HPDF_Image embed(const RecordedPixels& pixels, const Rectangle& uv, bool photograph) const {
        const auto left = static_cast<std::uint32_t>(std::clamp(uv.x, 0.0f, 1.0f) * static_cast<float>(pixels.width));
        const auto top  = static_cast<std::uint32_t>(std::clamp(uv.y, 0.0f, 1.0f) * static_cast<float>(pixels.height));
        const auto wide = std::min(pixels.width - left, std::max(1U, static_cast<std::uint32_t>(std::lround(uv.width * static_cast<float>(pixels.width)))));
        const auto tall = std::min(pixels.height - top, std::max(1U, static_cast<std::uint32_t>(std::lround(uv.height * static_cast<float>(pixels.height)))));
        if (wide == 0U || tall == 0U || pixels.rgba.size() < static_cast<std::size_t>(pixels.width) * pixels.height * 4UZ) {
            return nullptr;
        }
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(wide) * tall * 3UZ);
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(wide) * tall);
        bool                      opaque = true;
        for (std::uint32_t row = 0U; row < tall; ++row) {
            for (std::uint32_t column = 0U; column < wide; ++column) {
                const std::size_t from = ((static_cast<std::size_t>(top) + row) * pixels.width + left + column) * 4UZ;
                const std::size_t to   = static_cast<std::size_t>(row) * wide + column;
                rgb[to * 3UZ]          = pixels.rgba[from];
                rgb[to * 3UZ + 1UZ]    = pixels.rgba[from + 1UZ];
                rgb[to * 3UZ + 2UZ]    = pixels.rgba[from + 2UZ];
                mask[to]               = pixels.rgba[from + 3UZ];
                opaque                 = opaque && mask[to] == 255U;
            }
        }
        constexpr int kPhotographQuality = 90;
        if (photograph && opaque) {
            if (const std::vector<std::uint8_t> jpeg = encodeJpeg(rgb, wide, tall, kPhotographQuality); !jpeg.empty()) {
                return HPDF_LoadJpegImageFromMem(pdf, jpeg.data(), static_cast<HPDF_UINT>(jpeg.size()));
            }
        }
        HPDF_Image image = HPDF_LoadRawImageFromMem(pdf, rgb.data(), wide, tall, HPDF_CS_DEVICE_RGB, 8);
        if (image != nullptr && !opaque) {
            HPDF_Image_AddSMask(image, HPDF_LoadRawImageFromMem(pdf, mask.data(), wide, tall, HPDF_CS_DEVICE_GRAY, 8));
        }
        return image;
    }
};

[[nodiscard]] LoadedFace loadFace(HPDF_Doc pdf, std::span<const std::uint8_t> ttf) {
    const char* name = HPDF_LoadTTFontFromMemory(pdf, ttf.data(), static_cast<HPDF_UINT>(ttf.size()), HPDF_TRUE);
    if (name == nullptr) {
        return LoadedFace{};
    }
    HPDF_Font   font    = HPDF_GetFont(pdf, name, "UTF-8");
    const float ascent  = static_cast<float>(HPDF_Font_GetAscent(font));
    const float descent = static_cast<float>(HPDF_Font_GetDescent(font)); // negative, below the baseline
    const float tall    = ascent - descent;
    return tall > 0.0f ? LoadedFace{.font = font, .emPerSize = 1000.0f / tall, .ascent = ascent / tall} : LoadedFace{.font = font};
}

} // namespace

std::expected<std::vector<std::uint8_t>, std::string> writePdf(const PdfDocument& document, std::span<const PageRecording> pages) {
    Failure                                                                    failure;
    const std::unique_ptr<std::remove_pointer_t<HPDF_Doc>, void (*)(HPDF_Doc)> pdf{HPDF_New(reportError, &failure), HPDF_Free};
    if (!pdf) {
        return std::unexpected("libharu could not create a document");
    }
    HPDF_SetCompressionMode(pdf.get(), HPDF_COMP_ALL);
    HPDF_UseUTFEncodings(pdf.get());
    HPDF_SetCurrentEncoder(pdf.get(), "UTF-8");
    HPDF_SetInfoAttr(pdf.get(), HPDF_INFO_TITLE, document.title.c_str());
    HPDF_SetInfoAttr(pdf.get(), HPDF_INFO_AUTHOR, document.author.c_str());
    HPDF_SetInfoAttr(pdf.get(), HPDF_INFO_CREATOR, "gr4-present");

    std::map<RecordedFace, LoadedFace>             faces;
    std::map<std::string, LoadedFace, std::less<>> deckFaces;
    for (const PdfFont& font : document.fonts) {
        if (!font.deckFace.empty()) {
            if (const LoadedFace loaded = font.ttf.empty() ? LoadedFace{} : loadFace(pdf.get(), font.ttf); loaded.font != nullptr) {
                deckFaces.emplace(font.deckFace, loaded);
            }
            continue;
        }
        if (!faces.contains(font.face) && !font.ttf.empty()) {
            if (const LoadedFace loaded = loadFace(pdf.get(), font.ttf); loaded.font != nullptr) {
                faces.emplace(font.face, loaded);
            }
        }
    }
    if (!failure.message.empty()) {
        return std::unexpected(std::format("loading the fonts: {}", failure.message));
    }

    std::map<int, HPDF_ExtGState>     alphaStates;
    std::map<std::string, HPDF_Image> images;
    const float                       scale = document.pageWidth / document.recordedWidth;
    std::vector<HPDF_Page>            written;
    for (const PageRecording& recording : pages) {
        HPDF_Page page = HPDF_AddPage(pdf.get());
        written.push_back(page);
        if (page == nullptr || !failure.message.empty()) {
            return std::unexpected(std::format("page {}: {}", &recording - pages.data() + 1, failure.message));
        }
        HPDF_Page_SetWidth(page, document.pageWidth);
        HPDF_Page_SetHeight(page, document.pageHeight);
        const PageWriter writer{.pdf = pdf.get(), .page = page, .faces = faces, .deckFaces = deckFaces, .alphaStates = alphaStates, .scale = scale, .height = document.pageHeight, .vectorFor = document.vectorFor, .pixelsFor = document.pixelsFor, .images = images};
        for (const RecordedPrimitive& primitive : recording.primitives) {
            std::visit([&writer](const auto& drawn) { writer.draw(drawn); }, primitive);
        }
    }

    // What the pages lead to, now that every one exists: a bookmark per section, the presenter's notes as a comment in
    // the page's corner, and each link -- an address opens it, `#view` turns to that section's first page.
    HPDF_Encoder unicode = HPDF_GetEncoder(pdf.get(), "UTF-8");
    const auto   pageOf  = [&](std::string_view viewId) -> HPDF_Page {
        const auto found = std::ranges::find(pages, viewId, &PageRecording::viewId);
        return found == pages.end() ? nullptr : written[static_cast<std::size_t>(found - pages.begin())];
    };
    for (std::size_t index = 0UZ; index < pages.size(); ++index) {
        const PageRecording& recording = pages[index];
        HPDF_Page            page      = written[index];
        if (!recording.title.empty()) {
            HPDF_Outline bookmark = HPDF_CreateOutline(pdf.get(), nullptr, recording.title.c_str(), unicode);
            HPDF_Outline_SetDestination(bookmark, HPDF_Page_CreateDestination(page));
        }
        if (!recording.notes.empty()) {
            constexpr float kNoteSide = 18.0f; // points: the comment's icon, in the top right corner
            const HPDF_Rect corner{document.pageWidth - kNoteSide * 1.5f, document.pageHeight - kNoteSide * 1.5f, document.pageWidth - kNoteSide * 0.5f, document.pageHeight - kNoteSide * 0.5f};
            HPDF_Page_CreateTextAnnot(page, corner, recording.notes.c_str(), unicode);
        }
        for (const RecordedPrimitive& primitive : recording.primitives) {
            const auto* link = std::get_if<RecordedLink>(&primitive);
            if (link == nullptr || link->target.empty()) {
                continue;
            }
            const HPDF_Rect area{link->at.x * scale, document.pageHeight - (link->at.y + link->at.height) * scale, (link->at.x + link->at.width) * scale, document.pageHeight - link->at.y * scale};
            HPDF_Annotation annotation = nullptr;
            if (link->target.starts_with('#')) {
                if (HPDF_Page target = pageOf(std::string_view{link->target}.substr(1UZ)); target != nullptr) {
                    annotation = HPDF_Page_CreateLinkAnnot(page, area, HPDF_Page_CreateDestination(target));
                }
            } else if (link->target.contains("://") || link->target.starts_with("mailto:")) {
                annotation = HPDF_Page_CreateURILinkAnnot(page, area, link->target.c_str());
            }
            if (annotation != nullptr) {
                HPDF_LinkAnnot_SetBorderStyle(annotation, 0.0f, 0, 0); // the words are already set in the link colour
            }
        }
    }

    if (HPDF_SaveToStream(pdf.get()) != HPDF_OK || !failure.message.empty()) {
        return std::unexpected(std::format("writing the document: {}", failure.message));
    }
    std::vector<std::uint8_t> bytes(HPDF_GetStreamSize(pdf.get()));
    HPDF_UINT32               size = static_cast<HPDF_UINT32>(bytes.size());
    HPDF_ReadFromStream(pdf.get(), bytes.data(), &size);
    bytes.resize(size);
    return bytes;
}

} // namespace gr::present
