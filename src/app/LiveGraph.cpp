#include "LiveGraph.hpp"

#include "Fonts.hpp"
#include "Theme.hpp"
#include "blocks/DebounceTagBlock.hpp"
#include "blocks/Deinterleave.hpp"
#include "blocks/PeakDecimator.hpp"

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER

#include <Dashboard.hpp>
#include <DashboardView.hpp>
#include <FlowgraphPage.hpp>
#include <LogHistory.hpp>
#include <Setup.hpp>
#include <StatusBarView.hpp>
#include <ToolbarView.hpp>

#include <common/LookAndFeel.hpp>
#include <components/ColourManager.hpp>

#include <implot.h>

#include <magic_enum.hpp>

// Included for their side effect: each registers its type at namespace scope, and nothing else refers to them,
// so without this a workflow names a chart or a block the loader cannot build. `GR_REGISTER_BLOCK` beside them
// is only a marker for GNU Radio's code generator, which is never run over OpenDigitizer's own types.
#include <charts/SinkRegistry.hpp>
#include <charts/SpectrumView.hpp>
#include <charts/XYChart.hpp>

#include <blocks/Arithmetic.hpp>
#include <blocks/ImPlotSink.hpp>
#include <blocks/TestSpectrumGenerator.hpp>

// and the GNU Radio blocks these workflows name. Their libraries are shared objects natively and archives under
// Emscripten, where a member nothing refers to is never linked in -- so naming the types is what pulls the
// registrations in. Forcing the whole archives instead took the binary from 6.9 MB to 29.9 MB.
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/audio/AudioBlocks.hpp>
#include <gnuradio-4.0/basic/ClockSource.hpp>
#include <gnuradio-4.0/basic/FunctionGenerator.hpp>
#include <gnuradio-4.0/basic/SignalGenerator.hpp>
#include <gnuradio-4.0/basic/StreamToDataSet.hpp>
#include <gnuradio-4.0/fileio/WavBlocks.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/math/Math.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>
#include <gnuradio-4.0/trigger/SchmittTrigger.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <string>
#include <tuple>
#include <utility>

namespace gr::present {

/**
 * The dashboard a workflow describes, and the views that draw it. Both are OpenDigitizer's.
 *
 * A `Dashboard` reads the `dashboard:` section of the same file its graph came from, builds the charts it names
 * with the parameters and axes it gives them, and owns the scheduler that runs the graph on GR4's pools. A
 * `DashboardView` docks those charts as that section lays them out, into whatever region the caller opened. There is
 * one per dashboard: its charts are ImGui windows named after the plots, docked in one place at a time, which moves
 * with the region that draws them. A legend position that differs from the last draw takes a frame to settle.
 */
struct LiveGraph::Running {
    std::unique_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::DashboardView              view;
    DigitizerUi::StatusBarView              statusBar;
    DigitizerUi::ToolbarView                toolbar;
    DigitizerUi::FlowgraphPage              graph{};                // the same workflow seen as blocks and connections
    ImVec2                                  fittedTo{-1.0f, -1.0f}; // the pane size the graph was last fitted to
};

LiveGraph::LiveGraph(std::string_view workflow) { build(workflow); }

/// builds the dashboard, its charts and the scheduler that runs the graph
void LiveGraph::build(std::string_view workflow) {
    _problem.clear();
    releaseDashboard(); // the old dashboard's sinks leave the registry before the new one's arrive under the same names
    try {
        auto running       = std::make_unique<Running>();
        running->dashboard = DigitizerUi::Dashboard::create(nullptr, DigitizerUi::DashboardDescription::createEmpty("slide"));
        if (!running->dashboard) {
            _problem = "the dashboard could not be created";
            return;
        }

        // `loadAndThen` rather than `load`: the bytes are already here, fetched with the rest of the package, and
        // `load` would go looking for them through the storage its description names. A null REST client is
        // enough for a workflow that names no remote source.
        DigitizerUi::Dashboard& dashboard = *running->dashboard;
        dashboard.loadAndThen(std::string{workflow}, [&dashboard](gr::Graph&& graph) { dashboard.session.emplaceGraph(std::move(graph)); });
        running->graph.showEditorControls  = false; // a slide shows the graph, it does not edit it; set before the editor exists
        running->graph.showUiControlBlocks = false; // a workflow's controls are drawn in its toolbar, not as blocks
        running->graph.setDashboard(&dashboard);
        _running = std::move(running);

        // Their loader reports a graph it could not build by logging it, not by throwing, so a workflow whose
        // blocks are unknown still yields a dashboard -- with nothing in it. A region that would draw an empty
        // box says so instead, and the log says which block.
        if (charts().empty()) {
            _problem = "the workflow declares no chart that could be built";
            _running.reset();
        }
    } catch (const std::exception& failure) {
        _problem = failure.what(); // their loader reports a bad flowgraph by throwing, and this library does not
    } catch (...) {
        _problem = "the workflow could not be read";
    }
}

LiveGraph::~LiveGraph() { releaseDashboard(); }

/// the scheduler stops and joins its thread as it is destroyed, which GR4 now does without the thread being woken first
void LiveGraph::releaseDashboard() { _running.reset(); }

bool LiveGraph::running() const noexcept { return _running && _running->dashboard && _running->dashboard->session && _running->dashboard->session.state() == gr::lifecycle::State::RUNNING; }

std::string_view LiveGraph::stateName() const noexcept {
    if (!_running || !_running->dashboard || !_running->dashboard->session) {
        return {};
    }
    return magic_enum::enum_name(_running->dashboard->session.state());
}

void LiveGraph::setRunning(bool wanted) {
    _wanted = wanted;
    applyWantedState();
}

/**
 * Moves the graph towards the state that was last asked for.
 *
 * The wanted state is held and re-asserted every frame rather than requested once: a scheduler still on its way to
 * STOPPED cannot be started, and a slide returned to before it got there comes back once it has.
 *
 * Idling is a stop rather than a pause: a paused graph keeps a sound card or a radio claimed by blocks that have
 * merely gone quiet, and releasing them is the point.
 */
void LiveGraph::applyWantedState() {
    if (!_running || !_running->dashboard || !_running->dashboard->session) {
        return;
    }
    using enum gr::lifecycle::State;
    DigitizerUi::GraphSession& session = _running->dashboard->session;
    const gr::lifecycle::State state   = session.state();
    if (_wanted && (state == IDLE || state == STOPPED)) {
        session.start();
    } else if (!_wanted && state == RUNNING) {
        session.stop();
    }
}

void LiveGraph::pump() {
    if (_running && _running->dashboard) {
        _running->dashboard->handleMessages(); // once a frame, or nothing the scheduler says reaches the charts
    }
    applyWantedState();
    const auto now = std::chrono::steady_clock::now();
    if (!running()) {
        _progressSince = now;
        return;
    }
    std::vector<std::size_t> sinkProgress;
    const auto&              graph = _running->dashboard->session.graph();
    for (const auto& block : graph.blocks()) {
        if (const auto sink = opendigitizer::charts::SinkRegistry::instance().getSink(block->uniqueName())) {
            sinkProgress.push_back(sink->totalSampleCount());
        }
    }
    if (sinkProgress != _sinkProgressSeen || _progressSince == std::chrono::steady_clock::time_point{}) {
        _sinkProgressSeen = std::move(sinkProgress);
        _progressSince    = now;
    }
}

bool LiveGraph::stalled() const noexcept {
    constexpr std::chrono::seconds kStall{3};
    return running() && _progressSince != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() - _progressSince > kStall;
}

std::vector<std::string> LiveGraph::charts() const {
    std::vector<std::string> names;
    if (!_running || !_running->dashboard) {
        return names;
    }
    // the dashboard's windows hold only its charts, one per plot its file declares: the name is the plot's, not the block's
    for (const DigitizerUi::Dashboard::UIWindow& window : _running->dashboard->uiWindows) {
        if (window.window) {
            names.emplace_back(window.window->name);
        }
    }
    return names;
}

namespace {
float drawnChartBodyPixels = 0.0f;

[[nodiscard]] float chartBodyPixelsHere() {
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    return present::Fonts::chartLabelPixels(ImGui::GetStyle().FontSizeBase, viewport.x, viewport.y);
}
} // namespace

float chartBodyPixels() { return drawnChartBodyPixels; }

bool LiveGraph::draw(ChartLegend legend) {
    if (!_running || !_running->dashboard) {
        return false;
    }
    using Position = DigitizerUi::DashboardStyle::LegendPosition;
    constexpr std::array<Position, 5> kPositions{Position::Bottom, Position::Top, Position::Left, Position::Right, Position::None}; // in `ChartLegend`'s order
    DigitizerUi::LookAndFeel::mutableInstance().dashboardStyle.legend = kPositions[static_cast<std::size_t>(legend)];

    // one ID per graph rather than per region, as OpenDigitizer asks: the view's dockspace is identified under it
    ImGui::PushID(this);
    drawnChartBodyPixels                                                 = chartBodyPixelsHere();
    DigitizerUi::LookAndFeel::mutableInstance().chartStyle.labelFontSize = drawnChartBodyPixels;   // ticks, axis titles and tags
    DigitizerUi::LookAndFeel::mutableInstance().chartStyle.labelFont     = Fonts::instance().face; // the deck's body face, which a deck may set
    ImGui::PushFont(Fonts::instance().face, drawnChartBodyPixels);                                 // and the legend beside them
    std::ignore = _running->view.draw(*_running->dashboard, DigitizerUi::DashboardView::Mode::View);
    ImGui::PopFont();
    ImGui::PopID();
    return true;
}

void LiveGraph::restyle() {
    if (_running) {
        _running->graph.updateStyle();
    }
}

void applyLiveGraphScheme(bool dark) {
    DigitizerUi::LookAndFeel& look = DigitizerUi::LookAndFeel::mutableInstance();
    look.style                     = dark ? DigitizerUi::LookAndFeel::Style::Dark : DigitizerUi::LookAndFeel::Style::Light;
    // axes, ticks and labels in the slide's text colour, and the grid faint in it, as the deck's own plots draw them
    const ImVec4 text          = ImGui::GetStyle().Colors[ImGuiCol_Text]; // the theme's, set before this is called
    look.chartStyle.axisColour = text;
    look.chartStyle.gridColour = ImVec4{text.x, text.y, text.z, text.w * kGridAlpha};
    opendigitizer::ColourManager::instance().setCurrentMode(dark ? opendigitizer::ColourManager::ColourMode::Dark : opendigitizer::ColourManager::ColourMode::Light);
}

void LiveGraph::drawToolbar() {
    if (!_running || !_running->dashboard) {
        return;
    }
    ImGui::PushID(this);
    ImGui::PushFont(Fonts::instance().face, chartBodyPixelsHere());
    _running->toolbar.draw(_running->dashboard->session, false);
    ImGui::PopFont();
    ImGui::PopID();
}

void LiveGraph::drawStatusBar() {
    if (!_running || !_running->dashboard) {
        return;
    }
    ImGui::PushID(this);
    ImGui::PushFont(Fonts::instance().face, chartBodyPixelsHere());
    _running->statusBar.draw(&_running->dashboard->session);
    ImGui::PopFont();
    ImGui::PopID();
}

void captureLogFromStart() { std::ignore = DigitizerUi::logHistory(); }

void drawSessionStatusBar() {
    static DigitizerUi::StatusBarView bar; // no graph behind it: the log of the whole session, which it reads
    bar.version = "gr4-present";
    ImGui::PushFont(Fonts::instance().face, chartBodyPixelsHere());
    bar.draw(nullptr);
    ImGui::PopFont();
}

bool LiveGraph::drawFlowgraph(std::string_view pane) {
    if (!_running || !_running->dashboard) {
        return false;
    }
    ImGui::PushID(std::string{pane}.c_str());
    // fitted once the editor exists and again whenever its box changes size: their fit does not follow a resize
    if (const ImVec2 room = ImGui::GetContentRegionAvail(); _running->graph.editorCount() > 0 && (room.x != _running->fittedTo.x || room.y != _running->fittedTo.y)) {
        _running->graph.requestRelayout();
        _running->fittedTo = room;
    }
    _running->graph.draw();
    ImGui::PopID();
    return true;
}

bool liveGraphsAvailable() noexcept { return true; }

bool liveDeviceGranted(std::string_view device) {
#ifdef __EMSCRIPTEN__
    const gr::blocks::common::DeviceBase* found = gr::blocks::common::DeviceRegistry::instance().find(device);
    return found != nullptr && found->isGranted();
#else
    (void)device;
    return true;
#endif
}

void requestLiveDevice(std::string_view device) {
#ifdef __EMSCRIPTEN__
    if (gr::blocks::common::DeviceBase* found = gr::blocks::common::DeviceRegistry::instance().find(device); found != nullptr && !found->isGranted()) {
        found->requestPermission();
    }
#else
    (void)device;
#endif
}

bool liveGraphThreadsAvailable() {
#ifdef __EMSCRIPTEN__
    constexpr std::size_t kThreadsPerGraph = 5;
    constexpr std::size_t kSpare           = 1;
    return gr::thread_pool::getTotalThreadCount() + kThreadsPerGraph + kSpare <= gr::thread_pool::thread::getThreadLimit();
#else
    return true;
#endif
}

namespace {
/// referring to a type is what pulls its registration out of the archive it sits in; see the includes above
void registerTheBlocksTheWorkflowsName() {
    gr::registerBlock<gr::basic::SignalGenerator<float>, "">(gr::globalBlockRegistry());
    gr::registerBlock<gr::basic::FunctionGenerator<float>, "">(gr::globalBlockRegistry());
    // the spectrum form, which emits one magnitude/phase DataSet per transform. The primary `FFT<T>` template is
    // the raw complex transform and takes a complex input, so a float source cannot be connected to it.
    gr::registerBlock<gr::blocks::fft::FFT<float, gr::DataSet<float>>, "">(gr::globalBlockRegistry());
    gr::registerBlock<gr::basic::DefaultClockSource, "gr::basic::ClockSource">(gr::globalBlockRegistry());
    // under the names GNU Radio's own block library gives them, `Add<float32, std::plus<float32>>` and its kin
    gr::registerBlock<"gr::blocks::math::Add", gr::blocks::math::MathOpMultiPortImpl, gr::BlockParameters<float, std::plus<float>>>(gr::globalBlockRegistry());
    gr::registerBlock<"gr::blocks::math::Multiply", gr::blocks::math::MathOpMultiPortImpl, gr::BlockParameters<float, std::multiplies<float>>>(gr::globalBlockRegistry());
    // the live audio slide: the microphone, and the recording that stands in for it, played as it is analysed
    gr::registerBlock<gr::audio::AudioSource<float>, "">(gr::globalBlockRegistry());
    gr::registerBlock<gr::audio::AudioSink<float>, "">(gr::globalBlockRegistry());
    gr::registerBlock<gr::blocks::fileio::WavSource<float>, "">(gr::globalBlockRegistry());
    // its stereo split and the clap trigger: a Schmitt trigger, and one second cut around each clap, with a dead time
    gr::registerBlock<gr::present::blocks::Deinterleave<float>, "">(gr::globalBlockRegistry());
    gr::registerBlock<gr::present::blocks::PeakDecimator<float>, "">(gr::globalBlockRegistry()); // the time chart's envelope
    gr::registerBlock<gr::present::blocks::DebounceTagBlock<float>, "">(gr::globalBlockRegistry());
    gr::globalBlockRegistry().insert<gr::blocks::trigger::SchmittTrigger<float, gr::trigger::InterpolationMethod::NO_INTERPOLATION>>("gr::blocks::trigger::SchmittTriggerNoInterpolation", "float");
    gr::globalBlockRegistry().insert<gr::basic::StreamFilterImpl<float, false>>("gr::basic::StreamToDataSet", "float");
}
} // namespace

void prepareLiveGraphs() {
    static const bool once = [] {
        registerTheBlocksTheWorkflowsName();

        // OpenDigitizer's one setup call: the ImPlot context its charts draw in, the fonts they read from the atlas
        // (added to it, not rebuilt), and the chart blocks and plot sink a workflow names. Its log history feeds the
        // status bar a region may ask for.
        DigitizerUi::initialise(std::nullopt); // the log is always captured, for the status bar

        // A chart asks the colour manager for a slot in the palette of the current mode: the deck's own series
        // colours in both modes, so a live chart and a plot on another slide agree. The plot sits on the slide, as a
        // plot of the deck's own does: no background of its own, the same faint grid and the same line width.
        auto&                      colours = opendigitizer::ColourManager::instance();
        std::vector<std::uint32_t> deck;
        for (const ImU32 colour : kSeriesColours) {
            deck.push_back(((colour & 0xFFU) << 16U) | (colour & 0xFF00U) | ((colour >> 16U) & 0xFFU)); // ImGui's ABGR to 0xRRGGBB
        }
        colours.setPalette("gr4-present", deck);
        colours.setModePalette(opendigitizer::ColourManager::ColourMode::Light, "gr4-present");
        colours.setModePalette(opendigitizer::ColourManager::ColourMode::Dark, "gr4-present");
        DigitizerUi::ChartStyle& chart = DigitizerUi::LookAndFeel::mutableInstance().chartStyle;
        chart.plotBackground           = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
        chart.lineWidth                = kSeriesWidth;
        chart.colourAxesBySignal       = false; // the series colours tell the signals apart; the axes stay the slide's text
        chart.labelFont                = Fonts::instance().face;

        // the slide is the background: no window, border or canvas of their own behind the charts and the flow graph
        DigitizerUi::LookAndFeel& look  = DigitizerUi::LookAndFeel::mutableInstance();
        look.dashboardStyle.background  = false;
        look.flowgraph.canvasBackground = false;
        look.flowgraph.canvasGrid       = false;
        look.flowgraph.canvasBorder     = false;

        // a dashboard lays its charts out in a dockspace, which ImGui only provides when this is set
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        return true;
    }();
    std::ignore = once;
}

} // namespace gr::present

#else // a viewer built without OpenDigitizer says so, and a region draws its fallback

namespace gr::present {

struct LiveGraph::Running {};

LiveGraph::LiveGraph(std::string_view) : _problem("this viewer was built without the GNU Radio runtime") {}
LiveGraph::~LiveGraph() = default;

void LiveGraph::build(std::string_view) {}

void LiveGraph::applyWantedState() {}

bool                     LiveGraph::running() const noexcept { return false; }
bool                     LiveGraph::stalled() const noexcept { return false; }
std::string_view         LiveGraph::stateName() const noexcept { return {}; }
void                     LiveGraph::pump() {}
void                     LiveGraph::setRunning(bool) {}
void                     LiveGraph::drawToolbar() {}
std::vector<std::string> LiveGraph::charts() const { return {}; }
bool                     LiveGraph::draw(ChartLegend) { return false; }
bool                     LiveGraph::drawFlowgraph(std::string_view) { return false; }
void                     LiveGraph::drawStatusBar() {}
void                     captureLogFromStart() {}
void                     drawSessionStatusBar() {}
void                     LiveGraph::restyle() {}
void                     applyLiveGraphScheme(bool) {}
bool                     liveGraphsAvailable() noexcept { return false; }
bool                     liveDeviceGranted(std::string_view) { return false; }
void                     requestLiveDevice(std::string_view) {}
bool                     liveGraphThreadsAvailable() { return true; }
float                    chartBodyPixels() { return 0.0f; }
void                     prepareLiveGraphs() {}

} // namespace gr::present

#endif
