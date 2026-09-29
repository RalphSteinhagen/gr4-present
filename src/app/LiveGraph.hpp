#ifndef GR4_PRESENT_LIVE_GRAPH_HPP
#define GR4_PRESENT_LIVE_GRAPH_HPP

#include <gr4-present/ChartLegend.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// An OpenDigitizer dashboard running beside the slide it is drawn into: it reads a workflow's flowgraph and
/// `dashboard:` section, runs the graph on a thread of its own and draws the charts and legend into a region.
/// Construct only while the ImGui and ImPlot styles exist: inside `main`, never from a static destructor.
/// No GNU Radio or OpenDigitizer type appears here: screenshot tests link an ImGui whose ABI differs.
class LiveGraph {
public:
    /// reads a workflow (GNU Radio YAML with OpenDigitizer's dashboard section) and starts it running
    explicit LiveGraph(std::string_view workflow);
    ~LiveGraph();
    LiveGraph(const LiveGraph&)            = delete;
    LiveGraph& operator=(const LiveGraph&) = delete;

    /// why the workflow is not running; empty while it is
    [[nodiscard]] std::string_view problem() const noexcept { return _problem; }

    /// whether the graph has reached the state in which its blocks do work
    [[nodiscard]] bool running() const noexcept;

    /// the scheduler's lifecycle state by name, e.g. "PAUSED", for a test to read; empty without a graph
    [[nodiscard]] std::string_view stateName() const noexcept;

    /// running, but its graph has made no progress for a few seconds: what a slide should not keep showing as live
    [[nodiscard]] bool stalled(std::chrono::steady_clock::time_point now) const noexcept;

    /// relays scheduler state to the charts and drives `setRunning` to its wanted state; call once a frame
    void pump(std::chrono::steady_clock::time_point now);

    /// starts or stops the graph, leaving it built either way
    void setRunning(bool wanted);

    /// the charts the dashboard declares, in file order
    [[nodiscard]] std::vector<std::string> charts() const;

    /// draws the docked charts with a shared `legend`; the caller sets the size by opening a window or child
    [[nodiscard]] bool draw(ChartLegend legend);

    /// draws the workflow's toolbar blocks, without play, pause and stop: the deck runs the graph itself
    void drawToolbar();

    /// draws the workflow's status bar: latest warning or error, counts per level, log popup and its own blocks
    void drawStatusBar();

    /// draws the workflow as a flow graph rather than its charts
    [[nodiscard]] bool drawFlowgraph(std::string_view pane);

    /// the flow graph's editor keeps its colours until told of a changed scheme
    void restyle();

private:
    struct Running; // the dashboard and its page, incomplete here so neither leaks out

    void applyWantedState();

    void build(std::string_view workflow);
    void releaseDashboard();

    bool                                  _wanted = true; // a graph runs from the moment it is built
    std::unique_ptr<Running>              _running;
    std::string                           _problem;
    std::vector<std::size_t>              _sinkProgressSeen;
    std::chrono::steady_clock::time_point _progressSince{};
};

/// whether the browser can give one more graph its threads: each holds five of a fixed `GR_MAX_WASM_THREAD_COUNT`
/// pool, and an exhausted pool freezes the page, as the next thread needs the event loop the frame is blocking
[[nodiscard]] bool liveGraphThreadsAvailable();

/// the text size the last chart drawn used, one level in from the slide body; OpenDigitizer scales its faces from it
[[nodiscard]] float chartBodyPixels();

/// installs OpenDigitizer's log history as GR4's log backend, for a status bar; call first in `main`
void captureLogFromStart();

/// OpenDigitizer's status bar with no graph behind it
void drawSessionStatusBar();

/// false in a core-only build, without the runtime a live region needs
[[nodiscard]] bool liveGraphsAvailable() noexcept;

/// whether the browser device a workflow needs, such as "audio", is granted; natively a device needs no grant
[[nodiscard]] bool liveDeviceGranted(std::string_view device);

/// asks the browser for that device alone: asking every registered device would also open a USB picker
void requestLiveDevice(std::string_view device);

/// loads the fonts, palette and dockspace the charts draw with; call once from `main`, after the ImGui context exists
/// and while the font atlas can still be added to
void prepareLiveGraphs();

/// light or dark for every chart and flow graph, axes in ImGui's current text colour; a flow graph also needs `restyle`
void applyLiveGraphScheme(bool dark);

} // namespace gr::present

#endif // GR4_PRESENT_LIVE_GRAPH_HPP
