#ifndef GR4_PRESENT_BLOCKS_PEAK_DECIMATOR_HPP
#define GR4_PRESENT_BLOCKS_PEAK_DECIMATOR_HPP

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <cstddef>
#include <span>

namespace gr::present::blocks {

/// Each block of `block_size` samples as its smallest and its largest, in the order they came, as a peak meter or a
/// scope's peak-detect mode keeps them: a trace drawn from them has the envelope of every sample, at a fraction of the
/// points. GR4 scales the stream's sample rate by 2 / `block_size`, so its time axis stays true.
template<typename T>
struct PeakDecimator : gr::Block<PeakDecimator<T>, gr::Resampling<>> {
    using Description = gr::Doc<"each block of block_size samples as its minimum and maximum, in the order they occurred">;

    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    gr::Annotated<gr::Size_t, "block_size", gr::Doc<"samples per minimum-maximum pair">, gr::Visible, gr::Limits<2U, 1U << 20U>> block_size = 100U;

    GR_MAKE_REFLECTABLE(PeakDecimator, in, out, block_size);

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& /*newSettings*/) {
        this->input_chunk_size  = block_size;
        this->output_chunk_size = 2U;
    }

    [[nodiscard]] gr::work::Status processBulk(std::span<const T> input, std::span<T> output) noexcept {
        const std::size_t blockSamples = block_size;
        const std::size_t blocks       = std::min(input.size() / blockSamples, output.size() / 2UZ);
        for (std::size_t block = 0UZ; block < blocks; ++block) {
            const std::span<const T> samples = input.subspan(block * blockSamples, blockSamples);
            const auto [smallest, largest]   = std::ranges::minmax_element(samples);
            const bool smallestFirst         = smallest <= largest;
            output[2UZ * block]              = smallestFirst ? *smallest : *largest;
            output[2UZ * block + 1UZ]        = smallestFirst ? *largest : *smallest;
        }
        return gr::work::Status::OK;
    }
};

} // namespace gr::present::blocks

#endif // GR4_PRESENT_BLOCKS_PEAK_DECIMATOR_HPP
