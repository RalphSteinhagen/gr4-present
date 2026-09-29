#include <gr4-present/Manifest.hpp>
#include <gr4-present/Number.hpp>

#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>

namespace gr::present {

namespace {

// YAML infers scalar types, so an unquoted `minimumVersion: 0.1` arrives as a double rather than a string. Such a
// scalar is rendered back with its shortest round-trip representation, which reads `0.1` right but `0.10` as `0.1`:
// a version is quoted to be read as written.
[[nodiscard]] std::string scalarToString(const gr::Value& value) {
    if (const auto text = value.get_if<std::string_view>()) {
        return std::string{*text};
    }
    if (const auto* number = value.get_if<double>()) {
        return std::format("{}", *number);
    }
    if (const auto* integer = value.get_if<std::int64_t>()) {
        return std::format("{}", *integer);
    }
    return {};
}

[[nodiscard]] std::string stringField(const gr::property_map& map, std::string_view key) {
    const auto it = map.find(key);
    return it == map.cend() ? std::string{} : scalarToString(gr::Value{(*it).second});
}

/// a number, or `fallback` when the key is absent or not one; seconds throughout, because that is what an author
/// thinks in and a presentation has no use for a finer unit
[[nodiscard]] float secondsField(const gr::property_map& map, std::string_view key, float fallback) { return parseSeconds(stringField(map, key)).value_or(fallback); }

[[nodiscard]] gr::property_map mapField(const gr::property_map& map, std::string_view key) {
    const auto it = map.find(key);
    if (it == map.cend()) {
        return {};
    }
    const gr::Value entry = (*it).second;
    if (const auto nested = entry.get_if<gr::property_map>()) {
        return nested->owned(map.resource());
    }
    return {};
}

/// a legend position, or `fallback` when the key is absent; a value that names none is reported and ignored
[[nodiscard]] ChartLegend legendField(const gr::property_map& map, std::string_view key, ChartLegend fallback) {
    const std::string text = stringField(map, key);
    if (text.empty()) {
        return fallback;
    }
    if (const auto legend = parseChartLegend(text)) {
        return *legend;
    }
    gr::log::warning("{}: '{}' is no legend position (bottom, top, left, right or none); {} is used", key, text, kChartLegendNames[static_cast<std::size_t>(fallback)].second);
    return fallback;
}

/// a size in points, or `fallback` when the key is absent; a value that is no size is reported and ignored
[[nodiscard]] float pointsField(const gr::property_map& map, std::string_view key, float fallback) {
    const std::string text = stringField(map, key);
    if (text.empty()) {
        return fallback;
    }
    if (const auto size = parseTypeSize(text)) {
        return size->pointsWithin(fallback);
    }
    gr::log::warning("sizes: {}: '{}' is no size (14pt, 14, 80% or 1.2em); {} pt is used", key, text, fallback);
    return fallback;
}

[[nodiscard]] TypeScale sizesField(const gr::property_map& sizes) {
    constexpr TypeScale kDefault{};
    return TypeScale{.title = pointsField(sizes, "title", kDefault.title), .body = pointsField(sizes, "body", kDefault.body), .floor = pointsField(sizes, "floor", kDefault.floor)};
}

/// each entry of `fonts:`, a file or a map of `regular`, `bold`, `italic` and `bolditalic`, in the order written
[[nodiscard]] std::vector<std::pair<std::string, FontFamily>> fontsField(const gr::property_map& fonts) {
    std::vector<std::pair<std::string, FontFamily>> families;
    for (const auto& entry : fonts) {
        const std::string      name{entry.first};
        const gr::property_map files = mapField(fonts, name);
        families.emplace_back(name, files.empty() ? FontFamily{.regular = stringField(fonts, name), .bold = {}, .italic = {}, .boldItalic = {}} : FontFamily{.regular = stringField(files, "regular"), .bold = stringField(files, "bold"), .italic = stringField(files, "italic"), .boldItalic = stringField(files, "bolditalic")});
    }
    return families;
}

[[nodiscard]] std::vector<std::string> stringListField(const gr::property_map& map, std::string_view key) {
    const auto it = map.find(key);
    if (it == map.cend()) {
        return {};
    }
    const gr::Value             entry = (*it).second;
    const gr::Tensor<gr::Value> items = entry.value_or(gr::Tensor<gr::Value>{});
    return items | std::views::transform(scalarToString) | std::ranges::to<std::vector<std::string>>();
}

} // namespace

std::string ManifestError::message() const {
    switch (kind) {
    case Kind::invalidYaml: return std::format("presentation manifest is not valid YAML: {}", detail);
    case Kind::missingFormat: return "presentation manifest has no 'format' field";
    case Kind::unsupportedFormat: return std::format("unsupported manifest format '{}' (viewer supports '{}')", detail, kSupportedManifestFormat);
    case Kind::missingEntry: return "presentation manifest has no 'entry' field";
    case Kind::viewerTooOld: return std::format("presentation needs viewer {} or newer, this is {}", detail, kViewerVersion);
    }
    return "unknown manifest error";
}

bool versionAtLeast(std::string_view available, std::string_view required) noexcept {
    const auto component = [](std::string_view& text) {
        const auto dot   = text.find('.');
        const auto head  = text.substr(0UZ, dot);
        const int  value = parseNumber<int>(head).value_or(0);
        text             = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1UZ);
        return value;
    };
    // compared component by component, so 0.10 is newer than 0.9 rather than sorting before it as text would
    while (!available.empty() || !required.empty()) {
        const int have = component(available);
        const int want = component(required);
        if (have != want) {
            return have > want;
        }
    }
    return true;
}

std::expected<Manifest, ManifestError> parseManifest(std::string_view yaml) {
    const auto parsed = gr::pmt::yaml::deserialize(yaml);
    if (!parsed) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::invalidYaml, .detail = std::format("line {}, column {}: {}", parsed.error().line, parsed.error().column, parsed.error().message)});
    }

    Manifest manifest{
        .format               = stringField(*parsed, "format"),
        .title                = stringField(*parsed, "title"),
        .entry                = stringField(*parsed, "entry"),
        .minimumViewerVersion = stringField(mapField(*parsed, "viewer"), "minimumVersion"),
        .preload              = stringListField(mapField(*parsed, "resources"), "preload"),
        .transitionSeconds    = secondsField(mapField(*parsed, "transition"), "duration", kDefaultTransitionSeconds),
        .advanceSeconds       = secondsField(mapField(*parsed, "transition"), "advance", 0.0f),
        .liveWaitSeconds      = secondsField(mapField(*parsed, "export"), "live_wait", 3.0f),
        .liveLegend           = legendField(mapField(*parsed, "live"), "legend", ChartLegend::bottom),
        .fonts                = fontsField(mapField(*parsed, "fonts")),
        .sizes                = sizesField(mapField(*parsed, "sizes")),
        .background           = stringField(*parsed, "background"),
        .pointer              = stringField(*parsed, "pointer"),
        .breakEffect          = stringField(mapField(*parsed, "break"), "effect"),
        .breakMinutes         = parseNumber<float>(stringField(mapField(*parsed, "break"), "minutes")).value_or(10.0f),
    };

    if (manifest.format.empty()) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::missingFormat, .detail = {}});
    }
    if (manifest.format != kSupportedManifestFormat) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::unsupportedFormat, .detail = manifest.format});
    }
    if (!manifest.minimumViewerVersion.empty() && !versionAtLeast(kViewerVersion, manifest.minimumViewerVersion)) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::viewerTooOld, .detail = manifest.minimumViewerVersion});
    }
    if (manifest.entry.empty()) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::missingEntry, .detail = {}});
    }
    return manifest;
}

} // namespace gr::present
