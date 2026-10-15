// Load and Save natively, through SDL3's own file dialogs: a stand-in `zenity` answers each dialog with a path, so SDL's
// dialog driver, the thread it answers on and the viewer's answer run as they do when a person chooses.
//
// Ground truth: after Ctrl+O the window shows what a launch of the chosen deck shows; Ctrl+S writes <deck name>.tar.gz
// into the downloads folder (XDG's, pointed at a temporary one), which GNU tar unpacks to the deck's files byte for
// byte; a second Save writes <deck name> (1).tar.gz beside it, as a browser names a second download.
// A lone Markdown file loads as a deck of its own, a folder without index.yml as its least nested Markdown file, and the
// .tar.gz Save wrote as the deck it came from. A file that is no deck, a folder without one, a manifest whose entry
// is missing and a Markdown file that is not there are refused, the window staying as it was; a deck that never loaded
// writes nothing.

#include "NativeDisplay.hpp"

#include "FileContents.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

using namespace boost::ut;
using namespace std::chrono_literals;

namespace {
using gr::present::e2e::Box;
using gr::present::e2e::Pixels;
using gr::present::e2e::Screen;

constexpr Box kWindow{.x = 0, .y = 0, .width = 1280, .height = 720};

/// waits until the viewer has drawn and its picture holds still for a second, as long as a loaded machine needs
[[nodiscard]] Pixels settled(const Screen& screen) {
    Pixels last;
    for (auto deadline = std::chrono::steady_clock::now() + 30s; std::chrono::steady_clock::now() < deadline; std::this_thread::sleep_for(1s)) {
        Pixels     now   = screen.pixels(kWindow, 4);
        const bool drawn = std::ranges::any_of(now, [](const auto& row) { return std::ranges::any_of(row, [](std::uint32_t pixel) { return pixel != 0U; }); });
        if (drawn && std::ranges::equal(now, last)) {
            return now;
        }
        last = std::move(now);
    }
    return last;
}

/// the files under `root`, by path relative to it, sorted
[[nodiscard]] std::vector<std::string> filesUnder(const std::filesystem::path& root) {
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().lexically_relative(root).string());
        }
    }
    std::ranges::sort(files);
    return files;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return 2; // usage: qa_NativeLoadSave <gr4-present executable>
    }
    const std::filesystem::path viewerUnderTest = std::filesystem::absolute(argv[1]);

    "Ctrl+O loads the deck the dialog names, Ctrl+S saves it as a .tar.gz in the downloads folder"_test = [&viewerUnderTest] {
        const gr::present::e2e::VirtualDisplay display;
        expect(fatal(!display.name().empty())) << "Xvfb did not start";

        const std::filesystem::path root = std::filesystem::temp_directory_path() / "gr4-present-native-load-save";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "bin");
        std::filesystem::create_directories(root / "downloads");
        std::filesystem::create_directories(root / "config");
        std::ofstream{root / "config" / "user-dirs.dirs"} << std::format("XDG_DOWNLOAD_DIR=\"{}\"\n", (root / "downloads").string());
        ::setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
        std::filesystem::copy(GR4_PRESENT_REMOTE_DECK, root / "deck", std::filesystem::copy_options::recursive);
        // Load's dialog as a person would answer it, from a file the test rewrites
        std::ofstream{root / "bin" / "zenity"} << "#!/bin/sh\n"
                                                  "for argument in \"$@\"; do [ \"$argument\" = --version ] && { echo 4.0.0; exit 0; }; done\n"
                                                  "cat \"$GR4_PRESENT_TEST_LOAD_FROM\"\n";
        std::filesystem::permissions(root / "bin" / "zenity", std::filesystem::perms::owner_all);
        ::setenv("SDL_FILE_DIALOG_DRIVER", "zenity", 1); // never the desktop's portal: no dialog opens on a real screen
        ::setenv("PATH", std::format("{}:{}", (root / "bin").string(), std::getenv("PATH") != nullptr ? std::getenv("PATH") : "").c_str(), 1);
        const auto answer = [&root](const std::filesystem::path& chosen) { std::ofstream{root / "load-answer"} << chosen.string() << "\n"; };
        ::setenv("GR4_PRESENT_TEST_LOAD_FROM", (root / "load-answer").c_str(), 1);
        // the deck shown first holds still, unlike the built-in one's moving intro: a refusal must leave it as it was
        std::filesystem::create_directories(root / "first");
        std::ofstream{root / "first" / "first.md"} << "# The deck shown first {#first}\n\nnothing on it moves\n";
        std::filesystem::create_directories(root / "no-deck");
        std::filesystem::create_directories(root / "no-entry");
        std::ofstream{root / "no-entry" / "index.yml"} << "format: gr4-presentation/1\nentry: talk.md\n";
        std::ofstream{root / "notes.txt"} << "no deck\n";

        Pixels                   firstShown;
        Pixels                   afterRefusals;
        Pixels                   loneMarkdown;
        std::vector<Pixels>      between;
        Pixels                   loaded;
        std::vector<std::string> afterFirstSave;
        std::vector<std::string> afterSecondSave;
        Pixels                   folderLoaded;
        Pixels                   archiveLoaded;
        {
            gr::present::e2e::ViewerProcess process{viewerUnderTest, display.name(), {"--windowed", "--presentation=" + (root / "first" / "first.md").string()}};
            std::this_thread::sleep_for(2s);
            Screen screen{display.name()};
            firstShown = settled(screen);
            answer(root / "notes.txt");
            screen.chord("Control_L", "o");
            answer(root / "no-deck" / "index.yml");
            screen.chord("Control_L", "o");
            answer(root / "no-entry" / "index.yml");
            screen.chord("Control_L", "o");
            answer(root / "absent.md");
            screen.chord("Control_L", "o");
            afterRefusals = settled(screen);
            answer(root / "deck" / "talk.md"); // a lone Markdown file, a deck of its own
            screen.chord("Control_L", "o");
            loneMarkdown = settled(screen);
            answer(root / "deck" / "index.yml");
            {
                // a connection of its own reads the screen while Ctrl+O is pressed and the deck loads and blends in
                const std::jthread reader{[&display, &between] {
                    const Screen watcher{display.name()};
                    for (const auto until = std::chrono::steady_clock::now() + 4s; std::chrono::steady_clock::now() < until;) {
                        between.push_back(watcher.pixels(kWindow, 4));
                    }
                }};
                screen.chord("Control_L", "o");
            }
            loaded = settled(screen);
            screen.chord("Control_L", "s");
            for (auto deadline = std::chrono::steady_clock::now() + 10s; !std::filesystem::exists(root / "downloads" / "remote-deck.tar.gz") && std::chrono::steady_clock::now() < deadline;) {
                std::this_thread::sleep_for(200ms);
            }
            afterFirstSave = filesUnder(root / "downloads");
            screen.chord("Control_L", "s");
            std::this_thread::sleep_for(2s);
            afterSecondSave = filesUnder(root / "downloads");
            answer(root / "first"); // a folder without index.yml: its .md
            screen.chord("Control_L", "o");
            folderLoaded = settled(screen);
            answer(root / "downloads" / "remote-deck.tar.gz"); // what Save wrote
            screen.chord("Control_L", "o");
            archiveLoaded = settled(screen);
        }
        std::vector<std::string> afterUnloadedSave;
        {
            gr::present::e2e::ViewerProcess process{viewerUnderTest, display.name(), {"--windowed", "--presentation=" + (root / "absent").string()}};
            std::this_thread::sleep_for(3s);
            Screen screen{display.name()};
            screen.chord("Control_L", "s");
            std::this_thread::sleep_for(1s);
            afterUnloadedSave = filesUnder(root / "downloads");
        }
        Pixels launched;
        {
            gr::present::e2e::ViewerProcess process{viewerUnderTest, display.name(), {"--windowed", "--presentation=" + (root / "deck").string()}};
            std::this_thread::sleep_for(2s);
            Screen screen{display.name()};
            launched = settled(screen);
        }

        expect(std::ranges::equal(afterRefusals, firstShown)) << "a file that is no deck, a folder without one or without its entry replaced the deck shown";
        expect(afterUnloadedSave == afterSecondSave) << "Save wrote a deck that never loaded";
        expect(!std::ranges::equal(loneMarkdown, firstShown)) << "a lone Markdown file did not load";
        expect(!std::ranges::equal(loaded, firstShown)) << "Ctrl+O changed nothing";
        expect(std::ranges::equal(loaded, launched)) << "the loaded deck differs from a launch of it";
        expect(between.size() > 10UZ) << "only " << between.size() << " frames were read during the load";
        const auto changes     = between | std::views::transform([&](const Pixels& frame) { return gr::present::e2e::pixelsOutsideBlend(frame, loneMarkdown, launched); }) | std::ranges::to<std::vector<std::string>>();
        const auto firstChange = std::ranges::find_if(changes, [](const std::string& change) { return !change.empty(); });
        expect(firstChange == changes.end()) << "frames with a pixel outside the blend of the two pictures:" << std::ranges::count_if(changes, [](const std::string& change) { return !change.empty(); }) << "of" << changes.size() << "; the first, frame" << (firstChange - changes.begin()) << ":" << (firstChange == changes.end() ? std::string{} : *firstChange);
        expect(std::ranges::equal(folderLoaded, firstShown)) << "a folder without index.yml did not open its .md";
        expect(std::ranges::equal(archiveLoaded, launched)) << "the .tar.gz Save wrote differs, loaded, from a launch of its deck";
        expect(afterFirstSave == std::vector<std::string>{"remote-deck.tar.gz"}) << "Save wrote " << afterFirstSave.size() << " files";
        expect(afterSecondSave == std::vector<std::string>{"remote-deck (1).tar.gz", "remote-deck.tar.gz"}) << "a second Save is named as a second download";
        std::filesystem::create_directories(root / "unpacked");
        const std::string unpack = std::format("tar -xzf '{}' -C '{}'", (root / "downloads" / "remote-deck.tar.gz").string(), (root / "unpacked").string());
        expect(fatal(std::system(unpack.c_str()) == 0)) << "GNU tar cannot unpack the archive";
        expect(filesUnder(root / "unpacked") == std::vector<std::string>{"remote-deck/figures/square.svg", "remote-deck/index.yml", "remote-deck/talk.md"});
        for (const std::string file : {"index.yml", "talk.md", "figures/square.svg"}) {
            expect(fileContents(root / "unpacked" / "remote-deck" / file) == fileContents(root / "deck" / file)) << file << " differs from the deck's";
        }
        std::filesystem::remove_all(root);
    };

    return 0;
}
