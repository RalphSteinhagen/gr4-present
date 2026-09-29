#include "ImGuiTestHarness.hpp"

#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL.h>

#define GL_GLEXT_PROTOTYPES // framebuffer objects are GL 3.0 entry points, not in the GL 1.1 base header
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <thread>
#include <utility>
#include <vector>

namespace gr::present::test {

namespace {

void reverseRowOrder(unsigned char* pixels, int width, int height) {
    const auto stride = static_cast<std::size_t>(width) * 4UZ;
    auto*      top    = pixels;
    auto*      bottom = top + stride * static_cast<std::size_t>(height - 1);
    while (top < bottom) {
        std::swap_ranges(top, top + stride, bottom);
        top += stride;
        bottom -= stride;
    }
}

bool captureFramebuffer(ImGuiID, int x, int y, int width, int height, unsigned int* pixels, void*) {
    const int bottomUpY = static_cast<int>(ImGui::GetIO().DisplaySize.y) - (y + height);
    glReadBuffer(GL_COLOR_ATTACHMENT0); // the offscreen target, not a window buffer the compositor also owns
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, bottomUpY, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    reverseRowOrder(reinterpret_cast<unsigned char*>(pixels), width, height);
    return true;
}

} // namespace

std::filesystem::path Harness::captureDirectory() {
    const auto directory = std::filesystem::path{GR4_PRESENT_BUILD_DIRECTORY} / "captures";
    std::filesystem::create_directories(directory);
    return directory;
}

std::filesystem::path Harness::referenceDirectory() { return std::filesystem::path{GR4_PRESENT_REFERENCE_DIRECTORY}; }

void Harness::captureTo(ImGuiTestContext* context, const std::string& name) {
    // an explicit viewport rect captures everything drawn, including the background draw list, in a single frame;
    // naming a window instead would miss anything drawn outside it and depends on that window's id
    context->Yield(2); // the test function is entered before this test's own frames have been presented

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiCaptureArgs*    args     = context->CaptureArgs;
    args->InCaptureRect           = ImRect(viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y});
    args->InPadding               = 0.0f;
    std::snprintf(args->InOutputFile, IM_ARRAYSIZE(args->InOutputFile), "%s/%s.png", captureDirectory().c_str(), name.c_str());
    // IncludeOtherWindows is required: without it the tool hides every window that was not named by CaptureAddWindow
    context->CaptureScreenshot(ImGuiCaptureFlags_HideMouseCursor | ImGuiCaptureFlags_IncludeOtherWindows);
}

void Harness::addTest(const std::string& name, std::function<void(ImGuiTestContext*)> gui, std::function<void(ImGuiTestContext*)> test) {
    if (!_initialised && !initialise()) {
        return;
    }
    ImGuiTest* entry = IM_REGISTER_TEST(_engine, "gr4-present", name.c_str());
    _functions.push_back({std::move(gui), std::move(test)}); // deque keeps the addresses stable
    entry->UserData = &_functions.back();
    entry->GuiFunc  = [](ImGuiTestContext* context) {
        auto* functions = static_cast<Harness::TestFunctions*>(context->Test->UserData);
        functions->first(context);
    };
    entry->TestFunc = [](ImGuiTestContext* context) {
        auto* functions = static_cast<Harness::TestFunctions*>(context->Test->UserData);
        functions->second(context);
    };
}

void preferOffscreenVideoDriverUnlessOverridden() {
    if (std::getenv("SDL_VIDEODRIVER") == nullptr) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    }
}

bool Harness::initialise() {
    preferOffscreenVideoDriverUnlessOverridden();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "SDL3 initialisation failed: {}", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // no HIGH_PIXEL_DENSITY: a capture whose size depends on the reviewer's display scaling cannot be a reference.
    // The window exists only to own the GL context; everything is drawn into an offscreen framebuffer.
    _window = SDL_CreateWindow("gr4-present tests", static_cast<int>(windowSize.x), static_cast<int>(windowSize.y), SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (_window == nullptr) {
        std::println(stderr, "SDL3 window creation failed: {}", SDL_GetError());
        return false;
    }
    _glContext = SDL_GL_CreateContext(_window);
    if (_glContext == nullptr) {
        std::println(stderr, "OpenGL context creation failed: {}", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(_window, static_cast<SDL_GLContext>(_glContext));
    SDL_GL_SetSwapInterval(0);

    if (!createOffscreenTarget()) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForOpenGL(_window, static_cast<SDL_GLContext>(_glContext));
    ImGui_ImplOpenGL3_Init("#version 150");

    _engine                            = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& engineIo        = ImGuiTestEngine_GetIO(_engine);
    engineIo.ConfigVerboseLevel        = ImGuiTestVerboseLevel_Warning;
    engineIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Info;
    engineIo.ConfigRunSpeed            = ImGuiTestRunSpeed_Fast;
    engineIo.ConfigLogToTTY            = true;
    engineIo.ScreenCaptureFunc         = captureFramebuffer;
    engineIo.ConfigCaptureOnError      = false;
    engineIo.ConfigWatchdogKillTest    = 60.0f;
    ImGuiTestEngine_Start(_engine, ImGui::GetCurrentContext());

    _initialised = true;
    return true;
}

bool Harness::createOffscreenTarget() {
    const auto width  = static_cast<GLsizei>(windowSize.x);
    const auto height = static_cast<GLsizei>(windowSize.y);

    glGenRenderbuffers(1, &_colourBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _colourBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);

    glGenFramebuffers(1, &_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colourBuffer);

    if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
        std::println(stderr, "offscreen framebuffer incomplete: 0x{:x}", static_cast<unsigned>(status));
        return false;
    }
    return true;
}

bool Harness::run() {
    if (!_initialised && !initialise()) {
        return false;
    }
    ImGuiTestEngine_QueueTests(_engine, ImGuiTestGroup_Tests);

    while (!ImGuiTestEngine_IsTestQueueEmpty(_engine)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::Render();

        glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        glViewport(0, 0, static_cast<GLsizei>(windowSize.x), static_cast<GLsizei>(windowSize.y));
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // nothing is presented, so there is no swap to race; the capture reads the framebuffer we just finished
        glFinish();
        ImGuiTestEngine_PostSwap(_engine);
    }

    int tested    = 0;
    int succeeded = 0;
    ImGuiTestEngine_GetResult(_engine, tested, succeeded);
    std::println("[harness] {}/{} engine tests succeeded", succeeded, tested);
    return tested == succeeded;
}

Harness::~Harness() {
    // each resource is released on its own terms: initialise() can fail after any one of them, and an early return
    // here would leak everything acquired before the failure
    if (_engine != nullptr) {
        ImGuiTestEngine_Stop(_engine);
    }
    if (_framebuffer != 0U) {
        glDeleteFramebuffers(1, &_framebuffer);
    }
    if (_colourBuffer != 0U) {
        glDeleteRenderbuffers(1, &_colourBuffer);
    }
    if (_initialised) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext(); // the engine asserts that ImGui's context is gone before its own
    }
    if (_engine != nullptr) {
        ImGuiTestEngine_DestroyContext(_engine);
    }
    if (_glContext != nullptr) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(_glContext));
    }
    if (_window != nullptr) {
        SDL_DestroyWindow(_window);
    }
    SDL_Quit();
}

} // namespace gr::present::test
