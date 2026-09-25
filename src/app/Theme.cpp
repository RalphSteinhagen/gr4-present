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
            .scheme         = scheme,
            .background     = IM_COL32(0x00, 0x00, 0x00, 0xFF),
            .logoTint       = IM_COL32_WHITE,
            .text           = IM_COL32(0x9A, 0x9A, 0x9A, 0xFF),
            .track          = IM_COL32(0x33, 0x33, 0x33, 0xFF),
            .fill           = kBrandOrange,
            .menuBackground = IM_COL32(0x14, 0x14, 0x14, 0xF2),
        };
    }
    return Theme{
        .scheme         = scheme,
        .background     = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF),
        .logoTint       = IM_COL32_WHITE,
        .text           = IM_COL32(0x6A, 0x6A, 0x6A, 0xFF),
        .track          = IM_COL32(0xE4, 0xE4, 0xE4, 0xFF),
        .fill           = kBrandOrange,
        .menuBackground = IM_COL32(0xF4, 0xF4, 0xF4, 0xF2),
    };
}

} // namespace gr::present
