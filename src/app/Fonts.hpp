#ifndef GR4_PRESENT_FONTS_HPP
#define GR4_PRESENT_FONTS_HPP

#include "BuiltinFace.hpp"

#include <gr4-present/Diagnostics.hpp>
#include <gr4-present/Manifest.hpp>
#include <gr4-present/Markdown.hpp>
#include <gr4-present/TypeSize.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/// the faces one choice of font sets text in: the deck's roles, a family the deck names, or one face throughout
struct FaceSet {
    ImFont* regular    = nullptr;
    ImFont* bold       = nullptr;
    ImFont* italic     = nullptr;
    ImFont* boldItalic = nullptr;
    ImFont* mono       = nullptr;
    ImFont* title      = nullptr;

    /// strong in bold, emphasis in italic, code and a formula's source in mono, everything else regular
    [[nodiscard]] ImFont* forKind(InlineKind kind) const noexcept;

    bool operator==(const FaceSet&) const = default;
};

/// the deck's fonts; ImGui's built-in bitmap face blurs when scaled, so text uses Liberation Sans and Mono
struct Fonts {
    // the deck's roles: Liberation unless `fonts:` in index.yml says otherwise; the chrome draws with `face` too
    ImFont* face       = nullptr; // body
    ImFont* bold       = nullptr; // a weight step above the body, for **strong**
    ImFont* italic     = nullptr;
    ImFont* boldItalic = nullptr;
    ImFont* mono       = nullptr; // code spans and fenced blocks
    ImFont* title      = nullptr; // a slide's title
    ImFont* icons      = nullptr; // Font Awesome solid, subset to the glyphs the viewer draws

    TypeScale scale; // `sizes:` in index.yml: title, body and floor in points

    /// the icons the viewer uses, as UTF-8
    static constexpr std::string_view kWarning   = "\xef\x81\xb1";
    static constexpr std::string_view kExpand    = "\xef\x81\xa5";
    static constexpr std::string_view kPointer   = "\xef\x98\x81"; // location-crosshairs
    static constexpr std::string_view kNotes     = "\xef\x89\x89"; // note-sticky
    static constexpr std::string_view kPresenter = "\xee\x94\xbd"; // person-chalkboard
    static constexpr std::string_view kPdf       = "\xef\x87\x81"; // file-pdf
    static constexpr std::string_view kFolder    = "\xef\x81\xbc"; // folder-open
    static constexpr std::string_view kSave      = "\xef\x83\x87"; // floppy-disk
    static constexpr std::string_view kChecked   = "\xef\x85\x8a"; // square-check
    static constexpr std::string_view kUnchecked = "\xef\x83\x88"; // square
    static constexpr std::string_view kEllipsis  = "\xe2\x80\xa6";

    [[nodiscard]] static Fonts& instance() {
        static Fonts fonts;
        return fonts;
    }

    /// `label` cut, with an ellipsis, to fit `room` pixels at the current font
    [[nodiscard]] static std::string fitted(std::string_view label, float room);

    void load();

    [[nodiscard]] FaceSet roles() const noexcept { return FaceSet{.regular = face, .bold = bold, .italic = italic, .boldItalic = boldItalic, .mono = mono, .title = title}; }

    /// what `font=<name>` sets text in: empty is the roles, a family the deck named, or one role throughout;
    /// nothing for a name the deck does not have
    [[nodiscard]] std::optional<FaceSet> faceSet(std::string_view name) const;

    /// whether `font=<name>` names anything, answerable before the faces themselves are in the atlas
    [[nodiscard]] bool knowsName(std::string_view name) const;

    /// keeps the deck's face bytes until `applyStagedDeck`; a missing, external or non-TrueType file is reported and
    /// its role keeps the built-in
    void stageDeck(const Manifest& manifest, const std::function<std::span<const std::uint8_t>(std::string_view)>& bytesOf, Diagnostics& diagnostics);

    /// before `ImGui::NewFrame`: the staged faces replace the last deck's; false when nothing was staged
    bool applyStagedDeck();

    /// notes once per face that a glyph came from the merged built-in; `takeFallbackNotes` hands the notes over
    void                                                           noteFallbacks(ImFont* font, float size, std::string_view text);
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> takeFallbackNotes(); // (face, which glyph)

    struct DeckFace {
        ImFont*                       font = nullptr;
        std::string                   name;                         // as index.yml has it: a role, or a family's name and style
        BuiltinFace                   fallback = BuiltinFace::body; // the face merged behind it, which draws what it lacks
        std::span<const std::uint8_t> ttf;                          // what the PDF export embeds
    };

    /// whether `font` is one of the deck's faces rather than a built-in, and its name in index.yml
    [[nodiscard]] const std::string*        deckFaceName(const ImFont* font) const noexcept;
    [[nodiscard]] const DeckFace*           deckFace(const ImFont* font) const noexcept;
    [[nodiscard]] std::span<const DeckFace> deckFaces() const noexcept { return _deckFaces; }

    // the launch and fallback scenes and the footer are sized from the viewport height: they stand where a slide would
    [[nodiscard]] static float statusSize(float viewportHeight) noexcept { return viewportHeight * 0.022f; }
    [[nodiscard]] static float bodySize(float viewportHeight) noexcept { return viewportHeight * 0.032f; }
    [[nodiscard]] static float titleSize(float viewportHeight) noexcept { return viewportHeight * 0.075f; }

    // points are stated for a 13.33 x 7.5 inch slide drawn at 1280 x 720 (1 pt = 1.333 px) and scaled by the
    // viewport diagonal, which barely changes when a device rotates
    static constexpr float kPointsBody      = 18.0f;
    static constexpr float kPointsTitle     = 36.0f;
    static constexpr float kPointsHeading   = 24.0f; // the top of the ladder a sub-heading inside the flow steps down from
    static constexpr float kPixelsPerPoint  = 1.333f;
    static constexpr float kReferenceWidth  = 1280.0f;
    static constexpr float kReferenceHeight = 720.0f;

    // the presenter's controls, the status lines and the problems, in interface points: the device's own scale
    static constexpr float kInterfacePoints = 15.0f;

    [[nodiscard]] static float pointsToPixels(float points, float viewportWidth, float viewportHeight) noexcept { return points * kPixelsPerPoint * std::hypot(viewportWidth, viewportHeight) / std::hypot(kReferenceWidth, kReferenceHeight); }

    /// points by nesting depth: body, first indent, second, then the floor; the steps scale with a deck's body size
    [[nodiscard]] static float ladderPoints(int depth) noexcept {
        const TypeScale&           scale = instance().scale;
        const float                body  = scale.body;
        const std::array<float, 4> ladder{body, std::max(body * 15.0f / kPointsBody, scale.floor), std::max(body * 13.0f / kPointsBody, scale.floor), scale.floor};
        return ladder[static_cast<std::size_t>(std::clamp(depth, 0, static_cast<int>(ladder.size()) - 1))];
    }

    /// ticks, axis titles and legend of every chart: one ladder step in from `bodyPixels`, never below the floor
    [[nodiscard]] static float chartLabelPixels(float bodyPixels, float viewportWidth, float viewportHeight) noexcept {
        const float oneStepIn   = bodyPixels * ladderPoints(1) / instance().scale.body;
        const float floorPixels = pointsToPixels(instance().scale.floor, viewportWidth, viewportHeight);
        return std::max(oneStepIn, floorPixels);
    }

    /// a title over the body, and a heading inside the flow over the body, as the deck's sizes have them
    [[nodiscard]] static float titleRatio() noexcept { return instance().scale.title / instance().scale.body; }
    [[nodiscard]] static float headingRatio() noexcept { return kPointsHeading / kPointsBody; }

    [[nodiscard]] static float slideBodySize(float viewportWidth, float viewportHeight) noexcept { return pointsToPixels(instance().scale.body, viewportWidth, viewportHeight); }

    struct StagedFace {
        std::string               role;   // body, bold, italic, bolditalic, mono, title, or empty for a family's style
        std::string               family; // the family a style belongs to
        std::string               style;  // regular, bold, italic or bolditalic
        std::vector<std::uint8_t> ttf;
    };

    FaceSet                                          _builtins;
    std::vector<std::string>                         _familyNames; // every family the deck names, known from staging on
    std::optional<std::vector<StagedFace>>           _staged;
    std::vector<std::vector<std::uint8_t>>           _deckBytes; // held while the atlas reads them
    std::vector<DeckFace>                            _deckFaces;
    std::vector<std::pair<std::string, FaceSet>>     _families;
    std::vector<std::string>                         _fallbacksNoted;
    std::vector<std::pair<std::string, std::string>> _fallbacksPending;
};

/// pushes the body face, or `font`, at `pixels` and pops it at the end of the scope
struct ScopedFont {
    explicit ScopedFont(float pixels, ImFont* font = nullptr) { ImGui::PushFont(font != nullptr ? font : Fonts::instance().face, pixels); }
    ScopedFont(const ScopedFont&)            = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
    ~ScopedFont() { ImGui::PopFont(); }
};

} // namespace gr::present

#endif // GR4_PRESENT_FONTS_HPP
