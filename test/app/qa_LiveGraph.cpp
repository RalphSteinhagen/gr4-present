#include "LiveGraph.hpp"

#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <boost/ut.hpp>

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
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

/// waits, with a deadline rather than for a fixed length: a graph changes state on its own thread, and how long
/// that takes is the machine's business
[[nodiscard]] bool reaches(LiveGraph& graph, bool running) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (graph.running() != running && std::chrono::steady_clock::now() < deadline) {
        graph.pump(); // what a viewer does every frame, and what carries a graph through a lifecycle transition
        std::this_thread::yield();
    }
    return graph.running() == running;
}

[[nodiscard]] bool holds(const std::vector<std::string>& names, std::string_view wanted) { return std::ranges::find(names, wanted) != names.end(); }

} // namespace

/**
 * These run inside `main`, not from a `boost::ut::suite`.
 *
 * A suite is run from a static destructor, and a dashboard reads the ImGui and ImPlot styles as it is built and
 * takes its colours from a process-wide palette. After `main` returns those are gone, so building one there
 * reads torn-down state: it reported registered palettes as missing and aborted on a double free, which was read
 * as a defect in OpenDigitizer and reported as one. It was not. The rule is written here rather than remembered.
 *
 * Nothing here draws. Drawing needs a window already begun and an atlas the backend has uploaded; what is drawn
 * is checked in a browser instead.
 */
int main() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::GetIO().Fonts->AddFontDefault();
    std::ignore                = ImGui::GetIO().Fonts->Build();
    ImGui::GetIO().DisplaySize = ImVec2{1280.0f, 720.0f};
    prepareLiveGraphs();

    // The chart names are the `plots:` of each file's own `dashboard:` section, which came from OpenDigitizer's
    // sample dashboards unchanged -- a ground truth this project did not write -- but for the spectrum beside
    // controlled-sines, which this deck added.
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

    "the graph's own blocks are not offered as charts"_test = [] {
        // what draws is the chart the dashboard section declares, not the sinks feeding it or the sources behind
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

        expect(reaches(graph, true)) << "the graph never reached the state in which its blocks do work";
    };

    // A deck runs the slide it shows and idles the one it will advance to, so every graph but the first is idled
    // before it is ever seen. Idling releases the devices its sources hold, which is a stop; a stop is a one-way
    // door in their scheduler -- the loop exits, and the message `start()` sends has nobody left to read it -- and
    // the slide used to arrive to charts that stayed empty.
    "a graph the deck idles runs again when its slide arrives"_test = [] {
        LiveGraph graph{contentsOf("controlled-sines.grc")};
        expect(graph.problem().empty()) << graph.problem();
        expect(reaches(graph, true)) << "the graph never ran to begin with";

        graph.setRunning(false);
        expect(reaches(graph, false)) << "the graph kept running after the deck idled it";

        graph.setRunning(true);
        expect(reaches(graph, true)) << "an idled graph never ran again";
    };

    // Quick stepping through the live slides destroys graphs in whatever state the last key left them: running, on
    // the way out, stopped but for the thread still leaving it, or restarting. Each of them has to be destroyable
    // without waiting for something that no longer comes.
    // A stopped graph holds no pool task: the browser has 16 threads in all, and a task left behind by every stop
    // fills them after a few slides, which froze the deck. Ground truth is the pools before the graph ever ran.
    "starting and stopping a graph again and again leaves no pool task behind"_test = [] {
        using gr::thread_pool::Manager;
        const auto busy      = [] { return Manager::defaultIoPool()->numTasksRunning() + Manager::defaultCpuPool()->numTasksRunning(); };
        const auto settlesTo = [&busy](std::size_t wanted) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (busy() > wanted && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
            return busy();
        };
        const std::size_t before = settlesTo(0UZ);
        LiveGraph         graph{contentsOf("controlled-sines.grc")};
        for (int cycle = 0; cycle < 20; ++cycle) {
            graph.setRunning(true);
            expect(reaches(graph, true)) << "cycle " << cycle << ": the graph starts";
            graph.setRunning(false);
            expect(reaches(graph, false)) << "cycle " << cycle << ": the graph stops";
            const std::size_t after = settlesTo(before);
            expect(eq(after, before)) << "cycle " << cycle << ": tasks still running on the default pools after the stop, " << after << " against " << before << " before the graph first ran";
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
                        graph.pump();
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    if (phase.pumpsBeforeStop > 0 || phase.pumpsAfterStop > 0) {
                        graph.setRunning(false);
                    }
                    for (int pump = 0; pump < phase.pumpsAfterStop; ++pump) {
                        graph.pump();
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
                // said before leaving, because the test runner reports at exit and exit would wait for the very thing
                // that hangs; aborting leaves a core and lets a debugger show where every thread is
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
        // their loader logs the block it could not build rather than throwing, so what the viewer can promise is
        // that a region drawing nothing says so; which block it was is in the log beside it
        expect(!graph.problem().empty()) << "a workflow naming an unknown block was accepted";
        expect(graph.charts().empty());
    };

    return 0;
}
