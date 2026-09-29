#include "Viewer.hpp"

#include "Animation.hpp"
#include "BuiltinFace.hpp"
#include "EmbeddedLogos.hpp"
#include "Fonts.hpp"
#include "ImGuiScoped.hpp"
#include "MathRender.hpp"
#include "SwipeNavigation.hpp"

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
#include <common/LookAndFeel.hpp>
#include <common/TouchHandler.hpp>
#endif

#include <gnuradio-4.0/Logger.hpp>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

namespace gr::present {
namespace {

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

void drawFrameRate(const Viewer& viewer) {
    if (!viewer.options.contains("fps") || viewer.sections.empty() || viewer.loader.state() == LoadState::failed || viewer.exporting) {
        return;
    }
    ImGuiViewport&    viewport = *ImGui::GetMainViewport();
    const ImGuiIO&    io       = ImGui::GetIO();
    const std::string label    = std::format("{:.0f} FPS", io.Framerate);
    const ImVec2      size     = ImGui::CalcTextSize(label.c_str());
    constexpr float   kPadding = 8.0f;
    const ImVec2      position{viewport.Pos.x + viewport.Size.x - size.x - kPadding, viewport.Pos.y + kPadding};
    ImGui::GetForegroundDrawList(&viewport)->AddText(position, IM_COL32(255, 255, 255, 180), label.c_str());
}

void renderFrame(Viewer& viewer) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            viewer.isRunning = false;
        }
#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
        DigitizerUi::TouchHandler<>::processSDLEvent(event); // finger tracking, and the mouse events ImGui needs
#endif
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_FINGER_DOWN) {
            viewer.windowMode.onUserGesture();
        }
    }

    applyTheme(viewer, viewer.exporting ? ColourScheme::light : detectColourScheme()); // a PDF is for paper

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
    // pinch and rotation go unused here, but this also recovers a finger whose lift event never arrived
    DigitizerUi::TouchHandler<>::updateGestures();
#endif
    tendLiveGraphs(viewer);

    static_cast<void>(Fonts::instance().applyStagedDeck()); // between frames: the atlas is not to change under one
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    if (viewer.exporting) {
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX); // nothing hovered: a chart's tooltip is not part of a page
    }
    ImGui::NewFrame();

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
    // the flick is read first: endFrame() clears the per-frame finger flags, and a flick read twice advances twice
    if (!viewer.sections.empty()) {
        applyZoomInput(viewer);
        applyTouchNavigation(viewer);
        if (DigitizerUi::TouchHandler<>::nFingers == 0UZ) {
            viewer.pinching = false;
        }
    }
    DigitizerUi::TouchHandler<>::endFrame();
#else
    if (!viewer.sections.empty()) {
        applyZoomInput(viewer);
    }
#endif

    if (!viewer.launchComplete) {
        advanceLoading(viewer);
        viewer.launchComplete = viewer.launch.finished();
        viewer.launch.draw(viewer.logo.id, viewer.logo.scaledToWidth(ImGui::GetMainViewport()->Size.x * kSplashLogoWidthFraction), viewer.theme);
    } else {
        advanceLoading(viewer);
        beginExport(viewer);
        const bool recordingPage = viewer.exporting && prepareExportPage(viewer);
        if (!viewer.sections.empty() && !viewer.exporting) {
            followRemoteCursor(viewer);
            applyNavigationKeys(viewer);
            const auto& views   = viewer.navigator.graph.views;
            viewer.menu.current = static_cast<std::size_t>(std::ranges::find(views, viewer.navigator.cursor.viewId, &View::id) - views.begin());
            advanceTransition(viewer, ImGui::GetIO().DeltaTime);
            advanceOnItsOwn(viewer, ImGui::GetIO().DeltaTime);
        }
        if (viewer.loader.state() == LoadState::failed) {
            viewer.fallback.draw(viewer.logo, viewer.theme);
        } else {
            viewer.zoomedLists.clear();
            drawCurrentSection(viewer);
            drawPointer(viewer);
            tendEffects(viewer);
            drawFrameRate(viewer);
            viewer.zoomedLists.push_back(viewer.documentView.drawnInto);
            applyVideoButtons(viewer);
            applyLinkClicks(viewer);
            applyTextSelection(viewer);
        }
        if (recordingPage) {
            finishExportPage(viewer);
        }
        if (!viewer.exporting) { // a page is the slide alone
            viewer.menu.draw(viewer.theme);
            viewer.diagnosticsPanel.draw(viewer.diagnostics, viewer.theme);
            drawNotes(viewer);
            drawPhoneLink(viewer);
            drawBreakScreen(viewer);
        }
    }

    ImGui::Render();
    applyZoomToDrawData(viewer);
    int width  = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(viewer.window, &width, &height);
    const ImVec4 background = viewer.theme.backgroundColour();
    glViewport(0, 0, width, height);
    glClearColor(background.x, background.y, background.z, background.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (viewer.exporting) {
        captureExportPixels(viewer); // before the swap: a browser's buffer is not kept past it
    }
    SDL_GL_SwapWindow(viewer.window);
}

Viewer viewer; // the main loop is a callback under Emscripten, so the state outlives main()

#ifdef __EMSCRIPTEN__
/**
 * Keep the window the size of the canvas the browser is showing.
 *
 * The canvas is styled to fill the page, so its size is the browser's to decide and changes when the window is
 * resized or the device is turned over. SDL sets it once when the window is created and then stops following it,
 * falling back to its own 800x600 after the first key press, so every later slide would be laid out for a screen
 * it is not shown on.
 */
void followCanvas() {
    double cssWidth  = 0.0;
    double cssHeight = 0.0;
    if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS || cssWidth < 1.0 || cssHeight < 1.0) {
        return;
    }
    int have       = 0;
    int haveHeight = 0;
    SDL_GetWindowSize(viewer.window, &have, &haveHeight);
    if (const int wide = static_cast<int>(cssWidth), tall = static_cast<int>(cssHeight); wide != have || tall != haveHeight) {
        SDL_SetWindowSize(viewer.window, wide, tall);
    }
}

void emscriptenMainLoop() {
    if (!viewer.exporting) { // an export lays its pages out at 1920x1080, whatever the tab's size
        followCanvas();
    }
    renderFrame(viewer);
    if (!viewer.isRunning) {
        emscripten_cancel_main_loop();
    }
}
#endif

} // namespace
} // namespace gr::present

using namespace gr::present;

int main(int argc, char** argv) {
    captureLogFromStart();
    const std::vector<std::string_view> arguments(argv, argv + argc);
#ifdef __EMSCRIPTEN__
    passBrowserKeys();
    const std::string query    = emscriptenLocation("search");
    const std::string fragment = emscriptenLocation("hash");
    viewer.options             = LaunchOptions::from(query, fragment, arguments);
    if (!viewer.options.contains("export")) { // an export walks the deck on its own, and must not take other windows with it
        joinDeckChannel();
        followAddressBar();
        joinRelay();
    }
#else
    viewer.options = LaunchOptions::from("", "", arguments);
#endif

    viewer.presenter     = viewer.options.contains("presenter");
    viewer.notes.visible = viewer.presenter;
#ifdef __EMSCRIPTEN__
    if (viewer.presenter) {
        holdScreenAwake();
    }
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        gr::log::error("SDL3 initialisation failed: {}", SDL_GetError());
        return 1;
    }
    // Audio is a separate subsystem and was never started, so a clip asking for sound opened a device that could
    // not exist and played silently with nothing said. It is not required to show a presentation -- a machine with
    // no sound card is a normal machine to present from -- so a failure here is reported and survived.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        gr::log::warning("no audio: clips that ask for sound will play silently: {}", SDL_GetError());
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3); // effect shaders are GLSL 3.30; 3.2 still shows the slides
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    viewer.window = SDL_CreateWindow("gr4-present", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (viewer.window == nullptr) {
        gr::log::error("SDL3 window creation failed: {}", SDL_GetError());
        return 1;
    }

    viewer.context = SDL_GL_CreateContext(viewer.window);
#ifndef __EMSCRIPTEN__
    if (viewer.context == nullptr || std::getenv("GR4_PRESENT_GL32") != nullptr) {
        // a driver without 3.3 core draws every slide, and every transition as a fade
        if (viewer.context != nullptr) {
            SDL_GL_DestroyContext(viewer.context);
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        viewer.context         = SDL_GL_CreateContext(viewer.window);
        viewer.effects.enabled = false;
        gr::log::warning("OpenGL 3.3 core is not available; effect shaders are switched off");
    }
#endif
    if (viewer.context == nullptr) {
        gr::log::error("OpenGL context creation failed: {}", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(viewer.window, viewer.context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(viewer.window, viewer.context);
    ImGui_ImplOpenGL3_Init(glslVersion);

    Fonts::instance().load();
    prepareLiveGraphs(); // while the atlas is still open: a live region's chart adds its own faces to it
    applyTheme(viewer, detectColourScheme());
    viewer.navigator             = placeholderNavigatorUntilPackagesLoad();
    viewer.loader.builtInEffects = EffectLibrary::bundledNames() | std::ranges::to<std::vector<std::string>>();
    viewer.loader.begin(viewer.options.presentationBase().value_or(defaultPresentationBase(argc > 0 ? argv[0] : "")));
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

    // `viewer` outlives main(); a live graph left to its destructor would be released after GR4's thread pools, whose
    // destructor waits for workers the graph's sources keep busy, and after the ImGui context its dashboard reads
    viewer.graphs.clear();
    viewer.logo.release();
    viewer.videos.release(); // its audio streams belong to SDL, which is shut down below, before `viewer` is destroyed
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(viewer.context);
    SDL_DestroyWindow(viewer.window);
    SDL_Quit();
#endif
    return 0;
}
