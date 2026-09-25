#include <boost/ut.hpp>

#include <gr4-present/PresentationLoader.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

using namespace boost::ut;
using namespace gr::present;

namespace {
// the loader never blocks, so a test drives it the way a frame loop does
[[nodiscard]] LoadState settle(PresentationLoader& loader, std::chrono::milliseconds budget = std::chrono::seconds{10}) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (loader.state() == LoadState::loading && std::chrono::steady_clock::now() < deadline) {
        loader.advance();
        std::this_thread::yield();
    }
    return loader.state();
}
} // namespace

const suite<"PresentationLoader"> presentationLoaderTests = [] {
    const std::array<std::string, 2> equivalentAddressings{std::string{GR4_PRESENT_DEFAULT_PRESENTATION}, std::format("file:{}", GR4_PRESENT_DEFAULT_PRESENTATION)};
    for (const std::string& base : equivalentAddressings) {
        boost::ut::test("the default presentation loads from '" + base + "'") = [base] {
            PresentationLoader loader;
            loader.retryPolicy.enabled = false;
            loader.begin(base);
            expect(settle(loader) == LoadState::ready) << loader.diagnostic();
            expect(eq(loader.manifest().title, std::string{"gr4-present"}));
            expect(eq(loader.manifest().views.size(), 1UZ));
            if (!loader.manifest().views.empty()) {
                expect(loader.manifest().views.front().markdown.contains("stay tuned"));
            }
            expect(!loader.imageBytes().empty()) << "the view's image was not fetched";
            expect(loader.missingAsset().empty());
            expect(eq(loader.progress(), 1.0f));
        };
    }

    "a package root that holds no presentation fails and says where it looked"_test = [] {
        const auto absent = std::filesystem::temp_directory_path() / "NoPresentation";
        std::filesystem::create_directories(absent); // the root exists; the manifest does not

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(absent.string());
        expect(settle(loader) == LoadState::failed);
        expect(!loader.diagnostic().empty());
        expect(eq(std::string{loader.baseUri()}, absent.string())) << "the failure must name the root it tried";
        expect(loader.manifest().views.empty());
        expect(loader.imageBytes().empty());
    };

    "a missing package fails with the offending location"_test = [] {
        PresentationLoader loader;
        loader.retryPolicy.enabled = false; // otherwise it would keep trying, which is the point of the next test
        loader.begin((std::filesystem::temp_directory_path() / "gr4-present-absent").string());
        expect(settle(loader) == LoadState::failed);
        expect(!loader.diagnostic().empty());
        expect(eq(loader.attempts(), 1UZ));
    };

    "a failure is retried on a backoff rather than being final"_test = [] {
        PresentationLoader loader;
        loader.retryPolicy.initialDelay = std::chrono::milliseconds{5};
        loader.retryPolicy.maximumDelay = std::chrono::milliseconds{20};
        loader.begin((std::filesystem::temp_directory_path() / "gr4-present-absent").string());
        expect(settle(loader) == LoadState::failed);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (loader.attempts() < 3UZ && std::chrono::steady_clock::now() < deadline) {
            loader.advance();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        expect(loader.attempts() >= 3UZ) << "the loader stopped trying";
    };

    "a package whose asset is missing still presents its view"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / "gr4-present-partial";
        std::filesystem::create_directories(directory);
        std::ofstream{directory / "index.yml"} << "format: gr4-presentation/1\ntitle: partial\nentry: only\nviews:\n  - id: only\n    image: absent.png\n    markdown: |\n      ## still here\n";

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(directory.string());
        expect(settle(loader) == LoadState::ready) << loader.diagnostic();
        expect(eq(loader.manifest().views.size(), 1UZ));
        if (!loader.manifest().views.empty()) {
            expect(loader.manifest().views.front().markdown.contains("still here"));
        }
        expect(eq(std::string{loader.missingAsset()}, std::string{"absent.png"})) << "the missing asset was not reported";
        expect(loader.imageBytes().empty());
    };

    "references resolve against the base, whatever the scheme"_test = [] {
        PresentationLoader loader;
        loader.begin("https://example.invalid/talks/demo");
        expect(eq(loader.resolve("figures/a.svg"), std::string{"https://example.invalid/talks/demo/figures/a.svg"}));

        PresentationLoader trailing;
        trailing.begin("/tmp/demo/");
        expect(eq(trailing.resolve("index.yml"), std::string{"/tmp/demo/index.yml"}));
    };

    "a loader that was never started is idle"_test = [] {
        const PresentationLoader loader;
        expect(loader.state() == LoadState::idle);
        expect(eq(loader.progress(), 0.0f));
    };
};

int main() { return 0; }
