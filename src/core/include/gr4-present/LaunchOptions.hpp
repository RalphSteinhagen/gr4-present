#ifndef GR4_PRESENT_LAUNCH_OPTIONS_HPP
#define GR4_PRESENT_LAUNCH_OPTIONS_HPP

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/**
 * Launch parameters, from the command line natively and from the URL in the browser.
 *
 * The same presentation is opened both ways, so both spellings feed one store and the application never asks where a
 * value came from. A query string is part of a shared link and a fragment is what a viewer edits for this one visit,
 * so the fragment overrides the query; an explicit command line overrides both. Keys and the values of enumerated
 * parameters are matched case-insensitively, because a URL typed by hand is not reliably lower case.
 */
enum class WindowMode { windowed, fullscreen };

struct LaunchOptions {
    std::vector<std::pair<std::string, std::string>> entries; // insertion order: later wins

    /// `key=value` and `key` pairs from a `?query` or `#fragment`, with or without the leading `?`/`#`
    static void parseUrlParameters(LaunchOptions& into, std::string_view parameters);

    /// `--key=value`, `--key value`, and the bare `--windowed` / `--fullscreen` switches
    static void parseCommandLine(LaunchOptions& into, std::span<const std::string_view> arguments);

    /// query first, then fragment, then command line, so that each overrides the one before
    [[nodiscard]] static LaunchOptions from(std::string_view query, std::string_view fragment, std::span<const std::string_view> arguments);

    [[nodiscard]] std::optional<std::string_view> value(std::string_view key) const noexcept;
    [[nodiscard]] bool                            contains(std::string_view key) const noexcept { return value(key).has_value(); }
    void                                          set(std::string_view key, std::string_view value);

    /// `mode=windowed|fullscreen`; fullscreen unless the viewer asked otherwise
    [[nodiscard]] WindowMode windowMode() const noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_LAUNCH_OPTIONS_HPP
