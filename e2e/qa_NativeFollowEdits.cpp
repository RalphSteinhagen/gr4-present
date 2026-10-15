// A deck loaded from this computer follows its files: saving an edit reloads it, and it stays on the slide shown,
// blending from the old picture so that what the edit did not change does not flicker.
//
// Ground truth is a second launch of the edited deck opened at that slide, `--view second`: the reloaded window must
// show what that launch shows, and differ from what it showed before the edit; in every frame in between, each pixel
// stays between its two values, so one the edit did not change holds it (`pixelsOutsideBlend`).

#include "NativeDisplay.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <chrono>
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

void writeText(const std::filesystem::path& file, const std::string& text) { std::ofstream{file, std::ios::trunc} << text; }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return 2; // usage: qa_NativeFollowEdits <gr4-present executable>
    }
    const std::filesystem::path viewerUnderTest = std::filesystem::absolute(argv[1]);

    "an edit saved to a loaded deck's file reloads it on the slide shown"_test = [&viewerUnderTest] {
        const gr::present::e2e::VirtualDisplay display;
        expect(fatal(!display.name().empty())) << "Xvfb did not start";

        const std::filesystem::path deck = std::filesystem::temp_directory_path() / "gr4-present-follow-edits";
        std::filesystem::remove_all(deck);
        std::filesystem::copy(GR4_PRESENT_REMOTE_DECK, deck, std::filesystem::copy_options::recursive);
        const std::vector<std::string> launch{"--windowed", "--presentation=" + deck.string(), "--view", "second"};

        Pixels              before;
        std::vector<Pixels> between;
        Pixels              reloaded;
        {
            gr::present::e2e::ViewerProcess process{viewerUnderTest, display.name(), launch};
            std::this_thread::sleep_for(2s);
            Screen screen{display.name()};
            before = settled(screen);
            writeText(deck / "talk.md", "# A deck from elsewhere {#remote}\n\nunchanged\n\n# The second slide {#second}\n\n## Edited, and still here\n\nwith a heading it did not have.\n");
            // the files are compared every 2 s, then the deck loads and blends in over 1 s
            for (const auto until = std::chrono::steady_clock::now() + 5s; std::chrono::steady_clock::now() < until;) {
                between.push_back(screen.pixels(kWindow, 4));
            }
            reloaded = settled(screen);
        }
        Pixels freshLaunch;
        {
            gr::present::e2e::ViewerProcess process{viewerUnderTest, display.name(), launch};
            std::this_thread::sleep_for(2s);
            Screen screen{display.name()};
            freshLaunch = settled(screen);
        }
        std::filesystem::remove_all(deck);

        expect(!std::ranges::equal(reloaded, before)) << "the edit was not shown";
        expect(std::ranges::equal(reloaded, freshLaunch)) << "the reloaded deck differs from a launch of the edited deck at its second slide";
        expect(between.size() > 10UZ) << "only " << between.size() << " frames were read during the reload";
        const auto changes     = between | std::views::transform([&](const Pixels& frame) { return gr::present::e2e::pixelsOutsideBlend(frame, before, freshLaunch); }) | std::ranges::to<std::vector<std::string>>();
        const auto firstChange = std::ranges::find_if(changes, [](const std::string& change) { return !change.empty(); });
        expect(firstChange == changes.end()) << "frames with a pixel outside the blend of the two pictures:" << std::ranges::count_if(changes, [](const std::string& change) { return !change.empty(); }) << "of" << changes.size() << "; the first, frame" << (firstChange - changes.begin()) << ":" << (firstChange == changes.end() ? std::string{} : *firstChange);
    };

    return 0;
}
