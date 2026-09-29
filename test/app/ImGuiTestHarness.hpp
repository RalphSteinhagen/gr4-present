#ifndef GR4_PRESENT_IMGUI_TEST_HARNESS_HPP
#define GR4_PRESENT_IMGUI_TEST_HARNESS_HPP

#include <imgui.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#pragma GCC diagnostic pop

#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

struct SDL_Window;

namespace gr::present::test {

/**
 * Rendering goes to an offscreen framebuffer and the window stays hidden, so tests neither appear on a desktop nor
 * need a display server. It is also the only correct target: reading back a window's buffer races the compositor,
 * whereas a framebuffer object's contents are owned by the application.
 */
class Harness {
public:
    ImVec2 windowSize{1280.0f, 720.0f};

    Harness()                          = default;
    Harness(const Harness&)            = delete;
    Harness& operator=(const Harness&) = delete;
    Harness(Harness&&)                 = delete;
    Harness& operator=(Harness&&)      = delete;
    ~Harness();

    [[nodiscard]] bool run();

    void addTest(const std::string& name, std::function<void(ImGuiTestContext*)> gui, std::function<void(ImGuiTestContext*)> test);

    static void captureTo(ImGuiTestContext* context, const std::string& name);

    [[nodiscard]] static std::filesystem::path captureDirectory();

    [[nodiscard]] static std::filesystem::path referenceDirectory();

    using TestFunctions = std::pair<std::function<void(ImGuiTestContext*)>, std::function<void(ImGuiTestContext*)>>;

private:
    std::deque<TestFunctions> _functions;
    SDL_Window*               _window       = nullptr;
    void*                     _glContext    = nullptr;
    ImGuiTestEngine*          _engine       = nullptr;
    unsigned int              _framebuffer  = 0U;
    unsigned int              _colourBuffer = 0U;
    bool                      _initialised  = false;

    bool initialise();
    bool createOffscreenTarget();
};

} // namespace gr::present::test

#endif // GR4_PRESENT_IMGUI_TEST_HARNESS_HPP
