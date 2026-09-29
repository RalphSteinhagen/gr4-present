#include <boost/ut.hpp>

#include <gr4-present/Diagnostics.hpp>
#include <gr4-present/Manifest.hpp>

#include <string>

using namespace boost::ut;
using namespace gr::present;

const suite<"Diagnostics"> diagnosticsTests = [] {
    "every kind has its own sentence"_test = [] {
        const DiagnosticKind          kinds[]{DiagnosticKind::missingManifest, DiagnosticKind::missingResource, DiagnosticKind::invalidSvgAnchor, DiagnosticKind::unknownLiveWidget, DiagnosticKind::unsupportedBlock, DiagnosticKind::crossOriginFailure, DiagnosticKind::archiveError, DiagnosticKind::invalidNavigationTarget, DiagnosticKind::versionMismatch};
        std::vector<std::string_view> sentences;
        for (const DiagnosticKind kind : kinds) {
            expect(!describe(kind).empty()) << "a kind without a sentence would report as blank";
            sentences.push_back(describe(kind));
        }
        std::ranges::sort(sentences);
        expect(std::ranges::adjacent_find(sentences) == sentences.end()) << "two kinds sharing a sentence cannot be told apart";
        expect(eq(sentences.size(), 9UZ)) << "the specification names nine failure classes";
    };

    "a message names what failed and why"_test = [] {
        const Diagnostic missing{.kind = DiagnosticKind::missingResource, .subject = "figures/system.svg", .detail = "not found", .fatal = false};
        expect(missing.message().contains("figures/system.svg"));
        expect(missing.message().contains("not found"));

        const Diagnostic terse{.kind = DiagnosticKind::invalidNavigationTarget, .subject = "nowhere", .detail = {}, .fatal = false};
        expect(terse.message().contains("nowhere"));
        expect(!terse.message().ends_with("-- ")) << "an absent reason must not leave a dangling separator";
    };

    "reports accumulate and can be counted by kind"_test = [] {
        Diagnostics log;
        expect(log.empty());
        log.report(DiagnosticKind::missingResource, "a.png", "404");
        log.report(DiagnosticKind::missingResource, "b.png", "404");
        log.report(DiagnosticKind::invalidSvgAnchor, "nowhere", "no such id");
        expect(!log.empty());
        expect(eq(log.entries.size(), 3UZ));
        expect(eq(log.count(DiagnosticKind::missingResource), 2UZ));
        expect(eq(log.count(DiagnosticKind::invalidSvgAnchor), 1UZ));
        expect(eq(log.count(DiagnosticKind::archiveError), 0UZ));
        expect(!log.anyFatal()) << "nothing reported here was fatal";

        log.report(DiagnosticKind::missingManifest, "http://host/talk", "connection refused", true);
        expect(log.anyFatal());
        log.clear();
        expect(log.empty() && !log.anyFatal());
    };
};

// Ground truth is semantic-version ordering, not the parser: 0.10 is newer than 0.9 although it sorts before it.
const suite<"ManifestVersion"> manifestVersionTests = [] {
    "a version is compared component by component, not as text"_test = [] {
        expect(versionAtLeast("0.10", "0.9")) << "0.10 is newer than 0.9";
        expect(!versionAtLeast("0.9", "0.10"));
        expect(versionAtLeast("1.0", "0.99"));
        expect(versionAtLeast("2", "1.9.9"));
    };

    "equal versions satisfy the requirement, and missing components read as zero"_test = [] {
        expect(versionAtLeast("0.1", "0.1"));
        expect(versionAtLeast("0.1", "0.1.0"));
        expect(versionAtLeast("0.1.0", "0.1"));
        expect(!versionAtLeast("0.1", "0.1.1"));
        expect(versionAtLeast("0.1", ""));
    };

    "a manifest asking for a newer viewer is refused, and says which"_test = [] {
        const auto refused = parseManifest("format: gr4-presentation/1\nentry: talk.md\nviewer:\n  minimumVersion: 99.0\n");
        expect(!refused.has_value()) << "the viewer must not pretend it can show this";
        if (!refused.has_value()) {
            expect(refused.error().kind == ManifestError::Kind::viewerTooOld);
            // the YAML reader parses 99.0 as a number and renders it shortest-round-trip, so the detail is "99"
            expect(!refused.error().detail.empty()) << "the error must carry the version that was asked for";
            expect(refused.error().message().contains(refused.error().detail)) << "the message must name it: " << refused.error().message();
        }
    };

    "a manifest this viewer satisfies still parses"_test = [] {
        expect(parseManifest("format: gr4-presentation/1\nentry: talk.md\nviewer:\n  minimumVersion: 0.1\n").has_value());
        expect(parseManifest("format: gr4-presentation/1\nentry: talk.md\n").has_value()) << "no requirement means no obstacle";
    };
};

int main() { return 0; }
