#ifndef GR4_PRESENT_DIAGNOSTICS_HPP
#define GR4_PRESENT_DIAGNOSTICS_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// the ways a presentation can be wrong; each failure is named, as remote assets and live content fail at the worst
/// moment
enum class DiagnosticKind {
    missingManifest,
    missingResource,
    invalidSvgAnchor,
    unknownLiveWidget,
    unsupportedBlock,
    crossOriginFailure,
    archiveError,
    saveFailed,
    invalidNavigationTarget,
    versionMismatch,
    clippedContent,
    fontFallback,
    invalidEffect,
    slowEffect,
};

/// the sentence shown to a presenter; `subject` and `detail` say which one and why
[[nodiscard]] std::string_view describe(DiagnosticKind kind) noexcept;

struct Diagnostic {
    DiagnosticKind kind = DiagnosticKind::missingResource;
    std::string    subject; // the path, id or version the problem is about
    std::string    detail;  // why it failed, in the words of whatever failed
    bool           fatal = false;

    /// "missing resource: figures/system.svg -- not found"
    [[nodiscard]] std::string message() const;

    bool operator==(const Diagnostic&) const = default;
};

/// everything that went wrong loading and showing a presentation, recorded for the presenter and logged through GR4
struct Diagnostics {
    std::vector<Diagnostic> entries;

    void report(DiagnosticKind kind, std::string subject, std::string detail, bool fatal = false);

    [[nodiscard]] bool        empty() const noexcept { return entries.empty(); }
    [[nodiscard]] std::size_t count(DiagnosticKind kind) const noexcept;
    [[nodiscard]] bool        anyFatal() const noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_DIAGNOSTICS_HPP
