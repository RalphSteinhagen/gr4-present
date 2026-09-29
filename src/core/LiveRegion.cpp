#include <gr4-present/LiveRegion.hpp>
#include <gr4-present/Markdown.hpp>
#include <gr4-present/Number.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>
#include <utility>

namespace gr::present {

std::string withPackageUris(std::string_view workflow, std::string_view packageBase) {
    constexpr std::string_view kKey = "uri:";
    const auto                 base = packageBase.ends_with('/') ? packageBase.substr(0UZ, packageBase.size() - 1UZ) : packageBase;
    std::string                resolved;
    resolved.reserve(workflow.size());
    for (const auto lineRange : workflow | std::views::split('\n')) {
        std::string_view line{lineRange.begin(), lineRange.end()};
        const auto       keyAt = line.find_first_not_of(" \t");
        if (!resolved.empty() || lineRange.begin() != workflow.begin()) {
            resolved += '\n';
        }
        if (keyAt == std::string_view::npos || !line.substr(keyAt).starts_with(kKey)) {
            resolved += line;
            continue;
        }
        std::string_view value = line.substr(keyAt + kKey.size());
        value.remove_prefix(std::min(value.find_first_not_of(" \t"), value.size()));
        value.remove_suffix(value.size() - std::min(value.find_last_not_of(" \t\r") + 1UZ, value.size()));
        if (value.size() >= 2UZ && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1UZ, value.size() - 2UZ);
        }
        const bool relative = !value.empty() && !value.starts_with('/') && value.find(':') == std::string_view::npos;
        if (!relative) {
            resolved += line;
            continue;
        }
        resolved += std::format("{}{} \"{}/{}\"", line.substr(0UZ, keyAt), kKey, base, value);
    }
    return resolved;
}

LiveRegion liveRegionFrom(const std::vector<std::pair<std::string, std::string>>& fields) {
    const auto valueOf = [&fields](std::string_view key) { return std::string{valueFor(fields, key).value_or(std::string_view{})}; };

    LiveRegion binding{
        .workflow = valueOf("workflow"),
        .widget   = valueOf("widget"),
        .region   = valueOf("region"),
        .fallback = valueOf("fallback"),
        .standby  = valueOf("standby"),
        .needs    = valueOf("needs"),
        .toolbar  = valueOf("toolbar"),
        .status   = valueOf("status"),
    };
    if (const std::string wait = valueOf("export_wait"); !wait.empty()) {
        binding.exportWait = parseSeconds(wait).value_or(-1.0f);
    }
    binding.legend = parseChartLegend(valueOf("legend")); // a position that names none is reported where the deck is bound
    return binding;
}

} // namespace gr::present
