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
    constexpr std::string_view kKey    = "uri:";
    constexpr std::string_view kBlanks = " \t";
    const auto                 base    = packageBase.ends_with('/') ? packageBase.substr(0UZ, packageBase.size() - 1UZ) : packageBase;
    std::string                resolved;
    resolved.reserve(workflow.size());
    std::size_t copied = 0UZ;
    for (std::size_t at = workflow.find(kKey); at != std::string_view::npos; at = workflow.find(kKey, at + kKey.size())) {
        // a key where a key can stand: first on its line or list item, or after a flow map's `{` or `,`
        const std::size_t before = at == 0UZ ? std::string_view::npos : workflow.find_last_not_of(kBlanks, at - 1UZ);
        const char        lead   = before == std::string_view::npos ? '\n' : workflow[before];
        const bool        inFlow = lead == '{' || lead == ',';
        if (!inFlow && lead != '\n' && !(lead == '-' && before + 1UZ < at)) {
            continue;
        }
        std::size_t      from = std::min(workflow.find_first_not_of(kBlanks, at + kKey.size()), workflow.size());
        std::size_t      to   = 0UZ;
        std::string_view value;
        if (from < workflow.size() && (workflow[from] == '"' || workflow[from] == '\'')) {
            to = workflow.find(workflow[from], from + 1UZ);
            if (to == std::string_view::npos) {
                continue;
            }
            value = workflow.substr(from + 1UZ, to - from - 1UZ);
            ++to;
        } else {
            to    = std::min(workflow.find_first_of(inFlow ? ",}\n" : "\n", from), workflow.size());
            to    = std::max(from, std::min(workflow.find_last_not_of(" \t\r", to - 1UZ) + 1UZ, to));
            value = workflow.substr(from, to - from);
        }
        const bool relative = !value.empty() && !value.starts_with('/') && value.find(':') == std::string_view::npos;
        if (relative) {
            resolved += workflow.substr(copied, from - copied);
            resolved += std::format("\"{}/{}\"", base, value);
            copied = to;
        }
    }
    resolved += workflow.substr(copied);
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
