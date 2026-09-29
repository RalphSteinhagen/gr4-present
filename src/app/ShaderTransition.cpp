#ifndef __EMSCRIPTEN__
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES // framebuffers are GL 3 entry points, declared only with the extension prototypes
#endif
#endif

#include "ShaderTransition.hpp"

#include "EffectLibrary.hpp"

#include <gnuradio-4.0/Logger.hpp>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <algorithm>

namespace gr::present {

ShaderTransition::~ShaderTransition() = default;

bool ShaderTransition::prepare(EffectLibrary& library, std::string_view effectName) {
    _library = &library;
    if (_renderer == nullptr || _effectName != effectName) {
        _effectName = std::string{effectName};
        _renderer   = library.renderer(effectName);
    }
    if (_renderer == nullptr) {
        return false;
    }
    if (!_renderer->ready()) {
        library.reportFailure(*_renderer);
        return false;
    }
    return !_failed;
}

bool ShaderTransition::ensureTargets(int width, int height) {
    if (width == _width && height == _height && _targets[0].framebuffer != 0U) {
        return true;
    }
    _width  = width;
    _height = height;
    for (Target& target : _targets) {
        if (target.framebuffer == 0U) {
            glGenFramebuffers(1, &target.framebuffer);
            glGenTextures(1, &target.texture);
        }
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gr::log::warning("a transition's framebuffer is incomplete; it fades instead");
            _failed = true;
        }
    }
    return !_failed;
}

void ShaderTransition::beginCapture(ImDrawList& list, int slide, ImU32 background) {
    _background = background;
    _open       = static_cast<std::size_t>(std::clamp(slide, 0, 1));
    list.AddCallback(&ShaderTransition::onBegin, &_captures[_open]);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ShaderTransition::endCapture(ImDrawList& list) {
    list.AddCallback(&ShaderTransition::onEnd, &_captures[_open]);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ShaderTransition::composite(ImDrawList& list, EffectInputs inputs) {
    _inputs = std::move(inputs);
    list.AddCallback(&ShaderTransition::onComposite, this);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ShaderTransition::onBegin(const ImDrawList*, const ImDrawCmd* command) {
    auto&             capture = *static_cast<Capture*>(command->UserCallbackData);
    ShaderTransition& self    = *capture.owner;
    const ImGuiIO&    io      = ImGui::GetIO();
    const int         width   = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    const int         tall    = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    capture.saved             = SavedTarget::save();
    if (!self.ensureTargets(width, tall)) {
        capture.saved.restore();
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, self._targets[capture.slide].framebuffer);
    const ImVec4 clear = ImGui::ColorConvertU32ToFloat4(self._background);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.x, clear.y, clear.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void ShaderTransition::onEnd(const ImDrawList*, const ImDrawCmd* command) { static_cast<const Capture*>(command->UserCallbackData)->saved.restore(); }

void ShaderTransition::onComposite(const ImDrawList*, const ImDrawCmd* command) {
    auto& self = *static_cast<ShaderTransition*>(command->UserCallbackData);
    if (self._renderer == nullptr || self._failed || self._targets[0].framebuffer == 0U) {
        return;
    }
    self._inputs.slideFrom = self._targets[0].texture;
    self._inputs.slideTo   = self._targets[1].texture;
    glDisable(GL_SCISSOR_TEST);
    self._renderer->drawInto({0, 0, self._width, self._height}, self._inputs);
    if (self._renderer->failed() && self._library != nullptr) {
        self._library->reportFailure(*self._renderer);
    }
}

} // namespace gr::present
