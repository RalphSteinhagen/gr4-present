#include "Viewer.hpp"

#include <gr4-present/Number.hpp>

#include <gnuradio-4.0/TriggerMatcher.hpp> // trim

#include <imgui.h>
#include <imgui_internal.h>

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

[[nodiscard]] static std::string        slideNumber(const Viewer& viewer, const Section* section);
[[nodiscard]] static std::vector<float> measuredRows(Viewer& viewer, const SectionVisual& visual, std::span<const GridRow> rows, const ImGuiViewport& viewport);
static void                             beginTransition(Viewer& viewer, const Cursor& from);
static void                             transformDrawn(ImDrawList& list, int fromVertex, int toVertex, int fromCommand, int toCommand, float scale, ImVec2 offset);

/// a stop's own lines, and those of the `:::aside` box inside it -- the second box shown with the caption; a fence the
/// aside quotes inside its code is code, not its end
[[nodiscard]] static std::pair<std::vector<std::string>, std::vector<std::string>> withoutAside(const std::vector<std::string>& lines) {
    std::vector<std::string> caption;
    std::vector<std::string> aside;
    bool                     inside = false;
    bool                     code   = false;
    for (const std::string& line : lines) {
        const std::string_view body = gr::trigger::detail::trim(line);
        if (!inside && body.starts_with(":::aside")) {
            inside = true;
            continue;
        }
        if (inside && body.starts_with("```")) {
            code = !code;
        }
        if (inside && !code && body == ":::") {
            inside = false;
            continue;
        }
        (inside ? aside : caption).push_back(line);
    }
    return {std::move(caption), std::move(aside)};
}

/// `region=` or `via=` of a `:::stop`: a region `:::regions` declared for `source`, four fractions of the picture, or
/// `whole`; empty when absent or unreadable
[[nodiscard]] Rectangle stopRegionOf(const Viewer& viewer, std::string_view source, std::string_view text) {
    if (text == "whole") {
        return Rectangle{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};
    }
    if (const auto declared = viewer.imageRegions.find(std::string{source}); declared != viewer.imageRegions.end()) {
        if (const auto named = std::ranges::find(declared->second, text, &Area::id); named != declared->second.end()) {
            return Rectangle{.x = named->x, .y = named->y, .width = named->width, .height = named->height};
        }
    }
    const std::array<std::pair<std::string, std::string>, 1> field{std::pair<std::string, std::string>{"stop", std::string{text}}};
    const std::vector<Area>                                  parsed = regionsOf(field);
    return parsed.empty() ? Rectangle{} : Rectangle{.x = parsed.front().x, .y = parsed.front().y, .width = parsed.front().width, .height = parsed.front().height};
}

/// a `font=` the deck names no face for, or a `size=` that is no size, is reported once when the deck loads; the
/// text is then set in what surrounds it, which is a fault the author should hear about rather than discover
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
        if (block.info == "layout" || !isConfigurationDirective(block.info)) {
            check(block.field("font"), block.field("size"));
        }
        if (!isConfigurationDirective(block.info) && !block.lines.empty()) {
            checkSpans(boxDocumentOf(block.lines, document));
        }
    }
}

[[nodiscard]] SectionVisual visualFor(Viewer& viewer, const Cursor& cursor) {
    SectionVisual visual;
    const auto    found = std::ranges::find_if(viewer.sections, [&cursor](const Section& candidate) { return (candidate.id.empty() ? std::string{"start"} : candidate.id) == cursor.viewId; });
    if (found == viewer.sections.end()) {
        return visual;
    }
    visual.section    = &*found;
    visual.background = viewer.loader.manifest().background; // the deck's, unless the layout names its own

    // an author may place the boxes themselves rather than describe rows of them
    if (const auto declared = std::ranges::find_if(found->document.blocks, [](const Block& block) { return block.kind == BlockKind::directive && (block.info == "place" || block.info == "grid"); }); declared != found->document.blocks.end()) {
        visual.declaredGrid = regionsOf(declared->fields);
    }

    // a directive the viewer has no meaning for is a content box, and its lines are the prose that goes in it
    for (const Block& block : found->document.blocks) {
        if (block.kind != BlockKind::directive || isConfigurationDirective(block.info) || (block.lines.empty() && block.field("notes") != "all")) {
            continue;
        }
        visual.slots.push_back(AreaDocument{.id = block.info, .contents = boxDocumentOf(block.lines, found->document), .options = boxOptionsOf(block)});
    }

    const auto declaration = std::ranges::find_if(found->document.blocks, [](const Block& block) { return block.kind == BlockKind::directive && block.info == "layout"; });
    if (declaration == found->document.blocks.end()) {
        return visual;
    }
    // a screen taller than wide takes the portrait master when the author drew one
    const ImVec2 screen = ImGui::GetMainViewport()->Size;
    visual.source       = std::string{screen.y > screen.x && !declaration->field("portrait").empty() ? declaration->field("portrait") : declaration->field("source")};
    visual.transition   = std::string{declaration->field("transition")};
    visual.overlay      = std::string{declaration->field("overlay")};
    if (const std::string_view own = declaration->field("background"); !own.empty()) {
        visual.background = std::string{own};
    }
    visual.grid        = std::string{declaration->field("grid")};
    visual.duration    = parseSeconds(declaration->field("duration")).value_or(-1.0f);
    visual.advance     = parseSeconds(declaration->field("advance")).value_or(-1.0f);
    visual.rotation    = parseNumber<float>(declaration->field("rotate")).value_or(0.0f); // degrees, signed
    visual.font        = std::string{declaration->field("font")};
    visual.outline     = declaration->field("outline") == "on";
    visual.size        = parseTypeSize(declaration->field("size"));
    visual.contradicts = contradictsMaster(visual.source, visual.grid, !visual.declaredGrid.empty());
    for (const auto& [key, value] : declaration->fields) {
        if (key != "source" && key != "portrait" && !isLayoutSetting(key)) {
            visual.areaContent.emplace_back(key, value);
        }
    }

    // a raster source is framed by named regions the author declared rather than by boxes drawn into the file
    if (!visual.source.empty() && !isSvgReference(visual.source)) {
        visual.image = visual.source;
        visual.scope = cameraScopeOf(declaration->field("camera"));

        const auto declared = viewer.imageRegions.find(visual.source);
        const auto framed   = [&declared, &viewer](std::string_view name) -> Rectangle {
            if (name.empty() || declared == viewer.imageRegions.end()) {
                return Rectangle{};
            }
            const auto region = std::ranges::find(declared->second, name, &Area::id);
            return region == declared->second.end() ? Rectangle{} : Rectangle{.x = region->x, .y = region->y, .width = region->width, .height = region->height};
        };
        const std::string_view regionId = declaration->field("region");
        visual.region                   = framed(regionId);
        visual.via                      = framed(declaration->field("via"));
        if (!regionId.empty() && visual.region.width <= 0.0f) {
            visual.missingAnchor = std::string{regionId};
        }

        // a section may also step through its picture: the latest `:::stop` reached frames the camera, sets the pace
        // of the move into it, and carries the words shown beside what it frames
        const auto stops   = found->document.blocks | std::views::reverse;
        const auto reached = std::ranges::find_if(stops, [&cursor](const Block& block) { return block.kind == BlockKind::directive && block.info == "stop" && block.step <= static_cast<int>(cursor.step); });
        if (reached != stops.end()) {
            visual.region   = stopRegionOf(viewer, visual.source, reached->field("region"));
            visual.via      = stopRegionOf(viewer, visual.source, reached->field("via"));
            visual.duration = parseSeconds(reached->field("duration")).value_or(visual.duration);
            visual.rotation = parseNumber<float>(reached->field("rotate")).value_or(visual.rotation); // degrees, signed
            if (visual.region.width > 0.0f && !reached->lines.empty()) {
                const auto [caption, aside] = withoutAside(reached->lines);
                visual.info                 = boxDocumentOf(caption, found->document);
                visual.infoAside            = aside.empty() ? Document{} : boxDocumentOf(aside, found->document);
                visual.infoSide             = std::string{reached->field("box")};
            }
        }
        return visual;
    }

    const auto known = viewer.layouts.find(visual.source);
    if (known == viewer.layouts.end()) {
        return visual;
    }
    visual.layout = &known->second;
    // an anchor names the element the camera frames; without one the whole master is shown
    const std::string_view anchorId = declaration->field("anchor");
    if (const Area* anchor = known->second.find(anchorId); anchor != nullptr) {
        visual.frame = Rectangle{.x = anchor->x, .y = anchor->y, .width = anchor->width, .height = anchor->height};
    } else if (!anchorId.empty()) {
        visual.missingAnchor = std::string{anchorId};
    }
    return visual;
}

const FrontMatter kNoFrontMatter{};

/**
 * What the footer says about where this slide is.
 *
 * Counted by position in the document rather than by how many slides a presenter has walked through, so the
 * number is the same whichever branch was taken and matches a printed handout. A reveal step is not a slide.
 */
[[nodiscard]] static std::string slideNumber(const Viewer& viewer, const Section* section) {
    const std::string_view wanted = viewer.sections.empty() ? std::string_view{} : std::string_view{viewer.sections.front().document.front.numbering};
    if (section == nullptr || wanted == "none") {
        return {};
    }
    const std::size_t number = slideNumberOf(viewer, section->id.empty() ? std::string_view{"start"} : std::string_view{section->id});
    if (number == 0UZ) {
        return {};
    }
    const std::string here = std::to_string(number);
    return wanted == "number-of-total" ? here + " / " + std::to_string(viewer.sections.size()) : here;
}

/**
 * How tall each row of `rows` asks to be, for the rows that hold nothing but words.
 *
 * A row's width is settled by the shares its cells state and by nothing else, so the grid is laid out once to
 * learn the widths, each prose row is measured at the width it will really have, and the grid is laid out again
 * from what they asked for. A row holding a picture is left out: a picture has no height of its own, it fills
 * whatever box it is given, so it would measure as nothing and the row would collapse.
 *
 * Only for a grid of more than one row. A single row is the slide, and a slide takes the room it has.
 */
[[nodiscard]] static std::vector<float> measuredRows(Viewer& viewer, const SectionVisual& visual, std::span<const GridRow> rows, const ImGuiViewport& viewport) {
    if (rows.size() < 2UZ || visual.section == nullptr) {
        return {};
    }
    std::vector<std::string> filled{"content"};
    for (const AreaDocument& slot : visual.slots) {
        filled.push_back(slot.id);
    }
    for (const auto& [name, unusedResource] : visual.areaContent) {
        filled.push_back(name);
    }

    const Layout provisional = gridLayoutOf(rows, viewport.Size.x, viewport.Size.y, filled);
    const float  body        = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);

    std::vector<float> wanted(rows.size(), 0.0f);
    for (std::size_t index = 0UZ; index < rows.size(); ++index) {
        float tallest = 0.0f;
        bool  prose   = !rows[index].cells.empty();
        for (const GridCell& cell : rows[index].cells) {
            const Area* area = provisional.find(cell.name);
            if (area == nullptr) {
                continue; // nothing filled it, so it collapsed and asks for nothing
            }
            if (std::ranges::find(visual.areaContent, cell.name, &std::pair<std::string, std::string>::first) != visual.areaContent.end()) {
                prose = false;
                break;
            }
            const auto       slot     = std::ranges::find(visual.slots, cell.name, &AreaDocument::id);
            const Document&  contents = slot != visual.slots.end() ? slot->contents : visual.section->document;
            const BoxOptions options  = slot != visual.slots.end() ? slot->options : BoxOptions{};
            tallest                   = std::max(tallest, viewer.documentView.heightOf(viewer.theme, contents, area->width - body, body, area->height, options));
        }
        wanted[index] = prose ? tallest : 0.0f;
    }
    return wanted;
}

/// the width over height a `:::gr4` block asked its chart to keep, from `aspect: 4:3` or `aspect: 1.333`; zero fills the box
[[nodiscard]] float statedAspect(const Viewer& viewer, std::string_view region) {
    const Cursor& cursor  = viewer.navigator.cursor;
    const auto    section = std::ranges::find_if(viewer.sections, [&cursor](const Section& candidate) { return (candidate.id.empty() ? std::string{"start"} : candidate.id) == cursor.viewId; });
    if (section == viewer.sections.end()) {
        return 0.0f;
    }
    for (const Block& block : section->document.blocks) {
        if (block.kind != BlockKind::directive || block.info != "gr4" || block.field("id") != region) {
            continue;
        }
        return aspectOf(block.field("aspect"));
    }
    return 0.0f;
}

float aspectOf(std::string_view text) {
    const std::size_t colon  = text.find(':');
    const auto        width  = parseNumber<float>(text.substr(0UZ, colon));
    const auto        height = colon == std::string_view::npos ? std::optional{1.0f} : parseNumber<float>(text.substr(colon + 1UZ));
    return !width || !height || *width <= 0.0f || *height <= 0.0f ? 0.0f : *width / *height;
}

void drawSection(Viewer& viewer, const SectionVisual& visual, std::size_t step, float opacity, const Rectangle& frame, bool live, const DocumentView::CameraMove& move, float infoOpacity, float rotation) {
    if (visual.section == nullptr) {
        return;
    }
    // A cross-fade draws two sections into one window in the same frame, and a widget drawn twice would use the
    // same ImPlot identity twice. Only the section arriving draws its live regions; the one leaving shows the
    // outline it would show in a viewer without the runtime, for the length of a transition.
    // An effect viewport draws in either section: it is the viewer's own GL, with no identity to share.
    viewer.documentView.regionFor = [&viewer, &visual, live, step](std::string_view id, const DocumentView::RegionBoxes& boxes) {
        const auto stage = std::ranges::find_if(visual.section->document.blocks, [id](const Block& block) { return block.kind == BlockKind::directive && block.info == "stage" && (block.id.empty() ? std::string_view{block.info} : std::string_view{block.id}) == id; });
        if (stage != visual.section->document.blocks.end()) {
            return drawStage(viewer, visual.section->id, *stage, boxes.charts, step);
        }
        const auto shader = std::ranges::find_if(visual.section->document.blocks, [id](const Block& block) { return block.kind == BlockKind::directive && block.info == "shader" && (block.id.empty() ? std::string_view{block.info} : std::string_view{block.id}) == id; });
        if (shader != visual.section->document.blocks.end()) {
            return drawShaderViewport(viewer, visual.section->id, *shader, boxes.charts);
        }
        return live && liveGraphsAvailable() && drawLiveRegion(viewer, id, boxes);
    };
    viewer.documentView.layout         = visual.layout;
    viewer.documentView.layoutBackdrop = nullptr;
    viewer.documentView.layoutSource   = visual.source;
    viewer.documentView.layoutHidden.clear();
    viewer.documentView.cameraSource = visual.image;
    if (visual.layout == nullptr && visual.image.empty()) {
        // No drawing to lay this section out, so it gets a grid, generated at the viewport's own size: the
        // mapping is then one to one, nothing is letterboxed, and the title and footer keep their places from
        // slide to slide. A section that names no grid gets the one every deck starts with.
        const ImGuiViewport& viewport = *ImGui::GetMainViewport();

        // a box is filled by a `:::<name>` block or by a resource named in the layout directive; `content` is
        // filled by the section's own prose, so a plain slide keeps its box whether or not it names one
        std::vector<std::string> filled{"content"};
        for (const AreaDocument& slot : visual.slots) {
            filled.push_back(slot.id);
        }
        for (const auto& [name, unusedResource] : visual.areaContent) {
            filled.push_back(name);
        }
        // a live region or an effect viewport fills a box too, and a box nobody fills collapses -- which is what
        // happened to the first grid written for one: the chart had nowhere to go and the slide showed a caption and
        // nothing else
        for (const Block& block : visual.section->document.blocks) {
            if (block.kind == BlockKind::directive && (block.info == "gr4" || block.info == "shader" || block.info == "stage")) {
                std::string_view named = block.field("region");
                if (named.starts_with('#')) {
                    named.remove_prefix(1UZ);
                }
                if (!named.empty()) {
                    filled.emplace_back(named);
                }
                for (const std::string_view bar : {block.field("toolbar"), block.field("status")}) { // and so do the bars it puts elsewhere
                    if (!bar.empty() && bar != "here") {
                        filled.emplace_back(bar);
                    }
                }
            }
        }
        // narrow first, then measure: a row that will be laid out down the slide must be measured at the width it
        // will actually have, or its words are measured in a column they never appear in
        const std::vector<GridRow> rows = stackedWhenNarrow(gridRowsOf(visual.grid.empty() ? kDefaultGrid : std::string_view{visual.grid}), viewport.Size.x, viewport.Size.y);
        viewer.grid                     = visual.declaredGrid.empty() ? gridLayoutOf(rows, viewport.Size.x, viewport.Size.y, filled, measuredRows(viewer, visual, rows, viewport)) : declaredLayoutOf(visual.declaredGrid, viewport.Size.x, viewport.Size.y);
        viewer.documentView.layout      = &viewer.grid;
    }
    if (visual.layout != nullptr) {
        const std::vector<std::string> hidden = hiddenAt(*visual.layout, static_cast<int>(step));
        viewer.documentView.layoutHidden      = hidden;
        // A drawing far larger than the view is one the camera moves into, so it is rasterised at its own size (up
        // to a limit) rather than at the view's, which magnified it into a blur.
        constexpr float kLargestDrawingWidth = 4096.0f;
        const float     viewWidth            = ImGui::GetMainViewport()->Size.x;
        const float     rasterWidth          = visual.layout->width > viewWidth * 1.25f ? std::min(visual.layout->width, kLargestDrawingWidth) : viewWidth;
        viewer.documentView.layoutBackdrop   = viewer.figures.get(visual.source, static_cast<std::uint32_t>(rasterWidth), hidden);
    }

    // source, anchor and transition configure the layout; any other key names an area and what belongs in it
    viewer.documentView.areaContent   = visual.areaContent;
    viewer.documentView.areaDocuments = visual.slots;
    viewer.documentView.slideFont     = visual.font;
    viewer.documentView.outlineAreas  = visual.outline;
    viewer.documentView.slideSize     = visual.size;
    // the talk names itself and its author; the manifest's title is the fallback when the document says nothing
    const FrontMatter& front         = viewer.sections.empty() ? kNoFrontMatter : viewer.sections.front().document.front;
    const std::string  title         = front.title.empty() ? viewer.loader.manifest().title : front.title;
    const std::string  who           = front.author.empty() ? std::string{} : (front.email.empty() ? front.author : front.author + "  " + front.email);
    viewer.documentView.footerText   = who.empty() ? title : title + "  \u00b7  " + who;
    viewer.documentView.footerNumber = slideNumber(viewer, visual.section);
    viewer.documentView.document     = visual.section->document;
    viewer.documentView.step         = static_cast<int>(step);
    viewer.documentView.stepSeconds  = live && viewer.stepSince >= 0.0 ? static_cast<float>(ImGui::GetTime() - viewer.stepSince) : -1.0f;
    // the slide being entered, also while a push or zoom still moves it in: its boxes are held back until the move
    // has ended, rather than shown during it and hidden again after
    const bool entering                = viewer.arrivedAt >= 0.0 && (visual.section->id.empty() ? std::string_view{"start"} : std::string_view{visual.section->id}) == viewer.navigator.cursor.viewId;
    viewer.documentView.arriving       = entering;
    viewer.documentView.arrivedSeconds = entering ? static_cast<float>(ImGui::GetTime() - viewer.arrivedAt) : -1.0f;
    viewer.documentView.cameraFrame    = frame;
    viewer.documentView.opacity        = opacity;

    // a raster is framed in its own texture coordinates, so the camera needs no idea of the file's pixel size
    viewer.documentView.cameraImage    = visual.image.empty() ? nullptr : viewer.figures.get(visual.image, static_cast<std::uint32_t>(ImGui::GetMainViewport()->Size.x));
    viewer.documentView.cameraUv       = visual.image.empty() ? Rectangle{} : frame;
    viewer.documentView.cameraScope    = visual.scope;
    viewer.documentView.cameraMove     = visual.image.empty() ? DocumentView::CameraMove{} : move;
    viewer.documentView.cameraRotation = rotation;
    viewer.documentView.infoBox        = DocumentView::InfoBox{.contents = visual.info, .aside = visual.infoAside, .region = visual.region, .side = visual.infoSide, .opacity = infoOpacity};
    viewer.figures.clock               = ImGui::GetTime(); // one clock for every animation, so they do not drift apart
    viewer.videos.clock                = ImGui::GetTime();
    // a background effect goes over the master and under the words
    viewer.documentView.afterBackdrop = visual.background.empty() || visual.background == "none" ? std::function<void(ImDrawList&)>{} : [&viewer, &visual](ImDrawList& list) { drawBackgroundEffect(viewer, visual.background, list); };
    // an overlay takes the whole slide as a picture, inside whatever a transition is taking it for
    const std::function<void(ImDrawList&)> before = viewer.documentView.beforeDrawing;
    const std::function<void(ImDrawList&)> after  = viewer.documentView.afterDrawing;
    const std::string                      key    = std::format("overlay:{}", visual.section->id);
    if (!visual.overlay.empty() && visual.overlay != "none" && !viewer.exporting) { // an exported page has no overlay (D44)
        viewer.documentView.beforeDrawing = [&](ImDrawList& list) {
            if (before) {
                before(list);
            }
            beginOverlay(viewer, key, visual.overlay, list, 1.0f);
        };
        viewer.documentView.afterDrawing = [&](ImDrawList& list) {
            endOverlay(viewer, key, list);
            if (after) {
                after(list);
            }
        };
    }
    // a step or box whose `in=` names an effect arrives through it, and a box's `overlay=` is drawn through one; both are
    // omitted from an export, which shows the box as it is once arrived (D44)
    viewer.documentView.throughEffect = [&viewer, &visual](ImDrawList& list, int firstCommand, std::string_view effect, std::string_view boxKey, float progress, ImVec2 low, ImVec2 high) { return viewer.documentView.recording == nullptr && drawThroughEffect(viewer, std::format("through:{}/{}", visual.section->id, boxKey), effect, list, firstCommand, progress, low, high); };
    viewer.documentView.revealSeconds = [&viewer](std::string_view kind) { return revealSecondsOf(viewer, kind); };
    viewer.documentView.draw(viewer.theme);
    viewer.documentView.throughEffect = {};
    viewer.documentView.afterBackdrop = {};
    viewer.documentView.beforeDrawing = before;
    viewer.documentView.afterDrawing  = after;

    // A box told not to shrink whose content still does not fit is clipped, and says so. Reported from the frame
    // rather than from the load, because whether content fits depends on the window it is being shown in; reported
    // once per box, because a frame runs sixty times a second and a problems list is read by a person.
    for (const DocumentView::Overrun& box : viewer.documentView.clipped) {
        const std::string named = visual.section->id + "/" + box.id;
        if (std::ranges::find(viewer.reportedClips, named) == viewer.reportedClips.end()) {
            viewer.reportedClips.push_back(named);
            viewer.diagnostics.report(DiagnosticKind::clippedContent, named, std::format("{} px of content in a {} px box, {}", std::lround(box.wanted), std::lround(box.available), box.shrank ? "already at the smallest size allowed" : "and this box was told not to shrink"));
            buildSideMenu(viewer); // the menu counts the problems, and it is built once: without this it still says none
        }
    }

    // a glyph a deck's face lacks is drawn in the built-in face behind it, and the first such glyph per face says so
    for (auto& [face, detail] : Fonts::instance().takeFallbackNotes()) {
        viewer.diagnostics.report(DiagnosticKind::fontFallback, std::move(face), std::move(detail));
        buildSideMenu(viewer);
    }

    // likewise a bar a region puts in an area the layout does not have, which is drawn beside the charts instead
    for (const std::string& bar : viewer.documentView.barsWithoutArea) {
        const std::string named = visual.section->id + "/" + bar;
        if (std::ranges::find(viewer.reportedClips, named) == viewer.reportedClips.end()) {
            viewer.reportedClips.push_back(named);
            viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, named, "the layout has no such area; the bar is drawn beside the region's charts");
            buildSideMenu(viewer);
        }
    }

    // the layout decides where a region sits, so the registry is refreshed from it rather than the other way round
    for (const DocumentView::PlacedRegion& placed : viewer.documentView.placedRegions) {
        const Rectangle bounds{.x = placed.min.x, .y = placed.min.y, .width = placed.max.x - placed.min.x, .height = placed.max.y - placed.min.y};
        viewer.regions.updateGeometry(placed.id, RegionGeometry{.bounds = bounds, .clip = bounds, .opacity = opacity, .visible = opacity > 0.0f});
    }
    publishRegionBounds(viewer);
}

EffectInputs effectInputsFor(const Viewer& viewer) {
    const ImGuiIO&    io  = ImGui::GetIO();
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
    localtime_r(&now, &local);
    const auto colour = [](ImU32 packed) {
        const ImVec4 value = ImGui::ColorConvertU32ToFloat4(packed);
        return std::array{value.x, value.y, value.z, value.w};
    };
    return EffectInputs{.time = static_cast<float>(ImGui::GetTime()), .timeDelta = io.DeltaTime, .frame = ImGui::GetFrameCount(), .frameRate = io.Framerate, .mouse = {}, .date = {static_cast<float>(local.tm_year + 1900), static_cast<float>(local.tm_mon), static_cast<float>(local.tm_mday), static_cast<float>(local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec)}, .slideFrom = 0U, .slideTo = 0U, .content = 0U, .progress = 0.0f, .focus = {0.5f, 0.5f}, .dark = viewer.theme.scheme == ColourScheme::dark ? 1.0f : 0.0f, .themeBackground = colour(viewer.theme.background), .themeText = colour(viewer.theme.text), .themeAccent = colour(viewer.theme.fill), .parameters = {}, .keepAlpha = false};
}

void settleCursor(Viewer& viewer, const Cursor& before) {
    if (viewer.navigator.cursor == before) {
        return;
    }
    viewer.heldFor   = 0.0f; // the deck measures how long it has been on this view, so arriving anywhere restarts it
    viewer.viewSince = ImGui::GetTime();
    // reveals animate going forward only: stepping back, a deep link or a reload shows the step as it is
    const auto& views    = viewer.navigator.graph.views;
    const auto  indexOf  = [&views](std::string_view id) { return std::ranges::find(views, id, &View::id) - views.begin(); };
    const bool  sameView = viewer.navigator.cursor.viewId == before.viewId;
    viewer.stepSince     = sameView && viewer.navigator.cursor.step > before.step ? ImGui::GetTime() : -1.0;
    viewer.arrivedAt     = !sameView && indexOf(viewer.navigator.cursor.viewId) > indexOf(before.viewId) ? ImGui::GetTime() : (sameView ? viewer.arrivedAt : -1.0);
    viewer.infoSince     = -1.0;
    viewer.zoom.reset();
    beginTransition(viewer, before);
    if (viewer.arrivedAt >= 0.0 && viewer.transition.running()) {
        viewer.arrivedAt += static_cast<double>(viewer.transition.duration); // a box's clock starts when the move ends
    }
    publishCursor(viewer);
    if (viewer.navigator.cursor.viewId != before.viewId) {
        viewer.videos.rewind(); // leaving a view rewinds its clips; a reveal step within one must not
    }
}

static void beginTransition(Viewer& viewer, const Cursor& from) {
    if (from.viewId == viewer.navigator.cursor.viewId) {
        // a reveal step is not a move, unless it is a stop that frames another part of the picture
        const SectionVisual before = visualFor(viewer, from);
        const SectionVisual after  = visualFor(viewer, viewer.navigator.cursor);
        if (after.image.empty() || before.region == after.region) {
            viewer.transition.kind = Transition::Kind::none;
            return;
        }
        // stepping back retraces the move it undoes, at that move's pace
        const SectionVisual& paced   = viewer.navigator.cursor.step < from.step ? before : after;
        const float          seconds = paced.duration >= 0.0f ? paced.duration : viewer.loader.manifest().transitionSeconds;
        viewer.transition            = Transition{.kind = Transition::Kind::camera, .from = from, .elapsed = 0.0f, .duration = std::max(seconds, 0.001f)};
        return;
    }
    const SectionVisual leaving  = visualFor(viewer, from);
    const SectionVisual arriving = visualFor(viewer, viewer.navigator.cursor);
    // "the same picture" is what decides a camera move: one SVG master, or one raster.
    // Two stops on one photograph are as continuous to an audience as two anchors on one drawing.
    const bool       samePicture = (leaving.layout != nullptr && leaving.layout == arriving.layout) || (!leaving.image.empty() && leaving.image == arriving.image);
    Transition::Kind kind        = transitionKindFor(arriving.transition, samePicture);
    // an effect the deck and the viewer lack, or one that does not compile, moves as if the slide had not asked
    const EffectSource* effect = kind == Transition::Kind::shader ? viewer.effects.find(effectNamed(arriving.transition)) : nullptr;
    if (kind == Transition::Kind::shader && (effect == nullptr || !viewer.shaderTransition.prepare(viewer.effects, effectNamed(arriving.transition)))) {
        kind   = transitionKindFor({}, samePicture);
        effect = nullptr;
    }
    // the arriving section sets the pace of the move into it, then the effect, then the deck's once-stated pace
    const float            deckSeconds = effect != nullptr ? effect->duration.value_or(kShaderSeconds) : viewer.loader.manifest().transitionSeconds;
    const float            seconds     = arriving.duration >= 0.0f ? arriving.duration : deckSeconds;
    const std::string_view settings    = std::string_view{arriving.transition}.substr(std::min(arriving.transition.find(' '), arriving.transition.size()));
    // `camera ripple amplitude=0.2`: the camera or zoom moves as ever, seen through the effect named after it
    const bool seenThrough = (kind == Transition::Kind::camera || kind == Transition::Kind::zoom) && !effectNamed(arriving.transition).empty();
    viewer.transition      = Transition{.kind = kind == Transition::Kind::cut ? Transition::Kind::none : kind, .from = from, .elapsed = 0.0f, .duration = std::max(seconds, 0.001f), .direction = transitionDirectionFor(arriving.transition.substr(0UZ, arriving.transition.find(' '))), .effect = effect != nullptr ? std::string{effectNamed(arriving.transition)} : std::string{}, .parameters = effect != nullptr ? viewer.effects.parametersOf(*effect, settings) : std::map<std::string, std::array<float, 4>, std::less<>>{}, .moveEffect = seenThrough ? std::string{settings.substr(1UZ)} : std::string{}};
}

/**
 * Goes on by itself once a view has been held long enough.
 *
 * `advance:` says how long, and going on is exactly what pressing Next does -- the next reveal step if the
 * section has one, otherwise the next section. One rule: an author who wants a build-up to run by itself and an
 * author who wants a slide to hand over both write the same field, and neither has to know which they asked for.
 *
 * Any move restarts the clock, because it measures time spent on this view, so a presenter who navigates is
 * never racing the deck.
 */
void advanceOnItsOwn(Viewer& viewer, float seconds) {
    const SectionVisual visual = visualFor(viewer, viewer.navigator.cursor);
    // a step that states its own delay reveals itself that long after the one before it, whatever the slide says
    // A step's `after=` counts from when the step before it has finished arriving, as a presentation tool's "after
    // previous" does, so a delay never cuts a reveal short; `after=0` is straight after it.
    const Document* document  = visual.section == nullptr ? nullptr : &visual.section->document;
    const int       current   = static_cast<int>(viewer.navigator.cursor.step);
    const float     stepDelay = document == nullptr ? -1.0f : document->delayBefore(current + 1);
    const auto      reveal    = document == nullptr || viewer.stepSince < 0.0 ? nullptr : document->revealOf(current);
    const float     arriving  = reveal == nullptr ? 0.0f : reveal->seconds.value_or(revealSecondsOf(viewer, reveal->kind));
    const float     waiting   = stepDelay >= 0.0f ? stepDelay + arriving : (visual.advance >= 0.0f ? visual.advance : viewer.loader.manifest().advanceSeconds);
    if ((stepDelay < 0.0f && waiting <= 0.0f) || viewer.transition.running()) {
        viewer.heldFor = viewer.transition.running() ? 0.0f : viewer.heldFor;
        return; // nothing asked for it here, or the move into this view is still running
    }
    viewer.heldFor += seconds;
    if (viewer.heldFor < waiting) {
        return;
    }
    const Cursor before = viewer.navigator.cursor;
    viewer.navigator.next();
    settleCursor(viewer, before);
    viewer.heldFor = 0.0f; // and at the end of a deck, where next() does nothing, it simply waits again
}

void advanceTransition(Viewer& viewer, float seconds) {
    if (viewer.transition.kind == Transition::Kind::none) {
        return;
    }
    viewer.transition.elapsed += seconds;
    if (viewer.transition.elapsed >= viewer.transition.duration) {
        viewer.transition.kind = Transition::Kind::none;
    }
}

constexpr float kZoomTransitionReach = 0.1f; // how much larger the leaving slide grows, and how much smaller the arriving one starts

/// scales the vertices [fromVertex, toVertex) and commands [fromCommand, toCommand) of `list` about the viewport's
/// centre, then moves them by `offset`; the commands' clip rectangles go with what they clip
static void transformDrawn(ImDrawList& list, int fromVertex, int toVertex, int fromCommand, int toCommand, float scale, ImVec2 offset) {
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const ImVec2         centre{viewport.Pos.x + viewport.Size.x * 0.5f, viewport.Pos.y + viewport.Size.y * 0.5f};
    const auto           moved = [&](float x, float y) { return ImVec2{centre.x + (x - centre.x) * scale + offset.x, centre.y + (y - centre.y) * scale + offset.y}; };
    for (int index = fromVertex; index < toVertex; ++index) {
        list.VtxBuffer[index].pos = moved(list.VtxBuffer[index].pos.x, list.VtxBuffer[index].pos.y);
    }
    for (int index = fromCommand; index < toCommand; ++index) {
        ImVec4&      clip = list.CmdBuffer[index].ClipRect;
        const ImVec2 low  = moved(clip.x, clip.y);
        const ImVec2 high = moved(clip.z, clip.w);
        clip              = ImVec4{low.x, low.y, high.x, high.y};
    }
}

void drawCurrentSection(Viewer& viewer) {
    const SectionVisual incoming = visualFor(viewer, viewer.navigator.cursor);
    captureStageSlides(viewer, incoming);

    if (viewer.transition.kind == Transition::Kind::none) {
        // a stop's words fade in once the camera has arrived, rather than riding in on the move
        const double now  = ImGui::GetTime();
        viewer.infoSince  = viewer.infoSince < 0.0 ? now : viewer.infoSince;
        const float shown = std::clamp(static_cast<float>((now - viewer.infoSince) / kInfoFadeSeconds), 0.0f, 1.0f);
        drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, incoming.image.empty() ? incoming.frame : incoming.region, true, {}, shown, incoming.rotation);
        return;
    }
    viewer.infoSince = -1.0;

    const float         eased   = viewer.transition.progress();
    const SectionVisual leaving = visualFor(viewer, viewer.transition.from);

    // a camera or zoom move seen through an effect: everything the move draws is captured as one picture, from the
    // first command of the first slide drawn to the last of the second, and the effect draws from it
    DocumentView&      view        = viewer.documentView;
    const std::string& moveEffect  = viewer.transition.moveEffect;
    const auto         throughFrom = [&](bool first, bool last) {
        if (moveEffect.empty()) {
            return;
        }
        view.beforeDrawing = first ? std::function<void(ImDrawList&)>{[&](ImDrawList& list) { beginOverlay(viewer, "move", moveEffect, list, eased); }} : std::function<void(ImDrawList&)>{};
        view.afterDrawing  = last ? std::function<void(ImDrawList&)>{[&](ImDrawList& list) { endOverlay(viewer, "move", list); }} : std::function<void(ImDrawList&)>{};
    };
    const auto throughDone = [&] {
        view.beforeDrawing = {};
        view.afterDrawing  = {};
    };

    if (viewer.transition.kind == Transition::Kind::camera) {
        // One picture, one continuous move: only the incoming section is drawn, framed between the two anchors. A
        // section without an anchor frames the whole picture, and both ends are resolved to that before the move, or
        // zooming out of a detail walks the camera into the top-left corner instead of back to the full picture.
        const bool      raster = !incoming.image.empty();
        const Layout*   master = incoming.layout != nullptr ? incoming.layout : leaving.layout;
        const float     width  = raster ? 1.0f : (master != nullptr ? master->width : 0.0f);  // a raster is framed
        const float     height = raster ? 1.0f : (master != nullptr ? master->height : 0.0f); // in fractions of itself
        const Rectangle from   = wholeIfEmpty(raster ? leaving.region : leaving.frame, width, height);
        const Rectangle to     = wholeIfEmpty(raster ? incoming.region : incoming.frame, width, height);
        if (raster) {
            // the camera works out the path itself: it knows the room the picture has, which decides how each frame is fitted
            const bool      retracing = viewer.transition.from.viewId == viewer.navigator.cursor.viewId && viewer.navigator.cursor.step < viewer.transition.from.step;
            const Rectangle via       = retracing ? leaving.via : incoming.via;
            throughFrom(true, true);
            drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, to, true, DocumentView::CameraMove{.from = from, .via = via, .to = to, .progress = eased}, 0.0f, std::lerp(leaving.rotation, incoming.rotation, eased));
            throughDone();
            return;
        }
        throughFrom(true, true);
        drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, interpolateVia(from, incoming.via, to, eased), true, {}, 0.0f, std::lerp(leaving.rotation, incoming.rotation, eased));
        throughDone();
        return;
    }

    const Transition::Kind kind = viewer.transition.kind;
    if (kind == Transition::Kind::push || kind == Transition::Kind::cover || kind == Transition::Kind::uncover || kind == Transition::Kind::zoom) {
        // Both slides are drawn as they are and then moved: what each drew is a run of vertices and commands in the
        // one draw list, so pushing a slide aside or scaling it is a transform of its run. Live regions show their
        // outline for the length of the move, since a chart is a window of its own that does not travel with it.
        // The slide on top -- the arriving one under `cover`, the leaving one under `uncover` -- is drawn second, on a
        // background of its own, since the slide colour is the window's and would let the one below show through.
        const auto&  views    = viewer.navigator.graph.views;
        const auto   indexOf  = [&views](std::string_view id) { return std::ranges::find(views, id, &View::id) - views.begin(); };
        const float  forwards = indexOf(viewer.navigator.cursor.viewId) >= indexOf(viewer.transition.from.viewId) ? 1.0f : -1.0f;
        const ImVec2 size     = ImGui::GetMainViewport()->Size;
        const ImVec2 moves    = [&]() -> ImVec2 { // the way the slides travel, a unit vector
            switch (viewer.transition.direction) {
            case Transition::Direction::left: return {-1.0f, 0.0f};
            case Transition::Direction::right: return {1.0f, 0.0f};
            case Transition::Direction::up: return {0.0f, -1.0f};
            case Transition::Direction::down: return {0.0f, 1.0f};
            case Transition::Direction::byOrder: break;
            }
            return {-forwards, 0.0f};
        }();
        const bool incomingOnTop = kind != Transition::Kind::uncover;
        const bool zoom          = kind == Transition::Kind::zoom;
        const auto drawLeaving   = [&] { drawSection(viewer, leaving, viewer.transition.from.step, zoom ? 1.0f - eased : 1.0f, leaving.image.empty() ? leaving.frame : leaving.region, false); };
        const auto drawIncoming  = [&] { drawSection(viewer, incoming, viewer.navigator.cursor.step, zoom ? eased : 1.0f, incoming.image.empty() ? incoming.frame : incoming.region, false); };
        throughFrom(true, false);
        if (incomingOnTop) {
            drawLeaving();
        } else {
            drawIncoming();
        }
        throughFrom(false, true);
        ImDrawList* list = viewer.documentView.drawnInto;
        list->AddDrawCmd(); // so no command holds both slides, and each keeps a clip rectangle of its own
        const int belowVertices = list->VtxBuffer.Size;
        const int belowCommands = list->CmdBuffer.Size - 1;
        if (kind == Transition::Kind::cover || kind == Transition::Kind::uncover) {
            const ImVec2 origin = ImGui::GetMainViewport()->Pos;
            list->AddRectFilled(origin, ImVec2{origin.x + size.x, origin.y + size.y}, viewer.theme.background);
        }
        if (incomingOnTop) {
            drawIncoming();
        } else {
            drawLeaving();
        }
        throughDone();
        if (list != viewer.documentView.drawnInto) {
            return; // not one list after all; drawn in place rather than moved wrongly
        }
        const auto   move = [&](bool below, float scale, ImVec2 offset) { transformDrawn(*list, below ? 0 : belowVertices, below ? belowVertices : list->VtxBuffer.Size, below ? 0 : belowCommands, below ? belowCommands : list->CmdBuffer.Size, scale, offset); };
        const ImVec2 leavingTo{moves.x * eased * size.x, moves.y * eased * size.y};
        const ImVec2 incomingFrom{-moves.x * (1.0f - eased) * size.x, -moves.y * (1.0f - eased) * size.y};
        switch (kind) {
        case Transition::Kind::push:
            move(true, 1.0f, leavingTo);
            move(false, 1.0f, incomingFrom);
            break;
        case Transition::Kind::cover: move(false, 1.0f, incomingFrom); break; // the arriving slide slides over
        case Transition::Kind::uncover: move(false, 1.0f, leavingTo); break;  // the leaving slide slides off
        default:
            move(true, 1.0f + kZoomTransitionReach * eased, ImVec2{});
            move(false, 1.0f - kZoomTransitionReach * (1.0f - eased), ImVec2{});
            break;
        }
        return;
    }
    if (kind == Transition::Kind::morph) {
        // What both slides show moves from where it was to where it will be, the one fading into the other on the way;
        // the rest fades out or in. Both are drawn into the one list, then each matched block's vertices -- and each line a
        // code block keeps -- are mapped from its own box to the box between the two. Clipping is lifted for the move: a box's edge would cut a block in
        // transit. Live regions show their outline, as in every other move.
        drawSection(viewer, leaving, viewer.transition.from.step, 1.0f, leaving.image.empty() ? leaving.frame : leaving.region, false);
        ImDrawList* list = viewer.documentView.drawnInto;
        list->AddDrawCmd();
        const int                     split  = list->VtxBuffer.Size;
        const std::vector<DrawnPiece> before = viewer.documentView.drawnBlocks;
        drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, incoming.image.empty() ? incoming.frame : incoming.region, false);
        if (list != viewer.documentView.drawnInto) {
            return;
        }
        const std::vector<DrawnPiece>& after = viewer.documentView.drawnBlocks;
        const ImVec2                   whole = ImGui::GetMainViewport()->Size;
        for (ImDrawCmd& command : list->CmdBuffer) {
            command.ClipRect = ImVec4{0.0f, 0.0f, whole.x, whole.y};
        }
        const auto fade = [list](int from, int to, float alpha) {
            for (int index = from; index < to; ++index) {
                list->VtxBuffer[index].col = dimmed(list->VtxBuffer[index].col, alpha);
            }
        };
        // every vertex of each slide fades first; the matched ones are then moved as well
        fade(0, split, 1.0f - eased);
        fade(split, list->VtxBuffer.Size, eased);
        morphPositions(std::span<ImDrawVert>{list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.Size)}, before, after, eased);
        return;
    }
    if (kind == Transition::Kind::shader) {
        // both slides drawn as pictures rather than to the screen, and one effect shader mixes them; a ripple starts
        // where the presenter last clicked, or in the middle
        ShaderTransition& shader = viewer.shaderTransition;
        const ImVec2      click  = ImGui::GetIO().MouseClickedPos[0];
        const ImVec2      size   = ImGui::GetMainViewport()->Size;
        const ImVec2      focus  = click.x > 0.0f && click.y > 0.0f && click.x < size.x && click.y < size.y ? click : ImVec2{size.x * 0.5f, size.y * 0.5f};
        EffectInputs      inputs = effectInputsFor(viewer);
        inputs.progress          = eased;
        inputs.focus             = {focus.x / std::max(size.x, 1.0f), 1.0f - focus.y / std::max(size.y, 1.0f)};
        inputs.parameters        = viewer.transition.parameters;
        view.beforeDrawing       = [&](ImDrawList& list) { shader.beginCapture(list, 0, viewer.theme.background); };
        view.afterDrawing        = [&](ImDrawList& list) { shader.endCapture(list); };
        drawSection(viewer, leaving, viewer.transition.from.step, 1.0f, leaving.image.empty() ? leaving.frame : leaving.region, false);
        view.beforeDrawing = [&](ImDrawList& list) { shader.beginCapture(list, 1, viewer.theme.background); };
        view.afterDrawing  = [&](ImDrawList& list) {
            shader.endCapture(list);
            shader.composite(list, inputs);
        };
        drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, incoming.image.empty() ? incoming.frame : incoming.region, false);
        view.beforeDrawing = {};
        view.afterDrawing  = {};
        return;
    }
    if (kind == Transition::Kind::fadeThrough) {
        // the old slide is gone before the new one comes: two sets of words never overlap
        drawSection(viewer, leaving, viewer.transition.from.step, std::max(0.0f, 1.0f - 2.0f * eased), leaving.image.empty() ? leaving.frame : leaving.region, false);
        drawSection(viewer, incoming, viewer.navigator.cursor.step, std::max(0.0f, 2.0f * eased - 1.0f), incoming.image.empty() ? incoming.frame : incoming.region);
        return;
    }
    drawSection(viewer, leaving, viewer.transition.from.step, 1.0f - eased, leaving.image.empty() ? leaving.frame : leaving.region, false);
    drawSection(viewer, incoming, viewer.navigator.cursor.step, eased, incoming.image.empty() ? incoming.frame : incoming.region);
}

} // namespace gr::present
