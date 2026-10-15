#include <boost/ut.hpp>

#include <gr4-present/PackagePath.hpp>

using namespace boost::ut;
using namespace gr::present;

const suite<"PackagePath"> packagePathTests = [] {
    "reference relative to the referring resource"_test = [] {
        const auto resolved = resolvePackagePath("figures/system.svg", "./feedback.svg");
        expect(resolved.has_value());
        expect(eq(resolved->path, std::string{"figures/feedback.svg"}));
        expect(!resolved->external);
    };

    "bare relative reference behaves like ./"_test = [] {
        const auto resolved = resolvePackagePath("docs/talk.md", "intro.md");
        expect(resolved.has_value());
        expect(eq(resolved->path, std::string{"docs/intro.md"}));
    };

    "leading slash resolves from the package root"_test = [] {
        const auto resolved = resolvePackagePath("docs/deeply/nested.md", "/figures/system.svg");
        expect(resolved.has_value());
        expect(eq(resolved->path, std::string{"figures/system.svg"}));
    };

    "parent reference stays inside the package root"_test = [] {
        const auto resolved = resolvePackagePath("docs/chapter/talk.md", "../figures/system.svg");
        expect(resolved.has_value());
        expect(eq(resolved->path, std::string{"docs/figures/system.svg"}));
    };

    "parent reference may not escape the package root"_test = [] {
        const auto resolved = resolvePackagePath("talk.md", "../../etc/passwd");
        expect(!resolved.has_value());
        expect(resolved.error() == PathError::escapesPackageRoot);
    };

    "archive path traversal is rejected from the root itself"_test = [] {
        const auto resolved = resolvePackagePath("", "../secret");
        expect(!resolved.has_value());
        expect(resolved.error() == PathError::escapesPackageRoot);
    };

    "https reference is marked external and kept verbatim"_test = [] {
        const auto resolved = resolvePackagePath("talk.md", "https://remote.example/talk/system.svg");
        expect(resolved.has_value());
        expect(resolved->external);
        expect(eq(resolved->path, std::string{"https://remote.example/talk/system.svg"}));
    };

    "non-http schemes are rejected"_test = [] {
        for (const std::string_view reference : {"javascript:alert(1)", "data:text/html,<script/>", "file:///etc/passwd"}) {
            const auto resolved = resolvePackagePath("talk.md", reference);
            expect(!resolved.has_value()) << reference;
            expect(resolved.error() == PathError::unsupportedScheme) << reference;
        }
    };

    "empty reference is a diagnostic, not an empty path"_test = [] {
        const auto resolved = resolvePackagePath("talk.md", "");
        expect(!resolved.has_value());
        expect(resolved.error() == PathError::emptyReference);
    };

    "redundant separators and dot segments are normalised"_test = [] {
        const auto resolved = resolvePackagePath("", "figures//./sub/../system.svg");
        expect(resolved.has_value());
        expect(eq(resolved->path, std::string{"figures/system.svg"}));
    };

    "reference from a root-level resource resolves against the root"_test = [] {
        const auto resolved = resolvePackagePath("presentation.yaml", "workflows/demo.grc");
        expect(resolved.has_value());
        expect(eq(resolved->path, std::string{"workflows/demo.grc"}));
    };

    "an address on this computer is localhost, 127.0.0.0/8 or [::1], over http or https"_test = [] {
        expect(isLoopbackAddress("http://localhost:8000/deck"));
        expect(isLoopbackAddress("http://localhost"));
        expect(isLoopbackAddress("https://127.0.0.1/talks"));
        expect(isLoopbackAddress("http://127.1.2.3:9"));
        expect(isLoopbackAddress("http://[::1]:8443/"));
        expect(!isLoopbackAddress("http://localhost.example.org/deck")) << "a name that only starts like it";
        expect(!isLoopbackAddress("https://example.org/localhost"));
        expect(!isLoopbackAddress("http://10.0.0.1/"));
        expect(!isLoopbackAddress("/home/me/talks/tour")) << "a folder is no address";
        expect(!isLoopbackAddress("file://localhost/tmp"));
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
