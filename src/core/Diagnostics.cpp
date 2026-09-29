#include <gr4-present/Diagnostics.hpp>

#include <gnuradio-4.0/Logger.hpp>

#include <algorithm>
#include <format>

namespace gr::present {

std::string_view describe(DiagnosticKind kind) noexcept {
    switch (kind) {
    case DiagnosticKind::missingManifest: return "no presentation here";
    case DiagnosticKind::missingResource: return "missing resource";
    case DiagnosticKind::invalidSvgAnchor: return "no such element in the layout";
    case DiagnosticKind::unknownLiveWidget: return "unknown live widget";
    case DiagnosticKind::unsupportedBlock: return "unsupported block";
    case DiagnosticKind::crossOriginFailure: return "cross-origin fetch refused";
    case DiagnosticKind::archiveError: return "archive could not be read";
    case DiagnosticKind::invalidNavigationTarget: return "navigation target does not exist";
    case DiagnosticKind::versionMismatch: return "presentation needs a newer viewer";
    case DiagnosticKind::clippedContent: return "content does not fit its box";
    case DiagnosticKind::fontFallback: return "a deck's face lacks a glyph";
    }
    return "unknown problem";
}

std::string Diagnostic::message() const {
    if (detail.empty()) {
        return std::format("{}: {}", describe(kind), subject);
    }
    return std::format("{}: {} -- {}", describe(kind), subject, detail);
}

void Diagnostics::report(DiagnosticKind kind, std::string subject, std::string detail, bool fatal) {
    entries.push_back(Diagnostic{.kind = kind, .subject = std::move(subject), .detail = std::move(detail), .fatal = fatal});

    // the presenter's list and the log say the same thing, so a deck can be checked beforehand and traced afterwards
    const std::string text = entries.back().message();
    if (fatal) {
        gr::log::error("{}", text);
    } else {
        gr::log::warning("{}", text);
    }
}

std::size_t Diagnostics::count(DiagnosticKind kind) const noexcept { return static_cast<std::size_t>(std::ranges::count(entries, kind, &Diagnostic::kind)); }

bool Diagnostics::anyFatal() const noexcept {
    return std::ranges::any_of(entries, [](const Diagnostic& entry) { return entry.fatal; });
}

} // namespace gr::present
