#include "Viewer.hpp"

#include "Animation.hpp"
#include "BuiltinFace.hpp"
#include "EmbeddedLogos.hpp"
#include "Fonts.hpp"
#include "MathRender.hpp"
#include "ScopedWindow.hpp"
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
#include <ctime>
#include <filesystem>
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
    // beside the executable: the viewer may be launched from anywhere
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
    viewer.frameStart     = std::chrono::steady_clock::now();
    viewer.frameWallClock = std::time(nullptr);
    // the presenter's controls are sized like the device's own interface: 1 pt = 4/3 CSS px in a browser, the display's
    // scale setting natively; SDL reports 0 on failure, read as no scaling
    const float displayScale            = SDL_GetWindowDisplayScale(viewer.window);
    const float pixelDensity            = SDL_GetWindowPixelDensity(viewer.window);
    const float interfacePixelsPerPoint = Fonts::kPixelsPerPoint * (displayScale > 0.0f && pixelDensity > 0.0f ? displayScale / pixelDensity : 1.0f);
    SDL_Event   event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            viewer.isRunning = false;
        }
#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
        DigitizerUi::TouchHandler<>::processSDLEvent(event);
#endif
        // a browser grants a touch its activation as the finger lifts, a click and a key as they go down; a click on the
        // side menu is the menu's: going full screen would use its activation up, and Load's picker needs it
        const Rectangle& menu       = viewer.menu.drawnPanel;
        const bool       pressed    = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const bool       acrossMenu = event.button.x >= menu.x && event.button.x < menu.x + menu.width;
        const bool       alongMenu  = event.button.y >= menu.y && event.button.y < menu.y + menu.height;
        const bool       onMenu     = pressed && acrossMenu && alongMenu;
        const bool       gesture    = (pressed && !onMenu) || event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_FINGER_UP;
        if (gesture) {
            viewer.windowMode.onUserGesture();
        }
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr && !viewer.presenter && !viewer.exporting) {
            loadDroppedPath(viewer, event.drop.data);
        }
    }

    applyTheme(viewer, viewer.exporting ? ColourScheme::light : detectColourScheme()); // a PDF is for paper

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
    // pinch and rotation go unused here, but this also recovers a finger whose lift event never arrived
    DigitizerUi::TouchHandler<>::updateGestures();
#endif
    tendLiveGraphs(viewer);

    static_cast<void>(Fonts::instance().applyStagedDeck()); // between frames: the atlas must not change under one
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    if (viewer.exporting) {
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX); // nothing hovered, so no tooltip on a page
    }
    ImGui::NewFrame();

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
    // the flick is read first: endFrame() clears the per-frame finger flags, and a flick read twice advances twice
    if (!viewer.sections.empty()) {
        applyZoomInput(viewer);
        applyTouchNavigation(viewer);
        if (DigitizerUi::TouchHandler<>::nFingers == 0UZ) {
            viewer.input.pinching = false;
        }
    }
    DigitizerUi::TouchHandler<>::endFrame();
#else
    if (!viewer.sections.empty()) {
        applyZoomInput(viewer);
    }
#endif

    tendDeckFiles(viewer);
    // over everything drawn, the side menu included, so a pixel the old and the new frame share stays as it is
    const auto drawFrameBefore = [&viewer](float opacity) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2         corner{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y};
        const ImU32          seenThrough = dimmed(IM_COL32_WHITE, opacity);
        ImGui::GetForegroundDrawList()->AddImage(viewer.frameBefore.id, viewport->Pos, corner, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, seenThrough);
    };
    const ImVec2 splashLogo = viewer.logo.scaledToWidth(ImGui::GetMainViewport()->Size.x * kSplashLogoWidthFraction);
    if (!viewer.launchComplete) {
        advanceLoading(viewer);
        viewer.launchComplete = viewer.launch.finished();
        if (viewer.launchComplete) {
            viewer.launchFinishedAt = viewer.frameStart;
        }
        if (viewer.frameBefore.id != 0) {
            drawFrameBefore(1.0f);
        } else {
            viewer.launch.draw(viewer.logo.id, splashLogo, viewer.theme, 1.0f);
        }
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
        if (!viewer.exporting) {
            // a deck blends in from the one it replaced, or at launch and after F5 fades in from the splash
            const float sinceLoaded = std::chrono::duration<float>(viewer.frameStart - viewer.launchFinishedAt).count();
            const float before      = LoadingScreen::fadeOpacity(sinceLoaded);
            if (viewer.frameBefore.id != 0 && before == 0.0f) {
                viewer.frameBefore.release();
            } else if (viewer.frameBefore.id != 0) {
                drawFrameBefore(before);
            } else if (before > 0.0f) {
                viewer.launch.draw(viewer.logo.id, splashLogo, viewer.theme, before);
            }
            viewer.menu.draw(viewer.theme, interfacePixelsPerPoint);
            viewer.diagnosticsPanel.draw(viewer.diagnostics, viewer.theme, interfacePixelsPerPoint);
            drawNotes(viewer, interfacePixelsPerPoint);
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
    if (viewer.deckFiles.replacing.has_value()) {
        const std::size_t rgbaBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ;
        const auto        wide      = static_cast<std::uint32_t>(width);
        const auto        tall      = static_cast<std::uint32_t>(height);
        RecordedPixels    shown{.width = wide, .height = tall, .rgba = std::vector<std::uint8_t>(rgbaBytes)};
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, shown.rgba.data());
        uprightOpaqueFromReadBack(shown);
        replaceShownDeck(viewer, Texture::loadRgba(shown.rgba, width, height));
    }
    SDL_GL_SwapWindow(viewer.window);
}

Viewer viewer; // the main loop is a callback under Emscripten, so the state outlives main()

#ifdef __EMSCRIPTEN__
// SDL sets the window size once and falls back to 800x600 after the first key press, so follow the browser's canvas
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
    // audio is a separate subsystem and not required to present: a failure is reported and survived
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
    viewer.frameStart            = std::chrono::steady_clock::now();
    const std::string builtIn    = defaultPresentationBase(argc > 0 ? argv[0] : "");
#ifdef __EMSCRIPTEN__
    const std::string address = std::string{viewer.options.presentationBase().value_or(builtIn)};
    // `local:<name>` or a path: a deck from this computer, reopened through the file handle the browser kept (Chromium)
    if (address.starts_with("local:") || localPathOf(address).has_value()) {
        reopenLocalDeck(viewer, address, builtIn);
    } else {
        loadDeck(viewer, address, DeckOpening::launch);
    }
#else
    const std::string address = std::string{viewer.options.presentationBase().value_or(builtIn)};
    loadDeck(viewer, localPathOf(address).value_or(address), DeckOpening::launch); // `file:` or not, the path from here on
#endif
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

    // teardown, in this order: live graphs, then textures and video audio, then ImGui, the GL context and SDL;
    // `viewer` outlives main(): a graph left to its destructor would be released after GR4's thread pools and ImGui
    viewer.live.graphs.clear();
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
