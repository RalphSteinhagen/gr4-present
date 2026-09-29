#include <boost/ut.hpp>

#include <gr4-present/export/PdfWriter.hpp>

#include <gnuradio-4.0/Compression.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
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

[[nodiscard]] std::vector<std::uint8_t> bytesOf(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

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

    // A picture the viewer can outline is drawn as paths where it was placed. The drawing is 100 by 50 pixels with a
    // red square from (25, 10) to (75, 40); placed 400 by 200 at (200, 200) of a 1920-pixel frame on a 960-point page,
    // a pixel of it is two points, so the square covers points 150..250 by 120..180. Rendered at 72 dpi a point is a
    // pixel, so poppler's colour there and well outside it are the ground truth.
    "a picture outlined by the viewer is drawn as paths where it was placed"_test = [] {
        const auto square  = [](const RecordedImage&) -> std::optional<VectorDrawing> { return VectorDrawing{.width = 100.0f, .height = 50.0f, .paths = {VectorPath{.steps = {VectorPath::Step::move, VectorPath::Step::line, VectorPath::Step::line, VectorPath::Step::line, VectorPath::Step::close}, .points = {25.0f, 10.0f, 75.0f, 10.0f, 75.0f, 40.0f, 25.0f, 40.0f}, .fill = 0xFF0000FFU, .stroke = 0U, .strokeWidth = 0.0f, .evenOdd = false}}, .texts = {}}; };
        const auto pixelOf = [](const std::filesystem::path& pdf, int x, int y) {
            const auto prefix = std::filesystem::temp_directory_path() / "qa_PdfWriter_pixel"; // pdftoppm cannot write to a pipe
            std::ignore       = outputOf(std::format("pdftoppm -r 72 -x {} -y {} -W 1 -H 1 -singlefile {} {}", x, y, pdf.string(), prefix.string()));
            const auto ppm    = bytesOf(prefix.string() + ".ppm");
            return ppm.size() >= 3UZ ? std::array{static_cast<int>(ppm[ppm.size() - 3UZ]), static_cast<int>(ppm[ppm.size() - 2UZ]), static_cast<int>(ppm[ppm.size() - 1UZ])} : std::array{-1, -1, -1};
        };
        const auto pageWith = [&](Rectangle uv) {
            PageRecording page;
            page.primitives.emplace_back(RecordedImage{.kind = RecordedImageKind::figure, .source = "square", .at = Rectangle{.x = 200.0f, .y = 200.0f, .width = 400.0f, .height = 200.0f}, .uv = uv, .tint = 0xFFFFFFFFU, .clip = {}});
            const auto written = writePdf(PdfDocument{.title = "square", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}, .vectorFor = square}, std::vector{page});
            const auto path    = std::filesystem::temp_directory_path() / std::format("qa_PdfWriter_square_{}.pdf", static_cast<int>(uv.x * 10.0f));
            if (written.has_value()) {
                std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(written->data()), static_cast<std::streamsize>(written->size()));
            }
            return path;
        };

        const auto whole = pageWith(Rectangle{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f});
        expect(pixelOf(whole, 200, 150) == std::array{255, 0, 0}) << "inside the square";
        expect(pixelOf(whole, 140, 150) == std::array{255, 255, 255}) << "left of it";
        expect(pixelOf(whole, 200, 190) == std::array{255, 255, 255}) << "below it";
        expect(outputOf("pdfimages -list " + whole.string()).find("image") == outputOf("pdfimages -list " + whole.string()).rfind("image")) << "no picture embedded, only paths";

        // the right half of the drawing shown across the whole box: the square's right half, x 50..75 of the drawing,
        // lands on the left half of the box, points 100..200
        const auto half = pageWith(Rectangle{.x = 0.5f, .y = 0.0f, .width = 0.5f, .height = 1.0f});
        expect(pixelOf(half, 150, 150) == std::array{255, 0, 0}) << "the cropped square's half";
        expect(pixelOf(half, 260, 150) == std::array{255, 255, 255}) << "nothing past it";
    };

    // What a page leads to is not drawn, so poppler's text and pictures do not show it; the PDF's own objects do,
    // and they are written as plain dictionaries: a link's address, a link to another page, the notes as a comment,
    // and a bookmark per titled page. Text in them may be plain or UTF-16, so both spellings are looked for.
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

    "a page with nothing on it is still a page"_test = [] {
        const auto written = writePdf(PdfDocument{.title = "empty", .author = {}, .fonts = {PdfFont{.face = RecordedFace::body, .ttf = testFace()}}}, std::vector<PageRecording>(1UZ));
        expect(written.has_value() && written->size() > 100UZ);
    };
};

int main() { return 0; }
