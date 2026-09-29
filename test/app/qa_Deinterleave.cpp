#include "blocks/Deinterleave.hpp"

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/StreamToDataSet.hpp>
#include <gnuradio-4.0/fileio/WavBlocks.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>
#include <gnuradio-4.0/trigger/SchmittTrigger.hpp>

#include <boost/ut.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

using namespace boost::ut;

namespace {
using gr::testing::ProcessFunction;
using gr::testing::TagSink;
using gr::testing::TagSource;
} // namespace

const suite<"Deinterleave"> deinterleaveTests = [] {
    // Ground truth: samples 0..11 as six stereo frames split into evens (left) and odds (right), one channel each
    "six stereo frames become six mono samples per channel, at the frame rate"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<TagSource<float, ProcessFunction::USE_PROCESS_BULK>>({{"n_samples_max", gr::Size_t{12}}, {"sample_rate", 1000.0f}, {"mark_tag", false}, {"verbose_console", false}});
        source._tags     = {{0UZ, gr::property_map{{gr::tag::SAMPLE_RATE.shortKey(), 1000.0f}, {gr::tag::NUM_CHANNELS.key(), gr::Size_t{2U}}}}};
        auto& split      = graph.emplaceBlock<gr::present::blocks::Deinterleave<float>>({{"n_channels", gr::Size_t{2}}});
        auto& left       = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", true}, {"verbose_console", false}});
        auto& right      = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", true}, {"verbose_console", false}});
        expect(fatal(graph.connect<"out", "in">(source, split).has_value()));
        expect(fatal(graph.connect(split, "out#0", left, "in").has_value()));
        expect(fatal(graph.connect(split, "out#1", right, "in").has_value()));

        gr::scheduler::Simple scheduler;
        expect(fatal(scheduler.exchange(std::move(graph)).has_value()));
        expect(scheduler.runAndWait().has_value());

        const std::vector<float> evens{0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f};
        const std::vector<float> odds{1.0f, 3.0f, 5.0f, 7.0f, 9.0f, 11.0f};
        expect(std::ranges::equal(left._samples, evens)) << "the left channel is every first sample of a frame";
        expect(std::ranges::equal(right._samples, odds)) << "the right channel every second";
        expect(eq(left.sample_rate, 1000.0f) && eq(right.sample_rate, 1000.0f)) << "each channel keeps the frame rate, " << left.sample_rate;
        const auto channelCountSeen = [](const auto& sink) {
            const auto stated = std::ranges::find_if(sink._tags, [](const auto& logged) { return logged.map.contains(gr::tag::NUM_CHANNELS.key()); });
            return stated == sink._tags.end() ? gr::Size_t{0U} : stated->map.template value_or<gr::Size_t>(gr::tag::NUM_CHANNELS.key(), gr::Size_t{0U});
        };
        expect(eq(channelCountSeen(left), gr::Size_t{1U}) && eq(channelCountSeen(right), gr::Size_t{1U})) << "each channel is one channel";
    };

    // Ground truth: devtools/make-clap.mjs -- one 0.8 FS clap at 0.1 s over 0.001 FS noise; 0.5 s hold-off
    "the recorded clap makes one snapshot on the left channel"_test = [] {
        constexpr std::size_t kPre  = 2400UZ;
        constexpr std::size_t kPost = 21600UZ;
        gr::Graph             graph;
        auto&                 recording = graph.emplaceBlock<gr::blocks::fileio::WavSource<float>>({{"uri", std::pmr::string{GR4_PRESENT_PACKAGE_DIRECTORY "/media/clap.wav"}}});
        auto&                 channels  = graph.emplaceBlock<gr::present::blocks::Deinterleave<float>>({{"n_channels", gr::Size_t{2}}});
        auto&                 trigger   = graph.emplaceBlock<gr::blocks::trigger::SchmittTrigger<float, gr::trigger::InterpolationMethod::NO_INTERPOLATION>>({{"threshold", 0.1f}});
        auto&                 snapshot  = graph.emplaceBlock<gr::basic::StreamToDataSet<float>>({{"filter", std::pmr::string{"RISING"}}, {"n_pre", static_cast<gr::Size_t>(kPre)}, {"n_post", static_cast<gr::Size_t>(kPost)}, {"holdoff_samples", gr::Size_t{24000U}}});
        auto&                 sink      = graph.emplaceBlock<TagSink<gr::DataSet<float>, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", true}, {"log_tags", false}, {"verbose_console", false}});
        auto&                 right     = graph.emplaceBlock<TagSink<float, ProcessFunction::USE_PROCESS_BULK>>({{"log_samples", false}, {"log_tags", false}, {"verbose_console", false}});
        expect(fatal(graph.connect<"out", "in">(recording, channels).has_value()));
        expect(fatal(graph.connect(channels, "out#0", trigger, "in").has_value()));
        expect(fatal(graph.connect(channels, "out#1", right, "in").has_value()));
        expect(fatal(graph.connect<"out", "in">(trigger, snapshot).has_value()));
        expect(fatal(graph.connect<"out", "in">(snapshot, sink).has_value()));

        gr::scheduler::Simple scheduler;
        expect(fatal(scheduler.exchange(std::move(graph)).has_value()));
        expect(scheduler.runAndWait().has_value());

        expect(fatal(eq(sink._samples.size(), 1UZ))) << "one clap, one snapshot";
        const auto values = sink._samples.front().signalValues(0UZ);
        expect(fatal(eq(values.size(), kPre + kPost)));
        const auto loudest = [](std::span<const float> part) { return std::ranges::max(part | std::views::transform([](float sample) { return std::abs(sample); })); };
        expect(lt(loudest(values.first(kPre - 480UZ)), 0.01f)) << "room noise until 10 ms before the trigger";
        expect(gt(loudest(values.subspan(kPre, 4800UZ)), 0.5f)) << "the clap in the 0.1 s after it";
        const auto& time = sink._samples.front().axis_values[0UZ];
        expect(fatal(eq(time.size(), kPre + kPost)));
        expect(approx(time.front(), -0.05f, 1e-6f)) << "the snapshot starts 0.05 s before the trigger";
        expect(approx(time.back(), 0.45f - 1.0f / 48000.0f, 1e-5f)) << "and ends 0.45 s after it";
    };
};

int main() { return 0; }
