#include "EmbeddedLogos.hpp"
#include "FallbackScene.hpp"
#include "Fonts.hpp"
#include "ImGuiScoped.hpp"
#include "LoadingScreen.hpp"
#include "SideMenu.hpp"
#include "Texture.hpp"
#include "Theme.hpp"
#include "ViewScene.hpp"
#include "WindowModeControl.hpp"

#include <gr4-present/LaunchOptions.hpp>
#include <gr4-present/Navigation.hpp>
#include <gr4-present/PresentationLoader.hpp>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gr::present;

struct Viewer {
    SDL_Window*   window    = nullptr;
    SDL_GLContext context   = nullptr;
    bool          isRunning = true;

    Theme         theme;
    Texture       logo;
    Texture       viewArtwork;
    LoadingScreen launch;
    bool          launchComplete = false;

    LaunchOptions      options;
    PresentationLoader loader;
    WindowModeControl  windowMode;
    SideMenu           menu;
    ViewScene          viewScene;
    FallbackScene      fallback;
    Navigator          navigator;
};

constexpr float kSplashLogoWidthFraction = 0.20f; // of the viewport width

[[nodiscard]] Navigator placeholderNavigatorUntilPackagesLoad() {
    return Navigator{
        .graph   = NavigationGraph{.views =
                                       {
                                         View{.id = "intro", .stepCount = 1UZ, .next = {}},
                                         View{.id = "architecture", .stepCount = 3UZ, .next = {}},
                                         View{.id = "live-demo", .stepCount = 2UZ, .next = {}},
                                     }},
        .cursor  = Cursor{.viewId = "intro", .step = 0UZ},
        .history = {},
    };
}

#ifdef __EMSCRIPTEN__
[[nodiscard]] std::string emscriptenLocation(std::string_view component) {
    const char* text = emscripten_run_script_string(std::format("window.location.{}", component).c_str());
    return text != nullptr ? std::string{text} : std::string{};
}
#endif

[[nodiscard]] std::string defaultPresentationBase(std::string_view executable) {
#ifdef __EMSCRIPTEN__
    (void)executable;
    const std::string origin = emscriptenLocation("origin");
    std::string       path   = emscriptenLocation("pathname");
    if (const auto lastSlash = path.rfind('/'); lastSlash != std::string::npos) {
        path.erase(lastSlash);
    }
    return std::format("{}{}/default", origin, path);
#else
    // beside the executable, not the working directory: the viewer may be launched from anywhere
    std::error_code ignored;
    const auto      binary    = std::filesystem::weakly_canonical(std::filesystem::path{executable}, ignored);
    const auto      directory = binary.has_parent_path() ? binary.parent_path() : std::filesystem::current_path();
    return (directory / "default").string();
#endif
}

/// the first line of the view's Markdown, without its heading marks
[[nodiscard]] std::string headingOf(std::string_view markdown) {
    const auto end  = markdown.find('\n');
    auto       line = markdown.substr(0UZ, end == std::string_view::npos ? markdown.size() : end);
    while (!line.empty() && (line.front() == '#' || line.front() == ' ')) {
        line.remove_prefix(1UZ);
    }
    return std::string{line};
}

void advanceLoading(Viewer& viewer) {
    viewer.loader.advance();
    viewer.launch.setProgress(LoadStage::content, viewer.loader.progress());

    switch (viewer.loader.state()) {
    case LoadState::failed:
        viewer.fallback.address    = std::string{viewer.loader.baseUri()};
        viewer.fallback.reason     = std::string{viewer.loader.diagnostic()};
        viewer.fallback.attempt    = viewer.loader.attempts();
        viewer.fallback.untilRetry = viewer.loader.untilRetry();
        viewer.launch.setProgress(LoadStage::content, 1.0f);
        viewer.launch.setProgress(LoadStage::initialisation, 1.0f);
        return;
    case LoadState::ready: break;
    default: return;
    }

    if (viewer.viewArtwork.id == 0 && !viewer.loader.imageBytes().empty()) {
        viewer.viewArtwork = Texture::load(viewer.loader.imageBytes());
    }
    if (!viewer.loader.manifest().views.empty()) {
        viewer.viewScene.heading = headingOf(viewer.loader.manifest().views.front().markdown);
    }
    viewer.viewScene.hint = viewer.loader.missingAsset().empty() ? std::string{} : std::format("asset unavailable: {}", viewer.loader.missingAsset());
    viewer.launch.setProgress(LoadStage::initialisation, 1.0f);
}

void applyTheme(Viewer& viewer, ColourScheme scheme) {
    if (viewer.logo.id != 0 && viewer.theme.scheme == scheme) {
        return;
    }
    viewer.logo.release();
    viewer.theme = themeFor(scheme);
    viewer.logo  = Texture::loadLogo(scheme);

    ImGuiStyle& style               = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text]     = ImGui::ColorConvertU32ToFloat4(viewer.theme.text);
    style.Colors[ImGuiCol_WindowBg] = viewer.theme.backgroundColour();
}

void buildSideMenu(Viewer& viewer) {
    viewer.menu.items.clear();
    for (const View& candidate : viewer.navigator.graph.views) {
        viewer.menu.items.push_back({.label = candidate.id, .activate = [&viewer, id = candidate.id] { viewer.navigator.jumpTo(id); }, .separatorBefore = false});
    }
    viewer.menu.items.push_back({.label = "previous", .activate = [&viewer] { viewer.navigator.previous(); }, .separatorBefore = true});
    viewer.menu.items.push_back({.label = "next", .activate = [&viewer] { viewer.navigator.next(); }, .separatorBefore = false});
    viewer.menu.items.push_back({.label = "toggle full screen", .activate = [&viewer] { viewer.windowMode.toggle(); }, .separatorBefore = true});
}

void renderFrame(Viewer& viewer) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            viewer.isRunning = false;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_FINGER_DOWN) {
            viewer.windowMode.onUserGesture();
        }
    }

    applyTheme(viewer, detectColourScheme());

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (!viewer.launchComplete) {
        advanceLoading(viewer);
        viewer.launchComplete = viewer.launch.finished();
        viewer.launch.draw(viewer.logo.id, viewer.logo.scaledToWidth(ImGui::GetMainViewport()->Size.x * kSplashLogoWidthFraction), viewer.theme);
    } else {
        advanceLoading(viewer);
        if (viewer.loader.state() == LoadState::failed) {
            viewer.fallback.draw(viewer.logo, viewer.theme);
        } else {
            viewer.viewScene.draw(viewer.viewArtwork, viewer.theme);
        }
        viewer.menu.draw(viewer.theme);
    }

    ImGui::Render();
    int width  = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(viewer.window, &width, &height);
    const ImVec4 background = viewer.theme.backgroundColour();
    glViewport(0, 0, width, height);
    glClearColor(background.x, background.y, background.z, background.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(viewer.window);
}

Viewer viewer; // the main loop is a callback under Emscripten, so the state outlives main()

#ifdef __EMSCRIPTEN__
void emscriptenMainLoop() {
    renderFrame(viewer);
    if (!viewer.isRunning) {
        emscripten_cancel_main_loop();
    }
}
#endif

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> arguments(argv, argv + argc);
#ifdef __EMSCRIPTEN__
    const std::string query    = emscriptenLocation("search");
    const std::string fragment = emscriptenLocation("hash");
    viewer.options             = LaunchOptions::from(query, fragment, arguments);
#else
    viewer.options = LaunchOptions::from("", "", arguments);
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL3 initialisation failed: %s\n", SDL_GetError());
        return 1;
    }

#ifdef __EMSCRIPTEN__
    const char* glslVersion = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    const char* glslVersion = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    viewer.window = SDL_CreateWindow("gr4-present", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (viewer.window == nullptr) {
        std::fprintf(stderr, "SDL3 window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    viewer.context = SDL_GL_CreateContext(viewer.window);
    if (viewer.context == nullptr) {
        std::fprintf(stderr, "OpenGL context creation failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(viewer.window, viewer.context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(viewer.window, viewer.context);
    ImGui_ImplOpenGL3_Init(glslVersion);

    Fonts::instance().load();
    applyTheme(viewer, detectColourScheme());
    viewer.navigator = placeholderNavigatorUntilPackagesLoad();
    viewer.loader.begin(viewer.options.value("load").value_or(defaultPresentationBase(argc > 0 ? argv[0] : "")));
    buildSideMenu(viewer);

    viewer.windowMode.window = viewer.window;
    viewer.windowMode.apply(viewer.options.windowMode());

    // the runtime is already resident by the time main() runs; under Emscripten the shell page reports the download
    viewer.launch.setProgress(LoadStage::application, 1.0f);

#ifdef __EMSCRIPTEN__
    ImGui::GetIO().IniFilename = nullptr; // no filesystem to persist window layout to
    emscripten_set_main_loop(emscriptenMainLoop, 0, 1);
#else
    SDL_GL_SetSwapInterval(1);
    while (viewer.isRunning) {
        renderFrame(viewer);
    }

    viewer.logo.release();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(viewer.context);
    SDL_DestroyWindow(viewer.window);
    SDL_Quit();
#endif
    return 0;
}
