#include "NativeDisplay.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace gr::present::e2e {

namespace {
using namespace std::chrono_literals;

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;

/// starts `program` with `arguments`, in `directory` if one is given, its environment ours with `overrides` on top
pid_t spawn(const std::string& program, const std::vector<std::string>& arguments, const std::filesystem::path& directory, const std::vector<std::string>& overrides) {
    std::vector<std::string> environment = overrides;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string_view variable{*entry};
        const auto             overridden = [variable](const std::string& override) { return variable.substr(0UZ, variable.find('=') + 1UZ) == override.substr(0UZ, override.find('=') + 1UZ); };
        if (std::ranges::none_of(overrides, overridden)) {
            environment.emplace_back(variable);
        }
    }
    std::vector<char*> argv{const_cast<char*>(program.c_str())};
    for (const std::string& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    std::vector<char*> envp;
    for (std::string& variable : environment) {
        envp.push_back(variable.data());
    }
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    const int quiet = ::open("/dev/null", O_WRONLY);
    posix_spawn_file_actions_adddup2(&actions, quiet, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, quiet, STDERR_FILENO);
    if (!directory.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    }
    pid_t process = -1;
    if (posix_spawnp(&process, program.c_str(), &actions, nullptr, argv.data(), envp.data()) != 0) {
        process = -1;
    }
    posix_spawn_file_actions_destroy(&actions);
    ::close(quiet);
    return process;
}

/// SIGTERM, then waits up to `patience`; says whether it exited in that time, and kills it if not
bool stop(pid_t process, std::chrono::seconds patience) {
    if (process <= 0) {
        return true;
    }
    ::kill(process, SIGTERM);
    for (auto deadline = std::chrono::steady_clock::now() + patience; std::chrono::steady_clock::now() < deadline; std::this_thread::sleep_for(50ms)) {
        if (::waitpid(process, nullptr, WNOHANG) == process) {
            return true;
        }
    }
    ::kill(process, SIGKILL);
    ::waitpid(process, nullptr, 0);
    return false;
}
} // namespace

VirtualDisplay::VirtualDisplay() {
    // Xvfb picks a free display number and writes it to the descriptor it is given, so two of them never collide
    int written[2];
    if (::pipe(written) != 0) {
        return;
    }
    _server = spawn("Xvfb", {"-displayfd", std::to_string(written[1]), "-screen", "0", std::format("{}x{}x24", kWidth, kHeight), "-nolisten", "tcp"}, {}, {});
    ::close(written[1]);
    char       number[16]{};
    const auto read = ::read(written[0], number, sizeof(number) - 1UZ);
    ::close(written[0]);
    if (read > 0) {
        _name = ":" + std::string{number, static_cast<std::size_t>(read)};
        std::erase(_name, '\n');
    }
}

VirtualDisplay::~VirtualDisplay() { stop(_server, 10s); }

ViewerProcess::ViewerProcess(const std::filesystem::path& viewer, const std::string& display, const std::vector<std::string>& arguments) : _process(spawn(viewer.string(), arguments, viewer.parent_path(), {"DISPLAY=" + display, "SDL_VIDEO_DRIVER=x11"})) {}

ViewerProcess::~ViewerProcess() { std::ignore = quit(); }

bool ViewerProcess::quit(std::chrono::seconds patience) {
    const bool inTime = stop(_process, patience);
    _process          = -1;
    return inTime;
}

Screen::Screen(const std::string& display) : _display(XOpenDisplay(display.c_str())) {
    if (_display != nullptr) {
        _root = XDefaultRootWindow(_display);
    }
}

Screen::~Screen() {
    if (_display != nullptr) {
        XCloseDisplay(_display);
    }
}

void Screen::click(int x, int y) {
    XTestFakeMotionEvent(_display, -1, x, y, 0);
    XFlush(_display);
    std::this_thread::sleep_for(300ms);
    XTestFakeButtonEvent(_display, 1, True, 0);
    XFlush(_display);
    std::this_thread::sleep_for(150ms);
    XTestFakeButtonEvent(_display, 1, False, 0);
    XFlush(_display);
    std::this_thread::sleep_for(300ms);
}

void Screen::tap(int x, int y) {
    XTestFakeMotionEvent(_display, -1, x, y, 0);
    XTestFakeButtonEvent(_display, 1, True, 0);
    XFlush(_display);
    std::this_thread::sleep_for(30ms);
    XTestFakeButtonEvent(_display, 1, False, 0);
    XFlush(_display);
}

void Screen::key(std::string_view keysym) {
    const KeyCode code = XKeysymToKeycode(_display, XStringToKeysym(std::string{keysym}.c_str()));
    XTestFakeKeyEvent(_display, code, True, 0);
    XFlush(_display);
    std::this_thread::sleep_for(100ms);
    XTestFakeKeyEvent(_display, code, False, 0);
    XFlush(_display);
    std::this_thread::sleep_for(1500ms);
}

Pixels Screen::pixels(Box box, int stride) const {
    Pixels  rows;
    XImage* image = XGetImage(_display, _root, box.x, box.y, static_cast<unsigned>(box.width), static_cast<unsigned>(box.height), AllPlanes, ZPixmap);
    if (image == nullptr) {
        return rows;
    }
    for (int y = 0; y < box.height; y += stride) {
        std::vector<std::uint32_t>& row = rows.emplace_back();
        for (int x = 0; x < box.width; x += stride) {
            row.push_back(static_cast<std::uint32_t>(XGetPixel(image, x, y)) & 0xFFFFFFU);
        }
    }
    XDestroyImage(image);
    return rows;
}

} // namespace gr::present::e2e
