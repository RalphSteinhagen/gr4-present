#include <boost/ut.hpp>

#include <gr4-present/Manifest.hpp>

using namespace boost::ut;
using namespace gr::present;

const suite<"Manifest"> manifestTests = [] {
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

    "a view carries its image and inline markdown"_test = [] {
        const auto manifest = parseManifest(R"(format: gr4-presentation/1
title: gr4-present
entry: default

views:
  - id: default
    image: gen_spectrum.png
    markdown: |
      ## stay tuned ...
)");
        expect(manifest.has_value()) << (manifest ? std::string{} : manifest.error().message());
        expect(eq(manifest->views.size(), 1UZ));
        const ManifestView* view = manifest->findView("default");
        expect(view != nullptr);
        expect(eq(view->image, std::string{"gen_spectrum.png"}));
        expect(view->markdown.contains("stay tuned"));
        expect(manifest->findView("absent") == nullptr);
    };

    "a manifest without views parses and has none"_test = [] {
        const auto manifest = parseManifest("format: gr4-presentation/1\nentry: talk.md\n");
        expect(manifest.has_value());
        expect(manifest->views.empty());
        expect(manifest->findView("anything") == nullptr);
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
};

int main() { return 0; }
