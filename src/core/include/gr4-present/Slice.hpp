#ifndef GR4_PRESENT_SLICE_HPP
#define GR4_PRESENT_SLICE_HPP

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace gr::present {

/// `text.substr(from, count)` for `noexcept` code: equal to it inside the text, an empty view for a start past the end
/// where `substr` throws `std::out_of_range`
[[nodiscard]] constexpr std::string_view slice(std::string_view text, std::size_t from, std::size_t count = std::string_view::npos) noexcept {
    if (from > text.size()) {
        return {};
    }
    return {text.data() + from, std::min(count, text.size() - from)};
}

} // namespace gr::present

#endif // GR4_PRESENT_SLICE_HPP
