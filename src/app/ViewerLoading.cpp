#include "Viewer.hpp"

#include <gr4-present/DeckFiles.hpp>
#include <gr4-present/EffectSource.hpp>
#include <gr4-present/Number.hpp>
#include <gr4-present/PackagePath.hpp>

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

namespace {

/// reports once, at load, a `font=` naming no face or a `size=` that is no size; the surrounding style is used
void reportFontChoices(Viewer& viewer, std::string_view viewId, const Document& document) {
    std::vector<std::string> reported;
    const auto               check = [&](std::string_view font, std::string_view size) {
        const auto report = [&](std::string subject, std::string detail) {
            if (!std::ranges::contains(reported, subject)) {
                reported.push_back(subject);
                viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::move(subject), std::move(detail));
            }
        };
        if (!font.empty() && !Fonts::instance().knowsName(font)) {
            report(std::format("{}/font={}", viewId, font), "neither a role (body, bold, italic, bolditalic, mono, title) nor a face `fonts:` in index.yml names; the surrounding face is used");
        }
        if (!size.empty() && !parseTypeSize(size)) {
            report(std::format("{}/size={}", viewId, size), "no size: 14pt, 14, 80% or 1.2em; the surrounding size is used");
        }
    };
    const auto checkSpans = [&](const Document& checked) {
        for (const Block& block : checked.blocks) {
            for (const InlineSpan& span : block.spans) {
                check(span.font, span.size);
            }
            for (const InlineSpan& span : block.cells | std::views::join) {
                check(span.font, span.size);
            }
        }
    };
    checkSpans(document);
    for (const Block& block : document.blocks) {
        if (block.kind != BlockKind::directive) {
            continue;
        }
        if (block.info == "layout" || isBox(block)) {
            check(block.field("font"), block.field("size"));
        }
        if (isBox(block) && !block.lines.empty()) {
            checkSpans(boxDocumentOf(block.lines, document));
        }
    }
}

/// Chrome's local network access: a page from the web reads this computer only once the reader allows it, and a refused
/// read looks like any other that got no answer (status 0); nothing natively
[[nodiscard]] std::string localNetworkHint([[maybe_unused]] std::string_view base) {
#ifdef __EMSCRIPTEN__
    if (isLoopbackAddress(base)) {
        return "; a page from the web reads this computer only once the browser allows local network access, or use Load";
    }
#endif
    return {};
}

} // namespace

void advanceLoading(Viewer& viewer) {
    viewer.loader.advance(viewer.frameStart);
    viewer.launch.setProgress(LoadStage::content, viewer.loader.progress());

    switch (viewer.loader.state()) {
    case LoadState::failed:
        viewer.fallback.address    = std::string{viewer.loader.baseUri()};
        viewer.fallback.reason     = std::string{viewer.loader.diagnostic()} + localNetworkHint(viewer.loader.baseUri());
        viewer.fallback.attempt    = viewer.loader.attempts();
        viewer.fallback.untilRetry = viewer.loader.untilRetry(viewer.frameStart);
        viewer.launch.setProgress(LoadStage::content, 1.0f);
        viewer.launch.setProgress(LoadStage::initialisation, 1.0f);
        return;
    case LoadState::ready: break;
    default: return;
    }

    viewer.launch.setProgress(LoadStage::initialisation, 1.0f);
    if (!viewer.sections.empty()) {
        return;
    }

    const Document whole    = parseMarkdown(viewer.loader.documentSource());
    viewer.sections         = sectionsOf(whole);
    viewer.figures.bytesFor = [&viewer](std::string_view reference) { return viewer.loader.figureBytes(reference); };
    viewer.effects.reset([&viewer](std::string_view reference) { return viewer.loader.figureBytes(reference); }, &viewer.diagnostics);
    viewer.layouts.clear();
    viewer.imageRegions.clear();
    for (const Section& section : viewer.sections) {
        for (const Block& block : section.document.blocks) {
            if (block.kind == BlockKind::directive && block.info == "regions") {
                // a raster carries no boxes of its own: the author names its regions once, as fractions
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
    viewer.diagnostics = viewer.loader.diagnostics;
    if (const auto valid = viewer.navigator.graph.validate(); !valid) {
        viewer.diagnostics.report(DiagnosticKind::invalidNavigationTarget, "the deck's views", std::string{message(valid.error())});
    }
    viewer.navigator.cursor = Cursor{.viewId = viewer.navigator.graph.views.front().id, .step = 0UZ};
    switch (viewer.deckFiles.opening) {
    case DeckOpening::launch: // a deep link names where to start; an unknown view id does not fail the load
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
        break;
    case DeckOpening::load: break;
    case DeckOpening::reload: viewer.navigator.cursor = cursorAfterReload(viewer.deckFiles.viewsBefore, viewer.deckFiles.cursorBefore, viewer.navigator.graph); break;
    }
    for (const std::string& warning : whole.warnings) {
        viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, viewer.loader.manifest().entry, warning);
    }
    viewer.reportedClips.clear();
    Fonts::instance().stageDeck(viewer.loader.manifest(), [&viewer](std::string_view reference) { return viewer.loader.figureBytes(reference); }, viewer.diagnostics);
    for (const Section& section : viewer.sections) {
        reportFontChoices(viewer, viewIdOf(section), section.document);
    }
    for (const View& view : viewer.navigator.graph.views) {
        for (const std::string& target : view.next) {
            if (viewer.navigator.graph.find(target) == nullptr) {
                viewer.diagnostics.report(DiagnosticKind::invalidNavigationTarget, target, std::format("view '{}' points at a view this presentation does not have", view.id));
            }
        }
    }
    for (const Section& section : viewer.sections) {
        const Cursor        at{.viewId = std::string{viewIdOf(section)}, .step = 0UZ};
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
        for (const Block& block : section.document.blocks) {
            if (block.kind == BlockKind::directive && block.info == "grid") {
                viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/:::grid", at.viewId), "is now called `:::place`; read as such");
            }
        }
        // an unknown effect or transition would otherwise be drawn as the default without a word
        const auto commaSeparated = []<typename TNames>(TNames&& names) { return std::forward<TNames>(names) | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>(); };
        const auto reportUnknown  = [&](std::string_view value, std::span<const std::string_view> known, std::string_view what) {
            if (!value.empty() && !std::ranges::contains(known, value)) {
                viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, value), std::format("is no {} ({}); the default is used", what, commaSeparated(known)));
            }
        };
        // a reveal is a motion the viewer has, or an effect the deck or the viewer has
        const auto reportUnknownReveal = [&](std::string_view value) {
            if (!value.empty() && !std::ranges::contains(kRevealNames, value) && viewer.effects.find(value) == nullptr) {
                const std::string subject = std::format("{}/{}", at.viewId, value);
                const std::string message = std::format("is no way to arrive ({}) and no effect in the deck's effects/ or the viewer's; the fade is used", commaSeparated(kRevealNames));
                viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, subject, message);
            }
        };
        // a transition is one the viewer moves by itself, or an effect the deck or the viewer has
        if (const std::string_view effect = effectNamed(visual.transition); !effect.empty() && viewer.effects.find(effect) == nullptr) {
            const std::string subject        = std::format("{}/{}", at.viewId, effect);
            const std::string movedByViewer  = commaSeparated(kTransitionNames);
            const std::string bundledEffects = commaSeparated(EffectLibrary::bundledNames());
            const std::string message        = std::format("is no transition the viewer moves by itself ({}) and no effect in the deck's effects/ or the viewer's ({}); the default is used", movedByViewer, bundledEffects);
            viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, subject, message);
        }
        for (const Document::StepReveal& stepReveal : section.document.stepReveals) {
            reportUnknownReveal(stepReveal.reveal.kind);
            reportUnknown(stepReveal.reveal.from, kRevealDirections, "direction to arrive from");
        }
        for (const Block& block : section.document.blocks | std::views::filter([](const Block& block) { return block.kind == BlockKind::directive && block.info == "stage"; })) {
            for (const std::string& transition : parseNameList(block.field("transitions"))) {
                const Transition::Kind kind = transitionKindFor(transition, false);
                if (kind == Transition::Kind::camera || kind == Transition::Kind::morph) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, transition), "moves a whole slide and cannot be shown in a stage; a fade is shown");
                } else if (kind == Transition::Kind::shader && viewer.effects.find(effectNamed(transition)) == nullptr) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, std::format("{}/{}", at.viewId, transition), "is no transition the viewer moves by itself and no effect in the deck's effects/ or the viewer's; a fade is shown");
                }
            }
        }
        for (const Block& block : section.document.blocks) {
            reportUnknownReveal(block.reveal.kind);
            reportUnknown(block.reveal.from, kRevealDirections, "direction to arrive from");
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
                const std::string region{block.name()};
                const LiveRegion  binding = liveRegionFrom(block.fields);
                if (const std::string_view legend = block.field("legend"); !legend.empty() && !binding.legend) {
                    viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, region, std::format("'{}' is no legend position (bottom, top, left, right or none); the deck's is used", legend));
                }
                viewer.regions.bind(region, binding);
            }
        }
    }
    viewer.documentView.imageFor = [&viewer](std::string_view reference) {
        const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        const float  columnWidth  = DocumentView::columnWidth(viewportSize.x, Fonts::slideBodySize(viewportSize.x, viewportSize.y));
        return viewer.figures.get(reference, static_cast<std::uint32_t>(columnWidth));
    };
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
    Citations cited                = citationsOf(viewer.sections);
    viewer.documentView.citations  = std::move(cited.footnotes);
    viewer.documentView.references = std::move(cited.references);
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
    for (const auto& [workflow, graph] : viewer.live.graphs) {
        graph->restyle();
    }
}

} // namespace gr::present
