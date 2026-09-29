#ifndef GR4_PRESENT_BLOCKS_DEINTERLEAVE_HPP
#define GR4_PRESENT_BLOCKS_DEINTERLEAVE_HPP

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

namespace gr::present::blocks {

/// One stream per channel from a stream whose frames interleave them, `L R L R ...` as a sound card delivers stereo.
/// Its tags go to every channel at their frame with the rate unchanged: a sound card states the frame rate, which is
/// each channel's rate, and GR4's own forwarding would divide it by the channel count as it does for a decimator. A
/// channel count among them becomes 1, which each output is.
template<typename T>
struct Deinterleave : gr::Block<Deinterleave<T>, gr::Resampling<>, gr::NoTagPropagation> {
    using Description = gr::Doc<"splits a stream of interleaved frames -- L R L R ... -- into one stream per channel, each at the frame rate">;

    gr::PortIn<T>               in;
    std::vector<gr::PortOut<T>> out;

    gr::Annotated<gr::Size_t, "n_channels", gr::Doc<"channels per frame">, gr::Visible, gr::Limits<1U, 64U>> n_channels = 2U;

    GR_MAKE_REFLECTABLE(Deinterleave, in, out, n_channels);

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& /*newSettings*/) {
        out.resize(static_cast<std::size_t>(n_channels));
        this->input_chunk_size  = n_channels; // a whole frame in, one sample out on every channel
        this->output_chunk_size = 1U;
    }

    template<typename TOutputSpan>
    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& input, std::span<TOutputSpan>& outputs) noexcept {
        const std::size_t channels = outputs.size();
        if (channels == 0UZ) {
            return gr::work::Status::OK;
        }
        std::size_t frames = input.size() / channels;
        for (const auto& channel : outputs) {
            frames = std::min(frames, channel.size());
        }
        for (std::size_t frame = 0UZ; frame < frames; ++frame) {
            for (std::size_t channel = 0UZ; channel < channels; ++channel) {
                outputs[channel][frame] = input[frame * channels + channel];
            }
        }
        for (const auto& tag : input.rawTags()) {
            if (tag.index < input.streamIndex || tag.index >= input.streamIndex + frames * channels) {
                continue;
            }
            gr::property_map perChannel = tag.map;
            for (const std::string_view channelCount : {std::string_view{gr::tag::NUM_CHANNELS.key()}, std::string_view{gr::tag::NUM_CHANNELS.shortKey()}}) {
                if (perChannel.contains(channelCount)) {
                    perChannel.insert_or_assign(channelCount, gr::Size_t{1U});
                }
            }
            for (auto& channel : outputs) {
                channel.publishTag(perChannel, static_cast<std::size_t>(tag.index - input.streamIndex) / channels);
            }
        }
        return gr::work::Status::OK;
    }
};

} // namespace gr::present::blocks

#endif // GR4_PRESENT_BLOCKS_DEINTERLEAVE_HPP
