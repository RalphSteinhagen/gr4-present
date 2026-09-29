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
    // the projector's scheme need not match the presenter's desktop, so it can be forced
    if (const char* forced = std::getenv("GR4_PRESENT_COLOUR_SCHEME"); forced != nullptr) {
        if (std::string_view{forced} == "light") {
            return ColourScheme::light;
        }
        if (std::string_view{forced} == "dark") {
            return ColourScheme::dark;
        }
    }
#ifdef __EMSCRIPTEN__
    // SDL's Emscripten backend reports SDL_SYSTEM_THEME_UNKNOWN, so the media query decides
    const int prefersDark = emscripten_run_script_int("(window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) ? 1 : 0");
    return prefersDark == 1 ? ColourScheme::dark : ColourScheme::light;
#else
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK ? ColourScheme::dark : ColourScheme::light;
#endif
}

Theme themeFor(ColourScheme scheme) noexcept {
    if (scheme == ColourScheme::dark) {
        return Theme{
            .scheme     = scheme,
            .background = IM_COL32(0x00, 0x00, 0x00, 0xFF),
            .logoTint   = IM_COL32_WHITE,
            .text       = IM_COL32(0x9A, 0x9A, 0x9A, 0xFF),
            .track      = IM_COL32(0x33, 0x33, 0x33, 0xFF),
            .fill       = kBrandOrange,
            // 60 % alpha, so the slide shows through
            .menuBackground = IM_COL32(0x14, 0x14, 0x14, 0x99),
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
    // Solarized accents hold their contrast on either background; only text and comments, nearest it, differ
    constexpr ImU32 kYellow  = IM_COL32(0xB5, 0x89, 0x00, 0xFF);
    constexpr ImU32 kOrange  = IM_COL32(0xCB, 0x4B, 0x16, 0xFF);
    constexpr ImU32 kMagenta = IM_COL32(0xD3, 0x36, 0x82, 0xFF);
    constexpr ImU32 kBlue    = IM_COL32(0x26, 0x8B, 0xD2, 0xFF);
    constexpr ImU32 kCyan    = IM_COL32(0x2A, 0xA1, 0x98, 0xFF);
    constexpr ImU32 kGreen   = IM_COL32(0x85, 0x99, 0x00, 0xFF);

    const bool  dark  = theme.scheme == ColourScheme::dark;
    const ImU32 body  = dark ? IM_COL32(0x93, 0xA1, 0xA1, 0xFF) : IM_COL32(0x65, 0x7B, 0x83, 0xFF);
    const ImU32 muted = dark ? IM_COL32(0x58, 0x6E, 0x75, 0xFF) : IM_COL32(0x93, 0xA1, 0xA1, 0xFF);

    switch (kind) {
    case TokenKind::keyword: return kGreen;
    case TokenKind::type: return kYellow;
    case TokenKind::number: return kMagenta;
    case TokenKind::string: return kCyan;
    case TokenKind::comment: return muted;
    case TokenKind::directive: return kOrange;
    case TokenKind::punctuation: return body;
    case TokenKind::key: return kBlue;
    case TokenKind::text: return body;
    }
    return body;
}

ImU32 codePanel(const Theme& theme) noexcept {
    // Solarized base3 (light) and base03 (dark)
    return theme.scheme == ColourScheme::dark ? IM_COL32(0x00, 0x2B, 0x36, 0xFF) : IM_COL32(0xFD, 0xF6, 0xE3, 0xFF);
}

} // namespace gr::present
