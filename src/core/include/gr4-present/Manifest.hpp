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

/// directory, archive and remote URL are transports; the manifest defines a presentation, read with GR4's YAML reader
inline constexpr std::string_view kSupportedManifestFormat = "gr4-presentation/1";

/// the files of one face, package-relative; a face given as a single file has only `regular`
struct FontFamily {
    std::string regular;
    std::string bold;
    std::string italic;
    std::string boldItalic;

    bool operator==(const FontFamily&) const = default;
};

/// the move duration when the deck says none
inline constexpr float kDefaultTransitionSeconds = 0.5f;

struct Manifest {
    std::string format;
    std::string title;
    std::string entry; // package-relative path of the entry document, e.g. talk.md
    std::string minimumViewerVersion;

    /// seconds a move between views takes; a section overrides it
    float transitionSeconds = kDefaultTransitionSeconds;

    /// seconds a view is held before the deck goes on by itself; zero waits for a key
    float advanceSeconds = 0.0f;

    /// `export: { live_wait: 3s }`: how long a live chart runs before an exported page takes its picture
    float liveWaitSeconds = 3.0f;

    /// `live: { legend: bottom }`: where a live region's charts share their legend unless the region says otherwise
    ChartLegend liveLegend = ChartLegend::bottom;

    /// `fonts:` a role (body, bold, italic, bolditalic, mono, title) or a chosen name, mapped to a file or a family of up
    /// to four
    std::vector<std::pair<std::string, FontFamily>> fonts;

    /// `sizes: { title: 40pt, body: 20pt, floor: 12pt }`; a relative value scales the size the viewer would use
    TypeScale sizes;

    /// `background: plasma speed=1`: the effect shader behind every slide that does not name its own
    std::string background;

    /// `pointer: laser`: the effect shader the pointer draws with when `L` turns it on; empty takes the viewer's own
    std::string pointer;

    /// `break: { effect: plasma, minutes: 10 }`: what the break screen behind `B` shows, and how long it counts down
    std::string breakEffect;
    float       breakMinutes = 10.0f;

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
