#include "Viewer.hpp"

#include "MathRender.hpp"

#include <gnuradio-4.0/Logger.hpp>

#ifdef GR4_PRESENT_HAS_EXPORT
#include <gnuradio-4.0/algorithm/fileio/FileIo.hpp>
#include <gr4-present/export/PdfWriter.hpp>
#endif

#include <imgui.h>

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

[[nodiscard]] static double      liveWaitOf(const Viewer& viewer, std::string_view viewId);
[[nodiscard]] static std::string fileNameOf(std::string_view title, std::string_view mode);
static void                      writeExport(Viewer& viewer);
static void                      tendExportDelivery(Viewer& viewer);

/// `--export slides` makes a page of every section at its last step, `--export steps` one of every step a key reaches;
/// sections in document order, branches included
void startExport(Viewer& viewer, std::string_view mode, bool exitWhenDone) {
#ifdef GR4_PRESENT_HAS_EXPORT
    if (viewer.exporting || viewer.sections.empty() || viewer.navigator.graph.views.empty()) {
        return;
    }
    ExportWalk walk;
    walk.mode         = std::string{mode};
    walk.exitWhenDone = exitWhenDone;
    walk.resumeAt     = viewer.navigator.cursor;
    SDL_GetWindowSize(viewer.window, &walk.windowWidth, &walk.windowHeight);
    const bool everyStep = mode == "steps";
    for (const View& view : viewer.navigator.graph.views) {
        const std::size_t steps = std::max(view.stepCount, 1UZ);
        for (std::size_t step = everyStep ? 0UZ : steps - 1UZ; step < steps; ++step) {
            walk.pages.push_back(Cursor{.viewId = view.id, .step = step});
        }
    }
    viewer.exporting = std::move(walk);
    SDL_SetWindowSize(viewer.window, 1920, 1080); // the size a page is laid out at, whatever the screen
#else
    (void)viewer;
    (void)mode;
    (void)exitWhenDone;
#endif
}

void beginExport(Viewer& viewer) {
    if (const auto asked = viewer.options.value("export"); asked.has_value() && !viewer.exporting) {
        startExport(viewer, *asked, true);
    }
}

void tendExportDelivery(Viewer& viewer);

/// how long a page's live charts run before it is taken: the longest a region asks for, else the deck's `live_wait`;
/// nothing for a page without them, or whose regions need a device and show their recording
[[nodiscard]] static double liveWaitOf(const Viewer& viewer, std::string_view viewId) {
    const auto section = std::ranges::find_if(viewer.sections, [viewId](const Section& candidate) { return (candidate.id.empty() ? std::string{"start"} : candidate.id) == viewId; });
    double     wait    = 0.0;
    if (section == viewer.sections.end() || !liveGraphsAvailable()) {
        return wait;
    }
    for (const Block& block : section->document.blocks) {
        if (block.kind != BlockKind::directive || block.info != "gr4" || block.field("workflow").empty()) {
            continue;
        }
        const LiveRegion binding = liveRegionFrom(block.fields, 0UZ);
        if (binding.needs.empty()) {
            wait = std::max(wait, static_cast<double>(binding.exportWait >= 0.0f ? binding.exportWait : viewer.loader.manifest().liveWaitSeconds));
        }
    }
    return wait;
}

/// puts the cursor on the page being exported -- no transition, every reveal complete -- and, once it has settled,
/// gives this frame a recording; true when it does
[[nodiscard]] bool prepareExportPage(Viewer& viewer) {
    ExportWalk& walk = *viewer.exporting;
    if (walk.next >= walk.pages.size()) {
        tendExportDelivery(viewer); // every page is recorded; the last one stays on screen while the file goes out
        return false;
    }
    if (walk.frames == 0) {
        viewer.navigator.cursor = walk.pages[walk.next];
        viewer.transition       = Transition{};
        viewer.stepSince        = -1.0;
        viewer.arrivedAt        = -1.0;
        viewer.zoom.reset();
        walk.since = ImGui::GetTime();
    }
    ++walk.frames;
    constexpr int    kSettleFrames  = 3;   // figures and formulas are made in the frames after they are first asked for
    constexpr double kSettleSeconds = 0.6; // past the 0.3 s a stop's words take to fade in
    if (walk.frames < kSettleFrames || ImGui::GetTime() - walk.since < std::max(kSettleSeconds, liveWaitOf(viewer, walk.pages[walk.next].viewId))) {
        return false;
    }
    // a section's first page carries its bookmark and the presenter's notes; a later step of it carries neither
    const Cursor&  at             = walk.pages[walk.next];
    const bool     opening        = walk.next == 0UZ || walk.pages[walk.next - 1UZ].viewId != at.viewId;
    const auto     section        = std::ranges::find_if(viewer.sections, [&at](const Section& candidate) { return (candidate.id.empty() ? std::string{"start"} : candidate.id) == at.viewId; });
    PageRecording& recording      = walk.recorded.emplace_back();
    recording.viewId              = at.viewId;
    recording.title               = opening ? titleOf(viewer, at.viewId) : std::string{};
    recording.notes               = opening && section != viewer.sections.end() ? notesOf(section->document) : std::string{};
    viewer.documentView.recording = &recording;
    return true;
}

/// the file name a deck's title suggests: lower case, a dash for anything that is not a letter or a digit
[[nodiscard]] static std::string fileNameOf(std::string_view title, std::string_view mode) {
    std::string name;
    for (const char letter : title) {
        const bool kept = std::isalnum(static_cast<unsigned char>(letter)) != 0;
        if (kept || (!name.empty() && name.back() != '-')) {
            name += kept ? static_cast<char>(std::tolower(static_cast<unsigned char>(letter))) : '-';
        }
    }
    while (!name.empty() && name.back() == '-') {
        name.pop_back();
    }
    return std::format("{}-{}.pdf", name.empty() ? "deck" : name, mode);
}

/// writes the PDF once every page is recorded: natively to `--export-to`, in a browser as a download
static void writeExport(Viewer& viewer) {
#ifdef GR4_PRESENT_HAS_EXPORT
    ExportWalk&        walk  = *viewer.exporting;
    const FrontMatter& front = viewer.sections.front().document.front;
    const std::string  title = front.title.empty() ? viewer.loader.manifest().title : front.title;
    PdfDocument        document{.title = title,
               .author                 = front.author,
               .recordedWidth          = walk.recordedWidth,
               .recordedHeight         = walk.recordedHeight, //
               .fonts                  = {PdfFont{.face = RecordedFace::body, .ttf = builtinFaceTtf(BuiltinFace::body)}, PdfFont{.face = RecordedFace::bold, .ttf = builtinFaceTtf(BuiltinFace::bold)}, PdfFont{.face = RecordedFace::italic, .ttf = builtinFaceTtf(BuiltinFace::italic)}, PdfFont{.face = RecordedFace::boldItalic, .ttf = builtinFaceTtf(BuiltinFace::boldItalic)}, PdfFont{.face = RecordedFace::mono, .ttf = builtinFaceTtf(BuiltinFace::mono)}}};
    for (const Fonts::DeckFace& deckFace : Fonts::instance().deckFaces()) {
        document.fonts.push_back(PdfFont{.face = RecordedFace::body, .ttf = deckFace.ttf, .deckFace = deckFace.name}); // the deck's faces, as they were drawn
    }
    // A formula is outlined again from its LaTeX, in the colour it was tinted on the slide, and a drawing from its SVG
    // without the elements the slide left out, so both are drawn as vectors. A master is on many pages: it is
    // outlined once for each set of hidden elements.
    std::map<std::string, std::optional<VectorDrawing>> drawings;
    document.vectorFor = [&viewer, &drawings](const RecordedImage& image) -> std::optional<VectorDrawing> {
        if (image.kind == RecordedImageKind::formula && image.size > 0.0f) {
            auto outlined = outlineFormula(image.source, image.size, image.tint | 0xFF000000U, image.display);
            return outlined.has_value() ? std::optional{std::move(*outlined)} : std::nullopt;
        }
        if (image.kind != RecordedImageKind::figure && image.kind != RecordedImageKind::master) {
            return std::nullopt;
        }
        std::string key = image.source;
        for (const std::string& hidden : image.hidden) {
            key += '\n' + hidden;
        }
        auto [cached, added] = drawings.try_emplace(key);
        if (added) {
            auto outlined = outlineSvg(viewer.loader.figureBytes(image.source), image.hidden);
            if (outlined.has_value()) {
                cached->second = std::move(*outlined);
            } else {
                gr::log::warning("{} stays a picture in the PDF: {}", image.source, outlined.error());
            }
        }
        return cached->second;
    };
    // a picture's own pixels, at the resolution of its file rather than of the screen it was shown on; a QR code
    // drawn large enough that every module is many pixels
    document.pixelsFor = [&viewer](const RecordedImage& image) -> std::optional<RecordedPixels> {
        if (image.kind == RecordedImageKind::qrCode) {
            constexpr int kQrPixels = 1024;
            auto          code      = renderQr(image.source, kQrPixels);
            return code.has_value() ? std::optional{RecordedPixels{.width = code->width, .height = code->height, .rgba = std::move(code->rgba)}} : std::nullopt;
        }
        if (image.kind != RecordedImageKind::picture && image.kind != RecordedImageKind::figure) {
            return std::nullopt;
        }
        auto decoded = decodeAnimation(viewer.loader.figureBytes(image.source));
        if (!decoded.has_value() || decoded->frames.empty()) {
            return std::nullopt;
        }
        return RecordedPixels{.width = decoded->width, .height = decoded->height, .rgba = std::move(decoded->frames.front())};
    };
    const auto written = writePdf(document, walk.recorded);
    if (!written.has_value()) {
        walk.outcome = "failed: " + written.error();
        return;
    }
    const std::string& mode = walk.mode;
#ifdef __EMSCRIPTEN__
    // a blocking write is not allowed on the browser's main thread: the download is handed over, and finishes later
    if (auto delivery = gr::algorithm::fileio::writeAsync("download:/" + fileNameOf(title, mode), *written); delivery.has_value()) {
        walk.delivery = std::move(*delivery);
    } else {
        walk.outcome = "failed: " + delivery.error().message;
    }
#else
    const std::string target = std::filesystem::absolute(std::string{viewer.options.value("export-to").value_or(fileNameOf(title, mode))}).string();
    if (const auto saved = gr::algorithm::fileio::write("file:" + target, *written); !saved.has_value()) {
        walk.outcome = std::format("failed writing {}: {}", target, saved.error().message);
    } else {
        walk.outcome = std::format("exported {} pages to {}", walk.recorded.size(), target);
    }
#endif
#else
    viewer.exporting->outcome = "failed: this viewer was built without the PDF export";
#endif
}

/// after the last page: a browser's download finishing, then the outcome said -- and a native export ends there
static void tendExportDelivery(Viewer& viewer) {
    ExportWalk& walk = *viewer.exporting;
#ifdef GR4_PRESENT_HAS_EXPORT
    if (walk.delivery.has_value() && walk.delivery->finished()) {
        const auto result = walk.delivery->result();
        walk.outcome      = result.has_value() ? std::format("exported {} pages", walk.recorded.size()) : "failed: " + result.error().message;
        walk.delivery.reset();
    }
#endif
    if (walk.outcome.empty()) {
        return;
    }
#ifdef __EMSCRIPTEN__
    publishExport(static_cast<int>(walk.recorded.size()), walk.outcome.c_str());
#else
    std::fprintf(stderr, "%s\n", walk.outcome.c_str());
    if (walk.exitWhenDone) {
        viewer.isRunning = false;
        return;
    }
    viewer.navigator.cursor = walk.resumeAt;
    SDL_SetWindowSize(viewer.window, walk.windowWidth, walk.windowHeight);
    viewer.exporting.reset();
#endif
}

void finishExportPage(Viewer& viewer) {
    ExportWalk& walk              = *viewer.exporting;
    viewer.documentView.recording = nullptr;
    walk.recordedWidth            = ImGui::GetMainViewport()->Size.x;
    walk.recordedHeight           = ImGui::GetMainViewport()->Size.y;
    walk.frames                   = 0;
    ++walk.next;
}

/// after the frame a page was recorded in has been rendered: what it showed of clips and charts, read back from the
/// framebuffer, which is the only source they have -- and once the last page has it, the PDF is written
void captureExportPixels(Viewer& viewer) {
    ExportWalk& walk = *viewer.exporting;
    if (walk.recorded.empty() || walk.captured == walk.recorded.size()) {
        return;
    }
    walk.captured         = walk.recorded.size();
    int framebufferWidth  = 0;
    int framebufferHeight = 0;
    SDL_GetWindowSizeInPixels(viewer.window, &framebufferWidth, &framebufferHeight);
    const float perPixel = ImGui::GetIO().DisplaySize.x > 0.0f ? static_cast<float>(framebufferWidth) / ImGui::GetIO().DisplaySize.x : 1.0f;
    for (RecordedPrimitive& primitive : walk.recorded.back().primitives) {
        auto* image = std::get_if<RecordedImage>(&primitive);
        if (image == nullptr || (image->kind != RecordedImageKind::video && image->kind != RecordedImageKind::chart)) {
            continue;
        }
        const int left   = std::clamp(static_cast<int>(std::lround(image->at.x * perPixel)), 0, framebufferWidth);
        const int top    = std::clamp(static_cast<int>(std::lround(image->at.y * perPixel)), 0, framebufferHeight);
        const int width  = std::clamp(static_cast<int>(std::lround(image->at.width * perPixel)), 0, framebufferWidth - left);
        const int height = std::clamp(static_cast<int>(std::lround(image->at.height * perPixel)), 0, framebufferHeight - top);
        if (width <= 0 || height <= 0) {
            continue;
        }
        RecordedPixels pixels{.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ)};
        glReadPixels(left, framebufferHeight - top - height, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
        // OpenGL counts rows from the bottom; a recording, from the top
        const std::size_t stride = static_cast<std::size_t>(width) * 4UZ;
        for (int row = 0; row < height / 2; ++row) {
            std::swap_ranges(pixels.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row) * stride), pixels.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row + 1) * stride), pixels.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(height - 1 - row) * stride));
        }
        for (std::size_t alpha = 3UZ; alpha < pixels.rgba.size(); alpha += 4UZ) {
            pixels.rgba[alpha] = 255U; // the framebuffer's alpha is not the picture's: what shows is opaque
        }
        image->pixels = std::move(pixels);
    }
    if (walk.next == walk.pages.size()) {
        writeExport(viewer);
    }
}

} // namespace gr::present
