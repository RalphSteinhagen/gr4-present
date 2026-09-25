#include "WindowModeControl.hpp"

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

namespace gr::present {

namespace {
#ifdef __EMSCRIPTEN__
[[nodiscard]] bool browserIsFullscreen() {
    EmscriptenFullscreenChangeEvent state{};
    return emscripten_get_fullscreen_status(&state) == EMSCRIPTEN_RESULT_SUCCESS && state.isFullscreen;
}
#endif
} // namespace

bool WindowModeControl::isFullscreen() const noexcept {
#ifdef __EMSCRIPTEN__
    return browserIsFullscreen();
#else
    return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0U;
#endif
}

void WindowModeControl::apply(WindowMode mode) {
    desired = mode;
#ifdef __EMSCRIPTEN__
    if (mode == WindowMode::windowed) {
        _awaitingGesture = false;
        emscripten_exit_fullscreen();
        return;
    }
    // requesting fullscreen outside a gesture handler is rejected, so record the wish and retry on the next input
    _awaitingGesture = !browserIsFullscreen();
#else
    if (window != nullptr) {
        SDL_SetWindowFullscreen(window, mode == WindowMode::fullscreen);
    }
#endif
}

void WindowModeControl::onUserGesture() {
#ifdef __EMSCRIPTEN__
    if (!_awaitingGesture) {
        return;
    }
    EmscriptenFullscreenStrategy strategy{};
    strategy.scaleMode                 = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
    strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_HIDEF;
    strategy.filteringMode             = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
    if (emscripten_request_fullscreen_strategy("#canvas", 1, &strategy) == EMSCRIPTEN_RESULT_SUCCESS) {
        _awaitingGesture = false;
    }
#endif
}

void WindowModeControl::toggle() { apply(isFullscreen() || _awaitingGesture ? WindowMode::windowed : WindowMode::fullscreen); }

} // namespace gr::present
