#include "Theme.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <string_view>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace gr::present {

namespace {
constexpr ImU32 kBrandOrange = IM_COL32(0xFD, 0xB3, 0x42, 0xFF);
} // namespace

ColourScheme detectColourScheme() noexcept {
    // a presentation is often shown on a projector whose scheme has nothing to do with the presenter's desktop
    if (const char* forced = std::getenv("GR4_PRESENT_COLOUR_SCHEME"); forced != nullptr) {
        if (std::string_view{forced} == "light") {
            return ColourScheme::light;
        }
        if (std::string_view{forced} == "dark") {
            return ColourScheme::dark;
        }
    }
#ifdef __EMSCRIPTEN__
    // SDL's Emscripten backend reports SDL_SYSTEM_THEME_UNKNOWN, so the media query is the authority here
    const int prefersDark = emscripten_run_script_int("(window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) ? 1 : 0");
    return prefersDark == 1 ? ColourScheme::dark : ColourScheme::light;
#else
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK ? ColourScheme::dark : ColourScheme::light;
#endif
}

Theme themeFor(ColourScheme scheme) noexcept {
    if (scheme == ColourScheme::dark) {
        return Theme{
            .scheme = scheme, .background = IM_COL32(0x00, 0x00, 0x00, 0xFF), .logoTint = IM_COL32_WHITE, .text = IM_COL32(0x9A, 0x9A, 0x9A, 0xFF), .track = IM_COL32(0x33, 0x33, 0x33, 0xFF), .fill = kBrandOrange, .menuBackground = IM_COL32(0x14, 0x14, 0x14, 0x99), // 60 %: the slide shows through, the names stay readable
        };
    }
    return Theme{
        .scheme         = scheme,
        .background     = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF),
        .logoTint       = IM_COL32_WHITE,
        .text           = IM_COL32(0x6A, 0x6A, 0x6A, 0xFF),
        .track          = IM_COL32(0xE4, 0xE4, 0xE4, 0xFF),
        .fill           = kBrandOrange,
        .menuBackground = IM_COL32(0xF4, 0xF4, 0xF4, 0x99),
    };
}
ImU32 codeColour(const Theme& theme, TokenKind kind) noexcept {
    // Solarized's accents, which hold their contrast against either background; only the unclassified text and the
    // comments differ per scheme, because those are the two that sit closest to the background
    constexpr ImU32 yellow  = IM_COL32(0xB5, 0x89, 0x00, 0xFF);
    constexpr ImU32 orange  = IM_COL32(0xCB, 0x4B, 0x16, 0xFF);
    constexpr ImU32 magenta = IM_COL32(0xD3, 0x36, 0x82, 0xFF);
    constexpr ImU32 blue    = IM_COL32(0x26, 0x8B, 0xD2, 0xFF);
    constexpr ImU32 cyan    = IM_COL32(0x2A, 0xA1, 0x98, 0xFF);
    constexpr ImU32 green   = IM_COL32(0x85, 0x99, 0x00, 0xFF);

    const bool  dark  = theme.scheme == ColourScheme::dark;
    const ImU32 body  = dark ? IM_COL32(0x93, 0xA1, 0xA1, 0xFF) : IM_COL32(0x65, 0x7B, 0x83, 0xFF);
    const ImU32 muted = dark ? IM_COL32(0x58, 0x6E, 0x75, 0xFF) : IM_COL32(0x93, 0xA1, 0xA1, 0xFF);

    switch (kind) {
    case TokenKind::keyword: return green;
    case TokenKind::type: return yellow;
    case TokenKind::number: return magenta;
    case TokenKind::string: return cyan;
    case TokenKind::comment: return muted;
    case TokenKind::directive: return orange;
    case TokenKind::punctuation: return body;
    case TokenKind::key: return blue;
    case TokenKind::text: return body;
    }
    return body;
}

ImU32 codePanel(const Theme& theme) noexcept {
    // Solarized's own backgrounds: base3 under a light scheme, base03 under a dark one
    return theme.scheme == ColourScheme::dark ? IM_COL32(0x00, 0x2B, 0x36, 0xFF) : IM_COL32(0xFD, 0xF6, 0xE3, 0xFF);
}

} // namespace gr::present
