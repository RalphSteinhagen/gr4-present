#include <boost/ut.hpp>

#include "PresenterBundle.hpp"

#include <gr4-present/DeckFiles.hpp>

#include <gnuradio-4.0/Compression.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] std::span<const std::uint8_t> bytesOf(std::string_view text) { return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}; }

[[nodiscard]] std::string textOf(std::span<const std::byte> bytes) {
    return bytes | std::views::transform([](std::byte b) { return static_cast<char>(b); }) | std::ranges::to<std::string>();
}

constexpr std::string_view     kPage = "<!doctype html>\n<html lang=\"en\">\n  <head>\n    <meta name=\"gr4-site\" content=\"\" />\n    <script src=\"gr4-serviceworker.js\"></script>\n  </head>\n</html>\n";
constexpr std::string_view     kSite = "https://example.org/talks";
constexpr std::string_view     kWasm = "wasm bytes, as served";
const std::vector<PackageFile> kStagedSite{{.path = "index.html", .bytes = bytesOf(kPage)}, {.path = "index.js", .bytes = bytesOf("var Module;")}, {.path = "index.wasm", .bytes = bytesOf(kWasm)}, {.path = "gr4-serviceworker.js", .bytes = bytesOf("self.addEventListener('fetch', () => {});")}, {.path = "manifest.webmanifest", .bytes = bytesOf("{}")}, {.path = "icon.png", .bytes = bytesOf("PNG")}};
const std::vector<PackageFile> kDeck{{.path = "index.yml", .bytes = bytesOf("format: gr4-presentation/1\nentry: talk.md\n")}, {.path = "talk.md", .bytes = bytesOf("# hello\n")}, {.path = "figures/a.svg", .bytes = bytesOf("<svg/>")}};

[[nodiscard]] const gr::compression::TarEntry* entryNamed(const UnpackedDeck& deck, std::string_view path) {
    const auto found = std::ranges::find(deck.files, path, &gr::compression::TarEntry::path);
    return found == deck.files.end() ? nullptr : &*found;
}

} // namespace

const suite<"PresenterBundle"> presenterBundleTests = [] {
    "the bundle is the site's files, the deck under default/, five scripts and a README in one folder"_test = [] {
        const auto packed = packPresenterBundle("gr4-present-tour", PresenterBundle{.site = kSite, .title = "GR4: a tour", .siteFiles = kStagedSite, .deckFiles = kDeck});
        expect(fatal(packed.has_value())) << (packed ? std::string{} : packed.error());
        const auto unpacked = unpackDeck(*packed);
        expect(fatal(unpacked.has_value())) << (unpacked ? std::string{} : unpacked.error());
        expect(eq(unpacked->base, std::string{"gr4-present-tour/default"})) << "the deck is where the page looks without an address";
        const auto paths = unpacked->files | std::views::transform(&gr::compression::TarEntry::path) | std::ranges::to<std::vector<std::string>>();
        for (const std::string_view wanted : {"gr4-present-tour/index.html", "gr4-present-tour/index.js", "gr4-present-tour/index.wasm", "gr4-present-tour/gr4-serviceworker.js", "gr4-present-tour/manifest.webmanifest", "gr4-present-tour/icon.png", "gr4-present-tour/default/index.yml", "gr4-present-tour/default/talk.md", "gr4-present-tour/default/figures/a.svg", "gr4-present-tour/serve.sh", "gr4-present-tour/serve.cmd", "gr4-present-tour/serve.ps1", "gr4-present-tour/launch-chromium.sh", "gr4-present-tour/launch-chromium.cmd", "gr4-present-tour/README.txt"}) {
            expect(std::ranges::contains(paths, wanted)) << wanted << "is missing";
        }
        expect(eq(paths.size(), 15UZ));
        const auto wasm = entryNamed(*unpacked, "gr4-present-tour/index.wasm");
        expect(fatal(wasm != nullptr));
        expect(eq(textOf(wasm->bytes), std::string{kWasm})) << "a site file travels byte for byte";
        const auto figure = entryNamed(*unpacked, "gr4-present-tour/default/figures/a.svg");
        expect(fatal(figure != nullptr));
        expect(eq(textOf(figure->bytes), std::string{"<svg/>"}));
    };

    "the page names the site it came from, for its launch screen to offer when opened as a file"_test = [] {
        const auto packed   = packPresenterBundle("tour", PresenterBundle{.site = kSite, .title = "GR4: a tour", .siteFiles = kStagedSite, .deckFiles = kDeck});
        const auto unpacked = unpackDeck(*packed);
        expect(fatal(unpacked.has_value()));
        const auto page = entryNamed(*unpacked, "tour/index.html");
        expect(fatal(page != nullptr));
        const std::string text = textOf(page->bytes);
        expect(text.contains(R"(<meta name="gr4-site" content="https://example.org/talks" />)")) << text;
        expect(!text.contains(R"(content="")"));
        expect(text.contains(R"(<script src="gr4-serviceworker.js"></script>)")) << "and is otherwise as served";
    };

    "the scripts: serve.sh runs as ./serve.sh, the Windows ones are CR LF, the .cmd calls the .ps1, the launchers keep their profile apart"_test = [] {
        const auto packed   = packPresenterBundle("tour", PresenterBundle{.site = kSite, .title = "GR4: a tour", .siteFiles = kStagedSite, .deckFiles = kDeck});
        const auto unpacked = unpackDeck(*packed);
        expect(fatal(unpacked.has_value()));
        const auto sh = entryNamed(*unpacked, "tour/serve.sh");
        expect(fatal(sh != nullptr));
        expect(eq(sh->mode, 0755U));
        expect(textOf(sh->bytes).starts_with("#!/bin/sh\n"));
        expect(!textOf(sh->bytes).contains('\r'));
        const auto cmd = entryNamed(*unpacked, "tour/serve.cmd");
        const auto ps1 = entryNamed(*unpacked, "tour/serve.ps1");
        expect(fatal(cmd != nullptr && ps1 != nullptr));
        expect(eq(cmd->mode, 0644U));
        const auto crLfLines = [](std::string_view text) {
            std::size_t pairs = 0UZ;
            for (auto at = text.find("\r\n"); at != std::string_view::npos; at = text.find("\r\n", at + 2UZ)) {
                ++pairs;
            }
            return pairs > 0UZ && pairs == static_cast<std::size_t>(std::ranges::count(text, '\n'));
        };
        expect(crLfLines(textOf(cmd->bytes)) && crLfLines(textOf(ps1->bytes))) << "every line of the Windows scripts ends in CR LF";
        expect(textOf(cmd->bytes).contains("serve.ps1"));
        expect(textOf(ps1->bytes).contains("\r\n") && textOf(ps1->bytes).contains("Cross-Origin-Embedder-Policy")) << "the Windows server sends the headers itself";

        const auto launchSh  = entryNamed(*unpacked, "tour/launch-chromium.sh");
        const auto launchCmd = entryNamed(*unpacked, "tour/launch-chromium.cmd");
        expect(fatal(launchSh != nullptr && launchCmd != nullptr));
        expect(eq(launchSh->mode, 0755U));
        expect(!textOf(launchSh->bytes).contains('\r'));
        expect(crLfLines(textOf(launchCmd->bytes)));
        for (const std::string_view script : {textOf(launchSh->bytes), textOf(launchCmd->bytes)}) {
            for (const std::string_view wanted : {"--user-data-dir=", "--allow-file-access-from-files", "--enable-features=SharedArrayBuffer", "--app=", ".files"}) {
                expect(script.contains(wanted)) << wanted << "is not in a launcher: the switches only ever with a profile of its own";
            }
        }
    };

    "the README names the deck, the site, the scripts and what is where"_test = [] {
        const auto packed   = packPresenterBundle("tour", PresenterBundle{.site = kSite, .title = "GR4: a tour", .siteFiles = kStagedSite, .deckFiles = kDeck});
        const auto unpacked = unpackDeck(*packed);
        expect(fatal(unpacked.has_value()));
        const auto readme = entryNamed(*unpacked, "tour/README.txt");
        expect(fatal(readme != nullptr));
        const std::string text = textOf(readme->bytes);
        for (const std::string_view wanted : {"\"GR4: a tour\"", "https://example.org/talks", "serve.cmd", "sh serve.sh", "launch-chromium.cmd", "sh launch-chromium.sh", "default/", "http://localhost:8000/", "?presentation=<name>"}) {
            expect(text.contains(wanted)) << wanted << "is not in the README";
        }
    };

    "without a deck or a title the bundle still packs, and a page without the meta is carried as it is"_test = [] {
        const std::vector<PackageFile> plainPage{{.path = "index.html", .bytes = bytesOf("<html></html>")}};
        const auto                     packed = packPresenterBundle("gr4-present-deck", PresenterBundle{.site = kSite, .title = "", .siteFiles = plainPage, .deckFiles = {}});
        expect(fatal(packed.has_value())) << (packed ? std::string{} : packed.error());
        const auto unzipped = gr::compression::decompress(*packed, gr::compression::Format::gzip);
        expect(fatal(unzipped.has_value()));
        const auto files = gr::compression::unpackTar(*unzipped);
        expect(fatal(files.has_value()));
        expect(eq(files->size(), 7UZ)) << "the page, five scripts, the README";
        expect(eq(textOf((*files)[0].bytes), std::string{"<html></html>"}));
        expect(textOf(files->back().bytes).contains("\"untitled\""));
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
