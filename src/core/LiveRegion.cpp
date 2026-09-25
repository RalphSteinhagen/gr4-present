#include <gr4-present/LiveRegion.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
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

} // namespace gr::present
