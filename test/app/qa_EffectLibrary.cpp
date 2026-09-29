#include <boost/ut.hpp>

#include "EffectLibrary.hpp"
#include "EffectRenderer.hpp"

#include <gr4-present/Diagnostics.hpp>

#include <cmath>
#include <map>
#include <memory>
#include <string>

using namespace boost::ut;
using namespace gr::present;

namespace {

/// a deck that holds exactly `files`, by package path
[[nodiscard]] EffectLibrary::PackageBytes deckOf(std::map<std::string, std::string, std::less<>> files) {
    auto held = std::make_shared<const std::map<std::string, std::string, std::less<>>>(std::move(files));
    return [held](std::string_view path) -> std::span<const std::uint8_t> {
        const auto found = held->find(path);
        return found == held->end() ? std::span<const std::uint8_t>{} : std::span{reinterpret_cast<const std::uint8_t*>(found->second.data()), found->second.size()};
    };
}

constexpr std::string_view kImage = "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(1.0); }\n";

[[nodiscard]] bool near(float a, float b) noexcept { return std::abs(a - b) < 1e-5f; }

} // namespace

const suite<"EffectLibrary"> effectLibraryTests = [] {
    // Ground truth: the author's rule (2026-10-08) -- a bare name the viewer has is the viewer's own, so a deck is never
    // asked for it; a deck's effect of the same name is reached by its path; any other bare name is the deck's
    "a bare built-in name is the viewer's own, and the deck's of that name is reached by its path"_test = [] {
        Diagnostics   diagnostics;
        EffectLibrary library;
        library.reset(deckOf({{"effects/crt.glsl", std::string{"// @duration 7\n"} + std::string{kImage}}}), &diagnostics);
        const EffectSource* crt  = library.find("crt");
        const EffectSource* deck = library.find("effects/crt.glsl");
        expect(crt != nullptr && !(crt->duration.has_value() && near(*crt->duration, 7.0f))) << "the viewer's crt";
        expect(deck != nullptr && deck->duration.has_value() && near(*deck->duration, 7.0f)) << "the deck's crt, by its path";
        expect(library.find("ripple") != nullptr) << "the viewer's own ripple";
        expect(library.find("nosuch") == nullptr);
        expect(diagnostics.empty()) << "a name nobody has is the caller's to report";
    };

    // Ground truth: FR-16 -- values by declared type, clamped into [min, max] with a diagnostic, an undeclared key a
    // diagnostic, colours #rrggbb and #rrggbbaa
    "settings are read by their declared type, clamped into range, and what does not fit is reported"_test = [] {
        Diagnostics       diagnostics;
        EffectLibrary     library;
        const std::string effect = std::string{"// @param speed float 1.0 0.0 2.0\n// @param count int 3 1 5\n// @param tint colour #000000\n// @param at vec2 0.5,0.5 0 1\n"} + std::string{kImage};
        library.reset(deckOf({{"effects/fx.glsl", effect}}), &diagnostics);
        const EffectSource* fx = library.find("fx");
        expect(fatal(fx != nullptr));
        const auto values = library.parametersOf(*fx, " speed=1.5 count=9 tint=#ff800040 at=0.25,0.75");
        expect(diagnostics.empty() == false && eq(diagnostics.count(DiagnosticKind::invalidEffect), 1UZ)) << "count=9 is outside 1 … 5";
        expect(values.contains("speed") && near(values.at("speed")[0], 1.5f));
        expect(values.contains("count") && near(values.at("count")[0], 5.0f)) << "clamped to its maximum";
        expect(values.contains("tint") && near(values.at("tint")[0], 1.0f) && near(values.at("tint")[1], 128.0f / 255.0f) && near(values.at("tint")[2], 0.0f) && near(values.at("tint")[3], 64.0f / 255.0f));
        expect(values.contains("at") && near(values.at("at")[0], 0.25f) && near(values.at("at")[1], 0.75f)) << "a vector's unused components are not range-checked";

        diagnostics.clear();
        const auto rejected = library.parametersOf(*fx, "colour=red speed=fast");
        expect(rejected.empty()) << "an undeclared key and an unreadable value both keep the defaults";
        expect(eq(diagnostics.count(DiagnosticKind::invalidEffect), 2UZ));
    };

    "an effect that does not parse is reported once, with its file, and is not found"_test = [] {
        Diagnostics   diagnostics;
        EffectLibrary library;
        library.reset(deckOf({{"effects/broken.glsl", "//--- pass: BufferA\nvoid mainImage(out vec4 c, in vec2 p) { c = vec4(0.0); }\n"}}), &diagnostics);
        expect(library.find("broken") == nullptr) << "no Image pass";
        expect(library.find("broken") == nullptr);
        expect(eq(diagnostics.count(DiagnosticKind::invalidEffect), 1UZ));
        expect(!diagnostics.entries.empty() && diagnostics.entries.front().subject == "effects/broken.glsl");
    };
};

const suite<"iMouse"> mouseTests = [] {
    // Ground truth: FR-17 -- xy the position while pressed, zw the press position, z positive while the button is down,
    // w positive on the press frame only; a press outside the effect is not its press
    "a press inside sets both pairs, then the press frame's sign goes, then release turns both negative"_test = [] {
        MouseState state;
        state = nextMouse(state, PointerFrame{.x = 10.0f, .y = 20.0f, .inside = true, .clicked = true, .down = true});
        expect(state.pressed && state.mouse == std::array{10.0f, 20.0f, 10.0f, 20.0f}) << "press frame";
        state = nextMouse(state, PointerFrame{.x = 30.0f, .y = 40.0f, .inside = true, .clicked = false, .down = true});
        expect(state.mouse == std::array{30.0f, 40.0f, 10.0f, -20.0f}) << "dragging: xy follows, w negative";
        state = nextMouse(state, PointerFrame{.x = 300.0f, .y = 400.0f, .inside = false, .clicked = false, .down = true});
        expect(state.mouse == std::array{300.0f, 400.0f, 10.0f, -20.0f}) << "a drag that leaves the box is still followed";
        state = nextMouse(state, PointerFrame{.x = 50.0f, .y = 60.0f, .inside = true, .clicked = false, .down = false});
        expect(!state.pressed && state.mouse == std::array{300.0f, 400.0f, -10.0f, -20.0f}) << "released: xy stays, z and w negative";
        state = nextMouse(state, PointerFrame{.x = 70.0f, .y = 80.0f, .inside = true, .clicked = false, .down = false});
        expect(state.mouse == std::array{300.0f, 400.0f, -10.0f, -20.0f}) << "hovering changes nothing";
    };

    "a press outside the effect leaves its iMouse alone"_test = [] {
        const MouseState state = nextMouse(MouseState{}, PointerFrame{.x = -5.0f, .y = 5.0f, .inside = false, .clicked = true, .down = true});
        expect(!state.pressed && state.mouse == std::array{0.0f, 0.0f, 0.0f, 0.0f});
    };
};

int main() { return 0; }
