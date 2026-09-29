#ifndef GR4_PRESENT_E2E_NATIVE_DISPLAY_HPP
#define GR4_PRESENT_E2E_NATIVE_DISPLAY_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

struct _XDisplay;

namespace gr::present::e2e {

struct Box {
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;
};

using Pixels = std::vector<std::vector<std::uint32_t>>; // rows of 0xRRGGBB

/// an Xvfb server of its own, 1280 x 720, for as long as this lives
class VirtualDisplay {
public:
    VirtualDisplay();
    ~VirtualDisplay();
    VirtualDisplay(const VirtualDisplay&)            = delete;
    VirtualDisplay& operator=(const VirtualDisplay&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return _name; }

private:
    pid_t       _server = -1;
    std::string _name;
};

/// the viewer started on `display` from its own directory, asked to quit and waited for as this goes
class ViewerProcess {
public:
    ViewerProcess(const std::filesystem::path& viewer, const std::string& display, const std::vector<std::string>& arguments);
    ~ViewerProcess();
    ViewerProcess(const ViewerProcess&)            = delete;
    ViewerProcess& operator=(const ViewerProcess&) = delete;

    /// asks it to quit and says whether it did within `patience`; one that did not is killed
    bool quit(std::chrono::seconds patience = std::chrono::seconds{20});

private:
    pid_t _process = -1;
};

/// real pointer and key events through XTest, and the screen's pixels
class Screen {
public:
    explicit Screen(const std::string& display);
    ~Screen();
    Screen(const Screen&)            = delete;
    Screen& operator=(const Screen&) = delete;

    /// moved there, pressed, held 150 ms and released, as a mouse click
    void click(int x, int y);
    /// a quick press and release where the pointer is moved to, as a finger's tap
    void tap(int x, int y);
    /// pressed and released, then 1.5 s for the deck to follow
    void key(std::string_view keysym);
    /// every `stride`-th pixel of `box`, row by row
    [[nodiscard]] Pixels pixels(Box box, int stride = 1) const;

private:
    _XDisplay*    _display = nullptr;
    unsigned long _root    = 0UL;
};

} // namespace gr::present::e2e

#endif // GR4_PRESENT_E2E_NATIVE_DISPLAY_HPP
