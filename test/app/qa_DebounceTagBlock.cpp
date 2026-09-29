#include "blocks/DebounceTagBlock.hpp"

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>
#include <gnuradio-4.0/trigger/SchmittTrigger.hpp>

#include <boost/ut.hpp>

#include <algorithm>
#include <vector>

using namespace boost::ut;

namespace {
using gr::testing::ProcessFunction;
using gr::testing::TagSink;
using gr::testing::TagSource;

gr::property_map triggerTag(std::string_view name) { return {{std::string(gr::tag::TRIGGER_NAME.key()), std::string(name)}}; }
} // namespace

const suite<"DebounceTagBlock"> debounceTagBlockTests = [] {
    "passes samples, unrelated tags, and the first activation tag"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<TagSource<float, ProcessFunction::USE_PROCESS_BULK>>({{"n_samples_max", gr::Size_t{5U}}, {"values", std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}}, {"mark_tag", false}, {"verbose_console", false}});
        source._tags     = {{1UZ, triggerTag("other")}, {2UZ, triggerTag("RISING")}};
        auto& debounce   = graph.emplaceBlock<gr::present::blocks::DebounceTagBlock<float>>({{"activation_tag", std::string("RISING")}, {"dead_time_samples", gr::Size_t{3U}}});
        auto& sink       = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", true}, {"verbose_console", false}});
        expect(fatal(graph.connect<"out", "in">(source, debounce).has_value()));
        expect(fatal(graph.connect<"out", "in">(debounce, sink).has_value()));

        gr::scheduler::Simple scheduler;
        expect(fatal(scheduler.exchange(std::move(graph)).has_value()));
        expect(scheduler.runAndWait().has_value());
        expect(std::ranges::equal(sink._samples, std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f}));
        expect(eq(sink._tags.size(), 2UZ)) << "the unrelated tag and first activation tag pass";
        expect(eq(debounce.n_accepted, gr::Size_t{1U}));
    };

    "suppresses activation tags inside the dead time and accepts the boundary"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<TagSource<float, ProcessFunction::USE_PROCESS_BULK>>({{"n_samples_max", gr::Size_t{16U}}, {"values", std::vector<float>(12UZ, 0.f)}, {"mark_tag", false}, {"verbose_console", false}});
        source._tags     = {{1UZ, triggerTag("RISING")}, {3UZ, triggerTag("RISING")}, {6UZ, triggerTag("RISING")}, {7UZ, triggerTag("RISING")}, {12UZ, triggerTag("RISING")}};
        auto& debounce   = graph.emplaceBlock<gr::present::blocks::DebounceTagBlock<float>>({{"dead_time_samples", gr::Size_t{5U}}});
        auto& sink       = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", true}, {"verbose_console", false}});
        expect(fatal(graph.connect<"out", "in">(source, debounce).has_value()));
        expect(fatal(graph.connect<"out", "in">(debounce, sink).has_value()));

        gr::scheduler::Simple scheduler;
        expect(fatal(scheduler.exchange(std::move(graph)).has_value()));
        expect(scheduler.runAndWait().has_value());

        std::vector<std::size_t> indices;
        for (const auto& logged : sink._tags) {
            if (logged.map.contains(gr::tag::TRIGGER_NAME.key())) {
                indices.push_back(logged.index);
            }
        }
        expect(std::ranges::equal(indices, std::vector<std::size_t>{1UZ, 6UZ, 12UZ}));
        expect(eq(debounce.n_accepted, gr::Size_t{3U}));
        expect(eq(debounce.n_suppressed, gr::Size_t{2U}));
    };

    "SchmittTrigger dead time scans every sample and suppresses nearby edges"_test = [] {
        std::vector<float> samples(32UZ);
        for (std::size_t i = 0UZ; i < samples.size(); ++i) {
            samples[i] = (i / 4UZ) % 2UZ == 0UZ ? 0.f : 5.f;
        }

        gr::Graph graph;
        auto&     source  = graph.emplaceBlock<TagSource<float, ProcessFunction::USE_PROCESS_BULK>>({{"n_samples_max", gr::Size_t{32U}}, {"values", samples}, {"mark_tag", false}, {"verbose_console", false}});
        auto&     trigger = graph.emplaceBlock<gr::blocks::trigger::SchmittTrigger<float, gr::trigger::InterpolationMethod::NO_INTERPOLATION>>({{"threshold", 2.f}, {"offset", 2.5f}, {"n_dead_time", gr::Size_t{6U}}});
        auto&     sink    = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", true}, {"verbose_console", false}});
        expect(fatal(graph.connect<"out", "in">(source, trigger).has_value()));
        expect(fatal(graph.connect<"out", "in">(trigger, sink).has_value()));

        gr::scheduler::Simple scheduler;
        expect(fatal(scheduler.exchange(std::move(graph)).has_value()));
        expect(scheduler.runAndWait().has_value());
        expect(std::ranges::equal(sink._samples, samples));

        std::vector<std::size_t> risingEdges;
        for (const auto& logged : sink._tags) {
            if (logged.map.template get_if<std::string_view>(gr::tag::TRIGGER_NAME.key()).value_or("") == "RISING") {
                risingEdges.push_back(logged.index);
            }
        }
        expect(std::ranges::equal(risingEdges, std::vector<std::size_t>{4UZ, 12UZ, 20UZ, 28UZ}));
    };
};

int main() { return 0; }
