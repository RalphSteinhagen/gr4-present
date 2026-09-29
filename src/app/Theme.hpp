#ifndef GR4_PRESENT_THEME_HPP
#define GR4_PRESENT_THEME_HPP

#include <gr4-present/Highlight.hpp>

#include <imgui.h>

#include <array>

namespace gr::present {

enum class ColourScheme { light, dark };

[[nodiscard]] ColourScheme detectColourScheme() noexcept;

struct Theme {
    ColourScheme scheme         = ColourScheme::light;
    ImU32        background     = 0u;
    ImU32        logoTint       = 0u; // applied to the glyph texture, which is already baked per scheme
    ImU32        text           = 0u;
    ImU32        track          = 0u;
    ImU32        fill           = 0u; // brand orange, identical in both schemes
    ImU32        menuBackground = 0u;

    [[nodiscard]] ImVec4 backgroundColour() const noexcept { return ImGui::ColorConvertU32ToFloat4(background); }
};

[[nodiscard]] Theme themeFor(ColourScheme scheme) noexcept;

/// colours for a highlighted code block; Solarized accents stay legible on both a light and a dark background
/// E. Schoonover, "Solarized", 2011, https://ethanschoonover.com/solarized/
[[nodiscard]] ImU32 codeColour(const Theme& theme, TokenKind kind) noexcept;

/// the panel a fenced block sits on
[[nodiscard]] ImU32 codePanel(const Theme& theme) noexcept;

/// series colours of the deck's plots and OpenDigitizer's live charts: brand orange, then Solarized accents as in code
inline constexpr std::array<ImU32, 5UZ> kSeriesColours{IM_COL32(0xFD, 0xB3, 0x42, 0xFF), IM_COL32(0x26, 0x8B, 0xD2, 0xFF), IM_COL32(0x85, 0x99, 0x00, 0xFF), IM_COL32(0xD3, 0x36, 0x82, 0xFF), IM_COL32(0x2A, 0xA1, 0x98, 0xFF)};
inline constexpr float                  kSeriesWidthPixels = 3.0f;  // px
inline constexpr float                  kGridAlpha         = 0.18f; // of the text colour

/// `colour` with its alpha multiplied by `alpha`
[[nodiscard]] inline ImU32 dimmed(ImU32 colour, float alpha) noexcept {
    ImVec4 components = ImGui::ColorConvertU32ToFloat4(colour);
    components.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(components);
}

} // namespace gr::present

#endif // GR4_PRESENT_THEME_HPP
