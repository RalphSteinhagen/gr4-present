#include <boost/ut.hpp>

#include "FileContents.hpp"

#include <gr4-present/export/PdfWriter.hpp>

#include <gnuradio-4.0/Compression.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

// The ground truth is poppler, not the code under test: `pdftotext` reads back the words a page carries, `pdfinfo`
// its count and size, and `pdffonts` whether a face is embedded with a map back to Unicode -- which is what makes
// the words searchable and copyable rather than shapes that happen to look like letters.
namespace {

[[nodiscard]] std::vector<std::uint8_t> bytesOf(const std::filesystem::path& path) { return fileContents<std::vector<std::uint8_t>>(path); }

[[nodiscard]] std::string outputOf(const std::string& command) {
    std::string output;
    if (FILE* pipe = popen(command.c_str(), "r"); pipe != nullptr) {
        char buffer[4096];
        while (const std::size_t read = std::fread(buffer, 1UZ, sizeof(buffer), pipe)) {
            output.append(buffer, read);
        }
        pclose(pipe);
    }
    return output;
}

[[nodiscard]] RecordedText line(std::string text, float y) { return RecordedText{.text = std::move(text), .face = RecordedFace::body, .size = 48.0f, .x = 100.0f, .y = y, .colour = 0xFF202020U, .clip = {}}; }

[[nodiscard]] std::vector<std::uint8_t> inflated(const std::vector<std::uint8_t>& packed) {
    const auto plain = gr::compression::decompress(std::as_bytes(std::span{packed}), gr::compression::Format::gzip);
    if (!plain) {
        return {};
    }
    const auto bytes = std::as_bytes(std::span{*plain});
    return {reinterpret_cast<const std::uint8_t*>(bytes.data()), reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size())};
}

/// read on first use rather than at static initialisation, which runs before the suite's dependencies are ready under libc++
[[nodiscard]] const std::vector<std::uint8_t>& testFace() {
    static const std::vector<std::uint8_t> face = inflated(bytesOf(std::filesystem::path{GR4_PRESENT_LIBERATION_DIRECTORY} / "LiberationSans-Bold.ttf.gz"));
    return face;
}

/// a picture of one colour, `rgba` packed as ImGui packs it
[[nodiscard]] RecordedPixels filled(std::uint32_t width, std::uint32_t height, std::uint32_t rgba) {
    RecordedPixels pixels{.width = width, .height = height, .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4UZ)};
    for (std::size_t index = 0UZ; index < pixels.rgba.size(); ++index) {
        pixels.rgba[index] = static_cast<std::uint8_t>(rgba >> (8U * (index % 4UZ)));
    }
    return pixels;
}

[[nodiscard]] PageRecording pageWith(RecordedImage image) {
    PageRecording page;
    page.primitives.emplace_back(std::move(image));
    return page;
}

/// the colour poppler renders at pixel (`x`, `y`) of `page` at `dpi`; -1 where it renders nothing
[[nodiscard]] std::array<int, 3UZ> pixelOf(const std::filesystem::path& pdf, int x, int y, int dpi = 72, int page = 1) {
    const auto prefix = std::filesystem::temp_directory_path() / "qa_PdfWriter_pixel"; // pdftoppm cannot write to a pipe
    std::ignore       = outputOf(std::format("pdftoppm -r {} -f {} -l {} -x {} -y {} -W 1 -H 1 -singlefile {} {}", dpi, page, page, x, y, pdf.string(), prefix.string()));
    const auto ppm    = bytesOf(prefix.string() + ".ppm");
    return ppm.size() >= 3UZ ? std::array{static_cast<int>(ppm[ppm.size() - 3UZ]), static_cast<int>(ppm[ppm.size() - 2UZ]), static_cast<int>(ppm[ppm.size() - 1UZ])} : std::array{-1, -1, -1};
}

[[nodiscard]] std::filesystem::path writtenTo(const PdfDocument& document, const std::vector<PageRecording>& pages, std::string_view name) {
    const auto path  = std::filesystem::temp_directory_path() / std::format("qa_PdfWriter_{}.pdf", name);
    const auto bytes = writePdf(document, pages);
    if (bytes.has_value()) {
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    }
    return path;
}

/// a picture drawn on a page, as `pdfimages -list` reads it: its pixels and the object every page draws it from
struct ListedImage {
    int         page;
    int         width;
    int         height;
    std::string object;
    std::string encoding; // `image` for a lossless one, `jpeg`
};

[[nodiscard]] std::vector<ListedImage> imagesOf(const std::filesystem::path& pdf) {
    std::vector<ListedImage> listed;
    for (const auto row : outputOf("pdfimages -list " + pdf.string()) | std::views::split('\n')) {
        const auto words = std::string_view{row} | std::views::split(' ') | std::views::filter([](auto word) { return !word.empty(); }) | std::views::transform([](auto word) { return std::string{std::string_view{word}}; }) | std::ranges::to<std::vector>();
        if (words.size() > 11UZ && words[2] == "image") {
            listed.push_back(ListedImage{.page = std::stoi(words[0]), .width = std::stoi(words[3]), .height = std::stoi(words[4]), .object = words[10] + ' ' + words[11], .encoding = words[8]});
        }
    }
    return listed;
}

[[nodiscard]] std::size_t embeddings(const std::vector<ListedImage>& listed) { return (listed | std::views::transform(&ListedImage::object) | std::ranges::to<std::set>()).size(); }

} // namespace

const suite<"PdfWriter"> pdfWriterTests = [] {
    "each recording is a 13.33 by 7.5 inch page whose words read back as text"_test = [] {
        expect(!testFace().empty()) << "the test face is missing";
        PageRecording first;
        first.primitives.emplace_back(line("Präzision über alles", 100.0f));
        first.primitives.emplace_back(RecordedShape{.kind = RecordedShapeKind::filledRect, .points = {80.0f, 300.0f, 900.0f, 400.0f}, .colour = 0x80FF8000U, .thickness = 0.0f, .rounding = 12.0f, .clip = {}});
        PageRecording second;
        second.primitives.emplace_back(line("second page – with a dash", 200.0f));
        const PdfDocument document{.title = "A test deck", .author = "gr4-present", .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}};
        const auto        written = writePdf(document, std::vector{first, second});
        expect(written.has_value()) << (written.has_value() ? std::string{} : written.error());
        if (!written.has_value()) {
            return;
        }
        const auto path = std::filesystem::temp_directory_path() / "qa_PdfWriter.pdf";
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(written->data()), static_cast<std::streamsize>(written->size()));

        const std::string info = outputOf("pdfinfo " + path.string());
        expect(info.contains("Pages:           2")) << info;
        expect(info.contains("Page size:       960 x 540 pts")) << info;
        expect(info.contains("Title:           A test deck")) << info;

        const std::string text = outputOf("pdftotext -enc UTF-8 " + path.string() + " -");
        expect(text.contains("Präzision über alles")) << "first page: " << text;
        expect(text.contains("second page – with a dash")) << "second page: " << text;

        const std::string fonts = outputOf("pdffonts " + path.string());
        expect(fonts.contains("yes yes yes")) << "embedded, subset, with a Unicode map: " << fonts;
    };

    // A face a deck brings is embedded beside the built-ins, and words recorded in it are set in it. pdffonts names
    // each embedded face by its PostScript name, which for this file is PatrickHand-Regular.
    "a deck's own face is embedded, and its words read back"_test = [] {
        const std::vector<std::uint8_t> hand = bytesOf(std::filesystem::path{GR4_PRESENT_PACKAGE_DIRECTORY} / "fonts/PatrickHand-Regular.ttf");
        expect(!hand.empty()) << "the deck's test face is missing";
        PageRecording page;
        page.primitives.emplace_back(line("in the body face", 100.0f));
        RecordedText handWritten = line("written by hand", 200.0f);
        handWritten.deckFace     = "hand";
        page.primitives.emplace_back(handWritten);
        const auto written = writePdf(PdfDocument{.title = "faces", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}, PdfFont{.face = RecordedFace::body, .ttf = hand, .deckFace = "hand"}}}, std::vector{page});
        expect(written.has_value()) << (written.has_value() ? std::string{} : written.error());
        if (!written.has_value()) {
            return;
        }
        const auto path = std::filesystem::temp_directory_path() / "qa_PdfWriter_faces.pdf";
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(written->data()), static_cast<std::streamsize>(written->size()));
        const std::string fonts    = outputOf("pdffonts " + path.string());
        const std::size_t handAt   = fonts.find("PatrickHand-Regular");
        const std::string handLine = handAt == std::string::npos ? std::string{} : fonts.substr(handAt, fonts.find('\n', handAt) - handAt);
        expect(handLine.contains("yes yes yes")) << "the deck's face is embedded, subset and mapped: " << fonts;
        expect(fonts.contains("LiberationSans")) << "and the body face beside it: " << fonts;
        const std::string text = outputOf("pdftotext -enc UTF-8 " + path.string() + " -");
        expect(text.contains("written by hand") && text.contains("in the body face")) << text;
    };

    // A picture the viewer can outline is drawn as paths where it was placed.
    // The drawing is 100 by 50 pixels with a red square from (25, 10) to (75, 40);
    // placed 400 by 200 at (200, 200) of a 1920-pixel frame on a 960-point page, a pixel of it is two points,
    // so the square covers points 150..250 by 120..180.
    // Rendered at 72 dpi a point is a pixel, so poppler's colour there and well outside it are the ground truth.
    "a picture outlined by the viewer is drawn as paths where it was placed"_test = [] {
        const VectorDrawing squareDrawing{.width = 100.0f, .height = 50.0f, .paths = {VectorPath{.steps = {VectorPath::Step::move, VectorPath::Step::line, VectorPath::Step::line, VectorPath::Step::line, VectorPath::Step::close}, .points = {25.0f, 10.0f, 75.0f, 10.0f, 75.0f, 40.0f, 25.0f, 40.0f}, .fill = 0xFF0000FFU, .stroke = 0U, .strokeWidth = 0.0f, .evenOdd = false}}, .texts = {}};
        const auto          square    = [&squareDrawing](const RecordedImage&) { return &squareDrawing; };
        const auto          croppedTo = [&](Rectangle uv) {
            const PageRecording page    = pageWith(RecordedImage{.kind = RecordedImageKind::figure, .source = "square", .at = Rectangle{.x = 200.0f, .y = 200.0f, .width = 400.0f, .height = 200.0f}, .uv = uv, .tint = 0xFFFFFFFFU, .clip = {}});
            const auto          written = writePdf(PdfDocument{.title = "square", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .vectorFor = square}, std::vector{page});
            const auto          path    = std::filesystem::temp_directory_path() / std::format("qa_PdfWriter_square_{}.pdf", static_cast<int>(uv.x * 10.0f));
            if (written.has_value()) {
                std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(written->data()), static_cast<std::streamsize>(written->size()));
            }
            return path;
        };

        const auto whole = croppedTo(Rectangle{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f});
        expect(pixelOf(whole, 200, 150) == std::array{255, 0, 0}) << "inside the square";
        expect(pixelOf(whole, 140, 150) == std::array{255, 255, 255}) << "left of it";
        expect(pixelOf(whole, 200, 190) == std::array{255, 255, 255}) << "below it";
        expect(outputOf("pdfimages -list " + whole.string()).find("image") == outputOf("pdfimages -list " + whole.string()).rfind("image")) << "no picture embedded, only paths";

        // the right half of the drawing shown across the whole box: the square's right half, x 50..75 of the drawing,
        // lands on the left half of the box, points 100..200
        const auto half = croppedTo(Rectangle{.x = 0.5f, .y = 0.0f, .width = 0.5f, .height = 1.0f});
        expect(pixelOf(half, 150, 150) == std::array{255, 0, 0}) << "the cropped square's half";
        expect(pixelOf(half, 260, 150) == std::array{255, 255, 255}) << "nothing past it";
    };

    "an outline clipped by its drawing is painted only inside the clip"_test = [] {
        using enum VectorPath::Step;
        const VectorDrawing clippedDrawing{.width = 100.0f,
            .height                               = 50.0f, //
            .paths                                = {VectorPath{.steps = {move, line, line, line, close}, .points = {0.0f, 0.0f, 100.0f, 0.0f, 100.0f, 50.0f, 0.0f, 50.0f}, .fill = 0xFF0000FFU, .clips = {0U}}},
            .texts                                = {},
            .clips                                = {VectorClip{.steps = {move, line, line, line, close}, .points = {0.0f, 0.0f, 50.0f, 0.0f, 50.0f, 50.0f, 0.0f, 50.0f}}}};
        const auto          clipped = [&clippedDrawing](const RecordedImage&) { return &clippedDrawing; };
        // 400 by 200 recorded pixels at (200, 200): 200 by 100 points at (100, 100)
        const PageRecording page = pageWith(RecordedImage{.kind = RecordedImageKind::figure, .source = "clipped", .at = Rectangle{.x = 200.0f, .y = 200.0f, .width = 400.0f, .height = 200.0f}});
        const auto          pdf  = writtenTo(PdfDocument{.title = "clipped", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .vectorFor = clipped}, {page}, "clipped");
        const auto          red  = [&](int x, int y) {
            const std::array<int, 3UZ> rgb = pixelOf(pdf, x, y);
            return rgb[0] > 200 && rgb[1] < 60;
        };
        expect(red(150, 150)) << "inside the clip, the left half";
        expect(!red(250, 150)) << "outside it, the right half";
        expect(eq(imagesOf(pdf).size(), 0UZ)) << "paths only, no picture";
    };

    // Two drawings of 100 by 50 pixels, each a picture of one colour over all of it, clipped to its left half;
    // placed 400 by 200 at (200, 200) of the 1920-pixel frame they cover points 100..300 by 100..200, and paint 100..200 only.
    // Ground truth: poppler's colours either side of the clip, and the pictures `pdfimages` lists.
    "a picture in an outlined drawing is drawn inside its clips, and embedded once however many pages draw it"_test = [] {
        using enum VectorPath::Step;
        const auto pictured = [](std::uint32_t rgba) {
            return VectorDrawing{.width = 100.0f,
                .height                 = 50.0f, //
                .paths                  = {VectorPath{.clips = {0U}, .picture = VectorPicture{.pixels = filled(10U, 10U, rgba), .at = Rectangle{.x = 0.0f, .y = 0.0f, .width = 100.0f, .height = 50.0f}}}},
                .texts                  = {},
                .clips                  = {VectorClip{.steps = {move, line, line, line, close}, .points = {0.0f, 0.0f, 50.0f, 0.0f, 50.0f, 50.0f, 0.0f, 50.0f}}}};
        };
        const VectorDrawing redDrawing  = pictured(0xFF0000FFU);
        const VectorDrawing blueDrawing = pictured(0xFFFF0000U);
        const auto          drawingOf   = [&](const RecordedImage& image) { return image.source == "red" ? &redDrawing : &blueDrawing; };
        const auto          placed      = [](std::string source) { return pageWith(RecordedImage{.kind = RecordedImageKind::figure, .source = std::move(source), .at = Rectangle{.x = 200.0f, .y = 200.0f, .width = 400.0f, .height = 200.0f}}); };
        const auto          pdf         = writtenTo(PdfDocument{.title = "pictured", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .vectorFor = drawingOf}, {placed("red"), placed("blue"), placed("red")}, "pictured");
        expect(pixelOf(pdf, 150, 150) == std::array{255, 0, 0}) << "red inside the clip";
        expect(pixelOf(pdf, 250, 150) == std::array{255, 255, 255}) << "nothing outside it";
        expect(pixelOf(pdf, 150, 150, 72, 2) == std::array{0, 0, 255}) << "the second page's own blue";
        expect(pixelOf(pdf, 150, 150, 72, 3) == std::array{255, 0, 0}) << "the third page's red";
        const auto listed = imagesOf(pdf);
        expect(eq(listed.size(), 3UZ) && eq(embeddings(listed), 2UZ)) << "a picture per page, the red one embedded once";
        expect(listed.size() == 3UZ && eq(listed[0].width, 10) && eq(listed[0].height, 10)) << "with its own pixels";
    };

    // What a page leads to is not drawn, so poppler's text and pictures do not show it; the PDF's own objects do,
    // and they are written as plain dictionaries: a link's address, a link to another page, the notes as a comment,
    // and a bookmark per titled page.
    // Text in them may be plain or UTF-16, so both spellings are looked for.
    "links, notes and bookmarks are written into the pages they belong to"_test = [] {
        PageRecording first{.primitives = {RecordedLink{.target = "https://example.org/deck", .at = Rectangle{.x = 100.0f, .y = 100.0f, .width = 300.0f, .height = 40.0f}}, //
                                RecordedLink{.target = "#second", .at = Rectangle{.x = 100.0f, .y = 200.0f, .width = 300.0f, .height = 40.0f}},                             //
                                RecordedLink{.target = "#nowhere", .at = Rectangle{.x = 100.0f, .y = 300.0f, .width = 300.0f, .height = 40.0f}}},
            .viewId                     = "first",
            .title                      = "First section",
            .notes                      = "Say this out loud"};
        PageRecording second{.primitives = {}, .viewId = "second", .title = "Second section", .notes = {}};
        const auto    written = writePdf(PdfDocument{.title = "linked", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}}, std::vector{first, second});
        expect(written.has_value());
        if (!written.has_value()) {
            return;
        }
        const std::string bytes{written->begin(), written->end()};
        const auto        count = [&bytes](std::string_view needle) {
            std::size_t found = 0UZ;
            for (std::size_t at = bytes.find(needle); at != std::string::npos; at = bytes.find(needle, at + 1UZ)) {
                ++found;
            }
            return found;
        };
        const auto utf16 = [](std::string_view text) {
            std::string hex = "FEFF";
            for (const char letter : text) {
                hex += std::format("00{:02X}", static_cast<unsigned char>(letter));
            }
            return hex;
        };
        const auto has = [&](std::string_view text) { return bytes.contains(std::string{"("} + std::string{text} + ")") || bytes.contains(utf16(text)); };
        expect(eq(count("/Subtype /Link"), 2UZ)) << "the address and the page; a link to a section not in the deck is left out";
        // a PDF string may write any character as a backslash and three octal digits, and libharu writes '/' so
        const auto  uriAt = bytes.find("/URI (");
        std::string uri;
        for (std::size_t at = uriAt == std::string::npos ? bytes.size() : uriAt + 6UZ; at < bytes.size() && bytes[at] != ')'; ++at) {
            if (bytes[at] == '\\' && at + 3UZ < bytes.size()) {
                uri += static_cast<char>((bytes[at + 1UZ] - '0') * 64 + (bytes[at + 2UZ] - '0') * 8 + (bytes[at + 3UZ] - '0'));
                at += 3UZ;
            } else {
                uri += bytes[at];
            }
        }
        expect(uri == "https://example.org/deck") << "the address, as written: " << uri;
        expect(eq(count("/Subtype /Text"), 1UZ) && has("Say this out loud")) << "the notes, as one comment";
        expect(has("First section") && has("Second section")) << "a bookmark per titled page";
        expect(count("/Outlines") >= 1UZ) << "and the outline that holds them";
    };

    "a drawing embeds once per set of elements it was drawn without, and is shared by the pages that draw it so"_test = [] {
        const auto        stage    = [](const RecordedImage&) -> std::optional<RecordedPixels> { return filled(64U, 32U, 0xFF336699U); };
        const auto        drawnOut = [](std::vector<std::string> hidden) { return pageWith(RecordedImage{.kind = RecordedImageKind::master, .source = "stage.svg", .at = Rectangle{.x = 0.0f, .y = 0.0f, .width = 1920.0f, .height = 1080.0f}, .hidden = std::move(hidden)}); };
        const PdfDocument document{.title = "stage", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .pixelsFor = stage};
        const auto        listed = imagesOf(writtenTo(document, {drawnOut({"later"}), drawnOut({"later"}), drawnOut({})}, "steps"));
        expect(eq(listed.size(), 3UZ)) << "each page draws its picture";
        expect(eq(embeddings(listed), 2UZ)) << "the two pages without `later` share one, the third has its own";
    };

    // the page is 960 points, 13.33 inches, across the 1920 recorded pixels: a picture 960 pixels wide is drawn 6.67 inches wide
    "a picture with more pixels than the limit asks for is averaged down where it is drawn largest, never up"_test = [] {
        const auto        drawn    = [](std::string source, float width) { return pageWith(RecordedImage{.kind = RecordedImageKind::figure, .source = std::move(source), .at = Rectangle{.x = 0.0f, .y = 0.0f, .width = width, .height = width / 2.0f}}); };
        const auto        pictures = [](const RecordedImage& image) -> std::optional<RecordedPixels> { return image.source == "large" ? filled(4000U, 2000U, 0xFF336699U) : filled(500U, 250U, 0xFF336699U); };
        PdfDocument       document{.title = "dpi", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .pixelsFor = pictures};
        const std::vector pages{drawn("large", 480.0f), drawn("large", 960.0f), drawn("small", 960.0f)};

        const auto asIs = imagesOf(writtenTo(document, pages, "dpi_unlimited"));
        expect(eq(asIs.size(), 3UZ) && eq(asIs[0].width, 4000)) << "without a limit, the file's own pixels";

        document.imageDpi = 150.0f;
        const auto capped = imagesOf(writtenTo(document, pages, "dpi_150"));
        expect(eq(capped.size(), 3UZ) && eq(embeddings(capped), 2UZ)) << "one embedding per picture, whatever size it is drawn at";
        expect(capped.size() == 3UZ && eq(capped[0].width, 1000) && eq(capped[0].height, 500)) << "6.67 by 3.33 inches at 150 dpi, the larger of its two sizes";
        expect(capped.size() == 3UZ && eq(capped[2].width, 500) && eq(capped[2].height, 250)) << "fewer pixels than asked for stay as they are";

        for (const float absurd : {1e30f, std::numeric_limits<float>::infinity()}) {
            document.imageDpi = absurd;
            const auto kept   = imagesOf(writtenTo(document, pages, "dpi_absurd"));
            expect(kept.size() == 3UZ && eq(kept[0].width, 4000)) << absurd << " dpi keeps the file's own pixels";
        }
    };

    "a picture made for the page is made at the size it is drawn largest, wherever that comes in the deck"_test = [] {
        std::vector<float> asked;
        const auto         drawn    = [](float width) { return pageWith(RecordedImage{.kind = RecordedImageKind::master, .source = "refused.svg", .at = Rectangle{.x = 0.0f, .y = 0.0f, .width = width, .height = width / 2.0f}}); };
        const auto         pictures = [&asked](const RecordedImage& image) -> std::optional<RecordedPixels> {
            asked.push_back(image.at.width);
            return filled(64U, 32U, 0xFF336699U);
        };
        const PdfDocument document{.title = "largest", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .pixelsFor = pictures};
        const auto        listed = imagesOf(writtenTo(document, {drawn(480.0f), drawn(1920.0f)}, "largest"));
        expect(eq(listed.size(), 2UZ) && eq(embeddings(listed), 1UZ)) << "one embedding for both pages";
        expect(eq(asked.size(), 1UZ) && asked.front() == 1920.0f) << "made once, at the second page's full width: " << (asked.empty() ? 0.0f : asked.front());
    };

    "an opaque picture that deflates poorly embeds as JPEG, one that deflates well losslessly"_test = [] {
        RecordedPixels noise = filled(256U, 256U, 0xFF000000U);
        std::uint32_t  state = 12345U; // a fixed linear congruential sequence: noise no deflater can shrink
        for (std::size_t index = 0UZ; index < noise.rgba.size(); index += 4UZ) {
            for (std::size_t channel = 0UZ; channel < 3UZ; ++channel) {
                state                       = state * 1664525U + 1013904223U;
                noise.rgba[index + channel] = static_cast<std::uint8_t>(state >> 24U);
            }
        }
        const auto pictures = [&noise](const RecordedImage& image) -> std::optional<RecordedPixels> { return image.source == "noise" ? noise : filled(256U, 256U, 0xFF336699U); };
        const auto drawn    = [](std::string source) { return pageWith(RecordedImage{.kind = RecordedImageKind::figure, .source = std::move(source), .at = Rectangle{.x = 0.0f, .y = 0.0f, .width = 512.0f, .height = 512.0f}}); };
        const auto listed   = imagesOf(writtenTo(PdfDocument{.title = "deflate", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .pixelsFor = pictures}, {drawn("noise"), drawn("flat")}, "deflate"));
        expect(fatal(eq(listed.size(), 2UZ)));
        expect(listed[0].encoding == "jpeg") << "noise: " << listed[0].encoding;
        expect(listed[1].encoding == "image") << "one colour: " << listed[1].encoding;
    };

    "averaged down, a picture's edge keeps its colour where it meets transparency"_test = [] {
        // 300 by 10 pixels, transparent black left of column 151 and opaque red from it, shown as 100 by 10 at 150 dpi:
        // the red edge is two thirds covered; were the transparent black averaged in, it would darken the red
        RecordedPixels edge = filled(300U, 10U, 0x00000000U);
        for (std::size_t row = 0UZ; row < 10UZ; ++row) {
            for (std::size_t column = 151UZ; column < 300UZ; ++column) {
                edge.rgba[(row * 300UZ + column) * 4UZ]       = 255U;
                edge.rgba[(row * 300UZ + column) * 4UZ + 3UZ] = 255U;
            }
        }
        // 96 by 9.6 recorded pixels at (192, 192): 48 by 4.8 points at (96, 96), 100 by 10 pixels at 150 dpi
        const PageRecording page   = pageWith(RecordedImage{.kind = RecordedImageKind::chart, .source = "edge", .at = Rectangle{.x = 192.0f, .y = 192.0f, .width = 96.0f, .height = 9.6f}, .pixels = edge});
        const auto          pdf    = writtenTo(PdfDocument{.title = "edge", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .imageDpi = 150.0f}, {page}, "edge");
        const auto          listed = imagesOf(pdf);
        expect(listed.size() == 1UZ && eq(listed[0].width, 100)) << "averaged down";
        const std::array<int, 3UZ> rgb = pixelOf(pdf, 250, 205, 150);
        expect(rgb[0] >= 254) << "red over white at the edge, not darkened";
        expect(rgb[1] > 40 && rgb[1] < 200) << "and partly covered";
    };

    "a page with nothing on it is still a page"_test = [] {
        const auto written = writePdf(PdfDocument{.title = "empty", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}}, std::vector<PageRecording>(1UZ));
        expect(written.has_value() && written->size() > 100UZ);
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
