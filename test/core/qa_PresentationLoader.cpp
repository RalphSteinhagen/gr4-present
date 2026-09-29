#include <boost/ut.hpp>

#include <gr4-present/Diagnostics.hpp>
#include <gr4-present/Markdown.hpp>
#include <gr4-present/Navigation.hpp>
#include <gr4-present/Plot.hpp>
#include <gr4-present/PresentationLoader.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
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
            expect(eq(loader.manifest().entry, std::string{"talk.md"}));
            expect(loader.documentSource().contains("gr4-present")) << "the entry document was not fetched";
            // one reference of each kind the walk has to recognise: an image span, a layout field, a plot header's
            // source and a video directive's src. A kind the walk forgets is not "missing" -- it is never asked
            // for -- so nothing else here would notice.
            expect(!loader.figureBytes("figures/chain.svg").empty()) << "a figure the document references was not fetched";
            expect(!loader.figureBytes("figures/master.svg").empty()) << "a layout master was not fetched";
            expect(!loader.figureBytes("media/horse-in-motion.gif").empty()) << "an image the document shows was not fetched";
            expect(!loader.figureBytes("data/bode.csv").empty()) << "a plot's `source:` was not fetched";
            expect(!loader.figureBytes("media/Steamboat_Willie_(1928)_by_Walt_Disney_extract.webm").empty()) << "a video's `src:` was not fetched";
            expect(loader.missingAsset().empty()) << "unexpectedly missing: " << loader.missingAsset();
            expect(eq(loader.progress(), 1.0f));
        };
    }

    // Every reference the shipped document makes must have been fetched. A kind of reference the walk does not
    // know about is never asked for, so it is not "missing" either -- nothing else in this file would notice, and
    // the plot CSV and the video clip were both silently unfetched until this existed.
    "every asset the default presentation refers to is fetched"_test = [] {
        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(GR4_PRESENT_DEFAULT_PRESENTATION);
        expect(settle(loader) == LoadState::ready) << loader.diagnostic();

        const Document           document = parseMarkdown(loader.documentSource());
        std::vector<std::string> referenced;
        const auto               note = [&referenced](std::string_view target) {
            if (!target.empty() && !target.starts_with("http") && std::ranges::find(referenced, target) == referenced.end()) {
                referenced.emplace_back(target);
            }
        };

        // a `:::<name>` box holds a document of its own, and what it refers to is as much the deck's as the slide's
        std::vector<Document> scanned{document};
        for (const Block& block : document.blocks) {
            if (block.kind == BlockKind::directive && !block.lines.empty() && block.info != "layout" && block.info != "plot" && block.info != "video" && block.info != "gr4" && block.info != "regions" && block.info != "grid" && block.info != "view") {
                scanned.push_back(boxDocumentOf(block.lines, document));
            }
        }

        for (const Block& block : scanned | std::views::transform(&Document::blocks) | std::views::join) {
            for (const InlineSpan& span : block.spans) {
                if (span.kind == InlineKind::image) {
                    note(span.target);
                }
            }
            if (block.kind == BlockKind::plot) {
                for (const std::string& line : block.lines) {
                    if (const auto [key, value] = fieldOf(line); key == "source") {
                        note(value);
                    }
                }
            }
            if (block.kind == BlockKind::directive && block.info == "video") {
                note(block.field("src"));
            }
            if (block.kind == BlockKind::directive && block.info == "gr4") {
                note(block.field("workflow"));
            }
            if (block.kind == BlockKind::directive && (block.info == "regions" || block.info == "grid")) {
                // every other key of a regions directive names a region of the picture, and a region is not a file
                note(block.field("source"));
            }
            // an effect is the deck's file when the deck has one, else the viewer's own and nothing to fetch
            const auto noteEffect = [&](std::string_view value) {
                std::string_view effect = value.substr(0UZ, value.find(' '));
                if (effect == "camera" || effect == "zoom") {
                    value  = value.substr(std::min(value.find(' '), value.size()));
                    value  = value.substr(std::min(value.find_first_not_of(' '), value.size()));
                    effect = value.substr(0UZ, value.find(' '));
                }
                const std::string path = std::format("effects/{}.glsl", effect);
                if (!effect.empty() && std::filesystem::exists(std::filesystem::path{GR4_PRESENT_DEFAULT_PRESENTATION} / path)) {
                    note(path);
                }
            };
            noteEffect(block.revealKind);
            if (block.kind == BlockKind::directive) {
                noteEffect(block.field("in"));
                noteEffect(block.field("overlay"));
            }
            if (block.kind == BlockKind::directive && block.info == "shader") {
                noteEffect(block.field("effect"));
            }
            if (block.kind == BlockKind::directive && block.info == "stage") {
                for (const auto piece : std::views::split(block.field("transitions"), ',')) {
                    std::string_view value{piece.begin(), piece.end()};
                    noteEffect(value.substr(std::min(value.find_first_not_of(' '), value.size())));
                }
            }
            if (block.kind == BlockKind::directive && block.info == "layout") {
                noteEffect(block.field("transition"));
                noteEffect(block.field("background"));
                noteEffect(block.field("overlay"));
                for (const auto& [key, value] : block.fields) {
                    // anchor, region and via name places within the picture; transition, camera, outline and rotate name
                    // behaviour; background and overlay name effects
                    if (key != "anchor" && key != "transition" && key != "region" && key != "via" && key != "camera" && key != "grid" && key != "duration" && key != "advance" && key != "outline" && key != "rotate" && key != "background" && key != "overlay") {
                        note(schemeFilesOf(value).light);
                        note(schemeFilesOf(value).dark);
                    }
                }
            }
        }

        for (const Document::StepReveal& reveal : document.stepReveals) {
            if (const std::string path = std::format("effects/{}.glsl", reveal.kind); std::filesystem::exists(std::filesystem::path{GR4_PRESENT_DEFAULT_PRESENTATION} / path)) {
                note(path);
            }
        }
        expect(gt(referenced.size(), 5UZ)) << "the demo deck should refer to rather more than this";
        for (const std::string& reference : referenced) {
            expect(!loader.figureBytes(reference).empty()) << reference << " is referenced by the deck and was never fetched";
        }
    };

    // The shipped deck, walked the way the viewer walks it. A view that cannot be left strands the presenter
    // mid-talk with no way forward but the menu, and nothing else here would notice: every other test builds its
    // own little graph, so only the real document can show that the real document is navigable.
    "every view of the default presentation can be reached by pressing forward"_test = [] {
        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(GR4_PRESENT_DEFAULT_PRESENTATION);
        while (loader.state() == LoadState::loading) {
            loader.advance();
        }
        expect(loader.state() == LoadState::ready) << loader.diagnostic();

        const std::vector<Section> sections = sectionsOf(parseMarkdown(loader.documentSource()));
        Navigator                  navigator{.graph = graphOf(sections), .cursor = {}, .history = {}};
        expect(navigator.graph.validate().has_value()) << (navigator.graph.validate().has_value() ? std::string_view{} : message(navigator.graph.validate().error()));
        expect(gt(navigator.graph.views.size(), 10UZ)) << "the demo deck should have rather more views than this";

        navigator.cursor = Cursor{.viewId = navigator.graph.views.front().id, .step = 0UZ};
        std::vector<std::string> reached{navigator.cursor.viewId};

        // a step count per view, so the bound is the deck's own size rather than an arbitrary number
        const std::size_t steps = std::ranges::fold_left(navigator.graph.views, 0UZ, [](std::size_t total, const View& view) { return total + view.stepCount; });
        for (std::size_t press = 0UZ; press < steps + 1UZ; ++press) {
            if (!navigator.next()) {
                break;
            }
            if (std::ranges::find(reached, navigator.cursor.viewId) == reached.end()) {
                reached.push_back(navigator.cursor.viewId);
            }
        }

        // Branches are the one exception: `branch-b` is reachable only by choosing it, and forward takes `branch-a`. A
        // detour may run over several slides -- the transitions tour does -- so what forward reaches from a chosen
        // branch, until it is back on the main path, is the detour's too.
        std::vector<std::string> detours;
        for (const View& view : navigator.graph.views) {
            const bool chosen = std::ranges::any_of(navigator.graph.views, [&view](const View& other) { return other.next.size() > 1UZ && std::ranges::find(other.next, view.id) != other.next.end() && other.next.front() != view.id; });
            if (!chosen) {
                continue;
            }
            Navigator detour{.graph = navigator.graph, .cursor = Cursor{.viewId = view.id, .step = 0UZ}, .history = {}};
            detours.push_back(view.id);
            for (std::size_t press = 0UZ; press < steps + 1UZ && detour.next() && std::ranges::find(reached, detour.cursor.viewId) == reached.end(); ++press) {
                detours.push_back(detour.cursor.viewId);
            }
        }
        std::vector<std::string> unreached;
        for (const View& view : navigator.graph.views) {
            if (std::ranges::find(detours, view.id) == detours.end() && std::ranges::find(reached, view.id) == reached.end()) {
                unreached.push_back(view.id);
            }
        }
        expect(unreached.empty()) << "pressing forward never reaches: " << std::ranges::fold_left(unreached, std::string{}, [](std::string all, const std::string& id) { return all.empty() ? id : all + ", " + id; });
    };

    // Ground truth is a package written here, so what the loader should fetch is known independently of the loader.
    "a picture cited inside a box is fetched, and a layout's timing fields are not taken for assets"_test = [] {
        const auto root = std::filesystem::temp_directory_path() / "gr4-present-box-scan";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "media");
        const auto write = [&root](std::string_view name, std::string_view content) { std::ofstream{root / name, std::ios::binary} << content; };
        write("index.yml", "format: gr4-presentation/1\ntitle: box scan\nentry: talk.md\n\nviewer:\n  minimumVersion: 0.1\n");
        write("talk.md", "# Boxes\n\n:::layout\ngrid: [(left), (right)]\nduration: 2\nadvance: 4\n:::\n\n:::left\n![a picture](media/in-box.png)\n:::\n\n:::right\nwords\n:::\n");
        write("media/in-box.png", "not really a picture");

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(root.string());
        expect(settle(loader) == LoadState::ready) << loader.diagnostic();
        expect(!loader.figureBytes("media/in-box.png").empty()) << "a picture cited only inside a box was never asked for";
        expect(loader.missingAsset().empty()) << "asked for something that is not an asset: " << loader.missingAsset();
        expect(eq(loader.diagnostics.count(DiagnosticKind::missingResource), 0UZ)) << "the timing fields were fetched as if they named files";
    };

    // Ground truth is the syntax: every place a deck can name an effect, each naming a different file, so a place the
    // loader overlooks shows as that file unfetched. A bare name the deck lacks is the viewer's own and no error.
    "every effect a deck names is fetched with the assets its header asks for, and a bundled name is not missing"_test = [] {
        const auto root = std::filesystem::temp_directory_path() / "gr4-present-effect-scan";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "effects");
        const auto write = [&root](std::string_view name, std::string_view content) { std::ofstream{root / name, std::ios::binary} << content; };
        write("index.yml", "format: gr4-presentation/1\ntitle: effect scan\nentry: talk.md\nbackground: deckwide\npointer: dot\nbreak:\n  effect: pause minutes=1\n  minutes: 5\n\nviewer:\n  minimumVersion: 0.1\n");
        write("talk.md", "# One\n\n:::layout\ngrid: [(left), (right)]\ntransition: swirl speed=2\nbackground: waves\noverlay: tv\n:::\n\n:::left {in=burn overlay=glass overlay.k=1}\nwords\n:::\n\n:::right\nmore\n:::\n\n:::shader\nid: view\nregion: right\neffect: lava\nfallback: media/lava.png\n:::\n\n:::step {in=sparkle}\n\nlater\n\n# Two\n\n:::layout\ntransition: zoom lens\n:::\n\n# Three\n\n:::layout\ntransition: crt\n:::\n");
        constexpr std::array<std::string_view, 11> kNamed{"deckwide", "dot", "pause", "swirl", "waves", "tv", "burn", "glass", "lava", "sparkle", "lens"};
        for (const std::string_view effect : kNamed) {
            write(std::format("effects/{}.glsl", effect), "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n");
        }
        write("effects/lava.glsl", "// @channel0 rock.png\nvoid mainImage(out vec4 c, in vec2 p) { c = texture(iChannel0, p); }\n");
        write("effects/rock.png", "not really a picture");
        std::filesystem::create_directories(root / "media");
        write("media/lava.png", "not really a picture either");

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(root.string());
        expect(settle(loader) == LoadState::ready) << loader.diagnostic();
        for (const std::string_view effect : kNamed) {
            expect(!loader.figureBytes(std::format("effects/{}.glsl", effect)).empty()) << effect << " is named by the deck and was never fetched";
        }
        expect(!loader.figureBytes("effects/rock.png").empty()) << "the asset lava's header asks for was never fetched";
        expect(!loader.figureBytes("media/lava.png").empty()) << "the viewport's fallback still was never fetched";
        expect(loader.figureBytes("effects/crt.glsl").empty());
        expect(eq(loader.diagnostics.count(DiagnosticKind::missingResource), 0UZ)) << "the viewer's own crt was reported missing";
    };

    "a package root that holds no presentation fails and says where it looked"_test = [] {
        const auto absent = std::filesystem::temp_directory_path() / "NoPresentation";
        std::filesystem::create_directories(absent); // the root exists; the manifest does not

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(absent.string());
        expect(settle(loader) == LoadState::failed);
        expect(!loader.diagnostic().empty());
        expect(eq(loader.diagnostics.count(DiagnosticKind::missingManifest), 1UZ)) << "a root without a manifest is reported as such";
        expect(loader.diagnostics.anyFatal()) << "there is nothing to show, so this is fatal";
        expect(eq(std::string{loader.baseUri()}, absent.string())) << "the failure must name the root it tried";
        expect(loader.documentSource().empty());
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

    "a manifest demanding a newer viewer is refused and reported"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / "gr4-present-future";
        std::filesystem::create_directories(directory);
        std::ofstream{directory / "index.yml"} << "format: gr4-presentation/1\ntitle: from the future\nentry: talk.md\nviewer:\n  minimumVersion: 99\n";
        std::ofstream{directory / "talk.md"} << "# nothing to see\n";

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(directory.string());
        expect(settle(loader) == LoadState::failed) << "showing a presentation written for a newer viewer would misrepresent it";
        expect(eq(loader.diagnostics.count(DiagnosticKind::versionMismatch), 1UZ));
        expect(loader.diagnostics.anyFatal());
    };

    "a document whose figure is missing still presents"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / "gr4-present-partial";
        std::filesystem::create_directories(directory);
        std::ofstream{directory / "index.yml"} << "format: gr4-presentation/1\ntitle: partial\nentry: talk.md\n";
        std::ofstream{directory / "talk.md"} << "## still here\n\n![absent](absent.png)\n";

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(directory.string());
        expect(settle(loader) == LoadState::ready) << loader.diagnostic();
        expect(loader.documentSource().contains("still here"));
        expect(eq(std::string{loader.missingAsset()}, std::string{"absent.png"})) << "the missing figure was not reported";
        expect(loader.figureBytes("absent.png").empty());
        expect(eq(loader.diagnostics.count(DiagnosticKind::missingResource), 1UZ)) << "the presenter is not told what is missing";
        expect(!loader.diagnostics.anyFatal()) << "a missing figure must not make the presentation unshowable";
    };

    // The figures are fetched several at a time. What that must not change: every one of them arrives, every one
    // that cannot be read is reported exactly once, and the bar only ever goes forwards. A deck with more figures
    // than the window holds is the case the old one-at-a-time loop could not get wrong and this one can.
    //
    // That they really are fetched at once is a property of the clock and is measured in a browser, not asserted
    // here: a turn of this loop advances every request in flight, so counting turns measures how many polls a
    // reader takes to deliver a file and says nothing about how many files are in the air.
    "a deck with more figures than can be fetched at once still fetches all of them"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / "gr4-present-many";
        std::filesystem::create_directories(directory);
        std::ofstream{directory / "index.yml"} << "format: gr4-presentation/1\ntitle: many\nentry: talk.md\n";
        std::ofstream talk{directory / "talk.md"};
        talk << "## many figures\n\n";
        constexpr std::size_t kFigures = 17UZ; // comfortably more than the window, and not a multiple of it
        for (std::size_t index = 0UZ; index < kFigures; ++index) {
            const std::string name = std::format("figure{}.svg", index);
            std::ofstream{directory / name} << std::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="10"/>)", index + 1UZ);
            talk << std::format("![figure {}]({})\n\n", index, name);
        }
        talk.close();

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(directory.string());

        float      highest  = 0.0f;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (loader.state() == LoadState::loading && std::chrono::steady_clock::now() < deadline) {
            loader.advance();
            expect(ge(loader.progress(), highest)) << "the bar went backwards, from " << highest << " to " << loader.progress();
            highest = std::max(highest, loader.progress());
        }
        expect(loader.state() == LoadState::ready) << loader.diagnostic();
        expect(eq(loader.progress(), 1.0f));
        for (std::size_t index = 0UZ; index < kFigures; ++index) {
            expect(!loader.figureBytes(std::format("figure{}.svg", index)).empty()) << "figure " << index << " was never fetched";
        }
        expect(loader.diagnostics.empty()) << "a complete package reported a problem";
        std::filesystem::remove_all(directory);
    };

    "every figure that cannot be read is reported, not just the first"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / "gr4-present-absent";
        std::filesystem::create_directories(directory);
        std::ofstream{directory / "index.yml"} << "format: gr4-presentation/1\ntitle: absent\nentry: talk.md\n";
        std::ofstream talk{directory / "talk.md"};
        talk << "## missing figures\n\n";
        constexpr std::size_t kMissing = 9UZ; // more than are ever in flight at once, so none may be dropped
        for (std::size_t index = 0UZ; index < kMissing; ++index) {
            talk << std::format("![absent {0}](absent{0}.png)\n\n", index);
        }
        talk.close();

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(directory.string());
        expect(settle(loader) == LoadState::ready) << loader.diagnostic();
        expect(eq(loader.diagnostics.count(DiagnosticKind::missingResource), kMissing)) << "the presenter was told about only some of them";
        expect(!loader.diagnostics.anyFatal()) << "missing figures must not make the presentation unshowable";
        expect(eq(loader.progress(), 1.0f));
        std::filesystem::remove_all(directory);
    };

    // A remote package is untrusted: a reference may not name anything outside the package's own tree. Ground truth
    // is a real file just outside it, which a reference climbing out with `..` would otherwise read.
    "a figure reference that climbs out of the package is refused and reported, and nothing outside is read"_test = [] {
        const auto outside   = std::filesystem::temp_directory_path() / "gr4-present-escape";
        const auto directory = outside / "package";
        std::filesystem::create_directories(directory);
        std::ofstream{outside / "secret.svg"} << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"/>";
        std::ofstream{directory / "index.yml"} << "format: gr4-presentation/1\ntitle: escape\nentry: talk.md\n";
        std::ofstream{directory / "talk.md"} << "## inside\n\n![out](../secret.svg)\n\n![in](figures/../inside.svg)\n";
        std::ofstream{directory / "inside.svg"} << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"/>";

        PresentationLoader loader;
        loader.retryPolicy.enabled = false;
        loader.begin(directory.string());
        expect(settle(loader) == LoadState::ready) << loader.diagnostic();
        expect(loader.figureBytes("../secret.svg").empty()) << "a file outside the package was read";
        expect(eq(std::string{loader.missingAsset()}, std::string{"../secret.svg"}));
        expect(eq(loader.diagnostics.count(DiagnosticKind::missingResource), 1UZ)) << "the refused reference is reported, once";
        expect(!loader.figureBytes("figures/../inside.svg").empty()) << "a `..` that stays inside the package is fine";
        expect(!loader.diagnostics.anyFatal());
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
