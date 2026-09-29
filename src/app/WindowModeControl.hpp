#ifndef GR4_PRESENT_WINDOW_MODE_CONTROL_HPP
#define GR4_PRESENT_WINDOW_MODE_CONTROL_HPP

#include <gr4-present/LaunchOptions.hpp>

struct SDL_Window;

namespace gr::present {

/// applies and toggles the window mode; a browser enters fullscreen only from a user gesture, so a request is kept
/// and applied at the first click or key press
struct WindowModeControl {
    SDL_Window* window = nullptr;

    void apply(WindowMode mode);
    void toggle();

    /// call once per frame; enters a deferred fullscreen as soon as the browser allows it
    void onUserGesture();

    [[nodiscard]] bool isFullscreen() const noexcept;

private:
    bool _awaitingGesture = false;
};

} // namespace gr::present

#endif // GR4_PRESENT_WINDOW_MODE_CONTROL_HPP
