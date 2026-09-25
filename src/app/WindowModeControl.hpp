#ifndef GR4_PRESENT_WINDOW_MODE_CONTROL_HPP
#define GR4_PRESENT_WINDOW_MODE_CONTROL_HPP

#include <gr4-present/LaunchOptions.hpp>

struct SDL_Window;

namespace gr::present {

/**
 * Applies and toggles the window mode.
 *
 * Natively the mode can be set at any time. A browser refuses to enter fullscreen except from a user gesture, so a
 * requested fullscreen is remembered and applied at the first click or key press; until then the canvas merely fills
 * the tab. `pending()` reports that wait so the viewer can say why nothing has happened yet.
 */
struct WindowModeControl {
    SDL_Window* window  = nullptr;
    WindowMode  desired = WindowMode::fullscreen;

    void apply(WindowMode mode);
    void toggle();

    /// call once per frame; enters a deferred fullscreen as soon as the browser allows it
    void onUserGesture();

    [[nodiscard]] bool isFullscreen() const noexcept;
    [[nodiscard]] bool pending() const noexcept { return _awaitingGesture; }

private:
    bool _awaitingGesture = false;
};

} // namespace gr::present

#endif // GR4_PRESENT_WINDOW_MODE_CONTROL_HPP
