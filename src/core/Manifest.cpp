#include <gr4-present/Manifest.hpp>

#include <gnuradio-4.0/YamlPmt.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <ranges>

namespace gr::present {

namespace {

// YAML infers scalar types, so an unquoted `minimumVersion: 0.1` arrives as a double rather than a string. Render
// such scalars back with the shortest round-trip representation so that quoting is a matter of taste, not meaning.
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

[[nodiscard]] std::vector<std::string> stringListField(const gr::property_map& map, std::string_view key) {
    const auto it = map.find(key);
    if (it == map.cend()) {
        return {};
    }
    const gr::Value             entry = (*it).second;
    const gr::Tensor<gr::Value> items = entry.value_or(gr::Tensor<gr::Value>{});
    return items | std::views::transform(scalarToString) | std::ranges::to<std::vector<std::string>>();
}

[[nodiscard]] std::vector<ManifestView> viewsField(const gr::property_map& map) {
    const auto it = map.find(std::string_view{"views"});
    if (it == map.cend()) {
        return {};
    }
    const gr::Value           entry = (*it).second;
    std::vector<ManifestView> views;
    for (const gr::Value& item : entry.value_or(gr::Tensor<gr::Value>{})) {
        const auto nested = item.get_if<gr::property_map>();
        if (!nested) {
            continue;
        }
        const gr::property_map view = nested->owned(map.resource());
        views.push_back(ManifestView{.id = stringField(view, "id"), .image = stringField(view, "image"), .markdown = stringField(view, "markdown")});
    }
    return views;
}

} // namespace

const ManifestView* Manifest::findView(std::string_view id) const noexcept {
    const auto view = std::ranges::find(views, id, &ManifestView::id);
    return view == views.cend() ? nullptr : &*view;
}

std::string ManifestError::message() const {
    switch (kind) {
    case Kind::invalidYaml: return std::format("presentation manifest is not valid YAML: {}", detail);
    case Kind::missingFormat: return "presentation manifest has no 'format' field";
    case Kind::unsupportedFormat: return std::format("unsupported manifest format '{}' (viewer supports '{}')", detail, kSupportedManifestFormat);
    case Kind::missingEntry: return "presentation manifest has no 'entry' field";
    }
    return "unknown manifest error";
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
        .views                = viewsField(*parsed),
    };

    if (manifest.format.empty()) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::missingFormat, .detail = {}});
    }
    if (manifest.format != kSupportedManifestFormat) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::unsupportedFormat, .detail = manifest.format});
    }
    if (manifest.entry.empty()) {
        return std::unexpected(ManifestError{.kind = ManifestError::Kind::missingEntry, .detail = {}});
    }
    return manifest;
}

} // namespace gr::present
