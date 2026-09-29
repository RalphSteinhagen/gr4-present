#include "Viewer.hpp"

#include <gr4-present/Number.hpp>

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

void advanceLoading(Viewer& viewer) {
    viewer.loader.advance();
    viewer.launch.setProgress(LoadStage::content, viewer.loader.progress());

    switch (viewer.loader.state()) {
    case LoadState::failed:
        viewer.fallback.address    = std::string{viewer.loader.baseUri()};
        viewer.fallback.reason     = std::string{viewer.loader.diagnostic()};
        viewer.fallback.attempt    = viewer.loader.attempts();
        viewer.fallback.untilRetry = viewer.loader.untilRetry();
        viewer.launch.setProgress(LoadStage::content, 1.0f);
        viewer.launch.setProgress(LoadStage::initialisation, 1.0f);
        return;
    case LoadState::ready: break;
    default: return;
    }

    if (viewer.sections.empty()) {
        const Document whole    = parseMarkdown(viewer.loader.documentSource());
        viewer.sections         = sectionsOf(whole);
        viewer.figures.bytesFor = [&viewer](std::string_view reference) { return viewer.loader.figureBytes(reference); };
        viewer.effects.reset([&viewer](std::string_view reference) { return viewer.loader.figureBytes(reference); }, &viewer.diagnostics);
        viewer.layouts.clear();
        viewer.imageRegions.clear();
        for (const Section& section : viewer.sections) {
            for (const Block& block : section.document.blocks) {
                if (block.kind == BlockKind::directive && block.info == "regions") {
                    // a raster carries no boxes of its own, so the author names its regions once, as fractions
                    viewer.imageRegions.insert_or_assign(std::string{block.field("source")}, regionsOf(block.fields));
                    continue;
                }
                if (block.kind != BlockKind::directive || block.info != "layout") {
                    continue;
                }
                for (const std::string_view reference : {block.field("source"), block.field("portrait")}) {
                    if (auto parsed = layoutOfSvg(viewer.loader.figureBytes(reference))) {
                        viewer.layouts.emplace(std::string{reference}, std::move(*parsed));
                    }
                }
            }
        }
        viewer.navigator.graph = graphOf(viewer.sections);
        // an SVG group revealed at step 3 must be reachable even when the Markdown declares fewer steps
        for (auto&& [section, view] : std::views::zip(viewer.sections, viewer.navigator.graph.views)) {
            for (const Block& block : section.document.blocks) {
                if (block.kind != BlockKind::directive || block.info != "layout") {
                    continue;
                }
                for (const std::string_view reference : {block.field("source"), block.field("portrait")}) {
                    if (const auto known = viewer.layouts.find(reference); known != viewer.layouts.end()) {
                        const auto deepest = std::ranges::max_element(known->second.areas, {}, &Area::step);
                        view.stepCount     = deepest == known->second.areas.end() ? view.stepCount : std::max(view.stepCount, static_cast<std::size_t>(deepest->step + 1));
                    }
                }
            }
        }
        if (const auto valid = viewer.navigator.graph.validate(); !valid) {
            viewer.diagnostics.report(DiagnosticKind::invalidNavigationTarget, "the deck's views", std::string{message(valid.error())});
        }
        viewer.navigator.cursor = Cursor{.viewId = viewer.navigator.graph.views.front().id, .step = 0UZ};
        // a deep link names where to start; an unknown view id is ignored rather than failing the load
        if (const auto requested = viewer.options.value("view"); requested.has_value()) {
            if (!viewer.navigator.jumpTo(viewNamed(viewer, *requested))) {
                viewer.diagnostics.report(DiagnosticKind::invalidNavigationTarget, std::string{*requested}, "the link names a view this presentation does not have");
            }
        }
        if (const auto requested = viewer.options.value("step"); requested.has_value()) {
            if (const auto parsed = parseNumber<std::size_t>(*requested); parsed) {
                viewer.navigator.cursor.step = *parsed;
            }
        }
        viewer.diagnostics = viewer.loader.diagnostics;
        for (const std::string& warning : whole.warnings) {
            viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, viewer.loader.manifest().entry, warning);
        }
        viewer.reportedClips.clear(); // the list they were reported into is gone, so they are unreported again
        Fonts::instance().stageDeck(viewer.loader.manifest(), [&viewer](std::string_view reference) { return viewer.loader.figureBytes(reference); }, viewer.diagnostics);
        for (const Section& section : viewer.sections) {
            reportFontChoices(viewer, section.id.empty() ? std::string_view{"start"} : std::string_view{section.id}, section.document);
        }
        for (const View& view : viewer.navigator.graph.views) {
            for (const std::string& target : view.next) {
                if (viewer.navigator.graph.find(target) == nullptr) {
                    viewer.diagnostics.report(DiagnosticKind::invalidNavigationTarget, target, std::format("view '{}' points at a view this presentation does not have", view.id));
                }
            }
        }
        for (const Section& section : viewer.sections) {
            const Cursor        at{.viewId = section.id.empty() ? std::string{"start"} : section.id, .step = 0UZ};
            const SectionVisual visual = visualFor(viewer, at);
            if (!visual.missingAnchor.empty()) {
                viewer.diagnostics.report(DiagnosticKind::invalidSvgAnchor, visual.missingAnchor, std::format("section '{}' frames an element its layout does not contain", at.viewId));
            }
            for (const Block& block : section.document.blocks) {
                if (block.kind == BlockKind::directive && !block.closed) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/:::{}", at.viewId, block.info), "this fence is never closed, so everything after it in the file is inside it");
                }
                if (block.kind != BlockKind::directive || block.info != "stop") {
                    continue;
                }
                const std::string_view side  = block.field("box");
                const std::string      named = std::format("{}/stop {}", at.viewId, block.step);
                if (!block.field("region").empty() && stopRegionOf(viewer, visual.source, block.field("region")).width <= 0.0f) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, named, std::format("region '{}' is neither a declared region nor four fractions of the picture", block.field("region")));
                }
                if (!side.empty() && side != "left" && side != "right" && side != "above" && side != "below" && side != "auto") {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, named, std::format("box '{}' is not left, right, above, below or auto", side));
                }
            }
            // `:::place` puts boxes where it says; `grid:` lays them out in rows. `:::grid` was the first's old name, and
            // read beside `grid:` it said the opposite of what it did
            for (const Block& block : section.document.blocks) {
                if (block.kind == BlockKind::directive && block.info == "grid") {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/:::grid", at.viewId), "is now called `:::place`; read as such");
                }
            }
            // an effect or a transition the viewer does not have would be drawn as its default without a word
            const auto reportUnknown = [&](std::string_view value, std::span<const std::string_view> known, std::string_view what) {
                if (!value.empty() && !std::ranges::contains(known, value)) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, value), std::format("is no {} ({}); the default is used", what, known | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>()));
                }
            };
            // a reveal is a motion the viewer has, or an effect the deck or the viewer has
            const auto reportUnknownReveal = [&](std::string_view value) {
                if (!value.empty() && !std::ranges::contains(kRevealNames, value) && viewer.effects.find(value) == nullptr) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, value), std::format("is no way to arrive ({}) and no effect in the deck's effects/ or the viewer's; the fade is used", kRevealNames | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>()));
                }
            };
            // a transition is one the viewer moves by itself, or an effect the deck or the viewer has
            if (const std::string_view effect = effectNamed(visual.transition); !effect.empty() && viewer.effects.find(effect) == nullptr) {
                viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, effect), std::format("is no transition the viewer moves by itself ({}) and no effect in the deck's effects/ or the viewer's ({}); the default is used", kTransitionNames | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>(), EffectLibrary::bundledNames() | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>()));
            }
            for (const Document::StepReveal& reveal : section.document.stepReveals) {
                reportUnknownReveal(reveal.kind);
                reportUnknown(reveal.from, kRevealDirections, "direction to arrive from");
            }
            // a stage shows each of its transitions in a box: a move the viewer makes by itself, or an effect
            for (const Block& block : section.document.blocks | std::views::filter([](const Block& block) { return block.kind == BlockKind::directive && block.info == "stage"; })) {
                for (const std::string& transition : stageTransitionsOf(block)) {
                    const Transition::Kind kind = transitionKindFor(transition, false);
                    if (kind == Transition::Kind::camera || kind == Transition::Kind::morph) {
                        viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, transition), "moves a whole slide and cannot be shown in a stage; a fade is shown");
                    } else if (kind == Transition::Kind::shader && viewer.effects.find(effectNamed(transition)) == nullptr) {
                        viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, transition), "is no transition the viewer moves by itself and no effect in the deck's effects/ or the viewer's; a fade is shown");
                    }
                }
            }
            for (const Block& block : section.document.blocks) {
                reportUnknownReveal(block.revealKind);
                reportUnknown(block.revealFrom, kRevealDirections, "direction to arrive from");
            }
            for (const AreaDocument& slot : visual.slots) {
                reportUnknownReveal(slot.options.in);
                reportUnknown(slot.options.inFrom, kRevealDirections, "direction to arrive from");
            }
            if (!visual.declaredGrid.empty() && !visual.grid.empty()) {
                viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::string{at.viewId}, "both places boxes (`:::place`) and lays them out in rows (`grid:`); the placed boxes are used");
            }
            if (const std::string_view grid = visual.grid; !grid.empty()) {
                std::vector<std::string> problems;
                static_cast<void>(gridRowsOf(grid, &problems));
                for (std::string& problem : problems) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/grid", at.viewId), std::move(problem));
                }
            }
            if (visual.contradicts) {
                viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::string{at.viewId}, "the section names both a drawing to lay it out and a grid; the drawing is the layout, so the grid is ignored");
            }
        }
        buildSideMenu(viewer);
        publishCursor(viewer);
        viewer.regions.entries.clear();
        for (const Section& section : viewer.sections) {
            for (const Block& block : section.document.blocks) {
                if (block.kind == BlockKind::directive && block.info == "gr4") {
                    const std::string region  = block.id.empty() ? block.info : block.id;
                    const LiveRegion  binding = liveRegionFrom(block.fields, static_cast<std::size_t>(block.step));
                    if (const std::string_view legend = block.field("legend"); !legend.empty() && !binding.legend) {
                        viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, region, std::format("'{}' is no legend position (bottom, top, left, right or none); the deck's is used", legend));
                    }
                    viewer.regions.bind(region, binding);
                }
            }
        }
        viewer.documentView.imageFor          = [&viewer](std::string_view reference) { return viewer.figures.get(reference, static_cast<std::uint32_t>(DocumentView::columnWidth(ImGui::GetMainViewport()->Size.x, Fonts::slideBodySize(ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y)))); };
        viewer.documentView.linksFor          = [&viewer](std::string_view reference) { return viewer.figures.links(reference); };
        viewer.documentView.textRuns.labelsOf = [&viewer](std::string_view reference, std::span<const std::string> hidden) { return viewer.figures.labels(reference, hidden); };
#ifdef __EMSCRIPTEN__
        // `index.html` is the directory's own page, so the shorter address is the same deck
        std::string path = emscriptenLocation("pathname");
        if (path.ends_with("index.html")) {
            path.resize(path.size() - std::string_view{"index.html"}.size());
        }
        viewer.documentView.deckAddress = emscriptenLocation("origin") + path;
#endif
        // a plot's CSV comes through the same package reader as a figure, so a remote package works unchanged
        viewer.documentView.dataFor = [&viewer](std::string_view reference) {
            const std::span<const std::uint8_t> bytes = viewer.loader.figureBytes(reference);
            return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        };
        viewer.documentView.formulaFor = [&viewer](std::string_view latex, float pixels, bool display) { return &viewer.formulas.get(latex, pixels, display); };
        viewer.documentView.qrFor      = [&viewer](std::string_view text, float pixels) { return &viewer.qrCodes.get(text, pixels); };
        viewer.documentView.videoFor   = [&viewer](std::string_view reference, bool autoplay, bool loop, bool sound) { return viewer.videos.get(reference, autoplay, loop, sound); };
        viewer.videos.bytesFor         = [&viewer](std::string_view reference) { return viewer.loader.figureBytes(reference); };
        // Every link and every citation in the deck, gathered here because a view cannot see the others. The
        // citation order is reading order across the whole document, which is what IEEE numbering means and what
        // makes a marker on a slide and an entry in the reference list the same number.
        viewer.documentView.references.clear();
        viewer.documentView.citations.clear();
        const auto note = [&viewer](const std::vector<InlineSpan>& spans) {
            for (const InlineSpan& span : spans) {
                // a `#view` link turns to a slide of this deck, which is not a work it refers to
                if (span.kind == InlineKind::link && !span.target.starts_with('#') && std::ranges::none_of(viewer.documentView.references, [&span](const auto& seen) { return seen.second == span.target; })) {
                    viewer.documentView.references.emplace_back(span.text, span.target);
                }
                if (span.kind == InlineKind::footnote && std::ranges::find(viewer.documentView.citations, span.target) == viewer.documentView.citations.end()) {
                    viewer.documentView.citations.push_back(span.target);
                }
            }
        };
        const auto gather = [&note](const Document& document) {
            for (const Block& block : document.blocks) {
                note(block.spans);
                for (const std::vector<InlineSpan>& cell : block.cells) {
                    note(cell);
                }
            }
        };
        for (const Section& section : viewer.sections) {
            gather(section.document);
            // what a `:::<name>` box cites counts as much as the flow: a caption is where a picture's source belongs
            for (const Block& block : section.document.blocks) {
                if (block.kind == BlockKind::directive && !isConfigurationDirective(block.info) && !block.lines.empty()) {
                    gather(boxDocumentOf(block.lines, section.document));
                }
            }
        }
    }
    viewer.launch.setProgress(LoadStage::initialisation, 1.0f);
}

void applyTheme(Viewer& viewer, ColourScheme scheme) {
    if (viewer.logo.id != 0 && viewer.theme.scheme == scheme) {
        return;
    }
    viewer.logo.release();
    viewer.theme = themeFor(scheme);
    viewer.logo  = Texture::loadLogo(scheme);

    ImGuiStyle& style               = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text]     = ImGui::ColorConvertU32ToFloat4(viewer.theme.text);
    style.Colors[ImGuiCol_WindowBg] = viewer.theme.backgroundColour();

    applyLiveGraphScheme(scheme == ColourScheme::dark);
    for (const auto& [workflow, graph] : viewer.graphs) {
        graph->restyle();
    }
}

} // namespace gr::present
