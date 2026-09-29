#include "EffectLibrary.hpp"

#include "EmbeddedEffects.hpp"

#include <gnuradio-4.0/Logger.hpp>

#include <algorithm>
#include <format>
#include <ranges>

namespace gr::present {

namespace {

struct BundledEffect {
    std::string_view               name;
    std::span<const unsigned char> text;
};

const std::array<BundledEffect, 9> kBundled{
    BundledEffect{"crt", kEffectCrtGlsl},
    BundledEffect{"cube", kEffectCubeGlsl},
    BundledEffect{"curl", kEffectCurlGlsl},
    BundledEffect{"disintegrate", kEffectDisintegrateGlsl},
    BundledEffect{"flip", kEffectFlipGlsl},
    BundledEffect{"glitch", kEffectGlitchGlsl},
    BundledEffect{"laser", kEffectLaserGlsl},
    BundledEffect{"ripple", kEffectRippleGlsl},
    BundledEffect{"sine", kEffectSineGlsl},
};

const std::array<std::string_view, kBundled.size()> kBundledNames = [] {
    std::array<std::string_view, kBundled.size()> names{};
    std::ranges::transform(kBundled, names.begin(), &BundledEffect::name);
    return names;
}();

[[nodiscard]] std::string_view textOf(std::span<const std::uint8_t> bytes) noexcept { return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }

} // namespace

std::span<const std::string_view> EffectLibrary::bundledNames() noexcept { return kBundledNames; }

void EffectLibrary::reset(PackageBytes deck, Diagnostics* diagnostics) {
    _deck        = std::move(deck);
    _diagnostics = diagnostics;
    _entries.clear();
}

void EffectLibrary::report(std::string subject, std::string detail) {
    gr::log::warning("{}: {}", subject, detail);
    if (_diagnostics != nullptr) {
        _diagnostics->report(DiagnosticKind::invalidEffect, std::move(subject), std::move(detail));
    }
}

const EffectSource* EffectLibrary::find(std::string_view effectName) {
    if (effectName.empty()) {
        return nullptr;
    }
    if (const auto found = _entries.find(effectName); found != _entries.end()) {
        return found->second.effect.has_value() ? &*found->second.effect : nullptr;
    }
    Entry&            entry = _entries[std::string{effectName}];
    const std::string path  = effectPath(effectName);
    // a bare name the viewer has is its own effect; any other is the deck's, and a path always is
    const auto       bundled = effectName.contains('/') ? kBundled.end() : std::ranges::find(kBundled, effectName, &BundledEffect::name);
    std::string_view text    = bundled != kBundled.end() ? std::string_view{reinterpret_cast<const char*>(bundled->text.data()), bundled->text.size()} : (_deck ? textOf(_deck(path)) : std::string_view{});
    entry.fromDeck           = bundled == kBundled.end() && !text.empty();
    if (text.empty()) {
        return nullptr; // the caller says what it falls back to
    }
    const std::string_view stem   = effectName.substr(effectName.rfind('/') == std::string_view::npos ? 0UZ : effectName.rfind('/') + 1UZ);
    auto                   parsed = parseEffect(stem.substr(0UZ, stem.rfind(".glsl")), text);
    if (!parsed.has_value()) {
        report(path, parsed.error());
        return nullptr;
    }
    for (std::string& warning : parsed->warnings) {
        report(path, std::move(warning));
    }
    entry.effect = std::move(parsed->effect);
    return &*entry.effect;
}

std::unique_ptr<EffectRenderer> EffectLibrary::renderer(std::string_view effectName) {
    const EffectSource* effect = enabled ? find(effectName) : nullptr;
    if (effect == nullptr) {
        return nullptr;
    }
    const bool                 fromDeck = _entries.find(effectName)->second.fromDeck;
    EffectRenderer::AssetBytes assets   = [deck = _deck, fromDeck](std::string_view asset) -> std::span<const std::uint8_t> {
        if (!fromDeck || !deck) {
            return {}; // the viewer's own effects read no files
        }
        return deck(std::format("effects/{}", asset));
    };
    return std::make_unique<EffectRenderer>(*effect, std::move(assets));
}

void EffectLibrary::reportFailure(const EffectRenderer& renderer) {
    const auto found = _entries.find(renderer.effect().name);
    if (found != _entries.end() && found->second.reported) {
        return;
    }
    if (found != _entries.end()) {
        found->second.reported = true;
    }
    if (_diagnostics != nullptr) {
        _diagnostics->report(DiagnosticKind::invalidEffect, effectPath(renderer.effect().name), std::string{renderer.error()});
    }
}

void EffectLibrary::reportProblems(EffectRenderer& renderer) {
    for (std::string& problem : renderer.takeProblems()) {
        report(effectPath(renderer.effect().name), std::move(problem));
    }
}

std::map<std::string, std::array<float, 4>, std::less<>> EffectLibrary::parametersOf(const EffectSource& effect, std::string_view settings) {
    std::map<std::string, std::array<float, 4>, std::less<>> values;
    for (const auto piece : settings | std::views::split(' ')) {
        const std::string_view setting{piece.begin(), piece.end()};
        const auto             equals = setting.find('=');
        if (setting.empty() || equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key       = setting.substr(0UZ, equals);
        const std::string_view text      = setting.substr(equals + 1UZ);
        const EffectParameter* parameter = effect.parameter(key);
        if (parameter == nullptr) {
            report(effectPath(effect.name), std::format("has no parameter '{}'; ignored", key));
            continue;
        }
        auto value = parseParameterValue(parameter->type, text);
        if (!value.has_value()) {
            report(effectPath(effect.name), std::format("'{}' is not a value for {}; its default is used", text, key));
            continue;
        }
        if (parameter->range.has_value()) {
            const auto [low, high]       = *parameter->range;
            const std::size_t components = parameter->type == ParameterType::vec2 ? 2UZ : parameter->type == ParameterType::vec3 ? 3UZ : parameter->type == ParameterType::vec4 || parameter->type == ParameterType::colour ? 4UZ : 1UZ;
            const auto        used       = std::span{*value}.first(components);
            if (std::ranges::any_of(used, [&](float component) { return component < low || component > high; })) {
                report(effectPath(effect.name), std::format("{}={} is outside {} … {}; clamped", key, text, low, high));
                for (float& component : used) {
                    component = std::clamp(component, low, high);
                }
            }
        }
        values.insert_or_assign(std::string{key}, *value);
    }
    return values;
}

} // namespace gr::present
