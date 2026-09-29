#ifndef GR4_PRESENT_BLOCKS_DEBOUNCE_TAG_BLOCK_HPP
#define GR4_PRESENT_BLOCKS_DEBOUNCE_TAG_BLOCK_HPP

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

namespace gr::present::blocks {

template<typename T>
struct DebounceTagBlock : gr::Block<DebounceTagBlock<T>, gr::NoTagPropagation> {
    using Description = gr::Doc<"passes samples through while debouncing trigger tags by a sample dead time">;

    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    gr::Annotated<std::pmr::string, "activation tag", gr::Doc<"trigger_name value to debounce">>                         activation_tag    = "RISING";
    gr::Annotated<gr::Size_t, "dead time samples", gr::Doc<"minimum sample distance between forwarded activation tags">> dead_time_samples = 0U;
    gr::Annotated<gr::Size_t, "accepted tags", gr::Doc<"number of activation tags forwarded">>                           n_accepted        = 0U;
    gr::Annotated<gr::Size_t, "suppressed tags", gr::Doc<"number of activation tags suppressed">>                        n_suppressed      = 0U;

    GR_MAKE_REFLECTABLE(DebounceTagBlock, in, out, activation_tag, dead_time_samples, n_accepted, n_suppressed);

    bool        _hasAcceptedTag    = false;
    std::size_t _lastAcceptedIndex = 0UZ;

    template<typename TInputSpans, typename TOutputSpans>
    void forwardTags(TInputSpans&, TOutputSpans&, std::size_t) noexcept {}

    [[nodiscard]] bool isActivationTag(const gr::Tag& tag) const noexcept {
        const auto name = tag.map.template get_if<std::string_view>(gr::tag::TRIGGER_NAME.key());
        return name.has_value() && *name == std::string_view{activation_tag.value};
    }

    [[nodiscard]] bool accepts(std::size_t index) noexcept {
        if (!_hasAcceptedTag || index < _lastAcceptedIndex || index - _lastAcceptedIndex >= static_cast<std::size_t>(dead_time_samples)) {
            _hasAcceptedTag    = true;
            _lastAcceptedIndex = index;
            n_accepted         = n_accepted + 1U;
            return true;
        }
        n_suppressed = n_suppressed + 1U;
        return false;
    }

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& input, gr::OutputSpanLike auto& output) noexcept {
        const std::size_t nSamples = std::min(input.size(), output.size());
        if (nSamples == 0UZ) {
            return gr::work::Status::INSUFFICIENT_INPUT_ITEMS;
        }

        std::copy_n(input.begin(), nSamples, output.begin());
        for (const auto& tag : input.rawTags()) {
            if (tag.index < input.streamIndex || tag.index >= input.streamIndex + nSamples) {
                continue;
            }
            const std::size_t offset = tag.index - input.streamIndex;
            if (!isActivationTag(tag) || accepts(tag.index)) {
                output.publishTag(tag.map, offset);
            }
        }
        return gr::work::Status::OK;
    }
};

} // namespace gr::present::blocks

#endif // GR4_PRESENT_BLOCKS_DEBOUNCE_TAG_BLOCK_HPP
