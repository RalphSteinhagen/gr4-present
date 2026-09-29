#ifndef GR4_PRESENT_VIEWER_HPP
#define GR4_PRESENT_VIEWER_HPP

#include "DiagnosticsPanel.hpp"
#include "DocumentView.hpp"
#include "EffectLibrary.hpp"
#include "FallbackScene.hpp"
#include "FigureCache.hpp"
#include "FormulaCache.hpp"
#include "Grid.hpp"
#include "LiveGraph.hpp"
#include "LoadingScreen.hpp"
#include "NotesOverlay.hpp"
#include "QrCode.hpp"
#include "RasterCamera.hpp"
#include "ShaderTransition.hpp"
#include "SideMenu.hpp"
#include "SvgImage.hpp"
#include "Texture.hpp"
#include "Theme.hpp"
#include "Transition.hpp"
#include "VideoCache.hpp"
#include "WindowModeControl.hpp"
#include "ZoomPan.hpp"

#include <gr4-present/Diagnostics.hpp>
#include <gr4-present/LaunchOptions.hpp>
#include <gr4-present/Navigation.hpp>
#include <gr4-present/PresentationLoader.hpp>
#include <gr4-present/RegionGeometry.hpp>

#ifdef GR4_PRESENT_HAS_EXPORT
#endif

#include <imgui.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * The viewer's state and the functions that act on it, one `Viewer*.cpp` file per concern; main.cpp owns the one
 * `Viewer` and the frame.
 */
namespace gr::present {

/// how one use of an effect runs: `scale` of its size, at most `fps` frames a second (0: every frame), carrying on when
/// its slide comes back (`lifecycle: persistent`)
struct EffectRun {
    std::optional<float> scale        = {}; // absent: 1, or 0.5 for a background or viewport on touch
    float                fps          = 0.0f;
    bool                 keepsRunning = false;
};

struct EffectInstance {
    std::string                     spec      = {};
    std::unique_ptr<EffectRenderer> renderer  = {};
    EffectInputs                    inputs    = {};
    double                          startedAt = 0.0;
    std::optional<int>              lastFrame = {}; // the frame it was last drawn in; a gap restarts it
    std::array<int, 2>              size{};
    std::array<float, 4>            mouse{}; // `iMouse`
    std::unique_ptr<ContentCapture> capture      = {};
    std::unique_ptr<RevealCapture>  reveal       = {};
    bool                            pressed      = false;
    float                           scale        = 1.0f; // halved while too slow
    int                             rendered     = 0;    // its `iFrame`
    std::optional<double>           renderedAt   = {};   // ImGui time, for an `fps` cap
    bool                            slowReported = false;
    int                             stillFrames  = 0; // export: frames rendered from cleared buffers to reach the still time (D25)
};

/// a `:::stage`: the two slides it moves between, captured each frame, and where its current step began
struct StageState {
    ContentCapture from;
    ContentCapture to;
    std::size_t    step      = static_cast<std::size_t>(-1);
    double         startedAt = 0.0;
};

/// a deck being walked into a PDF: where each page is, and what each one drew once it had settled
struct ExportWalk {
    std::vector<Cursor>        pages;
    std::vector<PageRecording> recorded;
    std::size_t                next                = 0UZ;
    int                        frames              = 0;
    double                     since               = 0.0;
    float                      recordedWidthPixels = 0.0f; // the frame the pages were recorded in
#ifdef GR4_PRESENT_HAS_EXPORT
#endif
    std::size_t captured = 0UZ;       // pages whose clips and charts have been read back
    std::string outcome;              // empty until the PDF is delivered or has failed
    std::string mode;                 // `slides` or `steps`
    bool        exitWhenDone = false; // a `--export` launch ends with its file
    Cursor      resumeAt     = {};
    int         windowWidth  = 0;
    int         windowHeight = 0;
};

/// a drag over drawn text, from the run it started on to the run it reached, in the runs the slide last drew
struct TextSelection {
    std::optional<std::size_t> anchor   = std::nullopt;
    std::optional<std::size_t> focus    = std::nullopt;
    bool                       dragging = false; // past the drag threshold
    Cursor                     on       = {};    // the slide it was made on; turning to another clears it
    bool                       inNotes  = false;
};

struct Viewer {
    SDL_Window*   window    = nullptr;
    SDL_GLContext context   = nullptr;
    bool          isRunning = true;

    // read once where the frame starts (main.cpp), so the code below it is deterministic: what it times is measured from these
    std::chrono::steady_clock::time_point frameStart     = {};
    std::time_t                           frameWallClock = 0; // the local clock, for the presenter's time of day and an effect's date

    Theme         theme;
    Texture       logo;
    LoadingScreen launch;
    bool          launchComplete = false;

    LaunchOptions      options;
    PresentationLoader loader;
    WindowModeControl  windowMode;
    SideMenu           menu;
    DocumentView       documentView;
    DiagnosticsPanel   diagnosticsPanel;
    NotesOverlay       notes;
    FallbackScene      fallback;
    Navigator          navigator;

    std::vector<Section>                                  sections;
    FigureCache                                           figures;
    FormulaCache                                          formulas;
    QrCache                                               qrCodes;
    VideoCache                                            videos;
    std::map<std::string, Layout, std::less<>>            layouts;      // key: package-relative reference
    std::map<std::string, std::vector<Area>, std::less<>> imageRegions; // key: raster reference
    RegionRegistry                                        regions;
    Diagnostics                                           diagnostics;
    std::vector<std::string>                              reportedClips;           // "view/box" of every clipping and "stalled/<region>" already reported, so each is reported once
    bool                                                  presenter = false;       // `?presenter`: notes and clock, no live graphs, follows the audience window
    std::optional<ExportWalk>                             exporting;               // `--export slides|steps`
    bool                                                  followingRemote = false; // applying a remote cursor, so it is not sent back
    Transition                                            transition;
    EffectLibrary                                         effects;
    ZoomPan                                               zoom;        // 1: the whole slide
    std::vector<ImDrawList*>                              zoomedLists; // this frame: the window's own list and each chart's

    struct {                                         // owned by ViewerSlides.cpp
        Layout                grid;                  // regenerated each frame
        float                 heldForSeconds = 0.0f; // seconds on the current view, for `advance:`
        std::optional<double> infoSince      = {};   // when the camera last came to rest
        std::optional<double> stepSince      = {};   // empty arrives at once
        std::optional<double> arrivedAt      = {};   // empty arrives at once
        double                viewSince      = 0.0;  // when the cursor last moved
        ShaderTransition      shaderTransition;
    } slides;

    struct {                                                                      // owned by ViewerEffects.cpp and ViewerStage.cpp
        std::map<std::string, EffectInstance, std::less<>> instances;             // key: "background:<spec>", "viewport:<id>", …
        std::map<std::string, StageState, std::less<>>     stages;                // key: "<view>/<stage id>"
        float                                              slowForSeconds = 0.0f; // seconds the frame rate has been too low while effects drew
    } effectRuns;

    struct { // owned by ViewerInput.cpp
        bool          pointing      = false;
        bool          onBreak       = false;
        double        breakEndsAt   = 0.0; // ImGui time
        bool          showPhoneLink = false;
        bool          pinching      = false;
        float         pinchDistance = 0.0f;
        ImVec2        pinchCentre{0.0f, 0.0f};
        TextSelection selection;
    } input;

    struct {                                                        // owned by ViewerPresenter.cpp
        std::optional<double>                 lastNotesPress  = {}; // ImGui time
        NotesOverlay::Action                  lastNotesAction = NotesOverlay::Action::none;
        bool                                  phoneCodeShown  = false; // for a test to read
        std::chrono::steady_clock::time_point openedAt        = std::chrono::steady_clock::now();
    } presenterTools;

    // built for the shown slide and its neighbours, released once the cursor settles; see `tendLiveGraphs`
    struct { // owned by ViewerLive.cpp
        std::map<std::string, std::unique_ptr<LiveGraph>, std::less<>>            graphs;
        std::map<std::string, std::chrono::steady_clock::time_point, std::less<>> retryAfter;
        std::string                                                               tendedView;   // the view the graphs were last tended for
        std::map<std::string, std::string, std::less<>>                           sources;      // key: region id; "workflow" or "standby"
        std::map<std::string, Rectangle, std::less<>>                             toolbarBoxes; // key: region id
    } live;
};

/// what a section needs from its layout: the master, the part of it the camera frames, and any transition override
struct SectionVisual {
    const Section* section = nullptr;
    const Layout*  layout  = nullptr;
    std::string    source;        // package-relative reference of the master, empty when none
    Rectangle      frame;         // the anchor's box; zero-width means the whole document
    std::string    transition;    // `transition:` of the layout directive, empty when unset
    std::string    background;    // `background:` of the layout directive, else the deck's; `none` draws none
    std::string    overlay;       // `overlay:` of the layout directive
    std::string    missingAnchor; // an anchor the author named that the master lacks

    std::string               grid;                       // `grid:` of the layout directive, empty when unset
    float                     duration = -1.0f;           // `duration:` in seconds; negative takes the deck's pace
    std::vector<AreaDocument> slots;                      // the prose of each `:::<name>` block, and what its fence asked for
    std::vector<Area>         declaredGrid;               // boxes a `:::grid` block placed itself, as fractions
    bool                      contradicts = false;        // both a grid and a drawn master were named
    std::string               image;                      // a raster the camera moves over, from `source:` when not an SVG
    Rectangle                 region;                     // fractions of the image this section frames; empty is all of it
    Rectangle                 via;                        // an intermediate frame, so a move can pull back before it travels
    CameraScope               scope = CameraScope::image; // whether the title and footer travel with the picture
    Document                  info;                       // what the current stop says about its region
    Document                  infoAside;                  // its `:::aside`, in a second box on the other side
    std::string               infoSide;                   // `box=` of that stop
    float                     rotation = 0.0f;            // degrees: `rotate:` of the layout, or `rotate=` of the stop reached
    std::string               font;                       // `font:` of the layout: a role or a face the deck names
    bool                      outline = false;            // `outline: on`
    std::optional<TypeSize>   size;                       // `size:` of the layout: the slide's body size

    std::vector<std::pair<std::string, std::string>> areaContent; // area id -> resource, from the unreserved keys

    /// the box the camera rests on: the anchor's box over a drawing, the framed fractions of a raster
    [[nodiscard]] Rectangle cameraFrame() const noexcept { return image.empty() ? frame : region; }
};

inline constexpr float  kSplashLogoWidthFraction = 0.20f;  // of the viewport width
inline constexpr float  kDragThresholdSquared    = 25.0f;  // px^2: a press that moved less than this is a click, not a drag
inline constexpr float  kCompactPresenterSide    = 600.0f; // CSS px: a screen whose shorter side is below this is a phone
inline constexpr double kInfoFadeSeconds         = 0.3;

#ifdef __EMSCRIPTEN__
// the page's JavaScript: defined with EM_JS beside its first user, called from the others
extern "C" {
void holdScreenAwake();
void joinDeckChannel();
void joinRelay();
void followAddressBar();
void passBrowserKeys();
void publishExport(int pages, const char* outcome);
void openPresenterWindow();
}
#endif

#ifdef __EMSCRIPTEN__
[[nodiscard]] std::string emscriptenLocation(std::string_view component);
#endif
[[nodiscard]] std::string viewNamed(const Viewer& viewer, std::string_view said);
[[nodiscard]] std::size_t slideNumberOf(const Viewer& viewer, std::string_view viewId);
void                      publishCursor(const Viewer& viewer);
void                      followRemoteCursor(Viewer& viewer);
void                      publishRegionBounds(const Viewer& viewer);
[[nodiscard]] std::string phoneLink();
/// an Escape the page passed to the browser, once; false natively
[[nodiscard]] bool escapeReachedThePage();

void applyNavigationKeys(Viewer& viewer);
void applyZoomInput(Viewer& viewer);
void applyVideoButtons(Viewer& viewer);
void applyLinkClicks(Viewer& viewer);
void applyTextSelection(Viewer& viewer);
void applyZoomToDrawData(Viewer& viewer);
#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
void applyTouchNavigation(Viewer& viewer);
#endif

[[nodiscard]] Rectangle     stopRegionOf(const Viewer& viewer, std::string_view source, std::string_view text);
[[nodiscard]] SectionVisual visualFor(Viewer& viewer, const Cursor& cursor);
/// `aspect: 4:3` or `aspect: 1.333` as width over height; zero when it says neither
[[nodiscard]] float aspectOf(std::string_view text);
void                drawSection(Viewer& viewer, const SectionVisual& visual, std::size_t step, float opacity, const Rectangle& frame, bool live = true, const DocumentView::CameraMove& move = {}, float infoOpacity = 0.0f, float rotation = 0.0f);
/// the background effect `spec` over the whole view, behind the words and over the master
void drawBackgroundEffect(Viewer& viewer, std::string_view spec, ImDrawList& list);
/// a `:::shader` viewport drawn into `box`; the presenter's window and an export read it like a chart
[[nodiscard]] bool drawShaderViewport(Viewer& viewer, std::string_view viewId, const Block& block, const Rectangle& box);
/// `beginOverlay` captures the commands that follow, `endOverlay` draws the effect from them; `progress` is `iProgress`
void beginOverlay(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, float progress);
void endOverlay(Viewer& viewer, std::string_view key, ImDrawList& list);
/// the instance `key` drawn into screen pixels `min`..`max`, inputs changeable until the frame renders; null if not
EffectInstance* drawEffectInto(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, ImVec2 min, ImVec2 max, const EffectRun& run);
/// the pixels of the effect a recorded picture names (`effect:<instance>`); nothing when the picture is not an effect's
[[nodiscard]] std::optional<RecordedPixels> recordedEffectPixels(Viewer& viewer, std::string_view source);
/// an exported page shows a stage at rest: its `from` slide, or its `to` slide once a step has played
void recordStagePicture(Viewer& viewer, std::string_view stageKey, bool arrived, const ImDrawList& list, const Rectangle& box);
/// call before a slide with a `:::stage` is drawn: renders the stage's two slides into textures
void captureStageSlides(Viewer& viewer, const SectionVisual& visual);
/// step 0 shows the `from` slide, step k loops the k-th transition between the two slides
[[nodiscard]] bool drawStage(Viewer& viewer, std::string_view viewId, const Block& block, const Rectangle& box, std::size_t step);
/// once a frame: halves the scale of effects, down to a quarter, while the frame rate stays under the display's
void tendEffects(Viewer& viewer);
/// the pointer effect over the slide, under the menus; nothing when off, in the presenter's window or in an export
void drawPointer(Viewer& viewer);
void drawBreakScreen(Viewer& viewer);
/// redraws `list` from `firstCommand` on through the effect `spec`, `progress` of the way; false when undrawable
[[nodiscard]] bool drawThroughEffect(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, int firstCommand, float progress, ImVec2 low, ImVec2 high);

[[nodiscard]] EffectInputs effectInputsFor(const Viewer& viewer);
/// the default duration of a reveal `kind`: an effect's `@duration`, else the CPU reveals' pace
[[nodiscard]] float revealSecondsOf(Viewer& viewer, std::string_view kind);
/// everything a cursor change entails, from any source: the move, the address bar, and clips rewound on a view change
void settleCursor(Viewer& viewer, const Cursor& before);
void advanceOnItsOwn(Viewer& viewer, float seconds);
void advanceTransition(Viewer& viewer, float seconds);
void drawCurrentSection(Viewer& viewer);

[[nodiscard]] bool drawLiveRegion(Viewer& viewer, std::string_view id, const DocumentView::RegionBoxes& boxes);
void               tendLiveGraphs(Viewer& viewer);

void advanceLoading(Viewer& viewer);
void applyTheme(Viewer& viewer, ColourScheme scheme);

void                      drawPhoneLink(Viewer& viewer);
void                      drawNotes(Viewer& viewer, float interfacePixelsPerPoint);
[[nodiscard]] std::string titleOf(const Viewer& viewer, std::string_view viewId);
void                      buildSideMenu(Viewer& viewer);

void               startExport(Viewer& viewer, std::string_view mode, bool exitWhenDone);
void               beginExport(Viewer& viewer);
[[nodiscard]] bool prepareExportPage(Viewer& viewer);
void               finishExportPage(Viewer& viewer);
void               captureExportPixels(Viewer& viewer);

} // namespace gr::present

#endif // GR4_PRESENT_VIEWER_HPP
