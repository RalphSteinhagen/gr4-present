#ifndef GR4_PRESENT_SHADER_TRANSITION_HPP
#define GR4_PRESENT_SHADER_TRANSITION_HPP

#include "Transition.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace gr::present {

/**
 * A move between two slides that needs both as pictures: the leaving slide burns away, collapses like a cathode-ray
 * tube switched off, curls up like a page.
 *
 * Each slide's draw commands are bracketed by callbacks that render them into a texture of the framebuffer's size
 * instead of the screen; a last callback draws both textures through one fragment shader that mixes them as the
 * effect says, at `progress`. The slides are drawn as on any other frame, so whatever reaches the screen reaches
 * the textures, and at progress 1 the new slide is exactly what a cut would show -- which is also what the PDF
 * export, which records no transition, keeps.
 */
class ShaderTransition {
public:
    ShaderTransition()                                   = default;
    ShaderTransition(const ShaderTransition&)            = delete;
    ShaderTransition& operator=(const ShaderTransition&) = delete;
    ~ShaderTransition()                                  = default; // its GL objects end with the context, which ends with the program

    /// before a slide's commands: they go into the picture of the leaving (0) or the arriving (1) slide
    void beginCapture(ImDrawList& list, int slide, ImU32 background);
    /// after them: back to the screen
    void endCapture(ImDrawList& list);
    /// composites both pictures over the whole viewport; `focus` is where a ripple starts, in pixels
    void composite(ImDrawList& list, ShaderEffect effect, float progress, ImVec2 focus);

    /// whether the GL side was built; a viewer without it falls back to a fade
    [[nodiscard]] bool usable();

private:
    struct Target {
        unsigned framebuffer = 0U;
        unsigned texture     = 0U;
    };

    std::array<Target, 2> _targets{};
    int                   _width   = 0;
    int                   _height  = 0;
    unsigned              _program = 0U;
    unsigned              _array   = 0U;
    bool                  _failed  = false;

    // what the callbacks need, kept here because a callback carries one pointer
    struct Capture {
        ShaderTransition* owner = nullptr;
        std::size_t       slide = 0UZ;
    };
    std::array<Capture, 2> _captures{Capture{.owner = this, .slide = 0UZ}, Capture{.owner = this, .slide = 1UZ}};
    ImU32                  _background = 0U;
    ShaderEffect           _effect     = ShaderEffect::fire;
    float                  _progress   = 0.0f;
    ImVec2                 _focus{};
    float                  _time = 0.0f;

    bool ensureTargets(int width, int height);
    bool ensureProgram();

    static void onBegin(const ImDrawList* list, const ImDrawCmd* command);
    static void onEnd(const ImDrawList* list, const ImDrawCmd* command);
    static void onComposite(const ImDrawList* list, const ImDrawCmd* command);
};

} // namespace gr::present

#endif // GR4_PRESENT_SHADER_TRANSITION_HPP
