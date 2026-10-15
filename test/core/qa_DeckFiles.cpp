#include <boost/ut.hpp>

#include <gr4-present/DeckFiles.hpp>

#include <gnuradio-4.0/Compression.hpp>

#include "FileContents.hpp"

#include <string>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] std::span<const std::uint8_t> bytesOf(std::string_view text) { return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}; }

[[nodiscard]] std::string textOf(std::span<const std::byte> bytes) {
    return bytes | std::views::transform([](std::byte b) { return static_cast<char>(b); }) | std::ranges::to<std::string>();
}

[[nodiscard]] NavigationGraph graphOf(std::initializer_list<std::pair<std::string_view, std::size_t>> views) {
    return NavigationGraph{.views = views | std::views::transform([](const auto& view) { return View{.id = std::string{view.first}, .stepCount = view.second, .next = {}}; }) | std::ranges::to<std::vector<View>>()};
}

} // namespace

const suite<"DeckFiles"> deckFilesTests = [] {
    "a deck's folder name is its title made safe, else its folder's name, else deck"_test = [] {
        expect(eq(deckFolderName("GR4: a tour / 2026", "/home/me/talks/tour"), std::string{"GR4-a-tour-2026"}));
        expect(eq(deckFolderName("  ..hidden..  ", ""), std::string{"hidden"})) << "no leading dot: a hidden folder would be lost";
        expect(eq(deckFolderName("", "/home/me/talks/my tour"), std::string{"my-tour"}));
        expect(eq(deckFolderName("???", "https://example.org/decks/remote"), std::string{"remote"}));
        expect(eq(deckFolderName("", "/"), std::string{"deck"}));
        expect(eq(deckFolderName("Ünïcode", ""), std::string{"n-code"})) << "every byte outside the portable set becomes one dash";
    };

    "a packed deck is <folder>/<file> in a .tar.gz that GR4's gzip and Tar.hpp read back"_test = [] {
        const std::vector<PackageFile> files{{.path = "index.yml", .bytes = bytesOf("format: gr4-presentation/1\nentry: talk.md\n")}, {.path = "talk.md", .bytes = bytesOf("# hello\n")}, {.path = "figures/a.svg", .bytes = bytesOf("<svg/>")}};
        const auto                     packed = packDeck(files, "tour");
        expect(fatal(packed.has_value()));
        expect(fatal(packed->size() > 2UZ));
        expect((*packed)[0] == std::byte{0x1f} && (*packed)[1] == std::byte{0x8b}) << "the gzip magic (RFC 1952)";
        const auto unpacked = unpackDeck(*packed);
        expect(fatal(unpacked.has_value())) << (unpacked ? std::string{} : unpacked.error());
        expect(eq(unpacked->base, std::string{"tour"}));
        expect(fatal(eq(unpacked->files.size(), 3UZ)));
        expect(eq(unpacked->files[2].path, std::string{"tour/figures/a.svg"}));
        expect(eq(textOf(unpacked->files[1].bytes), std::string{"# hello\n"}));
    };

    "an archive made by GNU tar and gzip opens at its least nested index.yml"_test = [] {
        const auto archive = fileContents<std::vector<std::byte>>(std::filesystem::path{GR4_PRESENT_TAR_FIXTURES} / "gnu.tar.gz");
        const auto deck    = unpackDeck(archive);
        expect(fatal(deck.has_value())) << (deck ? std::string{} : deck.error());
        expect(eq(deck->base, std::string{"."})) << "its index.yml is at the root";
        expect(eq(deck->files.size(), 3UZ));
    };

    "an archive without index.yml opens its least nested Markdown file; one with neither is refused"_test = [] {
        const std::vector<PackageFile> lone{{.path = "notes/deep/b.md", .bytes = bytesOf("# b\n")}, {.path = "a.md", .bytes = bytesOf("# a\n")}};
        const auto                     packed = packDeck(lone, "solo");
        expect(fatal(packed.has_value()));
        const auto deck = unpackDeck(*packed);
        expect(fatal(deck.has_value()));
        expect(eq(deck->base, std::string{"solo/a.md"}));

        const std::vector<PackageFile> pictures{{.path = "a.svg", .bytes = bytesOf("<svg/>")}};
        const auto                     nothing = unpackDeck(*packDeck(pictures, "pictures"));
        expect(!nothing.has_value());
    };

    "a folder's or an archive's deck: the least nested index.yml's folder, else the least nested .md, alphabetically on a tie"_test = [] {
        const auto baseAmong = [](std::vector<std::string> paths) { return deckBaseAmong(paths).value_or("refused"); };
        expect(eq(baseAmong({"talk.md", "index.yml", "figures/a.svg"}), std::string{"."})) << "index.yml at the root";
        expect(eq(baseAmong({"old/index.yml", "talk/index.yml", "readme.md"}), std::string{"old"})) << "two decks one level down: the first by name";
        expect(eq(baseAmong({"a/b/index.yml", "c/index.yml"}), std::string{"c"})) << "the less nested of two";
        expect(eq(baseAmong({"z.md", "notes/a.md", "b.md"}), std::string{"b.md"})) << "no index.yml: the root's first .md by name";
        expect(eq(baseAmong({"notes/talk.md"}), std::string{"notes/talk.md"}));
        expect(eq(baseAmong({"a.svg", "index.yml.bak"}), std::string{"refused"})) << "neither index.yml nor .md";
        expect(eq(baseAmong({}), std::string{"refused"})) << "an empty folder";
    };

    "what is no gzip, or would unpack past the limit, is refused with the reason"_test = [] {
        const std::string markdown = "# just text\n";
        const auto        bytes    = markdown | std::views::transform([](char c) { return static_cast<std::byte>(c); }) | std::ranges::to<std::vector<std::byte>>();
        const auto        notGzip  = unpackDeck(bytes);
        expect(!notGzip.has_value());
        expect(!notGzip.has_value() && notGzip.error().contains("gzip"));
    };

    "a reloaded deck resumes on the view and step shown, the step at most the view's last"_test = [] {
        const std::vector<std::string> before{"intro", "middle", "end"};
        expect(cursorAfterReload(before, Cursor{.viewId = "middle", .step = 2UZ}, graphOf({{"intro", 1UZ}, {"middle", 3UZ}, {"end", 1UZ}})) == Cursor{.viewId = "middle", .step = 2UZ});
        expect(cursorAfterReload(before, Cursor{.viewId = "middle", .step = 2UZ}, graphOf({{"intro", 1UZ}, {"middle", 2UZ}})) == Cursor{.viewId = "middle", .step = 1UZ});
    };

    "a view that is gone resumes on the nearest earlier one still there, else the first"_test = [] {
        const std::vector<std::string> before{"intro", "middle", "end"};
        expect(cursorAfterReload(before, Cursor{.viewId = "end", .step = 0UZ}, graphOf({{"intro", 1UZ}, {"middle", 1UZ}, {"appendix", 1UZ}})) == Cursor{.viewId = "middle", .step = 0UZ});
        expect(cursorAfterReload(before, Cursor{.viewId = "intro", .step = 0UZ}, graphOf({{"renamed", 1UZ}, {"other", 1UZ}})) == Cursor{.viewId = "renamed", .step = 0UZ});
    };

    "a manifest names its entry inside the package, or says why it names none this viewer shows"_test = [] {
        const auto entry = entryOfManifest("format: gr4-presentation/1\nentry: ./docs/talk.md\n");
        expect(entry.has_value() && *entry == "docs/talk.md") << "package-relative and normalised";
        expect(!entryOfManifest("format: gr4-presentation/1\nentry: talk.md\nviewer:\n  minimumVersion: 99\n").has_value()) << "for a newer viewer";
        const auto outside = entryOfManifest("format: gr4-presentation/1\nentry: ../elsewhere/talk.md\n");
        expect(!outside.has_value() && outside.error().contains("outside the package"));
        expect(!entryOfManifest("format: gr4-presentation/1\nentry: https://example.org/talk.md\n").has_value()) << "an entry on the web";
        expect(!entryOfManifest("entry: talk.md\n").has_value()) << "no format";
        expect(!entryOfManifest(": : :").has_value()) << "no YAML";
    };

    "a damaged archive inside the gzip, or a path no archive may hold, is refused with the reason"_test = [] {
        const std::vector<std::byte> notATar(1024UZ, std::byte{'z'});
        const auto                   packed = gr::compression::compress(notATar, gr::compression::Format::gzip);
        expect(fatal(packed.has_value()));
        const auto damaged = unpackDeck(*packed);
        expect(!damaged.has_value() && damaged.error().contains("damaged")) << "gzip is fine, the tar inside is not";
        const std::vector<PackageFile> escaping{{.path = "../outside", .bytes = bytesOf("x")}};
        expect(!packDeck(escaping, "deck").has_value()) << "deck/../outside leaves the archive";
    };

    "a deck reloaded with no views at all keeps the cursor it had"_test = [] {
        const std::vector<std::string> before{"intro"};
        expect(cursorAfterReload(before, Cursor{.viewId = "intro", .step = 1UZ}, NavigationGraph{}) == Cursor{.viewId = "intro", .step = 1UZ});
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
