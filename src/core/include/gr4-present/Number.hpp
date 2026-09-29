#ifndef GR4_PRESENT_NUMBER_HPP
#define GR4_PRESENT_NUMBER_HPP

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>

namespace gr::present {

/// the whole of `text` as a number of type `T`; nothing when any of it, a blank included, is not part of the number
template<typename T>
[[nodiscard]] std::optional<T> parseNumber(std::string_view text) noexcept {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// a duration as an author writes it -- `2`, `0.5` or `3s` -- in seconds and not negative, or nothing
[[nodiscard]] inline std::optional<float> parseSeconds(std::string_view text) noexcept {
    if (text.ends_with('s')) {
        text.remove_suffix(1UZ);
    }
    const std::optional<float> seconds = parseNumber<float>(text);
    return seconds && *seconds >= 0.0f ? seconds : std::nullopt;
}

} // namespace gr::present

#endif // GR4_PRESENT_NUMBER_HPP
