#include "Viewer.hpp"

#include <gr4-present/DeckFiles.hpp>
#include <gr4-present/Manifest.hpp>

#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/algorithm/fileio/FileIo.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <SDL3/SDL_dialog.h>
#endif

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <utility>

namespace gr::present {

namespace {

#ifdef __EMSCRIPTEN__
// a browser keeps the files of a deck it loaded in its in-memory file system; one deck at a time
constexpr std::string_view kLocalRoot  = "/local";
constexpr std::string_view kIncoming   = "/incoming";      // a deck picked, checked here before it replaces the one in /local
constexpr std::string_view kPickedDeck = "/incoming/deck"; // a file's bytes arrive without its name

std::optional<gr::algorithm::fileio::Writer> savedArchive; // the browser's download of <deck>.tar.gz, finishing after the frame
#endif
constexpr int kDeepestDeckFile = 2; // how far below a chosen folder its deck's index.yml or .md is looked for

/// whether a deck on this computer would load, checked before it replaces the one shown: a lone `.md` that is there, or
/// a folder whose index.yml this viewer reads and whose entry document is there; the reason when not
[[nodiscard]] std::expected<void, std::string> checkLocalDeck(const std::filesystem::path& base) {
    if (base.extension() == ".md") {
        return std::filesystem::is_regular_file(base) ? std::expected<void, std::string>{} : std::unexpected(std::format("{} is not there", base.string()));
    }
    const std::filesystem::path manifestPath = base / PresentationLoader::kManifestName;
    std::error_code             sizeError;
    const auto                  manifestSize = std::filesystem::file_size(manifestPath, sizeError);
    std::ifstream               manifestFile{manifestPath, std::ios::binary};
    if (sizeError || !manifestFile) {
        return std::unexpected(std::format("{} has no {}", base.string(), PresentationLoader::kManifestName));
    }
    // read by its size: GCC 15 at -O2 reports a false null dereference inside istreambuf_iterator (test/FileContents.hpp)
    std::string manifest(static_cast<std::size_t>(manifestSize), '\0');
    manifestFile.read(manifest.data(), static_cast<std::streamsize>(manifest.size()));
    const auto entry = entryOfManifest(manifest);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    if (!std::filesystem::is_regular_file(base / *entry)) {
        return std::unexpected(std::format("its entry document {} is not in {}", *entry, base.string()));
    }
    return {};
}

/// the files written under `directory`, parents created; the reason at the first that fails. Written directly: FileIO's
/// blocking write may not run on the browser's main thread
[[nodiscard]] std::expected<void, std::string> writeFiles(const std::filesystem::path& directory, std::span<const PackageFile> files) {
    for (const PackageFile& file : files) {
        const std::filesystem::path target = directory / file.path;
        std::error_code             error;
        std::filesystem::create_directories(target.parent_path(), error);
        std::ofstream out{target, std::ios::binary};
        out.write(reinterpret_cast<const char*>(file.bytes.data()), static_cast<std::streamsize>(file.bytes.size()));
        if (error || !out) {
            return std::unexpected(std::format("{}: cannot be written", target.string()));
        }
    }
    return {};
}

[[nodiscard]] std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path) {
    std::error_code           sizeError;
    const auto                size = std::filesystem::file_size(path, sizeError);
    std::vector<std::uint8_t> bytes(sizeError ? 0UZ : static_cast<std::size_t>(size));
    std::ifstream{path, std::ios::binary}.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

/// `base` (`deckBaseAmong`) inside `folder`, the folder itself for `.`
[[nodiscard]] std::filesystem::path within(const std::filesystem::path& folder, const std::string& base) { return base == "." ? folder : (folder / base).lexically_normal(); }

/// the deck in a folder, found as in an archive of it (`deckBaseAmong`), no deeper than `kDeepestDeckFile`: natively a
/// dropped folder may be one's home
[[nodiscard]] std::expected<std::filesystem::path, std::string> deckInFolder(const std::filesystem::path& folder) {
    std::vector<std::string> paths;
    std::error_code          error;
    using Walk = std::filesystem::recursive_directory_iterator;
    for (auto entry = Walk{folder, std::filesystem::directory_options::skip_permission_denied, error}; !error && entry != Walk{}; entry.increment(error)) {
        if (entry.depth() >= kDeepestDeckFile) {
            entry.disable_recursion_pending();
        }
        if (entry->is_regular_file()) {
            paths.push_back(entry->path().lexically_relative(folder).generic_string());
        }
    }
    return deckBaseAmong(paths).transform([&folder](const std::string& base) { return within(folder, base); });
}

/// an unpacked archive's files written under `folder`; what of them to load
[[nodiscard]] std::expected<std::filesystem::path, std::string> writeUnpackedDeck(const std::filesystem::path& folder, const UnpackedDeck& deck) {
    const auto asPackageFile = [](const gr::compression::TarEntry& entry) {
        const std::span<const std::uint8_t> bytes{reinterpret_cast<const std::uint8_t*>(entry.bytes.data()), entry.bytes.size()};
        return PackageFile{.path = entry.path, .bytes = bytes};
    };
    const auto files = deck.files | std::views::transform(asPackageFile) | std::ranges::to<std::vector<PackageFile>>();
    return writeFiles(folder, files).transform([&] { return within(folder, deck.base); });
}

#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(void, wipeTree, (const char* path), {
    if (!globalThis.gr4WipeTree) {
        globalThis.gr4WipeTree = (at) => {
            if (!FS.analyzePath(at).exists) return;
            for (const name of FS.readdir(at)) {
                if (name === "." || name === "..") continue;
                const child = at + "/" + name;
                if (FS.isDir(FS.stat(child).mode)) globalThis.gr4WipeTree(child); else FS.unlink(child);
            }
            FS.rmdir(at);
        };
    }
    const at = UTF8ToString(path);
    if (at.startsWith("/local") || at.startsWith("/incoming")) globalThis.gr4WipeTree(at); // never more than this file's own trees
});
// Chromium keeps the handle of the folder or file last loaded from this computer, in IndexedDB, not its contents: F5,
// the presenter's window and a path in the address reopen it as it is now, and a ticked "follow edits" compares its
// files' times every 2 s. Every page-side answer waits in `gr4StagedDeck` as "<kind>|<staged path, or a reason>"
EM_JS(int, keepsFileHandles, (), { return typeof globalThis.showDirectoryPicker === "function" ? 1 : 0; });
EM_JS(void, tendKeptFiles, (int following), {
    if (!globalThis.gr4Kept) {
        const kept = globalThis.gr4Kept = { handle: null, signature: "", busy: false };
        const files = async (handle, at, found) => {
            if (handle.kind === "file") { found.push({ path: at, file: await handle.getFile() }); return found; }
            for await (const [name, child] of handle.entries()) await files(child, at + "/" + name, found);
            return found;
        };
        kept.signatureOf = async (handle) => {
            const stamps = (await files(handle, "", [])).map((entry) => entry.path + ":" + entry.file.lastModified + ":" + entry.file.size);
            return stamps.sort().join("|");
        };
        kept.stage = async (handle) => {
            globalThis.gr4WipeTree("/incoming");
            const root = "/incoming/" + handle.name;
            for (const entry of await files(handle, root, [])) {
                FS.mkdirTree(entry.path.substring(0, entry.path.lastIndexOf("/")));
                FS.writeFile(entry.path, new Uint8Array(await entry.file.arrayBuffer()));
            }
            return root;
        };
        kept.store = () => new Promise((resolve, reject) => {
            const opened = indexedDB.open("gr4-present", 1);
            opened.onupgradeneeded = () => opened.result.createObjectStore("decks");
            opened.onsuccess = () => {
                try {
                    resolve(opened.result.transaction("decks", "readwrite").objectStore("decks"));
                } catch (error) {
                    reject(error);
                }
            };
            opened.onerror = () => reject(opened.error);
        });
        kept.open = async (handle, opening, address) => {
            kept.staged = handle;
            kept.stagedAddress = address;
            kept.stagedSignature = await kept.signatureOf(handle);
            globalThis.gr4StagedDeck = opening + "|" + await kept.stage(handle);
        };
        kept.keep = (handle) => kept.open(handle, "load", "local:" + handle.name);
        // the staged deck loads: its handle is the one followed and stored for F5, the address names it as it was given
        kept.remember = async () => {
            kept.handle = kept.staged;
            kept.address = kept.stagedAddress;
            kept.signature = kept.stagedSignature;
            (await kept.store()).put(kept.handle, "local");
            const query = new URLSearchParams(location.search);
            query.set("presentation", kept.address);
            history.replaceState(null, "", location.pathname + "?" + query.toString() + location.hash);
        };
        kept.forget = () => {
            kept.handle = null;
            const query = new URLSearchParams(location.search);
            if ((query.get("presentation") || "").startsWith("local:")) {
                query.delete("presentation");
                history.replaceState(null, "", location.pathname + (query.toString() ? "?" + query.toString() : "") + location.hash);
            }
        };
        // a drop's items can be read only while it is dispatched; before SDL's listener on the canvas, which would copy a
        // file but not keep its handle (and does so where the browser keeps no handles)
        document.addEventListener("drop", (event) => {
            const item = Array.from(event.dataTransfer ? event.dataTransfer.items : []).find((entry) => entry.kind === "file");
            if (!item || typeof item.getAsFileSystemHandle !== "function" || typeof globalThis.showDirectoryPicker !== "function") return;
            event.preventDefault();
            event.stopPropagation();
            item.getAsFileSystemHandle().then((handle) => handle && kept.keep(handle)).catch((error) => { globalThis.gr4StagedDeck = "error|" + error; });
        }, true);
        setInterval(async () => {
            if (!kept.following || !kept.handle || kept.busy) return;
            kept.busy = true;
            try {
                const signature = await kept.signatureOf(kept.handle);
                const changed = signature !== kept.signature && signature !== kept.stagedSignature; // a refused state once
                if (changed) await kept.open(kept.handle, "reload", kept.address);
            } catch (error) {
                globalThis.gr4StagedDeck = "error|" + error;
            } finally {
                kept.busy = false;
            }
        }, 2000);
    }
    globalThis.gr4Kept.following = following !== 0;
});
EM_JS(void, pickKeptFolder, (), {
    globalThis.showDirectoryPicker({ id: "gr4-present-decks", mode: "read" })
        .then((handle) => globalThis.gr4Kept.keep(handle))
        .catch((error) => { if (error.name !== "AbortError") globalThis.gr4StagedDeck = "error|" + error; });
});
EM_JS(void, forgetKeptFiles, (), { if (globalThis.gr4Kept) globalThis.gr4Kept.forget(); });
EM_JS(void, rememberKeptFiles, (), { globalThis.gr4Kept.remember().catch((error) => { globalThis.gr4StagedDeck = "error|" + error; }); });
// the deck an address names on this computer, `local:<name>` or a path, by its last name: the kept handle if it has that
// name, else one chosen in the picker, which cannot open at a path, so the choice is checked by its name; what the browser
// asks is granted by a click on "Reopen <address>" or "Open <address>"
EM_JS(void, reopenKeptFiles, (const char* deckAddress), {
    const address = UTF8ToString(deckAddress); // read now: the C++ string is gone by the time a callback runs
    const fail = (reason) => { globalThis.gr4StagedDeck = "failed|" + reason; };
    const label = address.startsWith("local:") ? address.slice(6) : address;
    const name = (label.endsWith("/") ? label.slice(0, -1) : label).split("/").at(-1);
    if (typeof globalThis.showDirectoryPicker !== "function" || !globalThis.indexedDB) return fail("this browser cannot reopen " + label + ": use Load");
    (async () => {
        const store = await globalThis.gr4Kept.store();
        const kept = await new Promise((resolve, reject) => {
            const request = store.get("local");
            request.onsuccess = () => resolve(request.result);
            request.onerror = () => reject(request.error);
        });
        const handle = kept && kept.name === name ? kept : null;
        const reopen = (chosen) => { globalThis.gr4Kept.open(chosen, "launch", address).catch((error) => fail(String(error))); };
        if (handle && (await handle.queryPermission({ mode: "read" })) === "granted") return reopen(handle);
        const regranted = async () => ((await handle.requestPermission({ mode: "read" })) === "granted" ? handle : null);
        const options = { id: "gr4-present-decks", mode: "read" };
        const pickedFile = () => globalThis.showOpenFilePicker(options).then(([file]) => file);
        const picked = () => (name.endsWith(".md") ? pickedFile() : globalThis.showDirectoryPicker(options)).catch(() => null);
        const button = document.createElement("button");
        button.id = "gr4-reopen";
        button.textContent = (handle ? "Reopen " : "Open ") + label;
        button.style.cssText = "position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);z-index:10;font:20px sans-serif;padding:0.6em 1.2em";
        const note = document.createElement("p");
        note.style.cssText = "position:fixed;left:50%;top:calc(50% + 2.5em);transform:translateX(-50%);z-index:10;font:16px sans-serif;color:#c33";
        button.onclick = async () => {
            const chosen = await (handle ? regranted() : picked());
            if (!chosen) return; // refused or closed: the button stays for another try
            if (chosen.name !== name) {
                note.textContent = "this is " + chosen.name + ", the address names " + label + ": choose " + name;
                console.warn(note.textContent);
                return;
            }
            button.remove();
            note.remove();
            reopen(chosen);
        };
        document.body.append(button, note);
    })().catch((error) => fail(String(error)));
});
// where the browser keeps no handles: the folder's files go into the in-memory file system under /incoming/<folder>
EM_JS(void, pickDeckFolder, (), {
    const input = document.createElement("input");
    input.type = "file";
    input.webkitdirectory = true;
    input.style.display = "none";
    document.body.appendChild(input);
    input.addEventListener("cancel", () => input.remove());
    input.addEventListener("change", async () => {
        const files = Array.from(input.files || []);
        input.remove();
        if (files.length === 0) return;
        try {
            globalThis.gr4WipeTree("/incoming");
            for (const file of files) {
                const path = "/incoming/" + file.webkitRelativePath;
                FS.mkdirTree(path.substring(0, path.lastIndexOf("/")));
                FS.writeFile(path, new Uint8Array(await file.arrayBuffer()));
            }
            globalThis.gr4StagedDeck = "picked|/incoming/" + files[0].webkitRelativePath.split("/")[0];
        } catch (error) {
            globalThis.gr4StagedDeck = "error|" + error;
        }
    });
    input.click();
});
// clang-format on

/// a page-side value, read once and cleared, so a frame acts on it once
[[nodiscard]] std::string takePageValue(std::string_view name) {
    const std::string script = std::format("(() => {{ const value = globalThis.{0} || ''; globalThis.{0} = ''; return value; }})()", name);
    const char*       text   = emscripten_run_script_string(script.c_str());
    return text != nullptr ? std::string{text} : std::string{};
}

/// how a deck from this computer reached the browser: picked once (a snapshot), or through handles it keeps
enum class LocalSource { picked, kept };

/// a deck staged under /incoming takes the place of the one in /local and loads, if it would; else it is dropped, and the
/// deck shown stays with its files
void adoptStagedDeck(Viewer& viewer, const std::filesystem::path& stagedRoot, const std::filesystem::path& staged, DeckOpening opening, LocalSource source) {
    const auto stagedBase = std::filesystem::is_directory(staged) ? deckInFolder(staged) : std::expected<std::filesystem::path, std::string>{staged};
    if (const auto checked = stagedBase.and_then(checkLocalDeck); !checked) {
        // a folder chooser picks the subfolder highlighted in it, which inside a deck's folder is its first, often figures/
        const std::string_view highlighted = stagedBase ? "" : " (was one of its subfolders highlighted when the deck's folder was chosen?)";
        const std::string      reason      = std::format("{}{}; the deck shown stays", checked.error(), highlighted);
        viewer.diagnostics.report(DiagnosticKind::missingManifest, staged.lexically_relative(kIncoming).string(), reason);
        wipeTree(std::string{kIncoming}.c_str());
        return;
    }
    wipeTree(std::string{kLocalRoot}.c_str());
    const std::filesystem::path root = std::filesystem::path{kLocalRoot} / stagedRoot.lexically_relative(kIncoming);
    std::error_code             moved;
    std::filesystem::create_directories(root.parent_path(), moved);
    std::filesystem::rename(stagedRoot, root, moved);
    if (moved) {
        viewer.diagnostics.report(DiagnosticKind::archiveError, root.string(), moved.message());
        return;
    }
    const std::filesystem::path relative = stagedBase->lexically_relative(stagedRoot);
    loadDeck(viewer, (relative == "." ? root : root / relative).string(), opening);
    viewer.deckFiles.keptHandles = source == LocalSource::kept;
    if (source == LocalSource::kept) {
        rememberKeptFiles();
    } else {
        forgetKeptFiles();
    }
}

/// a dropped or kept file: a `.tar.gz` (told by gzip's magic bytes) unpacked, else one Markdown file
void adoptPickedFile(Viewer& viewer, std::span<const std::uint8_t> bytes, DeckOpening opening, LocalSource source) {
    const bool gzipped = bytes.size() >= 2UZ && bytes[0] == 0x1fU && bytes[1] == 0x8bU;
    if (!gzipped) {
        // a deck's index.yml read as Markdown would show its YAML as a slide: its folder is what loads it
        if (parseManifest(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()})) {
            constexpr std::string_view kManifest = PresentationLoader::kManifestName;
            const std::string          reason    = std::format("is a deck's {}: Load, or drop, the folder holding it; the deck shown stays", kManifest);
            viewer.diagnostics.report(DiagnosticKind::missingManifest, "the chosen file", reason);
            return;
        }
        wipeTree(std::string{kIncoming}.c_str());
        if (const auto written = writeFiles(kPickedDeck, std::vector<PackageFile>{{.path = "deck.md", .bytes = bytes}}); !written) {
            viewer.diagnostics.report(DiagnosticKind::archiveError, "the chosen file", written.error());
            return;
        }
        adoptStagedDeck(viewer, kPickedDeck, std::filesystem::path{kPickedDeck} / "deck.md", opening, source);
        return;
    }
    const auto deck = unpackDeck(std::as_bytes(std::span{bytes}));
    if (!deck) {
        viewer.diagnostics.report(DiagnosticKind::archiveError, "the chosen archive", std::format("{}; the deck shown stays", deck.error()));
        return;
    }
    wipeTree(std::string{kIncoming}.c_str());
    const auto written = writeUnpackedDeck(kPickedDeck, *deck);
    if (!written) {
        viewer.diagnostics.report(DiagnosticKind::archiveError, "the chosen archive", written.error());
        return;
    }
    adoptStagedDeck(viewer, kPickedDeck, *written, opening, source);
}

/// the page's answer: a deck staged from the kept handle (loaded, reloaded after an edit, or reopened after F5) or from a
/// folder picked where no handles are kept, or why there is none; a reopen that failed shows the built-in deck
void takeStagedDeck(Viewer& viewer) {
    const std::string answer = takePageValue("gr4StagedDeck");
    if (answer.empty()) {
        return;
    }
    const auto        bar    = answer.find('|');
    const std::string kind   = answer.substr(0UZ, bar);
    const std::string staged = answer.substr(bar + 1UZ);
    if (kind == "error" || kind == "failed") {
        if (kind == "failed") {
            const std::string builtIn = viewer.deckFiles.base;
            loadDeck(viewer, builtIn, DeckOpening::launch);
        }
        viewer.loader.diagnostics.report(DiagnosticKind::missingManifest, "the deck from this computer", staged);
        viewer.diagnostics.report(DiagnosticKind::missingManifest, "the deck from this computer", staged);
        return;
    }
    if (kind == "picked") {
        adoptStagedDeck(viewer, staged, staged, DeckOpening::load, LocalSource::picked);
        return;
    }
    const DeckOpening opening = kind == "reload" ? DeckOpening::reload : kind == "launch" ? DeckOpening::launch : DeckOpening::load;
    if (std::filesystem::is_directory(staged)) {
        adoptStagedDeck(viewer, staged, staged, opening, LocalSource::kept);
        return;
    }
    adoptPickedFile(viewer, fileBytes(staged), opening, LocalSource::kept);
}

void tendSaving(Viewer& viewer) {
    if (savedArchive.has_value() && savedArchive->finished()) {
        if (const auto result = savedArchive->result(); !result) {
            viewer.diagnostics.report(DiagnosticKind::saveFailed, "the deck's archive", result.error().message);
        }
        savedArchive.reset();
    }
}
#else
constexpr auto kWatchInterval = std::chrono::seconds{2};

[[nodiscard]] bool isLocalBase(std::string_view base) { return !base.contains("://"); }

/// natively, a deck from this computer follows its files: a change, a removal or a new file reloads it where it was
void followDeckFiles(Viewer& viewer) {
    const bool shown = viewer.loader.state() == LoadState::ready && !viewer.sections.empty();
    const bool due   = viewer.frameStart - viewer.deckFiles.checkedAt >= kWatchInterval;
    if (!viewer.deckFiles.following || !followableDeck(viewer) || !shown || !due) {
        return;
    }
    viewer.deckFiles.checkedAt = viewer.frameStart;
    const auto stateOf         = [&viewer] {
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> files;
        for (const PackageFile& file : viewer.loader.packageFiles()) {
            const std::filesystem::path path = std::filesystem::path{viewer.loader.baseUri()} / file.path;
            std::error_code             error;
            const auto                  modified = std::filesystem::last_write_time(path, error);
            files.emplace_back(path, error ? std::filesystem::file_time_type{} : modified); // a lone .md has no index.yml on disk
        }
        return files;
    };
    auto now = stateOf();
    if (viewer.deckFiles.watched.empty()) {
        viewer.deckFiles.watched = std::move(now);
        return;
    }
    if (now != viewer.deckFiles.watched) {
        const std::string base = viewer.deckFiles.base;
        loadDeck(viewer, base, DeckOpening::reload);
    }
}

/// a path chosen natively, in Load's dialog or by a drop: a deck's folder or its index.yml, a lone .md, or a .tar.gz
/// unpacked into the temporary folder; checked before it replaces the deck shown
void loadChosenPath(Viewer& viewer, const std::filesystem::path& chosen) {
    const bool archive = chosen.extension() == ".gz" || chosen.extension() == ".tgz";
    const auto base    = [&]() -> std::expected<std::filesystem::path, std::string> {
        if (std::filesystem::is_directory(chosen)) {
            return deckInFolder(chosen);
        }
        if (chosen.filename() == PresentationLoader::kManifestName) {
            return chosen.parent_path();
        }
        if (chosen.extension() == ".md") {
            return chosen;
        }
        if (!archive) {
            return std::unexpected(std::format("is neither a deck's folder, its {}, a Markdown file nor a .tar.gz", PresentationLoader::kManifestName));
        }
        const auto bytes = fileBytes(chosen);
        const auto deck  = unpackDeck(std::as_bytes(std::span{bytes}));
        if (!deck) {
            return std::unexpected(deck.error());
        }
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / "gr4-present-unpacked" / chosen.stem().stem();
        std::error_code             removed;
        std::filesystem::remove_all(folder, removed);
        return writeUnpackedDeck(folder, *deck);
    }();
    if (const auto checked = base.and_then(checkLocalDeck); !checked) {
        const DiagnosticKind kind = archive ? DiagnosticKind::archiveError : DiagnosticKind::missingManifest;
        viewer.diagnostics.report(kind, chosen.string(), std::format("{}; the deck shown stays", checked.error()));
        return;
    }
    loadDeck(viewer, base->string(), DeckOpening::load);
}

void takeDialogAnswers(Viewer& viewer) {
    std::optional<std::string> loadFrom;
    {
        const std::scoped_lock lock{viewer.deckFiles.answerLock};
        loadFrom = std::exchange(viewer.deckFiles.loadFrom, std::nullopt);
    }
    if (loadFrom.has_value()) {
        loadChosenPath(viewer, *loadFrom);
    }
}

/// where a native dialog opens: the folder of a deck from this computer, else the system's choice
[[nodiscard]] std::string dialogLocationOf(const Viewer& viewer) { return isLocalBase(viewer.deckFiles.base) ? std::string{viewer.loader.baseUri()} : std::string{}; }
#endif

} // namespace

namespace {
void beginLoading(Viewer& viewer, std::string_view base, DeckOpening opening) {
    if (opening == DeckOpening::reload) {
        viewer.deckFiles.viewsBefore  = viewer.navigator.graph.views | std::views::transform(&View::id) | std::ranges::to<std::vector<std::string>>();
        viewer.deckFiles.cursorBefore = viewer.navigator.cursor;
    }
    viewer.deckFiles.base    = std::string{base};
    viewer.deckFiles.opening = opening;
    viewer.deckFiles.watched.clear();

    viewer.sections.clear();
    viewer.figures = FigureCache{};
    viewer.videos.release();
    viewer.live.graphs.clear();
    viewer.live.retryAfter.clear();
    viewer.live.tendedView.clear();
    viewer.live.sources.clear();
    viewer.live.toolbarBoxes.clear();
    viewer.effectRuns.instances.clear();
    viewer.effectRuns.stages.clear();
    viewer.transition      = Transition{};
    viewer.zoom            = ZoomPan{};
    viewer.input.selection = TextSelection{};
    viewer.reportedClips.clear();
    viewer.loader.diagnostics = Diagnostics{};

    viewer.launch = LoadingScreen{};
    viewer.launch.setProgress(LoadStage::application, 1.0f);
    viewer.launchComplete = false;
    viewer.loader.begin(base, viewer.frameStart);
}
} // namespace

void loadDeck(Viewer& viewer, std::string_view base, DeckOpening opening) {
    const bool deckOnScreen = viewer.launchComplete && !viewer.exporting && (!viewer.sections.empty() || viewer.loader.state() == LoadState::failed);
    if (deckOnScreen) {
        viewer.deckFiles.replacing = {std::string{base}, opening};
        return;
    }
    beginLoading(viewer, base, opening);
}

void replaceShownDeck(Viewer& viewer, Texture lastFrame) {
    if (const auto replacing = std::exchange(viewer.deckFiles.replacing, std::nullopt); replacing.has_value()) {
        viewer.frameBefore = std::move(lastFrame);
        beginLoading(viewer, replacing->first, replacing->second);
    }
}

void chooseDeck([[maybe_unused]] Viewer& viewer) {
#ifdef __EMSCRIPTEN__
    if (keepsFileHandles() != 0) {
        pickKeptFolder();
    } else {
        pickDeckFolder();
    }
#else
    static constexpr SDL_DialogFileFilter kDeckFiles[]{{.name = "a deck: its index.yml, a Markdown file or a .tar.gz", .pattern = "yml;md;gz;tgz"}};
    const std::string                     location = dialogLocationOf(viewer);
    SDL_ShowOpenFileDialog(
        [](void* answer, const char* const* files, int) {
            auto& deckFiles = *static_cast<decltype(Viewer::deckFiles)*>(answer);
            if (files != nullptr && files[0] != nullptr) {
                const std::scoped_lock lock{deckFiles.answerLock};
                deckFiles.loadFrom = std::string{files[0]};
            }
        },
        &viewer.deckFiles, viewer.window, kDeckFiles, 1, location.empty() ? nullptr : location.c_str(), false);
#endif
}

void loadDroppedPath(Viewer& viewer, std::string_view path) {
#ifdef __EMSCRIPTEN__
    const std::filesystem::path dropped{path};
    const auto                  bytes = fileBytes(dropped);
    std::error_code             removed;
    std::filesystem::remove_all(dropped.parent_path(), removed); // SDL's /tmp/filedrop/<n>/
    adoptPickedFile(viewer, bytes, DeckOpening::load, LocalSource::picked);
#else
    loadChosenPath(viewer, path);
#endif
}

void saveDeck(Viewer& viewer) {
    if (viewer.loader.state() != LoadState::ready) {
        viewer.diagnostics.report(DiagnosticKind::saveFailed, "the deck", "is not loaded, so there is nothing to save");
        return;
    }
    const std::string name   = deckFolderName(viewer.loader.manifest().title, viewer.loader.baseUri());
    const auto        packed = packDeck(viewer.loader.packageFiles(), name);
    if (!packed) {
        viewer.diagnostics.report(DiagnosticKind::saveFailed, name, packed.error());
        return;
    }
    const auto bytes = std::span{reinterpret_cast<const std::uint8_t*>(packed->data()), packed->size()};
#ifdef __EMSCRIPTEN__
    if (auto delivery = gr::algorithm::fileio::writeAsync(std::format("download:/{}.tar.gz", name), bytes); delivery.has_value()) {
        savedArchive = std::move(*delivery);
    } else {
        viewer.diagnostics.report(DiagnosticKind::saveFailed, name, delivery.error().message);
    }
#else
    // where a browser would have downloaded it: the user's downloads folder, else home; a name taken gets (1), (2), …
    const char*                 downloads  = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
    const char*                 home       = SDL_GetUserFolder(SDL_FOLDER_HOME);
    const char*                 userFolder = downloads != nullptr ? downloads : home;
    const std::filesystem::path folder{userFolder != nullptr ? userFolder : "."};
    std::filesystem::path       target = folder / std::format("{}.tar.gz", name);
    for (int copy = 1; std::filesystem::exists(target); ++copy) {
        target = folder / std::format("{} ({}).tar.gz", name, copy);
    }
    if (const auto written = gr::algorithm::fileio::write("file:" + target.string(), bytes); !written) {
        viewer.diagnostics.report(DiagnosticKind::saveFailed, target.string(), written.error().message);
    } else {
        gr::log::info("saved the deck to {}", target.string());
    }
#endif
}

#ifdef __EMSCRIPTEN__
void reopenLocalDeck(Viewer& viewer, std::string_view address, std::string_view builtIn) {
    viewer.deckFiles.base = std::string{builtIn}; // what shows if the handles cannot be read
    wipeTree("");
    tendKeptFiles(0);
    reopenKeptFiles(std::string{address}.c_str());
}
#endif

bool followableDeck(const Viewer& viewer) {
#ifdef __EMSCRIPTEN__
    return viewer.deckFiles.keptHandles;
#else
    return isLocalBase(viewer.deckFiles.base);
#endif
}

void tendDeckFiles(Viewer& viewer) {
    // the menu counts the problems; it is rebuilt here, outside its drawing: a menu entry's own click may report one
    if (viewer.diagnostics.entries.size() != viewer.deckFiles.problemsInMenu) {
        viewer.deckFiles.problemsInMenu = viewer.diagnostics.entries.size();
        buildSideMenu(viewer);
    }
#ifdef __EMSCRIPTEN__
    wipeTree(""); // only installs the page's helper, before anything below may need it
#endif
    if (viewer.exporting) {
        return;
    }
#ifdef __EMSCRIPTEN__
    // the presenter's window mirrors the audience's deck, so it follows a deck the audience's window follows
    tendKeptFiles((viewer.deckFiles.following || viewer.presenter) && viewer.deckFiles.keptHandles ? 1 : 0);
    takeStagedDeck(viewer);
    tendSaving(viewer);
#else
    takeDialogAnswers(viewer);
    followDeckFiles(viewer);
#endif
    if (viewer.presenter) {
        return; // the presenter's window follows the audience's deck
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
        chooseDeck(viewer);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        saveDeck(viewer);
    }
}

} // namespace gr::present
