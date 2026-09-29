#ifndef GR4_PRESENT_SHADER_TRANSITION_HPP
#define GR4_PRESENT_SHADER_TRANSITION_HPP

#include "EffectRenderer.hpp"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace gr::present {

class EffectLibrary;

/// a move between two slides that needs both as pictures (burn, collapse, curl): callbacks render each slide's
/// commands into a texture, then an effect mixes both at `iProgress`; they restore the framebuffer they found, so
/// captures nest; at progress 1 the result is exactly what a cut shows, which is also what the PDF export keeps
class ShaderTransition {
public:
    ShaderTransition()                                   = default;
    ShaderTransition(const ShaderTransition&)            = delete;
    ShaderTransition& operator=(const ShaderTransition&) = delete;
    ~ShaderTransition(); // its GL objects end with the context, which ends with the program

    /// picks the effect the next transition draws; false when it cannot be drawn, and the caller fades instead
    [[nodiscard]] bool prepare(EffectLibrary& library, std::string_view effectName);

    /// before a slide's commands: they go into the picture of the leaving (0) or the arriving (1) slide
    void beginCapture(ImDrawList& list, int slide, ImU32 background);
    /// after them: back to where they were going
    void endCapture(ImDrawList& list);
    /// draws the prepared effect over the whole viewport from both pictures; `inputs` carries progress, focus and time,
    /// the slide textures are filled in here
    void composite(ImDrawList& list, EffectInputs inputs);

private:
    struct Target {
        unsigned framebuffer = 0U;
        unsigned texture     = 0U;
    };
    std::array<Target, 2> _targets{};
    int                   _width  = 0;
    int                   _height = 0;
    bool                  _failed = false;

    // what the callbacks need, kept here because a callback carries one pointer
    struct Capture {
        ShaderTransition* owner = nullptr;
        std::size_t       slide = 0UZ;
        SavedTarget       saved{};
    };
    std::array<Capture, 2>          _captures{Capture{.owner = this, .slide = 0UZ, .saved = {}}, Capture{.owner = this, .slide = 1UZ, .saved = {}}};
    std::size_t                     _open       = 0UZ; // the capture endCapture closes
    ImU32                           _background = 0U;
    std::string                     _effectName;
    std::unique_ptr<EffectRenderer> _renderer;
    EffectLibrary*                  _library = nullptr;
    EffectInputs                    _inputs;

    bool        ensureTargets(int width, int height);
    static void onBegin(const ImDrawList* list, const ImDrawCmd* command);
    static void onEnd(const ImDrawList* list, const ImDrawCmd* command);
    static void onComposite(const ImDrawList* list, const ImDrawCmd* command);
};

} // namespace gr::present

#endif // GR4_PRESENT_SHADER_TRANSITION_HPP
