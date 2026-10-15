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
#include <cmath>
#include <filesystem>
#include <functional>
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

// the deck's file row, driven by the test engine's pointer: what each press reached
struct FileRowPresses {
    int  loads       = 0;
    int  saves       = 0;
    bool stacked     = false; // Load, follow edits and Save beneath each other, as wide and high as each other
    bool following   = true;  // the "follow edits" check box's flag, ticked to begin with
    bool followShown = false; // the check box drawn under the row
} gFileRow;

SideMenu& fileRowMenu() {
    static SideMenu menu = [] {
        const auto entry = [](std::string label, std::function<void()> activate, std::string key) { return SideMenu::Item{.label = std::move(label), .activate = std::move(activate), .separatorBefore = false, .mnemonic = std::move(key)}; };
        SideMenu   built;
        built.items       = {entry("intro", [] {}, {})};
        built.utilities   = {entry("notes", [] {}, "N")};
        built.files       = {entry(std::string{Fonts::kFolder} + "  Load", [] { ++gFileRow.loads; }, "Ctrl+O"), entry(std::string{Fonts::kSave} + "  Save", [] { ++gFileRow.saves; }, "Ctrl+S")};
        built.followEdits = &gFileRow.following;
        return built;
    }();
    return menu;
}

void drawFileRow() {
    Fonts::instance().load();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    SideMenu&            menu     = fileRowMenu();
    if (ImGui::GetIO().MousePos.x > 300.0f || ImGui::GetIO().MousePos.x < 0.0f) {
        ImGui::GetIO().MousePos = ImVec2{4.0f, viewport->Size.y * 0.5f}; // inside the margin that opens the menu
    }
    menu.revealed = 1.0f;
    menu.draw(themeFor(ColourScheme::dark), Fonts::kPixelsPerPoint);
}

[[nodiscard]] ImVec2 centreOf(const Rectangle& box) { return ImVec2{box.x + box.width * 0.5f, box.y + box.height * 0.5f}; }

void pressFileRow(ImGuiTestContext* context) {
    SideMenu& menu = fileRowMenu();
    context->Yield(3);
    const auto&      row    = menu.drawnFiles;
    const Rectangle& follow = menu.drawnFollowEdits;
    if (row.size() != 2UZ) {
        return;
    }
    // the panel's first item sits a fraction of a pixel further in: the same size is the same to a pixel
    const auto sameSize  = [](const Rectangle& one, const Rectangle& other) { return std::abs(one.width - other.width) <= 1.0f && std::abs(one.height - other.height) <= 1.0f; };
    gFileRow.stacked     = row[0].second.y < follow.y && follow.y < row[1].second.y && sameSize(row[0].second, follow) && sameSize(follow, row[1].second);
    const Rectangle load = row[0].second;
    const Rectangle save = row[1].second;
    context->MouseMoveToPos(centreOf(load));
    context->MouseClick(0);
    context->Yield(2);
    context->MouseMoveToPos(centreOf(save));
    context->MouseClick(0);
    context->Yield(2);
    gFileRow.followShown = menu.drawnFollowEdits.width > 0.0f;
    if (gFileRow.followShown) {
        context->MouseMoveToPos(centreOf(menu.drawnFollowEdits));
        context->MouseClick(0);
        context->Yield(2);
    }
}

void drawScenario(const Scenario& scenario) {
    // the real default package, loaded the way the application loads it
    static PresentationLoader   loader;
    static FigureCache          figures;
    static std::vector<Section> sections;
    Fonts::instance().load(); // the harness rebuilds the font atlas per scenario, so a once-only load goes stale
    if (sections.empty()) {
        constexpr std::chrono::steady_clock::time_point kFrame{}; // the deck is local and retries are not under test
        loader.begin(GR4_PRESENT_DEFAULT_PRESENTATION, kFrame);
        while (loader.state() == LoadState::loading) {
            loader.advance(kFrame);
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
        DocumentView view{.document = &sections.front().document, .step = 0, .imageFor = [](std::string_view reference) { return figures.get(reference, static_cast<std::uint32_t>(DocumentView::columnWidth(ImGui::GetMainViewport()->Size.x, Fonts::slideBodySize(ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y)))); }, .dataFor = {}, .formulaFor = {}, .qrFor = {}, .videoFor = {}, .references = {}, .placedRegions = {}};
        view.draw(theme);
    }

    if (scenario.diagnostics) {
        Diagnostics problems;
        problems.report(DiagnosticKind::missingResource, "figures/absent.svg", "not found");
        problems.report(DiagnosticKind::invalidSvgAnchor, "subsystem-c", "section 'demo' frames an element its layout does not contain");
        problems.report(DiagnosticKind::versionMismatch, "2.0", "presentation needs viewer 2.0 or newer, this is 0.1", true);
        DiagnosticsPanel{.open = true}.draw(problems, theme, Fonts::kPixelsPerPoint); // a device at scale 1
    }

    if (scenario.menuRevealed) {
        static SideMenu menu{.items = {{.label = "intro", .activate = [] {}, .separatorBefore = false, .mnemonic = {}}, //
                                 {.label = "architecture", .activate = [] {}, .separatorBefore = false, .mnemonic = {}}},
            .utilities              = {{.label = std::string{Fonts::kExpand} + "  full screen", .activate = [] {}, .separatorBefore = false, .mnemonic = "F"}, {.label = std::string{Fonts::kPointer} + "  pointer", .activate = [] {}, .separatorBefore = false, .mnemonic = "L"}, {.label = std::string{Fonts::kNotes} + "  notes", .activate = [] {}, .separatorBefore = false, .mnemonic = "N"}, {.label = std::string{Fonts::kWarning} + "  problems (1)", .activate = [] {}, .separatorBefore = true, .mnemonic = {}}},
            .revealed               = 1.0f};
        ImGui::GetIO().MousePos = ImVec2{4.0f, viewport->Size.y * 0.5f}; // inside the margin that opens the menu
        menu.revealed           = 1.0f;
        menu.draw(theme, Fonts::kPixelsPerPoint); // a device at scale 1
    }
}

} // namespace

const boost::ut::suite<"MainScreen"> mainScreenTests = [] {
    "the ImGui test engine ran every main-screen scenario"_test = [] { expect(gEngineSucceeded); };

    "the deck's files: Load, follow edits and Save beneath each other, each pressed once"_test = [] {
        expect(gFileRow.stacked) << "Load, follow edits and Save are not beneath each other, each as wide and high";
        expect(eq(gFileRow.loads, 1)) << "Load ran once";
        expect(eq(gFileRow.saves, 1)) << "Save ran once";
        expect(gFileRow.followShown) << "a deck that can be followed shows follow edits";
        expect(!gFileRow.following) << "a click on follow edits, while on, turns it off";
    };

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
    harness.addTest("file-row", [](ImGuiTestContext*) { drawFileRow(); }, pressFileRow);
    gEngineSucceeded = harness.run();
    return 0;
}
