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
[[nodiscard]] static const Block*       layoutDeclarationOf(const Document& document);
static void                             transformDrawn(ImDrawList& list, int fromVertex, int toVertex, int fromCommand, int toCommand, float scale, ImVec2 offset);

/// a stop's own lines and those of its `:::aside` box; a fence inside the aside's code is code, not its end
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

/// `region=` or `via=` of a `:::stop`: a declared region, four fractions or `whole`; empty when unreadable
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

[[nodiscard]] SectionVisual visualFor(Viewer& viewer, const Cursor& cursor) {
    SectionVisual visual;
    const auto    found = sectionOf(viewer.sections, cursor.viewId);
    if (found == nullptr) {
        return visual;
    }
    visual.section    = found;
    visual.background = viewer.loader.manifest().background;

    if (const auto declared = std::ranges::find_if(found->document.blocks, [](const Block& block) { return block.kind == BlockKind::directive && (block.info == "place" || block.info == "grid"); }); declared != found->document.blocks.end()) {
        visual.declaredGrid = regionsOf(declared->fields);
    }

    // a directive the viewer has no meaning for is a content box
    for (const Block& block : found->document.blocks) {
        if (!isBox(block) || (block.lines.empty() && block.field("notes") != "all")) {
            continue;
        }
        visual.slots.push_back(AreaDocument{.id = block.info, .contents = boxDocumentOf(block.lines, found->document), .options = boxOptionsOf(block)});
    }

    const Block* declaration = layoutDeclarationOf(found->document);
    if (declaration == nullptr) {
        return visual;
    }
    const ImVec2 screen = ImGui::GetMainViewport()->Size;
    visual.source       = std::string{screen.y > screen.x && !declaration->field("portrait").empty() ? declaration->field("portrait") : declaration->field("source")};
    visual.transition   = std::string{declaration->field("transition")};
    visual.overlay      = std::string{declaration->field("overlay")};
    if (const std::string_view own = declaration->field("background"); !own.empty()) {
        visual.background = std::string{own};
    }
    visual.grid        = std::string{declaration->field("grid")};
    visual.duration    = parseSeconds(declaration->field("duration")).value_or(-1.0f);
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

    // a raster is framed by declared regions, not by boxes drawn into the file
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

        // the latest `:::stop` reached frames the camera, sets the pace of the move and carries the words beside it
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
    visual.layout                   = &known->second;
    const std::string_view anchorId = declaration->field("anchor");
    if (const Area* anchor = known->second.find(anchorId); anchor != nullptr) {
        visual.frame = Rectangle{.x = anchor->x, .y = anchor->y, .width = anchor->width, .height = anchor->height};
    } else if (!anchorId.empty()) {
        visual.missingAnchor = std::string{anchorId};
    }
    return visual;
}

const FrontMatter kNoFrontMatter{};

/// counted by position in the document, so it is the same on every branch and matches a handout
[[nodiscard]] static std::string slideNumber(const Viewer& viewer, const Section* section) {
    const std::string_view wanted = viewer.sections.empty() ? std::string_view{} : std::string_view{viewer.sections.front().document.front.numbering};
    if (section == nullptr || wanted == "none") {
        return {};
    }
    const std::size_t number = slideNumberOf(viewer, viewIdOf(*section));
    if (number == 0UZ) {
        return {};
    }
    const std::string here = std::to_string(number);
    return wanted == "number-of-total" ? here + " / " + std::to_string(viewer.sections.size()) : here;
}

/// the height each prose-only row asks for; a picture row is left out as it fills its box; empty for a single row
/// the boxes a slide fills; a box nobody fills collapses, so the slide is measured and drawn on this one list
[[nodiscard]] static std::vector<std::string> filledAreasOf(const SectionVisual& visual) {
    // `content` is filled by the section's own prose, so a plain slide keeps its box
    std::vector<std::string> filled{"content"};
    for (const AreaDocument& slot : visual.slots) {
        filled.push_back(slot.id);
    }
    for (const auto& [name, unusedResource] : visual.areaContent) {
        filled.push_back(name);
    }
    // a live region or effect viewport fills a box too, and so do the bars it places
    for (const Block& block : visual.section->document.blocks) {
        if (block.kind == BlockKind::directive && (block.info == "gr4" || block.info == "shader" || block.info == "stage")) {
            std::string_view named = block.field("region");
            if (named.starts_with('#')) {
                named.remove_prefix(1UZ);
            }
            if (!named.empty()) {
                filled.emplace_back(named);
            }
            for (const std::string_view bar : {block.field("toolbar"), block.field("status")}) {
                if (!bar.empty() && bar != "here") {
                    filled.emplace_back(bar);
                }
            }
        }
    }
    return filled;
}

[[nodiscard]] static std::vector<float> measuredRows(Viewer& viewer, const SectionVisual& visual, std::span<const GridRow> rows, const ImGuiViewport& viewport) {
    if (rows.size() < 2UZ || visual.section == nullptr) {
        return {};
    }
    const Layout provisional = gridLayoutOf(rows, viewport.Size.x, viewport.Size.y, filledAreasOf(visual));
    const float  body        = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);

    std::vector<float> wanted(rows.size(), 0.0f);
    for (std::size_t index = 0UZ; index < rows.size(); ++index) {
        float tallest = 0.0f;
        bool  prose   = !rows[index].cells.empty();
        for (const GridCell& cell : rows[index].cells) {
            const Area* area = provisional.find(cell.name);
            if (area == nullptr) {
                continue; // nothing filled it, so it collapsed
            }
            if (valueFor(visual.areaContent, cell.name)) {
                prose = false;
                break;
            }
            const auto slot = std::ranges::find(visual.slots, cell.name, &AreaDocument::id);
            if (cell.name != "content" && slot == visual.slots.end()) {
                continue; // a live chart, an effect viewport or a bar sizes itself; only prose is measured
            }
            const Document&  contents = slot != visual.slots.end() ? slot->contents : visual.section->document;
            const BoxOptions options  = slot != visual.slots.end() ? slot->options : BoxOptions{};
            tallest                   = std::max(tallest, viewer.documentView.heightOf(viewer.theme, contents, area->width - body, body, area->height, options));
        }
        wanted[index] = prose ? tallest : 0.0f;
    }
    return wanted;
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
    // a cross-fade draws two sections in one frame and a widget drawn twice would reuse one ImPlot identity, so only
    // the arriving section draws live regions; an effect viewport has no identity to share and draws in either
    viewer.documentView.regionFor = [&viewer, &visual, live, step](std::string_view id, const DocumentView::RegionBoxes& boxes) {
        const auto directiveNamed = [&visual, id](std::string_view info) {
            const auto isThisRegion = [id, info](const Block& block) {
                const std::string_view blockName = block.id.empty() ? std::string_view{block.info} : std::string_view{block.id};
                return block.kind == BlockKind::directive && block.info == info && blockName == id;
            };
            return std::ranges::find_if(visual.section->document.blocks, isThisRegion);
        };
        const auto stage = directiveNamed("stage");
        if (stage != visual.section->document.blocks.end()) {
            return drawStage(viewer, visual.section->id, *stage, boxes.charts, step);
        }
        const auto shader = directiveNamed("shader");
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
        // a grid at the viewport's own size maps one to one, so nothing is letterboxed and title and footer stay put
        const ImGuiViewport& viewport = *ImGui::GetMainViewport();

        // narrow first, then measure: a stacked row must be measured at the width it will actually have
        const std::string_view     gridSpec = visual.grid.empty() ? kDefaultGrid : std::string_view{visual.grid};
        const std::vector<GridRow> rows     = stackedWhenNarrow(gridRowsOf(gridSpec), viewport.Size.x, viewport.Size.y);
        if (visual.declaredGrid.empty()) {
            viewer.slides.grid = gridLayoutOf(rows, viewport.Size.x, viewport.Size.y, filledAreasOf(visual), measuredRows(viewer, visual, rows, viewport));
        } else {
            viewer.slides.grid = declaredLayoutOf(visual.declaredGrid, viewport.Size.x, viewport.Size.y);
        }
        viewer.documentView.layout = &viewer.slides.grid;
    }
    if (visual.layout != nullptr) {
        const std::vector<std::string> hidden = hiddenAt(*visual.layout, static_cast<int>(step));
        viewer.documentView.layoutHidden      = hidden;
        // a drawing far larger than the view is one the camera moves into: rasterised at its own size, up to a limit
        constexpr float kLargestDrawingWidth = 4096.0f;
        const float     viewWidth            = ImGui::GetMainViewport()->Size.x;
        const float     rasterWidth          = visual.layout->width > viewWidth * 1.25f ? std::min(visual.layout->width, kLargestDrawingWidth) : viewWidth;
        viewer.documentView.layoutBackdrop   = viewer.figures.get(visual.source, static_cast<std::uint32_t>(rasterWidth), hidden);
    }

    viewer.documentView.areaContent   = visual.areaContent;
    viewer.documentView.areaDocuments = visual.slots;
    viewer.documentView.slideFont     = visual.font;
    viewer.documentView.outlineAreas  = visual.outline;
    viewer.documentView.slideSize     = visual.size;
    const FrontMatter& front          = viewer.sections.empty() ? kNoFrontMatter : viewer.sections.front().document.front;
    const std::string  title          = front.title.empty() ? viewer.loader.manifest().title : front.title;
    const std::string  who            = front.author.empty() ? std::string{} : (front.email.empty() ? front.author : front.author + "  " + front.email);
    viewer.documentView.footerText    = who.empty() ? title : title + "  \u00b7  " + who;
    viewer.documentView.footerNumber  = slideNumber(viewer, visual.section);
    viewer.documentView.document      = &visual.section->document;
    viewer.documentView.step          = static_cast<int>(step);
    viewer.documentView.stepSeconds   = live && viewer.slides.stepSince ? static_cast<float>(ImGui::GetTime() - *viewer.slides.stepSince) : -1.0f;
    // the slide being entered, also while a push or zoom moves it in: its boxes are held back until the move ends
    const bool entering                = viewer.slides.arrivedAt.has_value() && viewIdOf(*visual.section) == viewer.navigator.cursor.viewId;
    viewer.documentView.arriving       = entering;
    viewer.documentView.arrivedSeconds = entering ? static_cast<float>(ImGui::GetTime() - *viewer.slides.arrivedAt) : -1.0f;
    viewer.documentView.cameraFrame    = frame;
    viewer.documentView.opacity        = opacity;

    // a raster is framed in its own texture coordinates, so the camera does not need the file's pixel size
    viewer.documentView.cameraImage    = visual.image.empty() ? nullptr : viewer.figures.get(visual.image, static_cast<std::uint32_t>(ImGui::GetMainViewport()->Size.x));
    viewer.documentView.cameraUv       = visual.image.empty() ? Rectangle{} : frame;
    viewer.documentView.cameraScope    = visual.scope;
    viewer.documentView.cameraMove     = visual.image.empty() ? DocumentView::CameraMove{} : move;
    viewer.documentView.cameraRotation = rotation;
    viewer.documentView.infoBox        = DocumentView::InfoBox{.contents = visual.info, .aside = visual.infoAside, .region = visual.region, .side = visual.infoSide, .opacity = infoOpacity};
    viewer.figures.clock               = ImGui::GetTime();
    viewer.videos.clock                = ImGui::GetTime();
    viewer.documentView.afterBackdrop  = visual.background.empty() || visual.background == "none" ? std::function<void(ImDrawList&)>{} : [&viewer, &visual](ImDrawList& list) { drawBackgroundEffect(viewer, visual.background, list); };
    // an overlay takes the whole slide as a picture, inside whatever a transition takes it for
    const std::function<void(ImDrawList&)> before = viewer.documentView.beforeDrawing;
    const std::function<void(ImDrawList&)> after  = viewer.documentView.afterDrawing;
    const std::string                      key    = std::format("overlay:{}", visual.section->id);
    if (!visual.overlay.empty() && visual.overlay != "none" && !viewer.exporting) { // an exported page has no overlay
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
    // `in=` and a box's `overlay=` name effects drawn through; an export shows the box as arrived
    viewer.documentView.throughEffect = [&viewer, &visual](ImDrawList& list, int firstCommand, std::string_view effect, std::string_view boxKey, float progress, ImVec2 low, ImVec2 high) {
        if (viewer.documentView.recording != nullptr) {
            return false;
        }
        const std::string effectKey = std::format("through:{}/{}", visual.section->id, boxKey);
        return drawThroughEffect(viewer, effectKey, effect, list, firstCommand, progress, low, high);
    };
    viewer.documentView.revealSeconds = [&viewer](std::string_view kind) { return revealSecondsOf(viewer, kind); };
    viewer.documentView.draw(viewer.theme);
    viewer.documentView.throughEffect = {};
    viewer.documentView.afterBackdrop = {};
    viewer.documentView.beforeDrawing = before;
    viewer.documentView.afterDrawing  = after;

    // reported per frame, not at load, as fit depends on the window; once per box, as a person reads the list
    for (const DocumentView::Overrun& box : viewer.documentView.clipped) {
        const std::string named = visual.section->id + "/" + box.id;
        if (std::ranges::find(viewer.reportedClips, named) == viewer.reportedClips.end()) {
            viewer.reportedClips.push_back(named);
            const char* const clipReason = box.shrank ? "already at the smallest size allowed" : "and this box was told not to shrink";
            viewer.diagnostics.report(DiagnosticKind::clippedContent, named, std::format("{} px of content in a {} px box, {}", std::lround(box.wanted), std::lround(box.available), clipReason));
            buildSideMenu(viewer); // the menu counts the problems and is built once
        }
    }

    // a glyph a deck's face lacks is drawn in the built-in face; the first per face is reported
    for (auto& [face, detail] : Fonts::instance().takeFallbackNotes()) {
        viewer.diagnostics.report(DiagnosticKind::fontFallback, std::move(face), std::move(detail));
        buildSideMenu(viewer);
    }

    // likewise a bar put in an area the layout lacks, drawn beside the charts
    for (const std::string& bar : viewer.documentView.barsWithoutArea) {
        const std::string named = visual.section->id + "/" + bar;
        if (std::ranges::find(viewer.reportedClips, named) == viewer.reportedClips.end()) {
            viewer.reportedClips.push_back(named);
            viewer.diagnostics.report(DiagnosticKind::unsupportedBlock, named, "the layout has no such area; the bar is drawn beside the region's charts");
            buildSideMenu(viewer);
        }
    }

    publishRegionBounds(viewer);
}

EffectInputs effectInputsFor(const Viewer& viewer) {
    const ImGuiIO&    io  = ImGui::GetIO();
    const std::time_t now = viewer.frameWallClock;
    std::tm           local{};
    localtime_r(&now, &local);
    const auto colour = [](ImU32 packed) {
        const ImVec4 value = ImGui::ColorConvertU32ToFloat4(packed);
        return std::array{value.x, value.y, value.z, value.w};
    };
    const std::array<float, 4> date{static_cast<float>(local.tm_year + 1900), static_cast<float>(local.tm_mon), static_cast<float>(local.tm_mday), static_cast<float>(local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec)};
    const float                dark            = viewer.theme.scheme == ColourScheme::dark ? 1.0f : 0.0f;
    const auto                 themeBackground = colour(viewer.theme.background);
    const auto                 themeText       = colour(viewer.theme.text);
    const auto                 themeAccent     = colour(viewer.theme.fill);
    return EffectInputs{
        .time            = static_cast<float>(ImGui::GetTime()),
        .timeDelta       = io.DeltaTime,
        .frame           = ImGui::GetFrameCount(),
        .frameRate       = io.Framerate,
        .mouse           = {},
        .date            = date,
        .slideFrom       = 0U,
        .slideTo         = 0U,
        .content         = 0U,
        .progress        = 0.0f,
        .focus           = {0.5f, 0.5f},
        .dark            = dark,
        .themeBackground = themeBackground,
        .themeText       = themeText,
        .themeAccent     = themeAccent,
        .parameters      = {},
        .keepAlpha       = false,
    };
}

void settleCursor(Viewer& viewer, const Cursor& before) {
    if (viewer.navigator.cursor == before) {
        return;
    }
    viewer.slides.heldForSeconds = 0.0f;
    viewer.slides.viewSince      = ImGui::GetTime();
    // reveals animate going forward only: stepping back, a deep link or a reload shows the step as it is
    const auto& views       = viewer.navigator.graph.views;
    const auto  indexOf     = [&views](std::string_view id) { return std::ranges::find(views, id, &View::id) - views.begin(); };
    const bool  sameView    = viewer.navigator.cursor.viewId == before.viewId;
    viewer.slides.stepSince = sameView && viewer.navigator.cursor.step > before.step ? std::optional{ImGui::GetTime()} : std::nullopt;
    viewer.slides.arrivedAt = !sameView && indexOf(viewer.navigator.cursor.viewId) > indexOf(before.viewId) ? std::optional{ImGui::GetTime()} : (sameView ? viewer.slides.arrivedAt : std::nullopt);
    viewer.slides.infoSince.reset();
    viewer.zoom.reset();
    beginTransition(viewer, before);
    if (viewer.slides.arrivedAt && viewer.transition.running()) {
        *viewer.slides.arrivedAt += static_cast<double>(viewer.transition.duration); // a box's clock starts when the move ends
    }
    publishCursor(viewer);
    if (viewer.navigator.cursor.viewId != before.viewId) {
        viewer.videos.rewind(); // a reveal step within a view must not
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
    // the same picture decides a camera move: one SVG master or one raster
    const bool       samePicture = (leaving.layout != nullptr && leaving.layout == arriving.layout) || (!leaving.image.empty() && leaving.image == arriving.image);
    Transition::Kind kind        = transitionKindFor(arriving.transition, samePicture);
    // an effect that is missing or does not compile moves as if the slide had not asked
    const EffectSource* effect = kind == Transition::Kind::shader ? viewer.effects.find(effectNamed(arriving.transition)) : nullptr;
    if (kind == Transition::Kind::shader && (effect == nullptr || !viewer.slides.shaderTransition.prepare(viewer.effects, effectNamed(arriving.transition)))) {
        kind   = transitionKindFor({}, samePicture);
        effect = nullptr;
    }
    // pace: the arriving section's, else the effect's, else the deck's
    const float            deckSeconds = effect != nullptr ? effect->durationSeconds.value_or(kShaderSeconds) : viewer.loader.manifest().transitionSeconds;
    const float            seconds     = arriving.duration >= 0.0f ? arriving.duration : deckSeconds;
    const std::string_view settings    = std::string_view{arriving.transition}.substr(std::min(arriving.transition.find(' '), arriving.transition.size()));
    // `camera ripple amplitude=0.2`: the camera or zoom move seen through the effect named after it
    const bool seenThrough = (kind == Transition::Kind::camera || kind == Transition::Kind::zoom) && !effectNamed(arriving.transition).empty();
    const auto direction   = transitionDirectionFor(arriving.transition.substr(0UZ, arriving.transition.find(' ')));
    const auto effectName  = effect != nullptr ? std::string{effectNamed(arriving.transition)} : std::string{};
    const auto moveEffect  = seenThrough ? std::string{settings.substr(1UZ)} : std::string{};
    viewer.transition      = Transition{
             .kind       = kind == Transition::Kind::cut ? Transition::Kind::none : kind,
             .from       = from,
             .elapsed    = 0.0f,
             .duration   = std::max(seconds, 0.001f),
             .direction  = direction,
             .effect     = effectName,
             .parameters = effect != nullptr ? viewer.effects.parametersOf(*effect, settings) : decltype(Transition::parameters){},
             .moveEffect = moveEffect,
    };
}

/// presses Next by itself once a view has been held for `advance:`; any move restarts the clock
void advanceOnItsOwn(Viewer& viewer, float seconds) {
    const Section* section = sectionOf(viewer.sections, viewer.navigator.cursor.viewId);
    // a step's `after=` counts from when the step before it has finished arriving, so a delay never cuts a reveal short
    const Document* document  = section == nullptr ? nullptr : &section->document;
    const Block*    layout    = document == nullptr ? nullptr : layoutDeclarationOf(*document);
    const float     held      = layout == nullptr ? -1.0f : parseSeconds(layout->field("advance")).value_or(-1.0f);
    const int       current   = static_cast<int>(viewer.navigator.cursor.step);
    const float     stepDelay = document == nullptr ? -1.0f : document->delayBefore(current + 1);
    const auto      reveal    = document == nullptr || !viewer.slides.stepSince ? nullptr : document->revealOf(current);
    const float     arriving  = reveal == nullptr ? 0.0f : reveal->seconds.value_or(revealSecondsOf(viewer, reveal->kind));
    const float     waiting   = stepDelay >= 0.0f ? stepDelay + arriving : (held >= 0.0f ? held : viewer.loader.manifest().advanceSeconds);
    if ((stepDelay < 0.0f && waiting <= 0.0f) || viewer.transition.running()) {
        viewer.slides.heldForSeconds = viewer.transition.running() ? 0.0f : viewer.slides.heldForSeconds;
        return; // nothing asked for it here, or the move into this view is still running
    }
    viewer.slides.heldForSeconds += seconds;
    if (viewer.slides.heldForSeconds < waiting) {
        return;
    }
    const Cursor before = viewer.navigator.cursor;
    viewer.navigator.next();
    settleCursor(viewer, before);
    viewer.slides.heldForSeconds = 0.0f; // at the end of a deck, where next() does nothing, it waits again
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
        // a stop's words fade in once the camera has arrived
        const double now        = ImGui::GetTime();
        viewer.slides.infoSince = viewer.slides.infoSince.value_or(now);
        const float shown       = std::clamp(static_cast<float>((now - *viewer.slides.infoSince) / kInfoFadeSeconds), 0.0f, 1.0f);
        drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, incoming.cameraFrame(), true, {}, shown, incoming.rotation);
        return;
    }
    viewer.slides.infoSince.reset();

    const float         eased   = viewer.transition.progress();
    const SectionVisual leaving = visualFor(viewer, viewer.transition.from);

    // a move seen through an effect captures everything it draws, first slide to last, as one picture
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
        // one continuous move: only the incoming section is drawn, with both ends resolved to the whole picture when
        // unanchored, or zooming out of a detail would walk the camera to the top-left corner
        const bool    raster = !incoming.image.empty();
        const Layout* master = incoming.layout != nullptr ? incoming.layout : leaving.layout;
        // a raster is framed in fractions of itself
        const float     width  = raster ? 1.0f : (master != nullptr ? master->width : 0.0f);
        const float     height = raster ? 1.0f : (master != nullptr ? master->height : 0.0f);
        const Rectangle from   = wholeIfEmpty(raster ? leaving.region : leaving.frame, width, height);
        const Rectangle to     = wholeIfEmpty(raster ? incoming.region : incoming.frame, width, height);
        if (raster) {
            // the camera works out the path itself, as it knows the room the picture has
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
        // each slide's run in the one draw list is transformed after drawing; live regions show only their outline
        const auto&  views    = viewer.navigator.graph.views;
        const auto   indexOf  = [&views](std::string_view id) { return std::ranges::find(views, id, &View::id) - views.begin(); };
        const float  forwards = indexOf(viewer.navigator.cursor.viewId) >= indexOf(viewer.transition.from.viewId) ? 1.0f : -1.0f;
        const ImVec2 size     = ImGui::GetMainViewport()->Size;
        const ImVec2 moves    = [&]() -> ImVec2 {
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
        const auto drawLeaving   = [&] { drawSection(viewer, leaving, viewer.transition.from.step, zoom ? 1.0f - eased : 1.0f, leaving.cameraFrame(), false); };
        const auto drawIncoming  = [&] { drawSection(viewer, incoming, viewer.navigator.cursor.step, zoom ? eased : 1.0f, incoming.cameraFrame(), false); };
        throughFrom(true, false);
        if (incomingOnTop) {
            drawLeaving();
        } else {
            drawIncoming();
        }
        throughFrom(false, true);
        ImDrawList* list = viewer.documentView.drawnInto;
        list->AddDrawCmd(); // no command holds both slides, so each keeps its own clip rectangle
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
            return; // not one list after all; drawn in place
        }
        const auto move = [&](bool below, float scale, ImVec2 offset) {
            const int firstVertex  = below ? 0 : belowVertices;
            const int endVertex    = below ? belowVertices : list->VtxBuffer.Size;
            const int firstCommand = below ? 0 : belowCommands;
            const int endCommand   = below ? belowCommands : list->CmdBuffer.Size;
            transformDrawn(*list, firstVertex, endVertex, firstCommand, endCommand, scale, offset);
        };
        const ImVec2 leavingTo{moves.x * eased * size.x, moves.y * eased * size.y};
        const ImVec2 incomingFrom{-moves.x * (1.0f - eased) * size.x, -moves.y * (1.0f - eased) * size.y};
        switch (kind) {
        case Transition::Kind::push:
            move(true, 1.0f, leavingTo);
            move(false, 1.0f, incomingFrom);
            break;
        case Transition::Kind::cover: move(false, 1.0f, incomingFrom); break;
        case Transition::Kind::uncover: move(false, 1.0f, leavingTo); break;
        default:
            move(true, 1.0f + kZoomReach * eased, ImVec2{});
            move(false, 1.0f - kZoomReach * (1.0f - eased), ImVec2{});
            break;
        }
        return;
    }
    if (kind == Transition::Kind::morph) {
        // matched blocks move between their boxes while the rest fades; clipping is lifted so a box edge cannot cut one
        drawSection(viewer, leaving, viewer.transition.from.step, 1.0f, leaving.cameraFrame(), false);
        ImDrawList* list = viewer.documentView.drawnInto;
        list->AddDrawCmd();
        const int                     split  = list->VtxBuffer.Size;
        const std::vector<DrawnPiece> before = viewer.documentView.drawnBlocks;
        drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, incoming.cameraFrame(), false);
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
        fade(0, split, 1.0f - eased);
        fade(split, list->VtxBuffer.Size, eased);
        morphPositions(std::span<ImDrawVert>{list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.Size)}, before, after, eased);
        return;
    }
    if (kind == Transition::Kind::shader) {
        // a ripple starts where the presenter last clicked, else in the middle
        ShaderTransition& shader = viewer.slides.shaderTransition;
        const ImVec2      click  = ImGui::GetIO().MouseClickedPos[0];
        const ImVec2      size   = ImGui::GetMainViewport()->Size;
        const ImVec2      focus  = click.x > 0.0f && click.y > 0.0f && click.x < size.x && click.y < size.y ? click : ImVec2{size.x * 0.5f, size.y * 0.5f};
        EffectInputs      inputs = effectInputsFor(viewer);
        inputs.progress          = eased;
        inputs.focus             = {focus.x / std::max(size.x, 1.0f), 1.0f - focus.y / std::max(size.y, 1.0f)};
        inputs.parameters        = viewer.transition.parameters;
        view.beforeDrawing       = [&](ImDrawList& list) { shader.beginCapture(list, 0, viewer.theme.background); };
        view.afterDrawing        = [&](ImDrawList& list) { shader.endCapture(list); };
        drawSection(viewer, leaving, viewer.transition.from.step, 1.0f, leaving.cameraFrame(), false);
        view.beforeDrawing = [&](ImDrawList& list) { shader.beginCapture(list, 1, viewer.theme.background); };
        view.afterDrawing  = [&](ImDrawList& list) {
            shader.endCapture(list);
            shader.composite(list, inputs);
        };
        drawSection(viewer, incoming, viewer.navigator.cursor.step, 1.0f, incoming.cameraFrame(), false);
        view.beforeDrawing = {};
        view.afterDrawing  = {};
        return;
    }
    if (kind == Transition::Kind::fadeThrough) {
        // two sets of words never overlap
        drawSection(viewer, leaving, viewer.transition.from.step, std::max(0.0f, 1.0f - 2.0f * eased), leaving.cameraFrame(), false);
        drawSection(viewer, incoming, viewer.navigator.cursor.step, std::max(0.0f, 2.0f * eased - 1.0f), incoming.cameraFrame());
        return;
    }
    drawSection(viewer, leaving, viewer.transition.from.step, 1.0f - eased, leaving.cameraFrame(), false);
    drawSection(viewer, incoming, viewer.navigator.cursor.step, eased, incoming.cameraFrame());
}

/// the slide's `:::layout` block, the first when it has several; null when it has none
[[nodiscard]] static const Block* layoutDeclarationOf(const Document& document) {
    const auto declaration = std::ranges::find_if(document.blocks, [](const Block& block) { return block.kind == BlockKind::directive && block.info == "layout"; });
    return declaration == document.blocks.end() ? nullptr : &*declaration;
}

} // namespace gr::present
