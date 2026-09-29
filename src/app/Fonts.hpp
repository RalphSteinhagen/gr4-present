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

/**
 * ImGui's built-in face is a small bitmap font that blurs when scaled, so text is drawn with Liberation Sans and Mono,
 * which ImGui bakes at whatever size is pushed. Sizes are fractions of the viewport height rather than fixed points:
 * a point size readable on a laptop is invisible on a projector and absurd on a phone.
 */
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
    static constexpr std::string_view kChevronLeft  = "\xef\x81\x93";
    static constexpr std::string_view kChevronRight = "\xef\x81\x94";
    static constexpr std::string_view kWarning      = "\xef\x81\xb1";
    static constexpr std::string_view kExpand       = "\xef\x81\xa5";
    static constexpr std::string_view kEllipsis     = "\xe2\x80\xa6";

    [[nodiscard]] static Fonts& instance() {
        static Fonts fonts;
        return fonts;
    }

    /// `label` cut, with an ellipsis, to fit `room` pixels at the current font
    [[nodiscard]] static std::string fitted(std::string_view label, float room);

    void load();

    [[nodiscard]] FaceSet roles() const noexcept { return FaceSet{.regular = face, .bold = bold, .italic = italic, .boldItalic = boldItalic, .mono = mono, .title = title}; }

    /// the faces slide text is set in now: the roles, or what the slide or box being laid out chose (`ScopedFaces`)
    [[nodiscard]] FaceSet activeFaces() const noexcept { return _active.value_or(roles()); }

    /// what `font=<name>` sets text in: empty is the roles, a family the deck named, or one role throughout;
    /// nothing for a name the deck does not have
    [[nodiscard]] std::optional<FaceSet> faceSet(std::string_view name) const;

    /// whether `font=<name>` names anything, answerable before the faces themselves are in the atlas
    [[nodiscard]] bool knowsName(std::string_view name) const;

    /**
     * Reads the deck's faces and keeps their bytes until `applyStagedDeck`, which puts them in the atlas between
     * frames. A file outside the package, missing, or not TrueType is reported and its role keeps the built-in.
     */
    void stageDeck(const Manifest& manifest, const std::function<std::span<const std::uint8_t>(std::string_view)>& bytesOf, Diagnostics& diagnostics);

    /// before `ImGui::NewFrame`: the staged faces replace the last deck's; false when nothing was staged
    bool applyStagedDeck();

    /**
     * A deck's face lacking a glyph draws it from the built-in face of its role, merged behind it. The first time
     * that happens to a face it is noted here, once, for the problems list; `takeFallbackNotes` hands them over.
     */
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

    // Chrome -- the launch screen, the menu, the presenter panel -- is sized from the viewport height, because it
    // sits in the window rather than on the slide.
    [[nodiscard]] static float statusSize(float viewportHeight) noexcept { return viewportHeight * 0.022f; }
    [[nodiscard]] static float bodySize(float viewportHeight) noexcept { return viewportHeight * 0.032f; }
    [[nodiscard]] static float titleSize(float viewportHeight) noexcept { return viewportHeight * 0.075f; }

    /**
     * The body size for slide text that has no master behind it, from the diagonal rather than the height.
     *
     * Sizing from the height alone makes a landscape phone unreadable -- 844x303 gave text under ten pixels tall
     * across ninety-character lines -- and makes the same phone change type size when it is turned. The diagonal
     * barely moves when a device rotates, so the text stays the size it was, which is what rotating a page should
     * do. The constant is chosen so a 1280x800 window keeps the size it has today.
     */
    /**
     * Points, as a presentation tool means them.
     *
     * A 13.33 x 7.5 inch slide drawn at 1280 x 720 puts 1 pt at 1.333 px, which is the reference this deck's
     * sizes are stated in: title 36 pt, body 18, first indent 15, then 13, and 12 as the floor. The size still
     * follows the viewport's diagonal rather than its height, so turning a phone does not change the type and a
     * projector and a laptop read alike -- the points fix what 1280 x 720 shows, and every other surface is that
     * same design scaled.
     */
    static constexpr float kPointsBody      = 18.0f;
    static constexpr float kPointsTitle     = 36.0f;
    static constexpr float kPointsHeading   = 24.0f; // the top of the ladder a sub-heading inside the flow steps down from
    static constexpr float kPixelsPerPoint  = 1.333f;
    static constexpr float kReferenceWidth  = 1280.0f;
    static constexpr float kReferenceHeight = 720.0f;

    [[nodiscard]] static float pointsToPixels(float points, float viewportWidth, float viewportHeight) noexcept { return points * kPixelsPerPoint * std::hypot(viewportWidth, viewportHeight) / std::hypot(kReferenceWidth, kReferenceHeight); }

    /// the ladder, by nesting depth: body, first indent, second, and the floor for anything deeper; a deck that
    /// sets its body moves the steps between in proportion
    [[nodiscard]] static float ladderPoints(int depth) noexcept {
        const TypeScale&           scale = instance().scale;
        const float                body  = scale.body;
        const std::array<float, 4> ladder{body, std::max(body * 15.0f / kPointsBody, scale.floor), std::max(body * 13.0f / kPointsBody, scale.floor), scale.floor};
        return ladder[static_cast<std::size_t>(std::clamp(depth, 0, static_cast<int>(ladder.size()) - 1))];
    }

    /// what every chart sets its ticks, axis titles and legend in, the deck's own plots and the live ones alike: one
    /// step in from `bodyPixels`, never below the floor
    [[nodiscard]] static float chartLabelPixels(float bodyPixels, float viewportWidth, float viewportHeight) noexcept { return std::max(bodyPixels * ladderPoints(1) / instance().scale.body, pointsToPixels(instance().scale.floor, viewportWidth, viewportHeight)); }

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
    std::optional<FaceSet>                           _active;

    [[nodiscard]] ImFont** roleSlot(std::string_view role) noexcept;
};

/// pushes the body face, or `font`, at `pixels` and pops it at the end of the scope
struct ScopedFont {
    explicit ScopedFont(float pixels, ImFont* font = nullptr) { ImGui::PushFont(font != nullptr ? font : Fonts::instance().face, pixels); }
    ScopedFont(const ScopedFont&)            = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
    ~ScopedFont() { ImGui::PopFont(); }
};

/// sets slide text in `faces` until the end of the scope, as `ImGui::PushFont` does for one face
struct ScopedFaces {
    explicit ScopedFaces(const FaceSet& faces) : _previous(std::exchange(Fonts::instance()._active, faces)) {}
    ScopedFaces(const ScopedFaces&)            = delete;
    ScopedFaces& operator=(const ScopedFaces&) = delete;
    ~ScopedFaces() { Fonts::instance()._active = _previous; }

    std::optional<FaceSet> _previous;
};

} // namespace gr::present

#endif // GR4_PRESENT_FONTS_HPP
