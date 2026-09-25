#include <gr4-present/PackagePath.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <vector>

namespace gr::present {

namespace {

[[nodiscard]] std::string_view uriScheme(std::string_view reference) noexcept {
    const auto isSchemeChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' || c == '-' || c == '.'; };
    if (reference.empty() || std::isalpha(static_cast<unsigned char>(reference.front())) == 0) {
        return {};
    }
    const auto colon = reference.find(':');
    if (colon == std::string_view::npos) {
        return {};
    }
    const std::string_view candidate = reference.substr(0UZ, colon);
    return std::ranges::all_of(candidate, isSchemeChar) ? candidate : std::string_view{};
}

[[nodiscard]] std::string_view directoryOf(std::string_view path) noexcept {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0UZ, slash);
}

// std::views::join_with is absent from libc++ before 21, which some supported toolchains still ship
[[nodiscard]] std::string joinWithSlash(const std::vector<std::string_view>& segments) {
    std::string joined;
    for (const std::string_view segment : segments) {
        if (!joined.empty()) {
            joined.push_back('/');
        }
        joined.append(segment);
    }
    return joined;
}

[[nodiscard]] std::expected<std::string, PathError> normalise(std::string_view path) {
    std::vector<std::string_view> segments;
    for (const auto part : path | std::views::split('/')) {
        const std::string_view segment{part};
        if (segment.empty() || segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (segments.empty()) {
                return std::unexpected(PathError::escapesPackageRoot);
            }
            segments.pop_back();
            continue;
        }
        segments.push_back(segment);
    }
    return joinWithSlash(segments);
}

} // namespace

std::expected<ResolvedPath, PathError> resolvePackagePath(std::string_view referrer, std::string_view reference) {
    if (reference.empty()) {
        return std::unexpected(PathError::emptyReference);
    }

    if (const std::string_view scheme = uriScheme(reference); !scheme.empty()) {
        if (scheme != "http" && scheme != "https") {
            return std::unexpected(PathError::unsupportedScheme);
        }
        return ResolvedPath{.path = std::string{reference}, .external = true};
    }

    const std::string_view directory = reference.starts_with('/') ? std::string_view{} : directoryOf(referrer);
    const std::string_view relative  = reference.starts_with('/') ? reference.substr(1UZ) : reference;
    const std::string      combined  = directory.empty() ? std::string{relative} : std::format("{}/{}", directory, relative);

    return normalise(combined).transform([](std::string path) { return ResolvedPath{.path = std::move(path), .external = false}; });
}

} // namespace gr::present
