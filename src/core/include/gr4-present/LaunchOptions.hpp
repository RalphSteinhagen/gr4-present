#ifndef GR4_PRESENT_LAUNCH_OPTIONS_HPP
#define GR4_PRESENT_LAUNCH_OPTIONS_HPP

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

enum class WindowMode { windowed, fullscreen };

/// launch parameters from the command line natively and from the URL in the browser, held in one store; keys and
/// enumerated values match case-insensitively
struct LaunchOptions {
    std::vector<std::pair<std::string, std::string>> entries; // insertion order: later wins

    /// `key=value` and `key` pairs from a `?query` or `#fragment`, with or without the leading `?`/`#`
    static void parseUrlParameters(LaunchOptions& into, std::string_view parameters);

    /// `--key=value`, `--key value`, and the bare `--windowed` / `--fullscreen` switches
    static void parseCommandLine(LaunchOptions& into, std::span<const std::string_view> arguments);

    /// query first, then fragment, then command line, so that each overrides the one before
    [[nodiscard]] static LaunchOptions from(std::string_view query, std::string_view fragment, std::span<const std::string_view> arguments);

    [[nodiscard]] std::optional<std::string_view> value(std::string_view key) const noexcept;

    /// the package to open: `presentation=` or `load=`
    [[nodiscard]] std::optional<std::string_view> presentationBase() const noexcept;
    [[nodiscard]] bool                            contains(std::string_view key) const noexcept { return value(key).has_value(); }
    void                                          set(std::string_view key, std::string_view value);

    /// `mode=windowed|fullscreen`; fullscreen unless the viewer asked otherwise
    [[nodiscard]] WindowMode windowMode() const noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_LAUNCH_OPTIONS_HPP
