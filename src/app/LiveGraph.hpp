#ifndef GR4_PRESENT_LIVE_GRAPH_HPP
#define GR4_PRESENT_LIVE_GRAPH_HPP

#include <gr4-present/ChartLegend.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * An OpenDigitizer dashboard running beside the slide it is drawn into.
 *
 * A workflow file carries both halves: a GNU Radio flowgraph, and a `dashboard:` section saying which of its
 * sinks are plotted together, on what axes, at what scale, and with which chart. This owns the dashboard that
 * reads both, runs the graph on a thread of its own, and draws its own charts and legend into a region of a
 * slide. The viewer contributes a lifetime and a rectangle; the picture is OpenDigitizer's, out of their file.
 *
 * No GNU Radio or OpenDigitizer type appears in this header, on purpose. The screenshot tests link an
 * instrumented ImGui whose ABI differs from the application's, and these headers reach ImGui through ImPlot, so
 * a second one would arrive in any test that included this. The translation unit behind it is also among the
 * most expensive in the project to compile.
 *
 * One thing a caller must know: a dashboard reads the ImGui and ImPlot styles as it is built, and its charts
 * take colours from a process-wide palette. So one may only be constructed while those exist -- which is to say
 * inside `main`, and never from a static destructor, where the objects behind them have already been torn down.
 */
class LiveGraph {
public:
    /// reads a workflow -- GNU Radio's YAML, with OpenDigitizer's dashboard section -- and starts it running
    explicit LiveGraph(std::string_view workflow);
    ~LiveGraph();
    LiveGraph(const LiveGraph&)            = delete;
    LiveGraph& operator=(const LiveGraph&) = delete;

    /// why the workflow is not running; empty while it is
    [[nodiscard]] std::string_view problem() const noexcept { return _problem; }

    /// whether the graph has reached the state in which its blocks do work; a loaded graph gets there on its own
    [[nodiscard]] bool running() const noexcept;

    /// the scheduler's lifecycle state by name, e.g. "PAUSED", for a test to read; empty without a graph
    [[nodiscard]] std::string_view stateName() const noexcept;

    /// running, but its graph has made no progress for a few seconds: what a slide should not keep showing as live
    [[nodiscard]] bool stalled() const noexcept;

    /// carries what the scheduler has to say to the charts and drives `setRunning` towards the state it asked
    /// for; call once a frame, or their data never arrives and a graph left mid-transition stays there
    void pump();

    /**
     * Starts or stops the graph, leaving it built either way.
     *
     * A deck keeps the slide it is showing running and the one it will advance to merely built, so the charts of
     * the next slide are ready without its sources holding a sound card or a radio nobody is listening to.
     */
    void setRunning(bool wanted);

    /// the charts this dashboard declares, in the order its file names them
    [[nodiscard]] std::vector<std::string> charts() const;

    /**
     * Draws the dashboard where ImGui is currently drawing, its charts docked as the workflow lays them out and
     * their shared legend at `legend`, and says whether it drew. One region at a time: the charts are docked in one
     * place, which follows the region drawing them.
     *
     * The caller decides the size by opening a window or a child of the size it wants: the view asks ImGui how
     * much room there is rather than being told, which is the contract OpenDigitizer's own application draws
     * under.
     */
    [[nodiscard]] bool draw(ChartLegend legend);

    /// draws OpenDigitizer's toolbar for this workflow where ImGui is drawing: its toolbar blocks, such as the
    /// controls that set a source's frequency, without play, pause and stop -- the deck runs the graph itself
    void drawToolbar();

    /// draws OpenDigitizer's status bar for this workflow where ImGui is drawing: the latest warning or error the
    /// process logged with the counts per level, a log popup on click, and the workflow's own status-bar blocks
    void drawStatusBar();

    /// draws the same workflow as a flow graph -- its blocks and their connections -- rather than its charts
    [[nodiscard]] bool drawFlowgraph(std::string_view pane);

    /// takes up a changed colour scheme in the flow graph, whose editor keeps its colours until told
    void restyle();

private:
    struct Running; // the dashboard and its page; incomplete here so that neither leaks out

    /// takes the graph one step towards `setRunning`'s wanted state, which neither transition reaches at once
    void applyWantedState();

    /// builds the dashboard and starts its graph
    void build(std::string_view workflow);
    void releaseDashboard();

    bool                                  _wanted = true; // a graph runs from the moment it is built
    std::unique_ptr<Running>              _running;
    std::string                           _problem;
    std::size_t                           _progressSeen = 0UZ;
    std::chrono::steady_clock::time_point _progressSince{}; // when the graph's progress last moved while running
};

/**
 * Whether the browser can give one more graph its threads. Every live graph holds five for as long as it lives --
 * measured as the growth of the running workers when a slide's graph was built -- and the pool is fixed at
 * `GR_MAX_WASM_THREAD_COUNT`. A pool that runs out is not an error the page survives: the next thread has to be
 * created by the event loop, and the frame waiting for it is the frame that loop belongs to. Three graphs held
 * beside the viewer's own threads did exactly that, and the page froze.
 */
[[nodiscard]] bool liveGraphThreadsAvailable();

/// the size the last chart drawn set its own text in, one level in from the slide body; OpenDigitizer scales its
/// small and tiny faces from it
[[nodiscard]] float chartBodyPixels();

/// installs OpenDigitizer's log history as GR4's log backend, so every message of the session is kept from the first
/// one on, for a status bar to show; call first thing in `main`
void captureLogFromStart();

/// OpenDigitizer's status bar with no graph behind it: the session's latest warning, counts per level and its history
void drawSessionStatusBar();

/// whether this viewer was built with the runtime a live region needs; false in a core-only build
[[nodiscard]] bool liveGraphsAvailable() noexcept;

/// whether the browser device a workflow needs, such as "audio", has been granted; natively a device needs no grant
[[nodiscard]] bool liveDeviceGranted(std::string_view device);

/// asks the browser for that device alone -- asking every registered device would also open a USB picker
void requestLiveDevice(std::string_view device);

/**
 * Makes ready whatever a live region draws with. Call once from `main`, after the ImGui context exists and while
 * the font atlas can still be added to.
 *
 * A dashboard's charts read fonts from a shared look-and-feel that is empty until they are loaded, take their
 * trace colours from a palette that must be chosen, and lay themselves out in a dockspace ImGui only provides
 * when asked. Doing this here rather than in the application keeps every one of those headers behind this one.
 */
void prepareLiveGraphs();

/// light or dark, for every chart and flow graph OpenDigitizer draws, with their axes in ImGui's current text colour;
/// a flow graph also needs `LiveGraph::restyle`
void applyLiveGraphScheme(bool dark);

} // namespace gr::present

#endif // GR4_PRESENT_LIVE_GRAPH_HPP
