#ifndef GR4_PRESENT_MANIFEST_HPP
#define GR4_PRESENT_MANIFEST_HPP

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * Directory, archive and remote URL are transport mechanisms; the manifest is what defines a presentation. It is
 * parsed with GR4's YAML reader so the project does not acquire a second file abstraction.
 */
inline constexpr std::string_view kSupportedManifestFormat = "gr4-presentation/1";

struct ManifestView {
    std::string id;
    std::string image;    // package-relative
    std::string markdown; // inline for now; a package will point at a document instead

    bool operator==(const ManifestView&) const = default;
};

struct Manifest {
    std::string               format;
    std::string               title;
    std::string               entry; // package-relative path of the entry document
    std::string               minimumViewerVersion;
    std::vector<std::string>  preload;
    std::vector<ManifestView> views;

    [[nodiscard]] const ManifestView* findView(std::string_view id) const noexcept;

    bool operator==(const Manifest&) const = default;
};

struct ManifestError {
    enum class Kind { invalidYaml, missingFormat, unsupportedFormat, missingEntry };

    Kind        kind;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::expected<Manifest, ManifestError> parseManifest(std::string_view yaml);

} // namespace gr::present

#endif // GR4_PRESENT_MANIFEST_HPP
