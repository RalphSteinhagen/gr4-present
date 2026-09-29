#ifndef GR4_PRESENT_MANIFEST_HPP
#define GR4_PRESENT_MANIFEST_HPP

#include <gr4-present/ChartLegend.hpp>
#include <gr4-present/TypeSize.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/**
 * Directory, archive and remote URL are transport mechanisms; the manifest is what defines a presentation. It is
 * parsed with GR4's YAML reader so the project does not acquire a second file abstraction.
 */
inline constexpr std::string_view kSupportedManifestFormat = "gr4-presentation/1";

/// the files of one face, package-relative; a face given as a single file has only `regular`
struct FontFamily {
    std::string regular;
    std::string bold;
    std::string italic;
    std::string boldItalic;

    bool operator==(const FontFamily&) const = default;
};

/// how long a move between slides takes when the deck does not say: long enough to be seen as the effect it is
inline constexpr float kDefaultTransitionSeconds = 0.5f;

struct Manifest {
    std::string              format;
    std::string              title;
    std::string              entry; // package-relative path of the entry document, e.g. talk.md
    std::string              minimumViewerVersion;
    std::vector<std::string> preload;

    /// how long a move between views takes, in seconds. A deck usually wants one pace, so it is said once here
    /// and a section says something only where it differs.
    float transitionSeconds = kDefaultTransitionSeconds;

    /// seconds a view is held before the deck goes on by itself; zero waits for a key, which is the default.
    /// A presentation that advances while somebody is answering a question is worse than one that never does.
    float advanceSeconds = 0.0f;

    /// `export: { live_wait: 3s }`: how long a live chart runs before an exported page takes its picture
    float liveWaitSeconds = 3.0f;

    /// `live: { legend: bottom }`: where a live region's charts share their legend unless the region says otherwise
    ChartLegend liveLegend = ChartLegend::bottom;

    /// `fonts:` a role (body, bold, italic, bolditalic, mono, title) or a name a slide, box or span can choose,
    /// mapped to a file, or to a family of up to four; checked when the files are read, not here
    std::vector<std::pair<std::string, FontFamily>> fonts;

    /// `sizes: { title: 40pt, body: 20pt, floor: 12pt }`; a relative value scales the size the viewer would use
    TypeScale sizes;

    bool operator==(const Manifest&) const = default;
};

struct ManifestError {
    enum class Kind { invalidYaml, missingFormat, unsupportedFormat, missingEntry, viewerTooOld };

    Kind        kind;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// this viewer's version, compared against a manifest's `viewer.minimumVersion`
inline constexpr std::string_view kViewerVersion = "0.1";

/// true when `required` is no newer than `available`; both are dotted numbers, missing components read as zero
[[nodiscard]] bool versionAtLeast(std::string_view available, std::string_view required) noexcept;

[[nodiscard]] std::expected<Manifest, ManifestError> parseManifest(std::string_view yaml);

} // namespace gr::present

#endif // GR4_PRESENT_MANIFEST_HPP
