#include "ImGuiTestHarness.hpp"
#include "ScreenshotDiff.hpp"

#include "../LoadingScreen.hpp"
#include "../Texture.hpp"
#include "../Theme.hpp"

#include <boost/ut.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

using namespace boost::ut;
using namespace gr::present;
using namespace gr::present::test;

namespace {

struct Scenario {
    std::string                        name;
    ColourScheme                       scheme;
    std::array<float, kLoadStageCount> progress;
    std::string                        statusOverride;
};

const std::vector<Scenario> kScenarios{
    {.name = "launch-dark-downloading", .scheme = ColourScheme::dark, .progress = {0.45f, 0.0f, 0.0f}, .statusOverride = {}},
    {.name = "launch-dark-content", .scheme = ColourScheme::dark, .progress = {1.0f, 0.6f, 0.0f}, .statusOverride = {}},
    {.name = "launch-dark-initialising", .scheme = ColourScheme::dark, .progress = {1.0f, 1.0f, 0.3f}, .statusOverride = {}},
    {.name = "launch-light-content", .scheme = ColourScheme::light, .progress = {1.0f, 0.6f, 0.0f}, .statusOverride = {}},
    {.name = "launch-light-custom-status", .scheme = ColourScheme::light, .progress = {1.0f, 1.0f, 0.8f}, .statusOverride = "starting the spectrum workflow"},
};

constexpr float kLogoWidth = 260.0f;

// filled by main() before Boost.UT runs the suites, which happens after main returns
bool gEngineSucceeded = false;

void drawScenario(const Scenario& scenario) {
    static Texture      logo;
    static ColourScheme loaded = ColourScheme::light;
    if (logo.id == 0 || loaded != scenario.scheme) {
        logo.release();
        logo   = Texture::loadLogo(scenario.scheme);
        loaded = scenario.scheme;
    }

    const Theme   theme = themeFor(scenario.scheme);
    LoadingScreen screen;
    screen.progress = scenario.progress;
    if (!scenario.statusOverride.empty()) {
        screen.setStatus(scenario.statusOverride);
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, ImGui::ColorConvertFloat4ToU32(theme.backgroundColour()));
    screen.draw(logo.id, logo.scaledToWidth(kLogoWidth), theme);
}

} // namespace

const boost::ut::suite<"LaunchScreen"> launchScreenTests = [] {
    "the ImGui test engine ran every launch-screen scenario"_test = [] { expect(gEngineSucceeded); };

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
