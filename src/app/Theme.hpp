#ifndef GR4_PRESENT_THEME_HPP
#define GR4_PRESENT_THEME_HPP

#include <imgui.h>

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

} // namespace gr::present

#endif // GR4_PRESENT_THEME_HPP
