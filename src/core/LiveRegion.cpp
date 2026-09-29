#include <gr4-present/LiveRegion.hpp>
#include <gr4-present/Number.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <ranges>
#include <utility>

namespace gr::present {

namespace {
constexpr std::array<std::pair<LifecyclePolicy, std::string_view>, 7> kLifecycleNames{{
    {LifecyclePolicy::preload, "preload"},
    {LifecyclePolicy::lazy, "lazy"},
    {LifecyclePolicy::startOnEnter, "start-on-enter"},
    {LifecyclePolicy::runWhileVisible, "run-while-visible"},
    {LifecyclePolicy::keepAlive, "keep-alive"},
    {LifecyclePolicy::resetOnEnter, "reset-on-enter"},
    {LifecyclePolicy::persistent, "persistent"},
}};

template<typename Enum, std::size_t nEntries>
[[nodiscard]] constexpr std::optional<Enum> valueNamed(const std::array<std::pair<Enum, std::string_view>, nEntries>& table, std::string_view name) noexcept {
    const auto entry = std::ranges::find(table, name, &std::pair<Enum, std::string_view>::second);
    return entry == table.cend() ? std::nullopt : std::optional{entry->first};
}
} // namespace

std::optional<LifecyclePolicy> parseLifecyclePolicy(std::string_view policyName) noexcept { return valueNamed(kLifecycleNames, policyName); }

std::string_view name(LifecyclePolicy policy) noexcept {
    const auto entry = std::ranges::find(kLifecycleNames, policy, &std::pair<LifecyclePolicy, std::string_view>::first);
    return entry == kLifecycleNames.cend() ? std::string_view{} : entry->second;
}

std::optional<gr::UICategory> parseUICategory(std::string_view categoryName) noexcept { return valueNamed(gr::meta::detail::EnumTraits<gr::UICategory>::entries, categoryName); }

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

LiveRegion liveRegionFrom(const std::vector<std::pair<std::string, std::string>>& fields, std::size_t step) {
    const auto valueOf = [&fields](std::string_view key) -> std::string {
        const auto entry = std::ranges::find(fields, key, &std::pair<std::string, std::string>::first);
        return entry == fields.end() ? std::string{} : entry->second;
    };

    LiveRegion binding{.workflow = valueOf("workflow"), .widget = valueOf("widget"), .category = parseUICategory(valueOf("category")), .region = valueOf("region"), .fallback = valueOf("fallback"), .step = step, .lifecycle = LifecyclePolicy::lazy, .mode = RenderMode::live, .standby = valueOf("standby"), .needs = valueOf("needs"), .toolbar = valueOf("toolbar"), .status = valueOf("status")};
    if (const std::string wait = valueOf("export_wait"); !wait.empty()) {
        binding.exportWait = parseSeconds(wait).value_or(-1.0f);
    }
    binding.legend = parseChartLegend(valueOf("legend")); // a position that names none is reported where the deck is bound
    if (const auto policy = parseLifecyclePolicy(valueOf("lifecycle"))) {
        binding.lifecycle = *policy;
    }
    return binding;
}

} // namespace gr::present
