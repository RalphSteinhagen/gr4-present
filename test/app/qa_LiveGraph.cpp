#include "LiveGraph.hpp"

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <boost/ut.hpp>

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] std::string contentsOf(std::string_view name) {
    std::ifstream     file{std::filesystem::path{GR4_PRESENT_PACKAGE_DIRECTORY} / "workflows" / name};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

constexpr auto kProgressEvery = std::chrono::seconds{5};

/// waits until the graph runs or has stopped, as a graph changes state on its own thread; it says every 5 s which state the
/// graph is in, and ctest's TIMEOUT fails one that never gets there, so a slow runner is no failure
void reach(LiveGraph& graph, bool running, std::string_view what) {
    auto report = std::chrono::steady_clock::now() + kProgressEvery;
    while (graph.running() != running) {
        graph.pump(std::chrono::steady_clock::now()); // carries a graph through lifecycle transitions, as a viewer does every frame
        std::this_thread::yield();
        if (std::chrono::steady_clock::now() >= report) {
            const std::string_view state = graph.stateName();
            std::fprintf(stderr, "%.*s: the graph is still in state '%.*s', waiting to be %s\n", static_cast<int>(what.size()), what.data(), static_cast<int>(state.size()), state.data(), running ? "running" : "stopped");
            report += kProgressEvery;
        }
    }
}

[[nodiscard]] bool holds(const std::vector<std::string>& names, std::string_view wanted) { return std::ranges::find(names, wanted) != names.end(); }

} // namespace

// runs in main, not as a suite: a suite runs after main returns, when the ImGui, ImPlot and palette state is torn down
// nothing here draws; drawing is checked in a browser
int main() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::GetIO().Fonts->AddFontDefault();
    std::ignore                = ImGui::GetIO().Fonts->Build();
    ImGui::GetIO().DisplaySize = ImVec2{1280.0f, 720.0f};
    prepareLiveGraphs();

    // Ground truth: OpenDigitizer's sample dashboards' `plots:`, except the spectrum this deck added
    "each workflow the package ships loads and offers the chart its dashboard declares"_test = [] {
        struct Expected {
            std::string_view file;
            std::string_view chart;
        };
        for (const Expected& workflow : {Expected{.file = "controlled-sines.grc", .chart = "Sine and sum"}, //
                 Expected{.file = "modulated-sines.grc", .chart = "Modulation spectrum"},                   //
                 Expected{.file = "functions.grc", .chart = "Function Generators"},                         //
                 Expected{.file = "spectrum.grc", .chart = "Spectrum View"}}) {
            const std::string text = contentsOf(workflow.file);
            expect(!text.empty()) << workflow.file << " is not in the package";

            LiveGraph graph{text};
            expect(graph.problem().empty()) << workflow.file << ": " << graph.problem();
            const std::vector<std::string> charts = graph.charts();
            std::string                    found;
            for (const std::string& name : charts) {
                found += (found.empty() ? "" : ", ") + name;
            }
            expect(holds(charts, workflow.chart)) << workflow.file << " does not offer " << workflow.chart << "; it offers: [" << found << "]";
        }
    };

    // GNU Radio applies the parameters a block knows and drops the rest without a word, so a setting renamed upstream would fail silently
    "every parameter a shipped workflow sets is a setting of its block"_test = [] {
        std::size_t checked = 0UZ;
        for (const auto& entry : std::filesystem::directory_iterator{std::filesystem::path{GR4_PRESENT_PACKAGE_DIRECTORY} / "workflows"}) {
            const std::string file   = entry.path().filename().string();
            const auto        parsed = gr::pmt::yaml::deserialize(contentsOf(file));
            expect(fatal(parsed.has_value())) << file;
            const auto blocksAt = parsed->find("blocks");
            expect(fatal(blocksAt != parsed->end())) << file << " declares no blocks";
            const gr::Value blocksValue = (*blocksAt).second; // an lvalue: the view below aliases its storage
            const auto      blocks      = blocksValue.get_if<gr::TensorView<gr::Value>>();
            expect(fatal(static_cast<bool>(blocks))) << file << ": blocks is not a list";
            for (const gr::Value& blockValue : *blocks) {
                const auto block = blockValue.get_if<gr::property_map>();
                expect(fatal(static_cast<bool>(block))) << file << ": a block is not a map";
                const std::string                     id    = std::string{block->get_if<std::string_view>("id").value_or("")};
                const std::unique_ptr<gr::BlockModel> model = gr::globalBlockRegistry().create(id, {});
                if (model == nullptr) {
                    continue; // OpenDigitizer's own blocks; an id nobody knows is the loader's to report
                }
                model->settings().init(); // fills the table of defaults, one entry per setting, as a graph does before it starts
                const gr::property_map& settings = model->settings().defaultParameters();
                for (const auto& [key, value] : block->value_or<gr::property_map>("parameters", gr::property_map{})) {
                    expect(settings.contains(key)) << file << ": " << id << " has no setting '" << key << "'";
                    ++checked;
                }
            }
        }
        expect(gt(checked, 0UZ)) << "no parameter of any workflow was checked";
    };

    // Ground truth: the shipped workflows carry data only; GR4 #881 put event ports first, so a stale index names one
    "every connection of a shipped workflow joins two data ports of one type"_test = [] {
        std::size_t checked = 0UZ;
        for (const auto& entry : std::filesystem::directory_iterator{std::filesystem::path{GR4_PRESENT_PACKAGE_DIRECTORY} / "workflows"}) {
            const std::string file  = entry.path().filename().string();
            auto              graph = gr::loadGrc(gr::globalPluginLoader(), contentsOf(file));
            expect(fatal(graph.has_value())) << file << ": " << (graph.has_value() ? std::string{} : graph.error().message);
            for (const gr::Edge& edge : (*graph)->edges()) {
                const auto source      = edge.sourceBlock()->dynamicOutputPort(edge.sourcePortDefinition());
                const auto destination = edge.destinationBlock()->dynamicInputPort(edge.destinationPortDefinition());
                expect(fatal(source.has_value() && destination.has_value())) << file << ": a connection of " << edge.sourceBlock()->name() << " names no port";
                const std::string from = (*source)->typeName();
                const std::string to   = (*destination)->typeName();
                expect(from == to && from != gr::meta::type_name<gr::property_map_view>()) << file << ": " << edge.sourceBlock()->name() << "." << (*source)->metaInfo.name.value << " (" << from << ") -> " << edge.destinationBlock()->name() << "." << (*destination)->metaInfo.name.value << " (" << to << ")";
                ++checked;
            }
        }
        expect(gt(checked, 0UZ)) << "no connection of any workflow was checked";
    };

    "the graph's own blocks are not offered as charts"_test = [] {
        LiveGraph graph{contentsOf("controlled-sines.grc")};
        expect(graph.problem().empty()) << graph.problem();
        const std::vector<std::string> charts = graph.charts();
        expect(!holds(charts, "sineSource1")) << "a signal generator was offered as a chart";
        expect(!holds(charts, "sumSink")) << "a data sink was offered as a chart";
        expect(!holds(charts, "frequency1")) << "a toolbar control was offered as a chart";
    };

    "a loaded workflow starts running"_test = [] {
        LiveGraph graph{contentsOf("controlled-sines.grc")};
        expect(graph.problem().empty()) << graph.problem();

        reach(graph, true, "the state in which its blocks do work");
    };

    // idling stops a graph, and a stopped scheduler loop exits with nobody left to read the message start() sends
    "a graph the deck idles runs again when its slide arrives"_test = [] {
        LiveGraph graph{contentsOf("controlled-sines.grc")};
        expect(graph.problem().empty()) << graph.problem();
        reach(graph, true, "the first start");

        graph.setRunning(false);
        reach(graph, false, "the deck idles it");

        graph.setRunning(true);
        reach(graph, true, "the idled graph runs again");
    };

    // a graph is destroyed in whatever state quick stepping left it, without waiting for something that no longer comes
    // Ground truth: the pools' task counts before the graph ever ran; the browser has 16 threads in all
    "starting and stopping a graph again and again leaves no pool task behind"_test = [] {
        using gr::thread_pool::Manager;
        const auto busy = [] { return Manager::defaultIoPool()->numTasksRunning() + Manager::defaultCpuPool()->numTasksRunning(); };
        // what is running before the graph first runs: whatever earlier tests left settles for a moment, then counts as is
        const auto settled = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (busy() > 0UZ && std::chrono::steady_clock::now() < settled) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        const std::size_t before = busy();
        // a stopped graph's tasks end on their own threads: waited for, saying every 5 s how many remain; ctest's TIMEOUT
        // fails a task that never ends
        const auto settlesTo = [&busy](std::size_t wanted, int cycle) {
            auto report = std::chrono::steady_clock::now() + kProgressEvery;
            while (busy() > wanted) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                if (std::chrono::steady_clock::now() >= report) {
                    std::fprintf(stderr, "cycle %d: %zu tasks still running on the default pools, %zu before the graph first ran\n", cycle, busy(), wanted);
                    report += kProgressEvery;
                }
            }
            return busy();
        };
        LiveGraph graph{contentsOf("controlled-sines.grc")};
        for (int cycle = 0; cycle < 20; ++cycle) {
            graph.setRunning(true);
            reach(graph, true, std::format("cycle {}: the graph starts", cycle));
            graph.setRunning(false);
            reach(graph, false, std::format("cycle {}: the graph stops", cycle));
            const std::size_t after = settlesTo(before, cycle);
            expect(le(after, before)) << "cycle " << cycle << ": tasks still running on the default pools after the stop, " << after << " against " << before << " before the graph first ran";
            if (after != before) {
                break;
            }
        }
    };

    "a graph destroyed in any phase of being idled and woken again does not hang"_test = [] {
        struct Phase {
            const char* name;
            int         pumpsBeforeStop;
            int         pumpsAfterStop;
            bool        wantedAgain;
        };
        for (const Phase& phase : {Phase{"just built", 0, 0, false}, Phase{"running", 20, 0, false}, Phase{"stop requested", 20, 0, false}, Phase{"stopping", 20, 3, false}, Phase{"woken while stopping", 20, 1, true}, Phase{"restarted", 20, 40, true}}) {
            auto       destroyed = std::async(std::launch::async, [&phase] {
                for (int round = 0; round < 8; ++round) {
                    LiveGraph graph{contentsOf("controlled-sines.grc")};
                    for (int pump = 0; pump < phase.pumpsBeforeStop; ++pump) {
                        graph.setRunning(true);
                        graph.pump(std::chrono::steady_clock::now());
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    if (phase.pumpsBeforeStop > 0 || phase.pumpsAfterStop > 0) {
                        graph.setRunning(false);
                    }
                    for (int pump = 0; pump < phase.pumpsAfterStop; ++pump) {
                        graph.pump(std::chrono::steady_clock::now());
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    if (phase.wantedAgain) {
                        graph.setRunning(true);
                    }
                }
            });
            const bool finished  = destroyed.wait_for(std::chrono::seconds{60}) == std::future_status::ready;
            expect(finished) << "destroying a graph hung when it was " << phase.name;
            if (!finished) {
                // report first: the runner reports at exit, which would wait for the hang; abort leaves a core to debug
                std::fprintf(stderr, "destroying a graph hung when it was %s\n", phase.name);
                std::abort();
            }
        }
    };

    "a workflow that cannot be read says why, and offers nothing"_test = [] {
        LiveGraph graph{"this is not a flowgraph\n\t- and not even YAML\n"};
        expect(!graph.problem().empty()) << "a broken workflow was accepted";
        expect(graph.charts().empty());
    };

    "a workflow naming a block that does not exist says which"_test = [] {
        LiveGraph graph{"blocks:\n  - id: nobody::Such<float32>\n    parameters:\n      name: absent\n"};
        // their loader logs an unbuildable block rather than throwing, so only a region drawing nothing is promised
        expect(!graph.problem().empty()) << "a workflow naming an unknown block was accepted";
        expect(graph.charts().empty());
    };

    // Ground truth: every id the workflows of a deck outside this repository name (beams and echoes: trigger,
    // localisation, filter, spectra), as they spell them
    "a block a deck's own workflow names can be built under its spelling"_test = [] {
        constexpr std::array kNamed{"DigitizerUi::ImControlNumber", "gr::audio::AudioSink<float32>", "gr::audio::AudioSourceMultiplexed<float32>", "gr::basic::SignalGenerator<float32>", "gr::basic::StreamsToTensor<float32>", "gr::basic::StreamToDataSet<float32>", "gr::basic::TensorToStreams<float32>", //
            "gr::blocks::fft::FFT<float32, gr::DataSet<float32>>", "gr::blocks::fft::STFT<float32>", "gr::blocks::fileio::WavSource<float32>", "gr::blocks::fourier::PeakDetector", "gr::blocks::math::Add<float32, std::plus<float32>>", "gr::blocks::math::ExpressionSISO<float32>",                         //
            "gr::blocks::math::Histogram<float32>", "gr::blocks::math::Multiply<float32, std::multiplies<float32>>", "gr::blocks::math::SubtractPair<float32, std::minus<float32>>", "gr::blocks::trigger::Coincidence", "gr::blocks::trigger::LevelTrigger<float32>",                                         //
            "gr::blocks::trigger::MultiChannelRecorder<float32>", "gr::blocks::trigger::SchmittTriggerNoInterpolation<float32>", "gr::blocks::trigger::TimeInterval<float64>", "gr::blocks::type::converter::Convert<float64, float32>",                                                                       //
            "gr::filter::BasicFilterProto<float32, gr::Resampling<1, 1, false>>", "gr::filter::Correlator<float32>", "gr::filter::IQDemodulator<float32, gr::Resampling<1024, 1, false>>", "gr::filter::PhaseUnwrap<float32>", "gr::localisation::PairCrossCorrelation<float32>",                              //
            "gr::localisation::PairCrossSpectra<float32>", "gr::localisation::PairDelay<float32>", "gr::localisation::PhaseSlopeDelay<float32>", "gr::localisation::SourceFromDelays<float32>", "gr::present::blocks::Deinterleave<float32>", "gr::testing::NullSink<float32>",                                //
            "opendigitizer::Arithmetic<float32>", "opendigitizer::ImPlotSink<float32>", "opendigitizer::ImPlotSink<gr::DataSet<float32>>", "opendigitizer::ImPlotSink<gr::Tensor<float32>>", "opendigitizer::charts::LocalisationChart", "opendigitizer::charts::TensorRowsChart"};
        for (const std::string_view id : kNamed) {
            expect(gr::globalBlockRegistry().create(id, {}) != nullptr) << id << " is not a block the registry builds";
        }
    };

    // Ground truth: one block of each GR4 library the workflows above name none of, as the registry spells it
    "a block of every GR4 block library can be built, not only those the shipped workflows name"_test = [] {
        constexpr std::array kOnePerLibrary{"gr::analog::FMDemodulator<float32>", "gr::digital::SymbolSync<float32>", "gr::beamforming::ConventionalBeamformer<float32, gr::beamforming::Constraint::conventional>", //
            "gr::blocks::measurement::Frequency<float32, gr::blocks::measurement::Quantity::frequency>", "gr::electrical::PowerFactor<float32, 1>", "gr::localisation::SteeredPowerMap<float32>"};
        for (const std::string_view id : kOnePerLibrary) {
            expect(gr::globalBlockRegistry().create(id, {}) != nullptr) << id << " is not a block the registry builds";
        }
    };

    return 0;
}
