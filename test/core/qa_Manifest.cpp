#include <boost/ut.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

#include <gr4-present/Manifest.hpp>

using namespace boost::ut;
using namespace gr::present;

const suite<"Manifest"> manifestTests = [] {
    // Ground truth is the author's own words: seconds, as written. A deck states its pace once and a section
    // says something only where it differs, so the manifest's job is the default and nothing more.
    "a deck states its pace once, in seconds"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
transition:
  duration: 1.25
  advance: 6
)");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(std::abs(manifest->transitionSeconds - 1.25f) < 0.001f);
        expect(std::abs(manifest->advanceSeconds - 6.0f) < 0.001f);
    };

    // a colour is quoted: GR4's YAML reader takes any '#' for a comment, where YAML 1.2 wants a space before it
    "a deck names its background, its pointer and its break once, each an effect with its settings"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
background: plasma speed=0.5
pointer: laser radius=8
break:
  effect: "waves tint=#ff0000"
  minutes: 7.5
)");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(eq(manifest->background, std::string{"plasma speed=0.5"}));
        expect(eq(manifest->pointer, std::string{"laser radius=8"}));
        expect(eq(manifest->breakEffect, std::string{"waves tint=#ff0000"}));
        expect(std::abs(manifest->breakMinutes - 7.5f) < 0.001f);
    };

    "a deck that names no effects has none, and a break of ten minutes"_test = [] {
        const auto manifest = parseManifest("format: gr4-presentation/1\nentry: talk.md\n");
        expect(manifest.has_value());
        expect(manifest->background.empty() && manifest->pointer.empty() && manifest->breakEffect.empty());
        expect(std::abs(manifest->breakMinutes - 10.0f) < 0.001f);
    };

    "a deck that says nothing waits for a key and moves at the usual pace"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
)");
        expect(manifest.has_value());
        expect(std::abs(manifest->transitionSeconds - 0.5f) < 0.001f) << "half a second, as the author set the viewer's default";
        expect(eq(manifest->advanceSeconds, 0.0f)) << "nothing advances on its own unless it is asked to";
    };

    "nonsense where a number should be keeps the default rather than stopping the deck"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
transition:
  duration: soon
  advance: -3
)");
        expect(manifest.has_value()) << "a mistyped pace is not a reason to refuse to show a presentation";
        expect(std::abs(manifest->transitionSeconds - 0.5f) < 0.001f);
        expect(eq(manifest->advanceSeconds, 0.0f)) << "and a negative delay is not a delay";
    };

    "an export says how long a live chart runs before its page is taken, in seconds with or without the unit"_test = [] {
        const auto stated = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
export:
  live_wait: 5s
)");
        expect(stated.has_value() && std::abs(stated->liveWaitSeconds - 5.0f) < 0.001f);
        const auto bare = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
export:
  live_wait: 1.5
)");
        expect(bare.has_value() && std::abs(bare->liveWaitSeconds - 1.5f) < 0.001f);
        const auto silent = parseManifest("format: gr4-presentation/1\nentry: talk.md\n");
        expect(silent.has_value() && std::abs(silent->liveWaitSeconds - 3.0f) < 0.001f) << "three seconds unless the deck says otherwise";
    };

    "a deck says once where its live charts share their legend, and a misspelt position keeps the default"_test = [] {
        const auto stated = parseManifest("format: gr4-presentation/1\nentry: talk.md\nlive:\n  legend: right\n");
        expect(stated.has_value() && stated->liveLegend == ChartLegend::right);
        const auto hidden = parseManifest("format: gr4-presentation/1\nentry: talk.md\nlive:\n  legend: none\n");
        expect(hidden.has_value() && hidden->liveLegend == ChartLegend::none);
        const auto silent = parseManifest("format: gr4-presentation/1\nentry: talk.md\n");
        expect(silent.has_value() && silent->liveLegend == ChartLegend::bottom) << "below the charts unless the deck says otherwise";
        const auto misspelt = parseManifest("format: gr4-presentation/1\nentry: talk.md\nlive:\n  legend: middle\n");
        expect(misspelt.has_value() && misspelt->liveLegend == ChartLegend::bottom) << "a position that names none is not a reason to refuse the deck";
    };

    "minimal manifest parses"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
title: GNU Radio 4.0
entry: talk.md
)");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(eq(manifest->format, std::string{"gr4-presentation/1"}));
        expect(eq(manifest->title, std::string{"GNU Radio 4.0"}));
        expect(eq(manifest->entry, std::string{"talk.md"}));
        expect(manifest->preload.empty());
    };

    "viewer and resource metadata parse"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
title: GNU Radio 4.0
entry: talk.md

viewer:
  minimumVersion: 0.1

resources:
  preload:
    - figures/system.svg
    - workflows/demo.grc
)");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(eq(manifest->minimumViewerVersion, std::string{"0.1"}));
        expect(eq(manifest->preload.size(), 2UZ));
        expect(eq(manifest->preload.at(0UZ), std::string{"figures/system.svg"}));
        expect(eq(manifest->preload.at(1UZ), std::string{"workflows/demo.grc"}));
    };

    "entry names the document the presentation starts from"_test = [] {
        const auto manifest = parseManifest("format: gr4-presentation/1\ntitle: a talk\nentry: docs/talk.md\n");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(eq(manifest->entry, std::string{"docs/talk.md"})) << "the entry is a package-relative document, not a view id";
        expect(eq(manifest->title, std::string{"a talk"}));
    };

    "the specification's own manifest example parses"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1

title: GNU Radio 4.0
entry: talk.md

viewer:
  minimumVersion: 0.1

resources:
  preload:
    - figures/system.svg
    - workflows/demo.grc
)");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(eq(manifest->entry, std::string{"talk.md"}));
        expect(eq(manifest->title, std::string{"GNU Radio 4.0"}));
        expect(eq(manifest->minimumViewerVersion, std::string{"0.1"}));
        expect(eq(manifest->preload.size(), 2UZ));
    };

    "missing format is reported"_test = [] {
        const auto manifest = parseManifest("title: no format\nentry: talk.md\n");
        expect(!manifest.has_value());
        expect(manifest.error().kind == ManifestError::Kind::missingFormat);
    };

    "unsupported format names both versions"_test = [] {
        const auto manifest = parseManifest("format: gr4-presentation/99\nentry: talk.md\n");
        expect(!manifest.has_value());
        expect(manifest.error().kind == ManifestError::Kind::unsupportedFormat);
        expect(manifest.error().message().contains("gr4-presentation/99"));
        expect(manifest.error().message().contains(kSupportedManifestFormat));
    };

    "missing entry is reported"_test = [] {
        const auto manifest = parseManifest("format: gr4-presentation/1\ntitle: no entry\n");
        expect(!manifest.has_value());
        expect(manifest.error().kind == ManifestError::Kind::missingEntry);
    };

    "invalid YAML reports line and column"_test = [] {
        const auto manifest = parseManifest("format: gr4-presentation/1\ntitle: \"unterminated\nentry: talk.md\n");
        expect(!manifest.has_value());
        expect(manifest.error().kind == ManifestError::Kind::invalidYaml);
        expect(manifest.error().message().contains("line"));
        expect(manifest.error().message().contains("column"));
    };

    "a quoted version is read verbatim"_test = [] {
        const auto manifest = parseManifest("format: gr4-presentation/1\nentry: talk.md\nviewer:\n  minimumVersion: \"0.1.0\"\n");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(eq(manifest->minimumViewerVersion, std::string{"0.1.0"}));
    };

    "empty manifest is a missing-format diagnostic"_test = [] {
        const auto manifest = parseManifest("");
        expect(!manifest.has_value());
        expect(manifest.error().kind == ManifestError::Kind::missingFormat);
    };

    "a deck names its faces by role or by a name of its own, as a file or a family"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
fonts:
  body: fonts/Body.ttf
  mono: fonts/Code.ttf
  hand:
    regular: fonts/Hand-Regular.ttf
    bold: fonts/Hand-Bold.ttf
    bolditalic: fonts/Hand-BoldItalic.ttf
)");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        const auto familyOf = [&manifest](std::string_view name) -> const FontFamily* {
            const auto found = std::ranges::find(manifest->fonts, name, [](const auto& entry) { return std::string_view{entry.first}; });
            return found == manifest->fonts.end() ? nullptr : &found->second;
        };
        expect(eq(manifest->fonts.size(), 3UZ));
        expect(familyOf("body") != nullptr && *familyOf("body") == FontFamily{.regular = "fonts/Body.ttf", .bold = {}, .italic = {}, .boldItalic = {}});
        expect(familyOf("mono") != nullptr && familyOf("mono")->regular == "fonts/Code.ttf");
        expect(familyOf("hand") != nullptr && *familyOf("hand") == FontFamily{.regular = "fonts/Hand-Regular.ttf", .bold = "fonts/Hand-Bold.ttf", .italic = {}, .boldItalic = "fonts/Hand-BoldItalic.ttf"}) << "a style the family leaves out stays empty";
    };

    "a deck sets its type in points, or relative to the sizes it would otherwise get"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
sizes:
  title: 40pt
  body: 150%
)");
        expect(manifest.has_value());
        expect(eq(manifest->sizes.title, 40.0f));
        expect(eq(manifest->sizes.body, 27.0f)) << "150 % of the 18 pt body";
        expect(eq(manifest->sizes.floor, 12.0f)) << "what is not said keeps its default";
    };

    "a size that is no size keeps the default rather than stopping the deck"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
entry: talk.md
sizes:
  body: large
  floor: -2pt
)");
        expect(manifest.has_value());
        expect(manifest->sizes == TypeScale{});
        expect(manifest->fonts.empty()) << "a deck that names no faces gets the built-in ones";
    };
};

int main() { return 0; }
