#include <gr4-present/LaunchOptions.hpp>

#include <algorithm>
#include <cctype>
#include <ranges>

namespace gr::present {

namespace {

[[nodiscard]] std::string lowered(std::string_view text) {
    std::string result{text};
    std::ranges::transform(result, result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

[[nodiscard]] std::string_view withoutLeading(std::string_view text, std::string_view prefixes) { return !text.empty() && prefixes.find(text.front()) != std::string_view::npos ? text.substr(1UZ) : text; }

} // namespace

void LaunchOptions::set(std::string_view key, std::string_view value) { entries.emplace_back(lowered(key), std::string{value}); }

std::optional<std::string_view> LaunchOptions::value(std::string_view key) const noexcept {
    const std::string wanted = lowered(key);
    const auto        entry  = std::ranges::find_if(entries | std::views::reverse, [&wanted](const auto& candidate) { return candidate.first == wanted; });
    return entry == (entries | std::views::reverse).end() ? std::nullopt : std::optional{std::string_view{entry->second}};
}

void LaunchOptions::parseUrlParameters(LaunchOptions& into, std::string_view parameters) {
    for (const auto field : withoutLeading(parameters, "?#") | std::views::split('&')) {
        const std::string_view pair{field};
        if (pair.empty()) {
            continue;
        }
        const auto separator = pair.find('=');
        if (separator == std::string_view::npos) {
            into.set(pair, "");
        } else {
            into.set(pair.substr(0UZ, separator), pair.substr(separator + 1UZ));
        }
    }
}

void LaunchOptions::parseCommandLine(LaunchOptions& into, std::span<const std::string_view> arguments) {
    for (std::size_t index = 0UZ; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (!argument.starts_with("--")) {
            continue;
        }
        const std::string_view body = argument.substr(2UZ);
        if (const auto separator = body.find('='); separator != std::string_view::npos) {
            into.set(body.substr(0UZ, separator), body.substr(separator + 1UZ));
            continue;
        }

        const std::string name = lowered(body);
        if (name == "fullscreen" || name == "windowed" || name == "window") {
            into.set("mode", name == "fullscreen" ? "fullscreen" : "windowed");
            continue;
        }
        // `--key value`, but only when the next argument is not itself an option
        const bool valueFollows = index + 1UZ < arguments.size() && !arguments[index + 1UZ].starts_with("--");
        into.set(body, valueFollows ? arguments[++index] : std::string_view{});
    }
}

LaunchOptions LaunchOptions::from(std::string_view query, std::string_view fragment, std::span<const std::string_view> arguments) {
    LaunchOptions options;
    parseUrlParameters(options, query);
    parseUrlParameters(options, fragment);
    parseCommandLine(options, arguments);
    return options;
}

WindowMode LaunchOptions::windowMode() const noexcept {
    const auto requested = value("mode");
    return requested && lowered(*requested) == "windowed" ? WindowMode::windowed : WindowMode::fullscreen;
}

} // namespace gr::present
