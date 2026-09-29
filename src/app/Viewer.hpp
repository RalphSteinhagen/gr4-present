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
#include <gnuradio-4.0/algorithm/fileio/FileIo.hpp>
#endif

#include <imgui.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * The viewer application's state and the functions that act on it, one file per concern: loading (ViewerLoading),
 * input (ViewerInput), drawing slides and their transitions (ViewerSlides), live regions (ViewerLive), the presenter's
 * notes and menu (ViewerPresenter), the PDF export (ViewerExport) and the browser page around the canvas
 * (ViewerBrowser). main.cpp owns the one `Viewer` and the frame.
 */
namespace gr::present {

/// how one use of an effect runs: `scale` of its size, at most `fps` frames a second (0: every frame), and whether it
/// carries on where it was when its slide comes back (`lifecycle: persistent`) rather than starting over
struct EffectRun {
    std::optional<float> scale        = {}; // absent: 1, or 0.5 for a background or viewport on a touch device
    float                fps          = 0.0f;
    bool                 keepsRunning = false;
};

struct EffectInstance {
    std::string                     spec      = {}; // the name and settings as the deck wrote them
    std::unique_ptr<EffectRenderer> renderer  = {};
    EffectInputs                    inputs    = {};
    double                          startedAt = 0.0;
    int                             lastFrame = -2; // the frame it was last drawn in; a gap restarts it
    std::array<int, 2>              size{};
    std::array<float, 4>            mouse{};              // `iMouse` as it stands between frames
    std::unique_ptr<ContentCapture> capture      = {};    // what an overlay is drawn from
    std::unique_ptr<RevealCapture>  reveal       = {};    // what a reveal effect mixes: the box before and after it is drawn
    bool                            pressed      = false; // a press that began inside it is still held
    float                           scale        = 1.0f;  // what it renders at now, halved while it is too slow
    int                             rendered     = 0;     // frames rendered since it started: its `iFrame`
    double                          renderedAt   = -1.0;  // ImGui time of the last render, for an `fps` cap
    bool                            slowReported = false;
    int                             stillFrames  = 0; // an export's page: frames rendered from cleared buffers to reach the still time (D25)
};

/// a deck being walked into a PDF: where each page is, and what each one drew once it had settled
/// a `:::stage`: the two slides it moves between, captured each frame, and where its current step began
struct StageState {
    ContentCapture from;
    ContentCapture to;
    std::size_t    step      = static_cast<std::size_t>(-1);
    double         startedAt = 0.0;
};

struct ExportWalk {
    std::vector<Cursor>        pages;
    std::vector<PageRecording> recorded;
    std::size_t                next           = 0UZ;  // the page being drawn
    int                        frames         = 0;    // frames drawn on it so far
    double                     since          = 0.0;  // when it was first drawn
    float                      recordedWidth  = 0.0f; // the frame the pages were recorded in
    float                      recordedHeight = 0.0f;
#ifdef GR4_PRESENT_HAS_EXPORT
    std::optional<gr::algorithm::fileio::Writer> delivery; // the browser's download, which finishes after the frame
#endif
    std::size_t captured = 0UZ;       // pages whose clips and charts have been read back from the frame
    std::string outcome;              // empty until the PDF is delivered or has failed, then what happened
    std::string mode;                 // `slides` or `steps`
    bool        exitWhenDone = false; // a `--export` launch ends with its file; one asked for while presenting goes back
    Cursor      resumeAt     = {};
    int         windowWidth  = 0;
    int         windowHeight = 0;
};

/// a drag over drawn text, from the run it started on to the run it reached, in the runs the slide last drew
struct TextSelection {
    std::optional<std::size_t> anchor   = std::nullopt;
    std::optional<std::size_t> focus    = std::nullopt;
    bool                       dragging = false; // past the drag threshold: a press on text that did not move is a click
    Cursor                     on       = {};    // the slide it was made on; turning to another clears it
    bool                       inNotes  = false; // in the speaker notes' lines rather than the slide's words
};

struct Viewer {
    SDL_Window*   window    = nullptr;
    SDL_GLContext context   = nullptr;
    bool          isRunning = true;

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
    std::map<std::string, Layout, std::less<>>            layouts;                 // package-relative reference -> its named areas
    std::map<std::string, std::vector<Area>, std::less<>> imageRegions;            // raster reference -> the regions declared for it
    Layout                                                grid;                    // regenerated each frame for the viewport it has to fit
    float                                                 heldFor   = 0.0f;        // seconds on the current view, for `advance:`
    double                                                infoSince = -1.0;        // when the camera last came to rest, for a stop's words to fade in
    double                                                stepSince = -1.0;        // when a forward step was reached in this view; negative arrives at once
    double                                                arrivedAt = -1.0;        // when this view was entered going forward; negative arrives at once
    double                                                viewSince = 0.0;         // when the cursor last moved, for a region that never got its graph
    bool                                                  presenter = false;       // `?presenter`: notes and clock up, no live graphs, follows the audience window
    std::optional<ExportWalk>                             exporting;               // `--export slides|steps`: the deck being walked into a PDF
    bool                                                  followingRemote = false; // applying a cursor another window sent, so it is not sent back
    bool                                                  showPhoneLink   = false; // Q: a QR code of the phone presenter view
    bool                                                  pointing        = false; // L: the pointer draws with the pointer effect
    bool                                                  onBreak         = false; // B: the break screen covers the slide
    double                                                breakEndsAt     = 0.0;   // ImGui time the break's countdown reaches zero
    float                                                 effectsSlowFor  = 0.0f;  // seconds the frame rate has been too low while effects drew
    bool                                                  phoneCodeShown  = false; // drawn on the first slide's notes this frame, for a test to read
    std::chrono::steady_clock::time_point                 openedAt        = std::chrono::steady_clock::now();
    RegionRegistry                                        regions;                                      // what the document declares, and where the layout most recently put it
    Diagnostics                                           diagnostics;                                  // the loader's, plus anything the viewer itself finds
    std::vector<std::string>                              reportedClips;                                // "view/box" of every clipping already in that list
    std::map<std::string, std::string, std::less<>>       liveSources;                                  // region id -> "workflow" or "standby", whichever it draws
    std::map<std::string, Rectangle, std::less<>>         toolbarBoxes;                                 // region id -> where its toolbar was last drawn
    double                                                lastNotesPress  = -1.0;                       // ImGui time of the last presenter button press acted on
    NotesOverlay::Action                                  lastNotesAction = NotesOverlay::Action::none; // and which button it was
    Transition                                            transition;
    EffectLibrary                                         effects;          // the deck's effect shaders, then the viewer's own
    ShaderTransition                                      shaderTransition; // the pictures of both slides a shader transition mixes
    std::map<std::string, StageState, std::less<>>        stages;           // "<view>/<stage id>" -> its two slides
    std::map<std::string, EffectInstance, std::less<>>    effectInstances;  // "background:<spec>", "viewport:<id>", … -> its state

    TextSelection selection; // the words a reader has dragged over, which Ctrl+C copies

    ZoomPan                  zoom;        // how far into the slide the audience is looking; one, the whole slide, until asked
    std::vector<ImDrawList*> zoomedLists; // what the slide drew into this frame: the window's own list and each chart's
    bool                     pinching      = false;
    float                    pinchDistance = 0.0f;
    ImVec2                   pinchCentre{0.0f, 0.0f};

    // A graph is built when its slide, or the slide either side of it, is shown, and released one at a time once the
    // cursor has settled elsewhere; see `tendLiveGraphs`.
    std::map<std::string, std::unique_ptr<LiveGraph>, std::less<>>            graphs;
    std::map<std::string, std::chrono::steady_clock::time_point, std::less<>> liveGraphRetryAfter;
    std::string                                                               tendedView; // the view the graphs were last tended for, so a moving cursor builds nothing
};

/// one effect shader drawn into a rectangle of a slide: a callback renders it into its own texture, which the slide
/// then shows as a picture, so the slide's zoom and the export treat it like any image
/// what a section needs from its layout: the master, the part of it the camera frames, and any transition override
struct SectionVisual {
    const Section* section = nullptr;
    const Layout*  layout  = nullptr;
    std::string    source;        // package-relative reference of the master, empty when the section has none
    Rectangle      frame;         // the anchor's box; zero-width means the whole document
    std::string    transition;    // `transition:` from the layout directive, empty when the author did not say
    std::string    background;    // `background:` from the layout directive, else the deck's; `none` draws none
    std::string    overlay;       // `overlay:` from the layout directive: an effect drawn over the whole slide
    std::string    missingAnchor; // an anchor the author named that the master does not contain

    std::string               grid;                       // `grid:` from the layout directive, empty when the author did not say
    float                     duration = -1.0f;           // `duration:` in seconds; negative takes the deck's pace
    float                     advance  = -1.0f;           // `advance:` in seconds; negative takes the deck's, zero waits for a key
    std::vector<AreaDocument> slots;                      // the prose a `:::<name>` block put in each box, and what its fence asked for
    std::vector<Area>         declaredGrid;               // boxes a `:::grid` block placed itself, as fractions
    bool                      contradicts = false;        // both a grid and a drawn master were named
    std::string               image;                      // a raster the camera moves over, from `source:` when it is not an SVG
    Rectangle                 region;                     // the part of it this section frames, in fractions; empty is all of it
    Rectangle                 via;                        // an intermediate frame, so a move can pull back before it travels
    CameraScope               scope = CameraScope::image; // whether the title and footer travel with the picture
    Document                  info;                       // what the current stop says about the region it frames
    Document                  infoAside;                  // its `:::aside`: a second box, on the other side
    std::string               infoSide;                   // `box=` of that stop
    float                     rotation = 0.0f;            // `rotate:` of the layout, or `rotate=` of the stop reached, in degrees
    std::string               font;                       // `font:` of the layout: the slide's face, a role or one the deck names
    bool                      outline = false;            // `outline: on`: the layout's areas drawn and named, for explaining or debugging one
    std::optional<TypeSize>   size;                       // `size:` of the layout: the slide's body size

    std::vector<std::pair<std::string, std::string>> areaContent; // area id -> resource, from the unreserved keys
};

inline constexpr float  kSplashLogoWidthFraction = 0.20f;  // of the viewport width
inline constexpr float  kDragThresholdSquared    = 25.0f;  // px^2: a press that moved less than this is a click, not a drag
inline constexpr float  kCompactPresenterSide    = 600.0f; // CSS px: a screen whose shorter side is below this is a phone
inline constexpr double kInfoFadeSeconds         = 0.3;    // how long a stop's words take to appear once the camera is still

#ifdef __EMSCRIPTEN__
// the page's JavaScript, defined with EM_JS beside its first user and called from the others
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

// in ViewerBrowser.cpp
#ifdef __EMSCRIPTEN__
[[nodiscard]] std::string emscriptenLocation(std::string_view component);
#endif
[[nodiscard]] std::string viewNamed(const Viewer& viewer, std::string_view said);
[[nodiscard]] std::size_t slideNumberOf(const Viewer& viewer, std::string_view viewId);
void                      publishCursor(const Viewer& viewer);
void                      followRemoteCursor(Viewer& viewer);
void                      publishRegionBounds(const Viewer& viewer);
[[nodiscard]] std::string phoneLink();
/// an Escape the page passed to the browser, once; always false natively, where ImGui sees the key
[[nodiscard]] bool escapeReachedThePage();

// in ViewerInput.cpp
void applyNavigationKeys(Viewer& viewer);
void applyZoomInput(Viewer& viewer);
void applyVideoButtons(Viewer& viewer);
void applyLinkClicks(Viewer& viewer);
void applyTextSelection(Viewer& viewer);
void applyZoomToDrawData(Viewer& viewer);
#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
void applyTouchNavigation(Viewer& viewer);
#endif

// in ViewerSlides.cpp
[[nodiscard]] Rectangle     stopRegionOf(const Viewer& viewer, std::string_view source, std::string_view text);
void                        reportFontChoices(Viewer& viewer, std::string_view viewId, const Document& document);
[[nodiscard]] SectionVisual visualFor(Viewer& viewer, const Cursor& cursor);
[[nodiscard]] float         statedAspect(const Viewer& viewer, std::string_view region);
/// `aspect: 4:3` or `aspect: 1.333` as width over height; zero when it says neither
[[nodiscard]] float aspectOf(std::string_view text);
void                drawSection(Viewer& viewer, const SectionVisual& visual, std::size_t step, float opacity, const Rectangle& frame, bool live = true, const DocumentView::CameraMove& move = {}, float infoOpacity = 0.0f, float rotation = 0.0f);
/// draws the background effect `spec` over the whole view, behind the words and over the master
void drawBackgroundEffect(Viewer& viewer, std::string_view spec, ImDrawList& list);
/// a `:::shader` viewport: its effect drawn into `box`; the presenter's window and an export read it like a chart
[[nodiscard]] bool drawShaderViewport(Viewer& viewer, std::string_view viewId, const Block& block, const Rectangle& box);
/// an `overlay:` effect, or the one a camera or zoom move is seen through: `beginOverlay` before the commands captures
/// them, `endOverlay` after them draws the effect from that picture where they were going; `progress` is its `iProgress`
void beginOverlay(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, float progress);
void endOverlay(Viewer& viewer, std::string_view key, ImDrawList& list);
/// draws the effect `spec` into `box` (screen pixels) as instance `key`, run as `run` says; the instance,
/// whose inputs may still be changed until the frame is rendered, or nothing when it cannot be drawn
EffectInstance* drawEffectInto(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, ImVec2 min, ImVec2 max, const EffectRun& run);
/// what an exported page shows of the effect a recorded picture names (`effect:<instance>`), read back from the texture
/// it rendered into; nothing when the picture is not an effect's
[[nodiscard]] std::optional<RecordedPixels> recordedEffectPixels(Viewer& viewer, std::string_view source);
/// an exported page shows a stage at rest: its `from` slide, or once a step has played, its `to` slide
void recordStagePicture(Viewer& viewer, std::string_view stageKey, bool arrived, const ImDrawList& list, const Rectangle& box);
/// the `transitions:` of a `:::stage`, in order: step k plays the k-th
[[nodiscard]] std::vector<std::string> stageTransitionsOf(const Block& block);
/// before a slide with a `:::stage` is drawn: the two slides it shows, drawn into textures
void captureStageSlides(Viewer& viewer, const SectionVisual& visual);
/// a `:::stage` in `box`: at step 0 its `from` slide, at step k the k-th transition between its two slides, over and over
[[nodiscard]] bool drawStage(Viewer& viewer, std::string_view viewId, const Block& block, const Rectangle& box, std::size_t step);
/// once a frame: an effect drawn while the frame rate stays under the display's is halved in scale, down to a quarter
void tendEffects(Viewer& viewer);
/// L: the pointer effect over the slide, under the menus; nothing while pointer mode is off, in the presenter's window
/// or in an export
void drawPointer(Viewer& viewer);
/// B: the break screen, an effect over the whole window with a countdown to the end of the break
void drawBreakScreen(Viewer& viewer);
/// what `list` holds from `firstCommand` on, drawn through the effect `spec` over what was under it: a box or step
/// arriving `progress` of the way, or a box's overlay; false when the effect cannot be drawn
[[nodiscard]] bool drawThroughEffect(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, int firstCommand, float progress, ImVec2 low, ImVec2 high);

/// what every effect shader reads this frame: the clock, the date, the frame rate and the theme
[[nodiscard]] EffectInputs effectInputsFor(const Viewer& viewer);
/// how long a reveal `kind` takes when the deck does not say: an effect's `@duration`, else the CPU reveals' pace
[[nodiscard]] float revealSecondsOf(Viewer& viewer, std::string_view kind);
/// everything a cursor change entails, wherever it came from -- key, flick, menu or another window: the move, the
/// address bar that follows it, and clips rewound when the view itself changed rather than its reveal step
void settleCursor(Viewer& viewer, const Cursor& before);
void advanceOnItsOwn(Viewer& viewer, float seconds);
void advanceTransition(Viewer& viewer, float seconds);
void drawCurrentSection(Viewer& viewer);

// in ViewerLive.cpp
[[nodiscard]] bool drawLiveRegion(Viewer& viewer, std::string_view id, const DocumentView::RegionBoxes& boxes);
void               tendLiveGraphs(Viewer& viewer);

// in ViewerLoading.cpp
void advanceLoading(Viewer& viewer);
void applyTheme(Viewer& viewer, ColourScheme scheme);

// in ViewerPresenter.cpp
void                      drawPhoneLink(Viewer& viewer);
void                      drawNotes(Viewer& viewer);
[[nodiscard]] std::string titleOf(const Viewer& viewer, std::string_view viewId);
void                      buildSideMenu(Viewer& viewer);

// in ViewerExport.cpp
void               startExport(Viewer& viewer, std::string_view mode, bool exitWhenDone);
void               beginExport(Viewer& viewer);
[[nodiscard]] bool prepareExportPage(Viewer& viewer);
void               finishExportPage(Viewer& viewer);
void               captureExportPixels(Viewer& viewer);

} // namespace gr::present

#endif // GR4_PRESENT_VIEWER_HPP
