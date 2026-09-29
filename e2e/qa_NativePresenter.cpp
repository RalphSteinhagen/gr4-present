// The presenter's next-step button takes one tap as one step, however the tap arrives: on a phone a single tap came
// in twice and moved the deck two steps, so a second press within 300 ms is the same press.
//
// Ground truth is a second launch opened at the step the taps must reach, `--step 2`: two taps 100 ms apart and one
// more 600 ms later are two presses, from step 0 to step 2. Without the de-bounce they would reach step 3, a slide that
// shows one more revealed block. Only the slide above the notes panel is compared; the panel's clock changes.

#include "NativeDisplay.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <thread>

using namespace boost::ut;
using namespace std::chrono_literals;

namespace {
using gr::present::e2e::Box;
using gr::present::e2e::Pixels;
using gr::present::e2e::Screen;

constexpr Box kSlide{.x = 0, .y = 0, .width = 1280, .height = 440}; // above the notes panel, which takes the lower third
// where the steps slide reveals its steps: right of its syntax panel, whose static code is no evidence of the step and
// has been seen to differ by a single sample between two launches
constexpr Box kSteps{.x = 560, .y = 0, .width = 720, .height = 440};
constexpr int kNextStepX = 1212; // the middle of the notes' next-step button at 1280 x 720
constexpr int kNextStepY = 556;

/// waits until the viewer has drawn and its picture holds still for a second, as long as a loaded machine needs
void settle(const Screen& screen) {
    Pixels last;
    for (auto deadline = std::chrono::steady_clock::now() + 20s; std::chrono::steady_clock::now() < deadline; std::this_thread::sleep_for(1s)) {
        Pixels     now   = screen.pixels(kSlide, 8);
        const bool drawn = std::ranges::any_of(now, [](const auto& row) { return std::ranges::any_of(row, [](std::uint32_t pixel) { return pixel != 0U; }); });
        if (drawn && std::ranges::equal(now, last)) {
            return;
        }
        last = std::move(now);
    }
}

/// the steps slide as the presenter shows it after `act`, launched with `arguments`
[[nodiscard]] Pixels slideAfter(const std::filesystem::path& viewer, const std::string& display, const std::vector<std::string>& arguments, const std::function<void(Screen&)>& act) {
    std::vector<std::string> launch{"--windowed", "--presenter", "--view", "steps"};
    launch.insert(launch.end(), arguments.begin(), arguments.end());
    gr::present::e2e::ViewerProcess process{viewer, display, launch};
    std::this_thread::sleep_for(2s);
    Screen screen{display};
    settle(screen);
    act(screen);
    std::this_thread::sleep_for(1s);
    settle(screen); // every reveal has finished arriving, however long a loaded machine takes
    return screen.pixels(kSteps, 4);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return 2; // usage: qa_NativePresenter <gr4-present executable>
    }
    const std::filesystem::path viewerUnderTest = std::filesystem::absolute(argv[1]);

    "two taps 100 ms apart are one press, a third 600 ms later another"_test = [&viewerUnderTest] {
        const gr::present::e2e::VirtualDisplay display;
        expect(fatal(!display.name().empty())) << "Xvfb did not start";

        const auto twoTapsThenOne = [](Screen& screen) {
            screen.tap(kNextStepX, kNextStepY);
            std::this_thread::sleep_for(100ms);
            screen.tap(kNextStepX, kNextStepY);
            std::this_thread::sleep_for(600ms);
            screen.tap(kNextStepX, kNextStepY);
        };
        const Pixels        tapped = slideAfter(viewerUnderTest, display.name(), {}, twoTapsThenOne);
        std::vector<Pixels> steps;
        for (int step = 0; step < 6; ++step) {
            steps.push_back(slideAfter(viewerUnderTest, display.name(), {"--step", std::to_string(step)}, [](Screen&) {}));
        }
        expect(!std::ranges::equal(steps[2], steps[1]) && !std::ranges::equal(steps[2], steps[3])) << "the reference step differs from the steps either side";
        std::string reached;
        for (std::size_t step = 0UZ; step < steps.size(); ++step) {
            if (std::ranges::equal(steps[step], tapped)) {
                reached += std::format("{} ", step);
            }
        }
        expect(reached == "2 ") << "two taps 100 ms apart are one press, the third 600 ms later another: step 2, reached " << reached;
    };

    return 0;
}
