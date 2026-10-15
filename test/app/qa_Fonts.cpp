#include "ImGuiTestHarness.hpp"

#include "BuiltinFace.hpp"
#include "Canvas.hpp"
#include "Fonts.hpp"

#include <boost/ut.hpp>

#include "FileContents.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace boost::ut;
using namespace gr::present;
using namespace gr::present::test;

namespace {

[[nodiscard]] bool approx(float value, float wanted, float tolerance) noexcept { return std::abs(value - wanted) < tolerance; }

// Ground truth: the codepoints Font Awesome 6 Free Solid publishes, not decoded from this project's constants
struct Icon {
    std::string_view name;
    std::string_view constant; // the UTF-8 the viewer puts in a label
    ImWchar          published;
};

const std::vector<Icon> kIcons{
    {.name = "triangle-exclamation", .constant = Fonts::kWarning, .published = 0xf071},
    {.name = "up-right-and-down-left-from-center", .constant = Fonts::kExpand, .published = 0xf065},
    {.name = "location-crosshairs", .constant = Fonts::kPointer, .published = 0xf601},
    {.name = "note-sticky", .constant = Fonts::kNotes, .published = 0xf249},
    {.name = "person-chalkboard", .constant = Fonts::kPresenter, .published = 0xe53d},
    {.name = "file-pdf", .constant = Fonts::kPdf, .published = 0xf1c1},
    {.name = "folder-open", .constant = Fonts::kFolder, .published = 0xf07c},
    {.name = "floppy-disk", .constant = Fonts::kSave, .published = 0xf0c7},
    {.name = "square-check", .constant = Fonts::kChecked, .published = 0xf14a},
    {.name = "square", .constant = Fonts::kUnchecked, .published = 0xf0c8},
};

/// the three bytes UTF-8 spends on a codepoint in Font Awesome's private-use range
[[nodiscard]] std::string utf8Of(ImWchar codepoint) { return {static_cast<char>(0xe0 | (codepoint >> 12)), static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)), static_cast<char>(0x80 | (codepoint & 0x3f))}; }

bool gEngineSucceeded = false;

const suite<"Fonts"> fontTests = [] {
    "the ImGui test engine ran the font scenario"_test = [] { expect(gEngineSucceeded); };

    "the type ladder is 36 pt for the title, 18 for the body, and stops at 12"_test = [] {
        // Ground truth: a 13.33 x 7.5 inch slide at 1280 x 720 has 96 px to the inch, 1.333 px to the point
        expect(approx(Fonts::pointsToPixels(Fonts::kPointsTitle, 1280.0f, 720.0f), 48.0f, 0.1f));
        expect(approx(Fonts::pointsToPixels(Fonts::kPointsBody, 1280.0f, 720.0f), 24.0f, 0.1f));
        expect(approx(Fonts::pointsToPixels(Fonts::kPointsTitle, 1280.0f, 720.0f) / Fonts::pointsToPixels(24.0f, 1280.0f, 720.0f), 1.5f, 0.001f)) << "a factor of 1.5 over the 24 pt title it replaced";
        expect(eq(Fonts::ladderPoints(0), 18.0f));
        expect(eq(Fonts::ladderPoints(1), 15.0f));
        expect(eq(Fonts::ladderPoints(2), 13.0f));
        expect(eq(Fonts::ladderPoints(3), 12.0f));
        expect(eq(Fonts::ladderPoints(9), 12.0f)) << "deeper nesting stays at the floor";
        expect(eq(Fonts::ladderPoints(-1), 18.0f)) << "and a negative depth is the body";
    };

    "the type scales with the viewport's diagonal, so turning a phone keeps the size"_test = [] {
        expect(approx(Fonts::slideBodySize(720.0f, 1280.0f), Fonts::slideBodySize(1280.0f, 720.0f), 0.001f));
        expect(approx(Fonts::slideBodySize(2560.0f, 1440.0f), 2.0f * Fonts::slideBodySize(1280.0f, 720.0f), 0.01f));
    };

    "the built-in faces inflate to the Liberation 2.1.5 files as published"_test = [] {
        struct Published {
            BuiltinFace      face;
            std::size_t      bytes; // the release's file sizes, as installed in /usr/share/fonts/truetype
            std::string_view name;
        };
        constexpr std::array kPublished{Published{BuiltinFace::body, 410712UZ, "LiberationSans-Regular"}, Published{BuiltinFace::bold, 414456UZ, "LiberationSans-Bold"}, Published{BuiltinFace::italic, 415816UZ, "LiberationSans-Italic"}, Published{BuiltinFace::boldItalic, 408996UZ, "LiberationSans-BoldItalic"}, Published{BuiltinFace::mono, 319508UZ, "LiberationMono-Regular"}};
        for (const Published& published : kPublished) {
            const std::span<const std::uint8_t> ttf = builtinFaceTtf(published.face);
            expect(eq(ttf.size(), published.bytes)) << published.name;
            expect(ttf.size() > 4UZ && ttf[0] == 0x00U && ttf[1] == 0x01U && ttf[2] == 0x00U && ttf[3] == 0x00U) << published.name << " does not start as a TrueType file";
        }
    };

    "every icon constant encodes the codepoint Font Awesome publishes for it"_test = [] {
        for (const Icon& icon : kIcons) {
            expect(std::string{icon.constant} == utf8Of(icon.published)) << icon.name << " does not encode U+" << std::to_string(icon.published);
        }
    };
};

// ImGui merges icons into the font added before them, so atlas build order decides whether the chrome's `face` has them
void checkTheFaceCarriesTheIcons() {
    Fonts::instance().load(); // the harness rebuilds the atlas per scenario, so a once-only load goes stale
    const Fonts& fonts = Fonts::instance();
    expect(fonts.face != nullptr) << "the body face was not loaded at all";
    if (fonts.face == nullptr) {
        return;
    }
    for (const Icon& icon : kIcons) {
        expect(fonts.face->IsGlyphInFont(icon.published)) << icon.name << " is missing from the face the chrome draws with";
    }
    // Greek and Cyrillic must render in prose and in code
    for (const ImWchar letter : {ImWchar{0x03C3}, ImWchar{0x0416}}) {
        expect(fonts.face->IsGlyphInFont(letter) && fonts.mono->IsGlyphInFont(letter)) << "U+" << std::to_string(letter) << " is missing from the body or the mono face";
    }
}

[[nodiscard]] std::vector<std::uint8_t> packageFile(std::string_view reference) { return fileContents<std::vector<std::uint8_t>>(std::filesystem::path{GR4_PRESENT_PACKAGE_DIRECTORY} / reference); }

// Ground truth: Patrick Hand's cmap (fontTools) maps 515 characters and not U+03C3, so a sigma comes from behind it
void checkADeckFaceFallsBackPerGlyph() {
    Fonts& fonts = Fonts::instance();
    fonts.load();
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> files;
    const auto                                                    bytesOf = [&files](std::string_view reference) -> std::span<const std::uint8_t> {
        auto& bytes = files[std::string{reference}];
        bytes       = packageFile(reference);
        return bytes;
    };
    Manifest manifest;
    manifest.fonts = {{"hand", FontFamily{.regular = "fonts/PatrickHand-Regular.ttf", .bold = {}, .italic = {}, .boldItalic = {}}}, //
        {"bold", FontFamily{.regular = "fonts/PatrickHand-Regular.ttf", .bold = {}, .italic = {}, .boldItalic = {}}},               //
        {"mono", FontFamily{.regular = "fonts/absent.ttf", .bold = {}, .italic = {}, .boldItalic = {}}},                            //
        {"title", FontFamily{.regular = "fonts/PatrickHand-OFL.txt", .bold = {}, .italic = {}, .boldItalic = {}}}};
    Diagnostics diagnostics;
    fonts.stageDeck(manifest, bytesOf, diagnostics);
    expect(fonts.applyStagedDeck()) << "the staged deck was not applied";
    expect(eq(diagnostics.count(DiagnosticKind::missingResource), 2UZ)) << "a missing file and one that is not TrueType are both reported";
    expect(fonts.mono == fonts.roles().mono && fonts.deckFaceName(fonts.mono) == nullptr) << "a role whose file failed keeps the built-in";
    const std::string* boldName = fonts.deckFaceName(fonts.bold);
    expect(boldName != nullptr && *boldName == "bold") << "the deck's bold face is not the bold role";

    // a role named as a font sets everything in it, code aside
    const std::optional<FaceSet> boldThroughout = fonts.faceSet("bold");
    expect(boldThroughout.has_value()) << "a role is a font name";
    if (boldThroughout.has_value()) {
        const FaceSet& set = *boldThroughout;
        expect(set.regular == fonts.bold && set.bold == fonts.bold && set.italic == fonts.bold && set.boldItalic == fonts.bold && set.title == fonts.bold) << "font=bold does not set every role but code in bold";
        expect(set.mono == fonts.mono) << "font=bold changed the code face";
    }
    const std::optional<FaceSet> monoThroughout = fonts.faceSet("mono");
    expect(monoThroughout.has_value() && monoThroughout->regular == fonts.mono && monoThroughout->title == fonts.mono) << "font=mono is not a monospace paragraph";

    const std::optional<FaceSet> hand = fonts.faceSet("hand");
    expect(hand.has_value() && hand->regular != nullptr && fonts.deckFaceName(hand->regular) != nullptr) << "the named face was not loaded";
    expect(!fonts.faceSet("nonesuch").has_value()) << "a name the deck does not have is no face";
    if (!hand.has_value() || hand->regular == nullptr) {
        return;
    }
    expect(hand->bold == hand->regular) << "a family without a bold sets strong words in its regular face";
    ImFontBaked* const baked = hand->regular->GetFontBaked(24.0f);
    const ImFontGlyph* latin = baked->FindGlyphNoFallback(ImWchar{'a'});
    const ImFontGlyph* sigma = baked->FindGlyphNoFallback(ImWchar{0x03C3});
    expect(latin != nullptr && latin->SourceIdx == 0U) << "an 'a' is Patrick Hand's own";
    expect(sigma != nullptr && sigma->SourceIdx == 1U) << "a sigma comes from the built-in face behind it, rather than showing '?'";

    fonts.noteFallbacks(hand->regular, 24.0f, "a \xcf\x83 \xcf\x84");
    fonts.noteFallbacks(hand->regular, 24.0f, "\xcf\x83 again");
    const auto notes = fonts.takeFallbackNotes();
    expect(eq(notes.size(), 1UZ)) << "the first fallback of a face is noted once";
    expect(fonts.takeFallbackNotes().empty());

    // recorded for the PDF in parts, each in a face that has its glyphs
    PageRecording recording;
    const Canvas  canvas{ImGui::GetForegroundDrawList(), &recording, nullptr};
    canvas.text(hand->regular, 24.0f, ImVec2{10.0f, 20.0f}, 0xFFFFFFFFU,
        "ab"
        "\xcf\x83"
        "cd");
    expect(eq(recording.primitives.size(), 3UZ)) << "a run with a glyph from the built-in face is recorded in three parts";
    if (recording.primitives.size() == 3UZ) {
        const auto& before    = std::get<RecordedText>(recording.primitives[0]);
        const auto& sigmaPart = std::get<RecordedText>(recording.primitives[1]);
        const auto& after     = std::get<RecordedText>(recording.primitives[2]);
        expect(before.text == "ab" && before.deckFace == "hand regular");
        expect(sigmaPart.text == "\xcf\x83" && sigmaPart.deckFace.empty() && sigmaPart.face == RecordedFace::body) << "the sigma is the built-in body face's";
        expect(after.text == "cd" && after.deckFace == "hand regular");
        expect(std::abs(sigmaPart.x - (10.0f + hand->regular->CalcTextSizeA(24.0f, FLT_MAX, 0.0f, "ab").x)) < 0.01f) << "each part starts where the one before it ends";
    }

    fonts.stageDeck(Manifest{}, bytesOf, diagnostics);
    expect(fonts.applyStagedDeck());
    expect(!fonts.faceSet("hand").has_value()) << "the last deck's face outlived it";
}

} // namespace

int main() {
    Harness harness;
    harness.addTest(
        "icons-in-body-face",                                     //
        [](ImGuiTestContext*) { checkTheFaceCarriesTheIcons(); }, //
        [](ImGuiTestContext*) {});
    harness.addTest(
        "deck-face-falls-back-per-glyph",                             //
        [](ImGuiTestContext*) { checkADeckFaceFallsBackPerGlyph(); }, //
        [](ImGuiTestContext*) {});
    gEngineSucceeded = harness.run();
    return 0;
}
