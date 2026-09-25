#include "ImGuiTestHarness.hpp"
#include "ScreenshotDiff.hpp"

#include "../FallbackScene.hpp"
#include "../Fonts.hpp"
#include "../SideMenu.hpp"
#include "../Texture.hpp"
#include "../Theme.hpp"
#include "../ViewScene.hpp"
#include <gr4-present/PresentationLoader.hpp>

#include <boost/ut.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace boost::ut;
using namespace gr::present;
using namespace gr::present::test;

namespace {

struct Scenario {
    std::string  name;
    ColourScheme scheme;
    bool         menuRevealed;
    bool         noSignal = false;
};

const std::vector<Scenario> kScenarios{
    {.name = "main-dark", .scheme = ColourScheme::dark, .menuRevealed = false},
    {.name = "main-light", .scheme = ColourScheme::light, .menuRevealed = false},
    {.name = "main-dark-menu", .scheme = ColourScheme::dark, .menuRevealed = true},
    {.name = "main-dark-no-signal", .scheme = ColourScheme::dark, .menuRevealed = false, .noSignal = true},
};

bool gEngineSucceeded = false;

void drawScenario(const Scenario& scenario) {
    static Texture artwork;
    if (artwork.id == 0) {
        Fonts::instance().load();
        PresentationLoader loader;
        loader.begin(GR4_PRESENT_DEFAULT_PRESENTATION);
        while (loader.state() == LoadState::loading) {
            loader.advance();
        }
        artwork = Texture::load(loader.imageBytes());
    }

    const Theme          theme    = themeFor(scenario.scheme);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, ImGui::ColorConvertFloat4ToU32(theme.backgroundColour()));

    if (scenario.noSignal) {
        static Texture mark;
        if (mark.id == 0) {
            mark = Texture::loadLogo(scenario.scheme);
        }
        FallbackScene{.address = "http://localhost:8000/presentations/default", .reason = "cannot read presentation: connection refused", .attempt = 3UZ, .untilRetry = std::chrono::milliseconds{4000}}.draw(mark, theme);
    } else {
        ViewScene{}.draw(artwork, theme);
    }

    if (scenario.menuRevealed) {
        static SideMenu menu{.items = {{.label = "intro", .activate = [] {}, .separatorBefore = false}, //
                                 {.label = "architecture", .activate = [] {}, .separatorBefore = false}, {.label = "previous", .activate = [] {}, .separatorBefore = true}, {.label = "next", .activate = [] {}, .separatorBefore = false}, {.label = "toggle full screen", .activate = [] {}, .separatorBefore = true}},
            .revealed               = 1.0f};
        ImGui::GetIO().MousePos = ImVec2{4.0f, viewport->Size.y * 0.5f}; // inside the margin that opens the menu
        menu.revealed           = 1.0f;
        menu.draw(theme);
    }
}

} // namespace

const boost::ut::suite<"MainScreen"> mainScreenTests = [] {
    "the ImGui test engine ran every main-screen scenario"_test = [] { expect(gEngineSucceeded); };

    for (const Scenario& scenario : kScenarios) {
        const std::filesystem::path captured  = Harness::captureDirectory() / (scenario.name + ".png");
        const std::filesystem::path reference = Harness::referenceDirectory() / (scenario.name + ".png");

        boost::ut::test("'" + scenario.name + "' matches its reference") = [captured, reference] {
            expect(std::filesystem::exists(captured)) << "no screenshot was captured";
            if (!std::filesystem::exists(captured)) {
                return;
            }
            if (updatingReferences()) {
                std::filesystem::create_directories(reference.parent_path());
                std::filesystem::copy_file(captured, reference, std::filesystem::copy_options::overwrite_existing);
                return;
            }
            const ComparisonResult result = compareScreenshot(captured, reference);
            expect(result.matches) << result.message;
        };
    }
};

int main() {
    Harness harness;
    for (const Scenario& scenario : kScenarios) {
        harness.addTest(
            scenario.name,                                              //
            [&scenario](ImGuiTestContext*) { drawScenario(scenario); }, //
            [&scenario](ImGuiTestContext* context) { Harness::captureTo(context, scenario.name); });
    }
    gEngineSucceeded = harness.run();
    return 0;
}
