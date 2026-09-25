#ifndef GR4_PRESENT_FONTS_HPP
#define GR4_PRESENT_FONTS_HPP

#include <imgui.h>

namespace gr::present {

/**
 * ImGui's built-in face is a small bitmap font that blurs when scaled, so text is drawn with the bundled Roboto,
 * which ImGui bakes at whatever size is pushed. Sizes are fractions of the viewport height rather than fixed points:
 * a point size readable on a laptop is invisible on a projector and absurd on a phone.
 */
struct Fonts {
    ImFont* face = nullptr;

    [[nodiscard]] static Fonts& instance();

    void load();

    [[nodiscard]] static float statusSize(float viewportHeight) noexcept { return viewportHeight * 0.022f; }
    [[nodiscard]] static float bodySize(float viewportHeight) noexcept { return viewportHeight * 0.032f; }
    [[nodiscard]] static float titleSize(float viewportHeight) noexcept { return viewportHeight * 0.075f; }
};

/// pushes the bundled face at `pixels` and pops it at the end of the scope
struct ScopedFont {
    explicit ScopedFont(float pixels) { ImGui::PushFont(Fonts::instance().face, pixels); }
    ScopedFont(const ScopedFont&)            = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
    ~ScopedFont() { ImGui::PopFont(); }
};

} // namespace gr::present

#endif // GR4_PRESENT_FONTS_HPP
