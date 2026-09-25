#ifndef GR4_PRESENT_PACKAGE_PATH_HPP
#define GR4_PRESENT_PACKAGE_PATH_HPP

#include <expected>
#include <string>
#include <string_view>

namespace gr::present {

/**
 * Where a presentation physically lives (directory, archive, HTTP hierarchy) must not leak into its syntax, so every
 * reference resolves against the package root. `..` may never escape that root: a remote package is untrusted and
 * must not be able to name a resource outside its own tree.
 */
enum class PathError { emptyReference, escapesPackageRoot, unsupportedScheme };

[[nodiscard]] constexpr std::string_view message(PathError error) noexcept {
    switch (error) {
    case PathError::emptyReference: return "empty resource reference";
    case PathError::escapesPackageRoot: return "resource reference escapes the presentation package root";
    case PathError::unsupportedScheme: return "resource reference uses an unsupported URI scheme (only http/https are external)";
    }
    return "unknown path error";
}

struct ResolvedPath {
    std::string path;             // package-relative and normalised, or the verbatim URL when external
    bool        external = false; // http/https reference subject to browser and security policy

    bool operator==(const ResolvedPath&) const = default;
};

/// an empty `referrer` denotes the package root
[[nodiscard]] std::expected<ResolvedPath, PathError> resolvePackagePath(std::string_view referrer, std::string_view reference);

} // namespace gr::present

#endif // GR4_PRESENT_PACKAGE_PATH_HPP
