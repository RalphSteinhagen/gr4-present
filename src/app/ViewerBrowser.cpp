#include "Viewer.hpp"

#include <gr4-present/Number.hpp>

#include <imgui.h>

#ifdef __EMSCRIPTEN__
#include <common/LookAndFeel.hpp> // the chart faces a test reads
#include <emscripten.h>
#endif

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

#ifdef __EMSCRIPTEN__
[[nodiscard]] std::string emscriptenLocation(std::string_view component) {
    const char* text = emscripten_run_script_string(std::format("window.location.{}", component).c_str());
    return text != nullptr ? std::string{text} : std::string{};
}
#endif

#ifdef __EMSCRIPTEN__
// clang-format off
// remembered, so the `hashchange` it causes is not taken for one the reader typed
EM_JS(void, setLocationFragment, (const char* fragment), {
    globalThis.gr4OwnHash = '#' + UTF8ToString(fragment);
    location.replace(globalThis.gr4OwnHash);
});
// F5 and Escape belong to the browser -- reload, and leave full screen -- but SDL prevents the default of every key it
// handles. Taken before SDL sees them, in the capture phase, with Escape still noted for the viewer's own zoom.
EM_JS(void, passBrowserKeys, (), {
    if (globalThis.gr4PassesKeys) return;
    globalThis.gr4PassesKeys = true;
    for (const type of ["keydown", "keyup"]) {
        window.addEventListener(type, (event) => {
            if (event.key !== "F5" && event.key !== "Escape") return;
            if (event.key === "Escape" && type === "keydown") globalThis.gr4Escape = true;
            event.stopImmediatePropagation();
        }, true);
    }
});
// a fragment typed into the address bar of the open page, waiting to be followed
EM_JS(void, followAddressBar, (), {
    if (globalThis.gr4FollowsAddress) return;
    globalThis.gr4FollowsAddress = true;
    window.addEventListener("hashchange", () => {
        if (location.hash !== globalThis.gr4OwnHash) globalThis.gr4AddressPending = location.hash.replace(/^#/, "");
    });
});
// clang-format on
EM_JS(void, publishLocation, (const char* fragment), { globalThis.gr4Location = '#' + UTF8ToString(fragment); });

// clang-format off
// A phone acting as the remote must not go dark mid-talk: the presenter view holds a screen wake lock, and takes it
// again whenever the page comes back into view, since the browser drops it when the page is hidden.
EM_JS(void, holdScreenAwake, (), {
    if (!navigator.wakeLock) return;
    const hold = () => { if (document.visibilityState === "visible") navigator.wakeLock.request("screen").catch(() => {}); };
    document.addEventListener("visibilitychange", hold);
    hold();
});
// clang-format on

// The audience window and a presenter window opened from it are two copies of the deck in one browser. They keep one
// cursor between them over a BroadcastChannel: whichever moves says where it went, and the other follows.
// clang-format off
EM_JS(void, joinDeckChannel, (), {
    if (globalThis.gr4Deck || typeof BroadcastChannel !== "function") return;
    globalThis.gr4Deck = new BroadcastChannel("gr4-present");
    globalThis.gr4DeckPending = "";
    globalThis.gr4Deck.onmessage = (event) => { globalThis.gr4DeckPending = String(event.data); };
});
EM_JS(void, postCursor, (const char* fragment), {
    const cursor = UTF8ToString(fragment);
    if (globalThis.gr4Deck) globalThis.gr4Deck.postMessage(cursor);
    if (globalThis.gr4Relay) fetch("relay/" + globalThis.gr4Relay, { method: "POST", body: JSON.stringify({ from: globalThis.gr4RelayId, cursor: cursor }) }).catch(() => {});
});
// With `?relay=<token>` from `devtools/serve.mjs --relay`, the same cursor also travels through the server, which
// is how a phone on the network follows and leads. A window ignores what it sent itself.
EM_JS(void, joinRelay, (), {
    if (globalThis.gr4Relay !== undefined) return;
    globalThis.gr4Relay = "";
    globalThis.gr4PhoneLink = "";
    globalThis.gr4RelayId = Math.random().toString(36).slice(2);
    globalThis.gr4DeckPending = globalThis.gr4DeckPending || "";
    const follow = async (token) => {
        globalThis.gr4Relay = token;
        let after = 0;
        for (;;) {
            try {
                const response = await fetch("relay/" + token + "?after=" + after, { cache: "no-store" });
                if (!response.ok) { await new Promise((resolve) => setTimeout(resolve, 2000)); continue; }
                const answer = await response.json();
                const message = answer.message ? JSON.parse(answer.message) : null;
                if (answer.seq > after && message && message.from !== globalThis.gr4RelayId) globalThis.gr4DeckPending = message.cursor;
                after = answer.seq;
            } catch (error) {
                await new Promise((resolve) => setTimeout(resolve, 2000));
            }
        }
    };
    const token = new URLSearchParams(location.search).get("relay");
    if (token) { follow(token); return; }
    // opened on the presenter's own machine without a token: the server says which, and the phone's link with it
    fetch("relay-link", { cache: "no-store" }).then((response) => response.ok ? response.json() : null).then((link) => {
        if (!link) return;
        globalThis.gr4PhoneLink = link.phone;
        follow(link.token);
    }).catch(() => {});
});
EM_JS(void, publishExport, (int pages, const char* outcome), { globalThis.gr4Export = { pages: pages, outcome: UTF8ToString(outcome) }; });
EM_JS(void, openPresenterWindow, (), { window.open(location.pathname + "?presenter" + location.hash, "gr4-present-presenter", "popup"); });
// clang-format on
#endif

/// the view `#view=` names: a slide number as the footer counts it, from 1 in document order, or a view's id
[[nodiscard]] std::string viewNamed(const Viewer& viewer, std::string_view said) {
    const std::optional<std::size_t> number = parseNumber<std::size_t>(said);
    if (!number || *number == 0UZ || *number > viewer.sections.size()) {
        return std::string{said};
    }
    const Section& section = viewer.sections[*number - 1UZ];
    return section.id.empty() ? std::string{"start"} : section.id;
}

/// the footer's number of a view, from 1 in document order; 0 for none
[[nodiscard]] std::size_t slideNumberOf(const Viewer& viewer, std::string_view viewId) {
    const auto found = std::ranges::find_if(viewer.sections, [viewId](const Section& section) { return (section.id.empty() ? std::string_view{"start"} : std::string_view{section.id}) == viewId; });
    return found == viewer.sections.end() ? 0UZ : static_cast<std::size_t>(found - viewer.sections.begin()) + 1UZ;
}

void publishCursor(const Viewer& viewer) {
#ifdef __EMSCRIPTEN__
    const Cursor&     cursor = viewer.navigator.cursor;
    const std::size_t number = slideNumberOf(viewer, cursor.viewId);
    std::string       shown  = number == 0UZ ? std::format("view={}&step={}", cursor.viewId, cursor.step) : std::format("view={}&step={}", number, cursor.step);
    if (viewer.options.contains("fps")) {
        shown += "&fps";
    }
    setLocationFragment(shown.c_str());
    const std::string fragment = std::format("view={}&step={}", cursor.viewId, cursor.step);
    publishLocation(fragment.c_str());
    if (!viewer.followingRemote) {
        postCursor(fragment.c_str());
    }
#else
    (void)viewer;
#endif
}

/// moves to where the other window of this deck went, if it went anywhere since the last frame
bool escapeReachedThePage() {
#ifdef __EMSCRIPTEN__
    return emscripten_run_script_int("(() => { const pressed = !!globalThis.gr4Escape; globalThis.gr4Escape = false; return pressed ? 1 : 0; })()") != 0;
#else
    return false;
#endif
}

void followRemoteCursor(Viewer& viewer) {
#ifdef __EMSCRIPTEN__
    // another window's cursor, by view id, and the address bar's, by footer number or id
    for (const char* pending : {"gr4DeckPending", "gr4AddressPending"}) {
        const char*            sent = emscripten_run_script_string(std::format("(() => {{ const text = globalThis.{0} || ''; globalThis.{0} = ''; return text; }})()", pending).c_str());
        const std::string_view text = sent != nullptr ? std::string_view{sent} : std::string_view{};
        if (text.empty()) {
            continue;
        }
        LaunchOptions remote;
        LaunchOptions::parseUrlParameters(remote, text);
        const auto said = remote.value("view");
        if (!said.has_value()) {
            continue;
        }
        const std::string view   = viewNamed(viewer, *said);
        const std::size_t parsed = parseNumber<std::size_t>(remote.value("step").value_or("0")).value_or(0UZ);
        const Cursor      before = viewer.navigator.cursor;
        if (before.viewId == view && before.step == parsed) {
            continue;
        }
        // a step back in the other window is a step back here too, so Previous keeps meaning where this window came from
        auto& history = viewer.navigator.history;
        if (!history.empty() && history.back().viewId == view && history.back().step == parsed) {
            viewer.navigator.previous();
        } else if (viewer.navigator.jumpTo(view)) {
            viewer.navigator.cursor.step = parsed;
        } else {
            continue;
        }
        viewer.followingRemote = true;
        settleCursor(viewer, before);
        viewer.followingRemote = false;
    }
#else
    (void)viewer;
#endif
}

/// what a browser test reads to compare what a chart drew with the box the layout gave it and the type it was set in
void publishRegionBounds(const Viewer& viewer) {
#ifdef __EMSCRIPTEN__
    std::string regions;
    for (const DocumentView::PlacedRegion& placed : viewer.documentView.placedRegions) {
        regions += std::format("{}\"{}\":[{},{},{},{}]", regions.empty() ? "" : ",", placed.id, std::lround(placed.min.x), std::lround(placed.min.y), std::lround(placed.max.x - placed.min.x), std::lround(placed.max.y - placed.min.y));
    }
    std::string buttons;
    for (const DocumentView::VideoButton& button : viewer.documentView.videoButtons) {
        buttons += std::format("{}[{},{},{},{},{},{}]", buttons.empty() ? "" : ",", static_cast<int>(button.action), std::lround(button.area.x), std::lround(button.area.y), std::lround(button.area.width), std::lround(button.area.height), button.active ? 1 : 0);
    }
    const Rectangle box        = viewer.documentView.infoBoxDrawn;
    const Rectangle face       = viewer.documentView.infoFaceDrawn;
    const Rectangle frameDrawn = viewer.documentView.cameraFrameDrawn;
    std::string     notesButtons;
    for (const Rectangle& button : viewer.notes.buttons) {
        if (button.width > 0.0f) {
            notesButtons += std::format("{}[{},{},{},{}]", notesButtons.empty() ? "" : ",", std::lround(button.x), std::lround(button.y), std::lround(button.width), std::lround(button.height));
        }
    }
    std::string graphs; // what a test checks a running graph or a presenter window against: each graph and its scheduler's state
    for (const auto& [workflow, graph] : viewer.graphs) {
        graphs += std::format("{}\"{}\": \"{}\"", graphs.empty() ? "" : ", ", workflow, graph->stateName());
    }
    std::string miniatures; // the phone remote's slide now shown and next, for a test to find
    for (const Rectangle& miniature : viewer.notes.miniatures) {
        if (miniature.width > 0.0f) {
            miniatures += std::format("{}[{},{},{},{}]", miniatures.empty() ? "" : ",", std::lround(miniature.x), std::lround(miniature.y), std::lround(miniature.width), std::lround(miniature.height));
        }
    }
    std::string toolbars;
    for (const auto& [region, toolbar] : viewer.toolbarBoxes) {
        toolbars += std::format("{}\"{}\": [{},{},{},{}]", toolbars.empty() ? "" : ", ", region, std::lround(toolbar.x), std::lround(toolbar.y), std::lround(toolbar.width), std::lround(toolbar.height));
    }
    std::string liveSources;
    for (const auto& [region, source] : viewer.liveSources) {
        liveSources += std::format("{}\"{}\": \"{}\"", liveSources.empty() ? "" : ", ", region, source);
    }
    const NotesOverlay& notes     = viewer.notes;
    const std::string   notesArea = std::format("{},{},{},{},{:.1f},{:.0f},{:.0f}", std::lround(notes.notesArea.x), std::lround(notes.notesArea.y), std::lround(notes.notesArea.width), std::lround(notes.notesArea.height), notes.notesPixels, notes.notesScroll, notes.notesScrollMax);
    const std::string   infoBox   = box.width <= 0.0f ? std::string{} : std::format("{},{},{},{},{},{},{},{}", std::lround(box.x), std::lround(box.y), std::lround(box.width), std::lround(box.height), std::lround(face.x), std::lround(face.y), std::lround(face.width), std::lround(face.height));
    // to a quarter of a second, so that a playing clip does not republish every frame
    const double                    clipPosition = viewer.documentView.videoButtons.empty() ? 0.0 : std::floor(viewer.videos.positionOf(viewer.documentView.videoButtons.front().source) * 4.0) / 4.0;
    const DigitizerUi::LookAndFeel& lookAndFeel  = DigitizerUi::LookAndFeel::instance();
    const float                     normal       = lookAndFeel.fontNormal[0] != nullptr ? lookAndFeel.fontNormal[0]->LegacySize : 0.0f;
    const float                     small        = lookAndFeel.fontSmall[0] != nullptr && normal > 0.0f ? lookAndFeel.fontSmall[0]->LegacySize / normal : 0.0f;
    const float                     tiny         = lookAndFeel.fontTiny[0] != nullptr && normal > 0.0f ? lookAndFeel.fontTiny[0]->LegacySize / normal : 0.0f;
    std::string                     menuEntries;
    for (const auto& [index, entry] : viewer.menu.drawnEntries) {
        menuEntries += std::format("{}[{},{},{},{},{}]", menuEntries.empty() ? "" : ",", index, std::lround(entry.x), std::lround(entry.y), std::lround(entry.width), std::lround(entry.height));
    }
    // a test aims at what the slide drew, in the units the pointer arrives in: the words a reader can select, and
    // the areas a click follows
    const auto quoted = [](std::string_view text) {
        std::string json = "\"";
        for (const char letter : text) {
            if (letter == '"' || letter == '\\') {
                json += '\\';
            }
            json += static_cast<unsigned char>(letter) < 0x20U ? ' ' : letter;
        }
        return json + "\"";
    };
    // only once the slide has settled: while it moves every word's rectangle changes, and rebuilding the whole slide's
    // text for the page every frame of a transition is work the audience would pay for and no test reads
    static std::string textRuns;
    static std::string links;
    if (!viewer.transition.running()) {
        textRuns.clear();
        for (const TextRun& run : viewer.documentView.textRuns.runs) {
            textRuns += std::format("{}[{},{},{},{},{}]", textRuns.empty() ? "" : ",", std::lround(run.area.x), std::lround(run.area.y), std::lround(run.area.width), std::lround(run.area.height), quoted(run.text));
        }
        links.clear();
        for (const DocumentView::LinkArea& link : viewer.documentView.linkAreas) {
            links += std::format("{}[{},{},{},{},{}]", links.empty() ? "" : ",", std::lround(link.area.x), std::lround(link.area.y), std::lround(link.area.width), std::lround(link.area.height), quoted(link.target));
        }
    }
    const std::string  script = std::format("window.gr4TextRuns = [{}]; window.gr4Links = [{}]; window.gr4Regions = {{{}}}; window.gr4ChartType = [{:.2f}, {:.4f}, {:.4f}]; window.gr4Zoom = [{:.3f}, {:.1f}, {:.1f}]; window.gr4VideoButtons = [{}]; window.gr4VideoPosition = {:.2f}; window.gr4InfoBox = [{}]; window.gr4CameraFrame = [{:.3f},{:.3f},{:.3f},{:.3f}]; window.gr4CameraRotation = {:.1f}; window.gr4PhoneCode = {}; window.gr4NotesButtons = [{}]; window.gr4MenuEntries = [{}]; window.gr4NotesArea = [{}]; window.gr4LiveSource = {{{}}}; window.gr4Toolbars = {{{}}}; window.gr4Graphs = {{{}}}; window.gr4Miniatures = [{}];", textRuns, links, regions, chartBodyPixels(), small, tiny, viewer.zoom.scale, viewer.zoom.offsetX, viewer.zoom.offsetY, buttons, clipPosition, infoBox, frameDrawn.x, frameDrawn.y, frameDrawn.width, frameDrawn.height, viewer.documentView.cameraRotation, viewer.phoneCodeShown ? "true" : "false", notesButtons, menuEntries, notesArea, liveSources, toolbars, graphs, miniatures);
    static std::string published;
    if (script != published) {
        published = script;
        emscripten_run_script(script.c_str());
    }
#else
    (void)viewer;
#endif
}

/// the phone presenter view's address: from the server when this deck was opened on its machine, else built from a
/// `?relay=` link; empty when there is no relay
[[nodiscard]] std::string phoneLink() {
#ifdef __EMSCRIPTEN__
    if (const char* told = emscripten_run_script_string("globalThis.gr4PhoneLink || ''"); told != nullptr && *told != '\0') {
        return told;
    }
    const char* token = emscripten_run_script_string("globalThis.gr4Relay || ''");
    return token != nullptr && *token != '\0' ? emscriptenLocation("origin") + emscriptenLocation("pathname") + "?presenter&relay=" + token : std::string{};
#else
    return {};
#endif
}

} // namespace gr::present
