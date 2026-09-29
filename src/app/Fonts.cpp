#include "Fonts.hpp"

#include <span>

#include "BuiltinFace.hpp"
#include "EmbeddedLogos.hpp"

#include <imgui_internal.h> // ImTextCharFromUtf8, ImTextFindPreviousUtf8Codepoint

#include <format>
#include <ranges>

namespace gr::present {

namespace {

constexpr std::array<std::string_view, 6> kRoles{"body", "bold", "italic", "bolditalic", "mono", "title"};

[[nodiscard]] bool isRole(std::string_view name) noexcept { return std::ranges::contains(kRoles, name); }

/// the built-in face a deck's face falls back to for a glyph it lacks
[[nodiscard]] BuiltinFace builtinBehind(std::string_view roleOrStyle) noexcept {
    if (roleOrStyle == "bold") {
        return BuiltinFace::bold;
    }
    if (roleOrStyle == "italic") {
        return BuiltinFace::italic;
    }
    if (roleOrStyle == "bolditalic") {
        return BuiltinFace::boldItalic;
    }
    return roleOrStyle == "mono" ? BuiltinFace::mono : BuiltinFace::body;
}

// the TrueType signatures: 0x00010000, and 'true' as Apple writes it; 'OTTO' is CFF, which the PDF cannot embed
[[nodiscard]] bool isTrueType(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::array<std::uint8_t, 4> kVersion1{0x00U, 0x01U, 0x00U, 0x00U};
    constexpr std::array<std::uint8_t, 4> kApple{'t', 'r', 'u', 'e'};
    return bytes.size() > 4UZ && (std::ranges::equal(bytes.first(4UZ), kVersion1) || std::ranges::equal(bytes.first(4UZ), kApple));
}

[[nodiscard]] bool endsWithTtf(std::string_view path) noexcept {
    return path.size() > 4UZ && std::ranges::equal(path.substr(path.size() - 4UZ), std::string_view{".ttf"}, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

// Merged into a face so an icon can sit in a line of text without switching fonts mid-string -- and merged directly
// after it, because ImGui merges into the font added before it. Added last, the icons went into the mono face
// instead and every one of them drew as a replacement box in the chrome.
void mergeIcons(const ImFontConfig& configuration) {
    ImFontConfig merged = configuration;
    merged.MergeMode    = true;
    static const ImWchar range[]{0xf000, 0xf8ff, 0};
    merged.GlyphRanges = range;
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontAwesomeSolidOtf.data()), static_cast<int>(kFontAwesomeSolidOtf.size()), 0.0f, &merged);
}

[[nodiscard]] ImFont* addFace(std::span<const std::uint8_t> ttf, const ImFontConfig& configuration) { return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(ttf.data()), static_cast<int>(ttf.size()), 0.0f, &configuration); }

} // namespace

ImFont* FaceSet::forKind(InlineKind kind) const noexcept {
    switch (kind) {
    case InlineKind::strong: return bold;
    case InlineKind::emphasis: return italic;
    case InlineKind::code:
    case InlineKind::math: return mono;
    case InlineKind::text:
    case InlineKind::link:
    case InlineKind::image:
    case InlineKind::footnote:
    case InlineKind::lineBreak: return regular;
    }
    return regular;
}

std::string Fonts::fitted(std::string_view label, float room) {
    if (ImGui::CalcTextSize(label.data(), label.data() + label.size()).x <= room) {
        return std::string{label};
    }
    std::string cut{label};
    while (!cut.empty()) {
        cut.resize(static_cast<std::size_t>(ImTextFindPreviousUtf8Codepoint(cut.data(), cut.data() + cut.size()) - cut.data())); // a whole character
        const std::string tried = cut + std::string{kEllipsis};
        if (ImGui::CalcTextSize(tried.c_str()).x <= room) {
            return tried;
        }
    }
    return std::string{kEllipsis};
}

void Fonts::load() {
    ImFontConfig configuration;
    configuration.FontDataOwnedByAtlas = false; // the bytes are held for the program's life and outlive the atlas

    face = addFace(builtinFaceTtf(BuiltinFace::body), configuration);
    mergeIcons(configuration);
    bold       = addFace(builtinFaceTtf(BuiltinFace::bold), configuration);
    italic     = addFace(builtinFaceTtf(BuiltinFace::italic), configuration);
    boldItalic = addFace(builtinFaceTtf(BuiltinFace::boldItalic), configuration);
    mono       = addFace(builtinFaceTtf(BuiltinFace::mono), configuration);
    title      = face;
    _builtins  = roles();
    _deckFaces.clear();
    _families.clear();
}

std::optional<FaceSet> Fonts::faceSet(std::string_view name) const {
    const FaceSet deck = roles();
    if (name.empty() || name == "body") {
        return deck;
    }
    if (const auto family = std::ranges::find(_families, name, [](const auto& entry) { return std::string_view{entry.first}; }); family != _families.end()) {
        return family->second;
    }
    if (!knowsName(name)) {
        return std::nullopt;
    }
    // a role named as a font sets everything in it, code aside: `font=mono` is a monospace paragraph
    ImFont* const throughout = name == "bold" ? deck.bold : name == "italic" ? deck.italic : name == "bolditalic" ? deck.boldItalic : name == "mono" ? deck.mono : name == "title" ? deck.title : deck.regular;
    return FaceSet{.regular = throughout, .bold = throughout, .italic = throughout, .boldItalic = throughout, .mono = deck.mono, .title = throughout};
}

bool Fonts::knowsName(std::string_view name) const { return name.empty() || isRole(name) || std::ranges::contains(_familyNames, name); }

void Fonts::stageDeck(const Manifest& manifest, const std::function<std::span<const std::uint8_t>(std::string_view)>& bytesOf, Diagnostics& diagnostics) {
    std::vector<StagedFace> staged;
    _familyNames.clear();
    const auto read = [&](std::string_view path, std::string role, std::string family, std::string style) {
        if (path.empty()) {
            return;
        }
        if (path.contains("://")) {
            diagnostics.report(DiagnosticKind::missingResource, std::string{path}, "a font must be a file in the package, not an address");
            return;
        }
        const std::span<const std::uint8_t> bytes = bytesOf(path);
        if (bytes.empty()) {
            diagnostics.report(DiagnosticKind::missingResource, std::string{path}, "the font file is not in the package");
            return;
        }
        if (!endsWithTtf(path) || !isTrueType(bytes)) {
            diagnostics.report(DiagnosticKind::missingResource, std::string{path}, "only TrueType (.ttf) faces can be used, because only those embed in the PDF export");
            return;
        }
        staged.push_back(StagedFace{.role = std::move(role), .family = std::move(family), .style = std::move(style), .ttf = {bytes.begin(), bytes.end()}});
    };
    for (const auto& [name, family] : manifest.fonts) {
        if (isRole(name)) {
            read(family.regular, name, {}, {});
            continue;
        }
        _familyNames.push_back(name);
        read(family.regular, {}, name, "regular");
        read(family.bold, {}, name, "bold");
        read(family.italic, {}, name, "italic");
        read(family.boldItalic, {}, name, "bolditalic");
    }
    scale   = manifest.sizes;
    _staged = std::move(staged);
}

bool Fonts::applyStagedDeck() {
    if (!_staged) {
        return false;
    }
    for (const DeckFace& deckFace : _deckFaces) {
        ImGui::GetIO().Fonts->RemoveFont(deckFace.font);
    }
    _deckFaces.clear();
    _families.clear();
    _fallbacksNoted.clear();
    _deckBytes.clear(); // only now: the atlas no longer reads them

    face       = _builtins.regular;
    bold       = _builtins.bold;
    italic     = _builtins.italic;
    boldItalic = _builtins.boldItalic;
    mono       = _builtins.mono;
    title      = _builtins.title;

    ImFontConfig configuration;
    configuration.FontDataOwnedByAtlas = false; // `_deckBytes` holds them until the next deck replaces these faces
    for (StagedFace& staged : *_staged) {
        const std::string_view roleOrStyle = staged.role.empty() ? std::string_view{staged.style} : std::string_view{staged.role};
        _deckBytes.push_back(std::move(staged.ttf));
        ImFont* const added = addFace(_deckBytes.back(), configuration);
        if (added == nullptr) {
            continue;
        }
        ImFontConfig merged = configuration;
        merged.MergeMode    = true;
        static_cast<void>(addFace(builtinFaceTtf(builtinBehind(roleOrStyle)), merged));
        if (staged.role == "body") {
            mergeIcons(configuration); // the chrome draws its icons in the body face
        }
        _deckFaces.push_back(DeckFace{.font = added, .name = staged.role.empty() ? std::format("{} {}", staged.family, staged.style) : staged.role, .fallback = builtinBehind(roleOrStyle), .ttf = _deckBytes.back()});
        if (ImFont** const role = roleSlot(staged.role); role != nullptr) {
            *role = added;
        }
    }
    if (std::ranges::none_of(*_staged, [](const StagedFace& staged) { return staged.role == "title"; })) {
        title = face; // a deck that sets its body and not its title has titles in its body face, as the built-ins do
    }

    for (const std::string& name : _familyNames) {
        const auto styleOf = [&](std::string_view style) -> ImFont* {
            const auto found = std::ranges::find(_deckFaces, std::format("{} {}", name, style), &DeckFace::name);
            return found == _deckFaces.end() ? nullptr : found->font;
        };
        ImFont* const regular   = styleOf("regular") != nullptr ? styleOf("regular") : face;
        const auto    orRegular = [regular](ImFont* style) { return style != nullptr ? style : regular; };
        _families.emplace_back(name, FaceSet{.regular = regular, .bold = orRegular(styleOf("bold")), .italic = orRegular(styleOf("italic")), .boldItalic = orRegular(styleOf("bolditalic")), .mono = mono, .title = regular});
    }
    _staged.reset();
    return true;
}

ImFont** Fonts::roleSlot(std::string_view role) noexcept { return role == "body" ? &face : role == "bold" ? &bold : role == "italic" ? &italic : role == "bolditalic" ? &boldItalic : role == "mono" ? &mono : role == "title" ? &title : nullptr; }

const Fonts::DeckFace* Fonts::deckFace(const ImFont* font) const noexcept {
    const auto found = std::ranges::find(_deckFaces, font, &DeckFace::font);
    return found == _deckFaces.end() ? nullptr : &*found;
}

const std::string* Fonts::deckFaceName(const ImFont* font) const noexcept {
    const DeckFace* found = deckFace(font);
    return found == nullptr ? nullptr : &found->name;
}

void Fonts::noteFallbacks(ImFont* font, float size, std::string_view text) {
    const std::string* name = deckFaceName(font);
    if (name == nullptr || std::ranges::contains(_fallbacksNoted, *name)) {
        return;
    }
    ImFontBaked* const baked = font->GetFontBaked(size);
    const char*        at    = text.data();
    const char* const  end   = text.data() + text.size();
    while (at < end) {
        unsigned int codepoint = 0U;
        at += ImTextCharFromUtf8(&codepoint, at, end);
        const ImFontGlyph* glyph = baked->FindGlyphNoFallback(static_cast<ImWchar>(codepoint));
        if (glyph != nullptr && glyph->SourceIdx == 1U) { // the built-in merged behind the deck's face; the icons come after it
            _fallbacksNoted.push_back(*name);
            _fallbacksPending.emplace_back(*name, std::format("U+{:04X}, and any other glyph the face lacks, is drawn in the built-in face", codepoint));
            return;
        }
    }
}

std::vector<std::pair<std::string, std::string>> Fonts::takeFallbackNotes() { return std::exchange(_fallbacksPending, {}); }

} // namespace gr::present
