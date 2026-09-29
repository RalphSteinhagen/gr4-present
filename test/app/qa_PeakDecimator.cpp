#include "blocks/PeakDecimator.hpp"

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>

#include <boost/ut.hpp>

#include <algorithm>
#include <vector>

using namespace boost::ut;

namespace {
using gr::testing::ProcessFunction;
using gr::testing::TagSink;
using gr::testing::TagSource;
} // namespace

const suite<"PeakDecimator"> peakDecimatorTests = [] {
    // Ground truth, the author's example (2026-10-06): 200 samples, all zero but 0.8 at 12, -1.2 at 35, 2.1 at 123 and
    // -0.4 at 178, in blocks of 100. The first block's extremes are 0.8 then -1.2, the second's 2.1 then -0.4, each
    // pair in the order the samples came; 1000 samples per second become 20 pairs' worth, 20 per second.
    "two blocks of a hundred become their extremes in order, at a fiftieth of the rate"_test = [] {
        std::vector<float> samples(200UZ, 0.0f);
        samples[12UZ]  = 0.8f;
        samples[35UZ]  = -1.2f;
        samples[123UZ] = 2.1f;
        samples[178UZ] = -0.4f;

        gr::Graph graph;
        auto&     source = graph.emplaceBlock<TagSource<float, ProcessFunction::USE_PROCESS_BULK>>({{"n_samples_max", gr::Size_t{200U}}, {"values", samples}, {"mark_tag", false}, {"verbose_console", false}});
        source._tags     = {{0UZ, gr::property_map{{gr::tag::SAMPLE_RATE.key(), 1000.0f}}}}; // prefixed, as a source publishes it and GR4 forwards it
        auto& peaks      = graph.emplaceBlock<gr::present::blocks::PeakDecimator<float>>({{"block_size", gr::Size_t{100U}}});
        auto& sink       = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", true}, {"verbose_console", false}});
        expect(fatal(graph.connect<"out", "in">(source, peaks).has_value()));
        expect(fatal(graph.connect<"out", "in">(peaks, sink).has_value()));

        gr::scheduler::Simple scheduler;
        expect(fatal(scheduler.exchange(std::move(graph)).has_value()));
        expect(scheduler.runAndWait().has_value());

        const std::vector<float> expected{0.8f, -1.2f, 2.1f, -0.4f};
        expect(std::ranges::equal(sink._samples, expected)) << "the extremes of each block, in the order they occurred";
        expect(eq(sink.sample_rate, 20.0f)) << "the rate of the pairs, " << sink.sample_rate;
    };

    "a block with one value is that value twice"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<TagSource<float, ProcessFunction::USE_PROCESS_BULK>>({{"n_samples_max", gr::Size_t{4U}}, {"values", std::vector<float>{0.5f}}, {"mark_tag", false}, {"verbose_console", false}});
        auto&     peaks  = graph.emplaceBlock<gr::present::blocks::PeakDecimator<float>>({{"block_size", gr::Size_t{4U}}});
        auto&     sink   = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", false}, {"verbose_console", false}});
        expect(fatal(graph.connect<"out", "in">(source, peaks).has_value()));
        expect(fatal(graph.connect<"out", "in">(peaks, sink).has_value()));
        gr::scheduler::Simple scheduler;
        expect(fatal(scheduler.exchange(std::move(graph)).has_value()));
        expect(scheduler.runAndWait().has_value());
        expect(std::ranges::equal(sink._samples, std::vector<float>{0.5f, 0.5f}));
    };
};

int main() { return 0; }
