#include "ImGuiTestHarness.hpp"
#include "ScreenshotDiff.hpp"

#include "DiagnosticsPanel.hpp"
#include "DocumentView.hpp"
#include "FallbackScene.hpp"
#include "FigureCache.hpp"
#include "Fonts.hpp"
#include "SideMenu.hpp"
#include "Texture.hpp"
#include "Theme.hpp"
#include <gr4-present/PresentationLoader.hpp>

#include <boost/ut.hpp>

#include <chrono>
#include <filesystem>
#include <map>
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
    bool         noSignal    = false;
    bool         diagnostics = false;
};

const std::vector<Scenario> kScenarios{
    {.name = "main-dark", .scheme = ColourScheme::dark, .menuRevealed = false},
    {.name = "main-light", .scheme = ColourScheme::light, .menuRevealed = false},
    {.name = "main-dark-menu", .scheme = ColourScheme::dark, .menuRevealed = true},
    {.name = "main-dark-no-signal", .scheme = ColourScheme::dark, .menuRevealed = false, .noSignal = true},
    {.name = "main-dark-diagnostics", .scheme = ColourScheme::dark, .menuRevealed = false, .noSignal = false, .diagnostics = true},
};

bool gEngineSucceeded = false;

void drawScenario(const Scenario& scenario) {
    // the real default package, loaded the way the application loads it, so this is an end-to-end check of it
    static PresentationLoader   loader;
    static FigureCache          figures;
    static std::vector<Section> sections;
    Fonts::instance().load(); // the harness rebuilds the font atlas per scenario, so a once-only load goes stale
    if (sections.empty()) {
        loader.begin(GR4_PRESENT_DEFAULT_PRESENTATION);
        while (loader.state() == LoadState::loading) {
            loader.advance();
        }
        sections = sectionsOf(parseMarkdown(loader.documentSource()));
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
        figures.bytesFor = [](std::string_view reference) { return loader.figureBytes(reference); };
        DocumentView view{.document = sections.front().document, .step = 0, .imageFor = [](std::string_view reference) { return figures.get(reference, static_cast<std::uint32_t>(DocumentView::columnWidth(ImGui::GetMainViewport()->Size.x, Fonts::slideBodySize(ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y)))); }, .dataFor = {}, .formulaFor = {}, .qrFor = {}, .videoFor = {}, .references = {}, .placedRegions = {}};
        view.draw(theme);
    }

    if (scenario.diagnostics) {
        Diagnostics problems;
        problems.report(DiagnosticKind::missingResource, "figures/absent.svg", "not found");
        problems.report(DiagnosticKind::invalidSvgAnchor, "subsystem-c", "section 'demo' frames an element its layout does not contain");
        problems.report(DiagnosticKind::versionMismatch, "2.0", "presentation needs viewer 2.0 or newer, this is 0.1", true);
        DiagnosticsPanel{.open = true}.draw(problems, theme);
    }

    if (scenario.menuRevealed) {
        // the slides scroll and the controls do not, which is the shape the menu has in the viewer
        static SideMenu menu{.items = {{.label = "intro", .activate = [] {}, .separatorBefore = false, .mnemonic = {}}, //
                                 {.label = "architecture", .activate = [] {}, .separatorBefore = false, .mnemonic = {}}},
            .utilities              = {{.label = std::string{Fonts::kChevronLeft} + "  previous", .activate = [] {}, .separatorBefore = false, .mnemonic = {}}, //
                             {.label = std::string{Fonts::kChevronRight} + "  next", .activate = [] {}, .separatorBefore = false, .mnemonic = {}},              //
                             {.label = std::string{Fonts::kWarning} + "  problems (1)", .activate = [] {}, .separatorBefore = false, .mnemonic = {}},           //
                             {.label = "speaker notes", .activate = [] {}, .separatorBefore = false, .mnemonic = "N"},                                          //
                             {.label = std::string{Fonts::kExpand} + "  toggle full screen", .activate = [] {}, .separatorBefore = false, .mnemonic = {}}},
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
