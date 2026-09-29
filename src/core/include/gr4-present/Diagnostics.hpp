#ifndef GR4_PRESENT_DIAGNOSTICS_HPP
#define GR4_PRESENT_DIAGNOSTICS_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * The ways a presentation can be wrong.
 *
 * A framework whose assets are remote and whose content is live fails at the worst possible moment, so every failure
 * is named rather than left to a generic message. The list is the one the functional specification enumerates.
 */
enum class DiagnosticKind { missingManifest, missingResource, invalidSvgAnchor, unknownLiveWidget, unsupportedBlock, crossOriginFailure, archiveError, invalidNavigationTarget, versionMismatch, clippedContent, fontFallback, invalidEffect, slowEffect };

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

/**
 * Everything that went wrong loading and showing a presentation.
 *
 * Reporting both records the problem for the presenter and publishes it through GR4's logger, so a deck can be
 * checked before a talk and traced afterwards without two places deciding what counts as an error.
 */
struct Diagnostics {
    std::vector<Diagnostic> entries;

    void report(DiagnosticKind kind, std::string subject, std::string detail, bool fatal = false);
    void clear() noexcept { entries.clear(); }

    [[nodiscard]] bool        empty() const noexcept { return entries.empty(); }
    [[nodiscard]] std::size_t count(DiagnosticKind kind) const noexcept;
    [[nodiscard]] bool        anyFatal() const noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_DIAGNOSTICS_HPP
