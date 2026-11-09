#include "LiveGraph.hpp"

#include "Fonts.hpp"
#include "ScopedWindow.hpp"
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

// included for their side effect: each registers its type at namespace scope and nothing else refers to them
#include <charts/SinkRegistry.hpp>
#include <charts/SpectrumView.hpp>
#include <charts/XYChart.hpp>

#include <blocks/Arithmetic.hpp>
#include <blocks/ImPlotSink.hpp>
#include <blocks/TestSpectrumGenerator.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrAnalogBlocks.hpp>
#include <gnuradio-4.0/GrAudioBlocks.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrBeamformingBlocks.hpp>
#include <gnuradio-4.0/GrDigitalBlocks.hpp>
#include <gnuradio-4.0/GrElectricalBlocks.hpp>
#include <gnuradio-4.0/GrFileIoBlocks.hpp>
#include <gnuradio-4.0/GrFilterBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrHttpBlocks.hpp>
#include <gnuradio-4.0/GrLocalisationBlocks.hpp>
#include <gnuradio-4.0/GrMathBlocks.hpp>
#include <gnuradio-4.0/GrMeasurementBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/GrTriggerBlocks.hpp>
#include <gnuradio-4.0/audio/AudioBlocks.hpp>
#include <gnuradio-4.0/basic/StreamToDataSet.hpp>
#include <gnuradio-4.0/filter/FrequencyEstimator.hpp>
#include <gnuradio-4.0/filter/time_domain_filter.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>
#include <gnuradio-4.0/trigger/SchmittTrigger.hpp>
#include <gnuradio-4.0/trigger/TimeInterval.hpp>
#include <gnuradio-4.0/trigger/ValueTrigger.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <string>
#include <tuple>
#include <utility>

namespace gr::present {

/// the workflow's OpenDigitizer dashboard and its views: charts are ImGui windows named after the plots, docked in one
/// place at a time, and a legend position that differs from the last draw takes a frame to settle
struct LiveGraph::Running {
    std::unique_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::DashboardView              view;
    DigitizerUi::StatusBarView              statusBar;
    DigitizerUi::ToolbarView                toolbar;
    DigitizerUi::FlowgraphPage              graph{};
    ImVec2                                  fittedTo{-1.0f, -1.0f}; // the pane size the graph was last fitted to
};

LiveGraph::LiveGraph(std::string_view workflow) { build(workflow); }

void LiveGraph::build(std::string_view workflow) {
    _problem.clear();
    releaseDashboard(); // the old sinks must leave the registry before the new ones arrive under the same names
    try {
        auto running       = std::make_unique<Running>();
        running->dashboard = DigitizerUi::Dashboard::create(nullptr, DigitizerUi::DashboardDescription::createEmpty("slide"));
        if (!running->dashboard) {
            _problem = "the dashboard could not be created";
            return;
        }

        // `loadAndThen`, not `load`: the bytes came with the package; a null REST client suffices
        DigitizerUi::Dashboard& dashboard = *running->dashboard;
        dashboard.loadAndThen(std::string{workflow}, [&dashboard](gr::Graph&& graph) { dashboard.session.emplaceGraph(std::move(graph)); });
        running->graph.showEditorControls  = false; // set before the editor exists
        running->graph.showUiControlBlocks = false; // a workflow's controls are drawn in its toolbar, not as blocks
        running->graph.setDashboard(&dashboard);
        _running = std::move(running);

        // their loader logs an unbuildable graph instead of throwing, so unknown blocks yield an empty dashboard
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

/// the scheduler stops and joins its thread as it is destroyed
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

/// re-asserted every frame, as a scheduler still on its way to STOPPED cannot be started; idling stops rather than
/// pauses, as a paused graph keeps a sound card or radio claimed
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

void LiveGraph::pump(std::chrono::steady_clock::time_point now) {
    if (_running && _running->dashboard) {
        _running->dashboard->handleMessages(); // once a frame, or nothing the scheduler says reaches the charts
    }
    applyWantedState();
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

bool LiveGraph::stalled(std::chrono::steady_clock::time_point now) const noexcept {
    constexpr std::chrono::seconds kStall{3};
    return running() && _progressSince != std::chrono::steady_clock::time_point{} && now - _progressSince > kStall;
}

std::vector<std::string> LiveGraph::charts() const {
    std::vector<std::string> names;
    if (!_running || !_running->dashboard) {
        return names;
    }
    // the windows hold only the charts, named after the plot, not the block
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

    // one ID per graph, not per region, as OpenDigitizer asks: the view's dockspace is identified under it
    const ScopedPointerId perGraph{this};
    drawnChartBodyPixels                                                 = chartBodyPixelsHere();
    DigitizerUi::LookAndFeel::mutableInstance().chartStyle.labelFontSize = drawnChartBodyPixels;
    DigitizerUi::LookAndFeel::mutableInstance().chartStyle.labelFont     = Fonts::instance().face;
    const ScopedFont chartFont(drawnChartBodyPixels);
    std::ignore = _running->view.draw(*_running->dashboard, DigitizerUi::DashboardView::Mode::View);
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
    // axes in the slide's text colour and the grid faint in it, as the deck's own plots draw them
    const ImVec4 text          = ImGui::GetStyle().Colors[ImGuiCol_Text]; // set by `applyTheme` before this is called
    look.chartStyle.axisColour = text;
    look.chartStyle.gridColour = ImVec4{text.x, text.y, text.z, text.w * kGridAlpha};
    opendigitizer::ColourManager::instance().setCurrentMode(dark ? opendigitizer::ColourManager::ColourMode::Dark : opendigitizer::ColourManager::ColourMode::Light);
}

void LiveGraph::drawToolbar() {
    if (!_running || !_running->dashboard) {
        return;
    }
    const ScopedPointerId perGraph{this};
    const ScopedFont      chartFont(chartBodyPixelsHere());
    _running->toolbar.draw(_running->dashboard->session, false);
}

void LiveGraph::drawStatusBar() {
    if (!_running || !_running->dashboard) {
        return;
    }
    const ScopedPointerId perGraph{this};
    const ScopedFont      chartFont(chartBodyPixelsHere());
    _running->statusBar.draw(&_running->dashboard->session);
}

void captureLogFromStart() { std::ignore = DigitizerUi::logHistory(); }

void drawSessionStatusBar() {
    static DigitizerUi::StatusBarView bar;
    bar.version = "gr4-present";
    const ScopedFont chartFont(chartBodyPixelsHere());
    bar.draw(nullptr);
}

bool LiveGraph::drawFlowgraph(std::string_view pane) {
    if (!_running || !_running->dashboard) {
        return false;
    }
    const ScopedId perPane{std::string{pane}.c_str()};
    // their fit does not follow a resize: fitted once the editor exists and whenever its box changes size
    if (const ImVec2 room = ImGui::GetContentRegionAvail(); _running->graph.editorCount() > 0 && (room.x != _running->fittedTo.x || room.y != _running->fittedTo.y)) {
        _running->graph.requestRelayout();
        _running->fittedTo = room;
    }
    _running->graph.draw();
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

bool requestLiveDevice(std::string_view device, bool askedBefore) {
#ifdef __EMSCRIPTEN__
    gr::blocks::common::DeviceBase* found = gr::blocks::common::DeviceRegistry::instance().find(device);
    if (found == nullptr) {
        return false; // not registered yet: asked for on a later visit
    }
    // `DeviceBase` cannot tell an open request from a refused one; the microphone keeps its own record
    // (`EmscriptenAudioBackend.hpp`: 1 granted, -1 refused, 0 open or never asked)
    const bool refused = device == gr::audio::detail::WebAudioDevice::kId && EM_ASM_INT({ return globalThis.__grAudioMicGranted < 0 ? 1 : 0; }) == 1;
    if (!found->isGranted() && (!askedBefore || refused)) {
        found->requestPermission();
    }
    return true;
#else
    (void)device;
    (void)askedBefore;
    return true;
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
/// referring to a type pulls its registration out of its archive; see the includes above
void registerTheBlocksTheWorkflowsName() {
    // the live audio slide's microphone, one port per channel
    gr::globalBlockRegistry().insert<gr::audio::AudioSourceMultiplexed<float>>("gr::audio::AudioSourceMultiplexed", "float");
    // the recording's stereo split, the time chart's envelope and the clap trigger's dead time
    gr::registerBlock<gr::present::blocks::Deinterleave<float>, "">(gr::globalBlockRegistry());
    gr::registerBlock<gr::present::blocks::PeakDecimator<float>, "">(gr::globalBlockRegistry());
    gr::registerBlock<gr::present::blocks::DebounceTagBlock<float>, "">(gr::globalBlockRegistry());
    // the spellings the workflows use where GR4 registers another
    gr::globalBlockRegistry().insert<gr::blocks::trigger::SchmittTrigger<float, gr::trigger::InterpolationMethod::NO_INTERPOLATION>>("gr::blocks::trigger::SchmittTriggerNoInterpolation", "float");
    gr::globalBlockRegistry().insert<gr::basic::StreamFilterImpl<float, false>>("gr::basic::StreamToDataSet", "float");
    gr::globalBlockRegistry().insert<gr::blocks::trigger::ValueTrigger<float, gr::blocks::trigger::ValueCondition::level>>("gr::blocks::trigger::LevelTrigger", "float");
    // spelt out in full: Clang leaves a template argument equal to its default out of a type's name (`TimeInterval<>`,
    // `Resampling<>`) and GCC keeps it, so the name a block registers itself under depends on the compiler
    gr::globalBlockRegistry().insert<gr::blocks::trigger::TimeInterval<double>>("=gr::blocks::trigger::TimeInterval<float64>");
    gr::globalBlockRegistry().insert<gr::filter::BasicFilterProto<float, gr::Resampling<1UZ, 1UZ, false>>>("=gr::filter::BasicFilterProto<float32, gr::Resampling<1, 1, false>>");
    gr::globalBlockRegistry().insert<gr::filter::IQDemodulator<float, gr::Resampling<1024U, 1U, false>>>("=gr::filter::IQDemodulator<float32, gr::Resampling<1024, 1, false>>");
}
} // namespace

void prepareLiveGraphs() {
    static const bool once = [] {
        // every block of GR4's libraries, then the spellings the workflows add
        std::ignore = gr::blocklib::initGrAnalogBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrAudioBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrBasicBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrBeamformingBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrDigitalBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrElectricalBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrFileIoBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrFilterBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrFourierBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrHttpBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrLocalisationBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrMathBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrMeasurementBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrTestingBlocks(gr::globalBlockRegistry());
        std::ignore = gr::blocklib::initGrTriggerBlocks(gr::globalBlockRegistry());
        registerTheBlocksTheWorkflowsName();

        // OpenDigitizer's one setup call: ImPlot context, chart fonts (added to the atlas), chart blocks and plot sink
        DigitizerUi::initialise(std::nullopt);

        // the deck's series colours in both modes, so a live chart and a plot on another slide agree
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
        chart.lineWidth                = kSeriesWidthPixels;
        chart.colourAxesBySignal       = false; // the series colours tell the signals apart; the axes stay the slide's text
        chart.labelFont                = Fonts::instance().face;

        // the slide is the background: no window, border or canvas behind the charts and the flow graph
        DigitizerUi::LookAndFeel& look  = DigitizerUi::LookAndFeel::mutableInstance();
        look.dashboardStyle.background  = false;
        look.flowgraph.canvasBackground = false;
        look.flowgraph.canvasGrid       = false;
        look.flowgraph.canvasBorder     = false;

        // a dashboard lays its charts out in a dockspace, which ImGui provides only when this is set
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        return true;
    }();
    std::ignore = once;
}

} // namespace gr::present

#else // without OpenDigitizer a region draws its fallback

namespace gr::present {

struct LiveGraph::Running {};

LiveGraph::LiveGraph(std::string_view) : _problem("this viewer was built without the GNU Radio runtime") {}
LiveGraph::~LiveGraph() = default;

void LiveGraph::build(std::string_view) {}

void LiveGraph::applyWantedState() {}

bool                     LiveGraph::running() const noexcept { return false; }
bool                     LiveGraph::stalled(std::chrono::steady_clock::time_point) const noexcept { return false; }
std::string_view         LiveGraph::stateName() const noexcept { return {}; }
void                     LiveGraph::pump(std::chrono::steady_clock::time_point) {}
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
bool                     requestLiveDevice(std::string_view, bool) { return true; }
bool                     liveGraphThreadsAvailable() { return true; }
float                    chartBodyPixels() { return 0.0f; }
void                     prepareLiveGraphs() {}

} // namespace gr::present

#endif
