#include <gr4-present/EffectSource.hpp>
#include <gr4-present/Markdown.hpp>
#include <gr4-present/PackagePath.hpp>
#include <gr4-present/Plot.hpp>
#include <gr4-present/PresentationLoader.hpp>

#include <gnuradio-4.0/algorithm/fileio/FileIo.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>

namespace gr::present {

namespace {

// fileio dispatches on the scheme, and a bare path has none; anything without one is a local directory
[[nodiscard]] std::string withScheme(std::string_view uri) { return uri.contains("://") || uri.starts_with("file:") || uri.starts_with("dialog:") ? std::string{uri} : std::format("file:{}", uri); }

[[nodiscard]] std::string_view withoutTrailingSlash(std::string_view uri) { return uri.ends_with('/') ? uri.substr(0UZ, uri.size() - 1UZ) : uri; }

} // namespace

struct PresentationLoader::Request {
    gr::algorithm::fileio::Reader reader;
    std::string                   reference;
    std::vector<std::uint8_t>     bytes;
    std::optional<std::string>    error;
    bool                          finished = false;

    void collect() {
        reader.poll([this](const gr::algorithm::fileio::Reader::PollResult& result) {
            if (!result.data) {
                error = result.data.error().message;
            } else {
                bytes.insert(bytes.end(), result.data->begin(), result.data->end());
            }
            if (result.isFinal) {
                finished = true;
            }
        });
    }
};

namespace {
// six, as a browser opens to one host anyway; more would queue there and make the progress bar lie
constexpr std::size_t kConcurrentFetches = 6UZ;
} // namespace

PresentationLoader::PresentationLoader()  = default;
PresentationLoader::~PresentationLoader() = default;

std::string PresentationLoader::resolve(std::string_view reference) const { return std::format("{}/{}", withoutTrailingSlash(_baseUri), reference); }

void PresentationLoader::fail(std::string_view reason, std::chrono::steady_clock::time_point now) {
    _state      = LoadState::failed;
    _diagnostic = std::string{reason};
    _request.reset();
    _inFlight.clear();

    if (!retryPolicy.enabled) {
        return;
    }
    // exponential backoff: a server that is not up yet is the common case
    auto delay = retryPolicy.initialDelay;
    for (std::size_t doubling = 1UZ; doubling < _attempts && delay < retryPolicy.maximumDelay; ++doubling) {
        delay *= 2;
    }
    _nextAttempt = now + std::min(delay, retryPolicy.maximumDelay);
}

std::chrono::milliseconds PresentationLoader::untilRetry(std::chrono::steady_clock::time_point now) const noexcept {
    if (_state != LoadState::failed || !retryPolicy.enabled) {
        return std::chrono::milliseconds::zero();
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(_nextAttempt - now);
    return std::max(remaining, std::chrono::milliseconds::zero());
}

std::unique_ptr<PresentationLoader::Request> PresentationLoader::open(std::string_view uri, std::string_view reference) {
    auto reader = gr::algorithm::fileio::readAsync(withScheme(uri));
    if (!reader) {
        return nullptr;
    }
    return std::make_unique<Request>(std::move(*reader), std::string{reference});
}

void PresentationLoader::startRequest(std::string_view uri, std::chrono::steady_clock::time_point now) {
    _request = open(uri, uri);
    if (!_request) {
        fail(std::format("cannot read {}", uri), now);
    }
}

void PresentationLoader::begin(std::string_view baseUri, std::chrono::steady_clock::time_point now) {
    _baseUri  = std::string{baseUri};
    _state    = LoadState::loading;
    _step     = Step::manifest;
    _progress = 0.0f;
    _diagnostic.clear();
    _manifest = {};
    _documentSource.clear();
    _pendingFigures.clear();
    _optionalFigures.clear();
    _inFlight.clear();
    _figureCount = 0UZ;
    _figures.clear();
    ++_attempts;
    startRequest(resolve(kManifestName), now);
}

void PresentationLoader::advance(std::chrono::steady_clock::time_point now) {
    if (_state == LoadState::failed && retryPolicy.enabled && now >= _nextAttempt) {
        const std::size_t attempts = _attempts;
        begin(_baseUri, now);
        _attempts = attempts + 1UZ;
        return;
    }
    if (_state != LoadState::loading) {
        return;
    }
    if (_step == Step::figures) {
        pollFigures();
        return;
    }
    if (!_request) {
        return;
    }

    _request->collect();

    if (_request->error) {
        if (_step == Step::document) {
            diagnostics.report(DiagnosticKind::missingResource, _manifest.entry, *_request->error, true);
            _state    = LoadState::ready;
            _progress = 1.0f;
            _step     = Step::done;
            _request.reset();
            return;
        }
        const bool refused = _request->error->contains("CORS") || _request->error->contains("cross-origin");
        diagnostics.report(refused ? DiagnosticKind::crossOriginFailure : DiagnosticKind::missingManifest, _baseUri, *_request->error, true);
        fail(std::format("cannot read presentation: {}", *_request->error), now);
        return;
    }
    if (!_request->finished) {
        return;
    }

    if (_step == Step::manifest) {
        const std::string_view text{reinterpret_cast<const char*>(_request->bytes.data()), _request->bytes.size()};
        const auto             parsed = parseManifest(text);
        if (!parsed) {
            const bool tooOld = parsed.error().kind == ManifestError::Kind::viewerTooOld;
            diagnostics.report(tooOld ? DiagnosticKind::versionMismatch : DiagnosticKind::missingManifest, _baseUri, parsed.error().message(), true);
            fail(parsed.error().message(), now);
            return;
        }
        _manifest = *parsed;
        _progress = 0.5f;

        const auto entry = resolvePackagePath({}, _manifest.entry);
        if (!entry || entry->external) {
            const std::string reason = entry ? "the document is not part of the package" : std::string{message(entry.error())};
            diagnostics.report(DiagnosticKind::missingResource, _manifest.entry, reason, true);
            fail(reason, now);
            return;
        }
        _step = Step::document;
        startRequest(resolve(entry->path), now);
        return;
    }

    if (_step == Step::document) {
        _documentSource.assign(reinterpret_cast<const char*>(_request->bytes.data()), _request->bytes.size());
        FilesToFetch files = filesToFetch(parseMarkdown(_documentSource), _manifest, builtInEffects);
        _pendingFigures    = std::move(files.wanted);
        _optionalFigures   = std::move(files.optional);
        _progress          = 0.75f;
        _step              = Step::figures;
        _figureCount       = _pendingFigures.size();
        _request.reset();
        fillRequests();
        return;
    }
}

FilesToFetch filesToFetch(const Document& document, const Manifest& manifest, std::span<const std::string> builtInEffects) {
    FilesToFetch files;
    const auto   wanted = [&files](std::string_view reference) {
        if (!reference.empty() && !reference.starts_with("http") && !std::ranges::contains(files.wanted, reference)) {
            files.wanted.emplace_back(reference);
        }
    };
    // a bare name may also be a bundled effect, so its deck file is looked for without complaint
    const auto wantedEffect = [&](std::string_view value) {
        const std::string_view effectName = effectNamed(value);
        if (effectName.empty()) {
            return;
        }
        if (!effectName.contains('/') && std::ranges::contains(builtInEffects, effectName)) {
            return; // the viewer's own; a deck that has its own of that name names it by path
        }
        const std::string path = effectPath(effectName);
        if (!effectName.contains('/') && !std::ranges::contains(files.optional, path)) {
            files.optional.push_back(path);
        }
        wanted(path);
    };
    // what a `:::box` holds is a document of its own, and it may cite a picture or a plot like the slide does
    std::vector<Document> scanned{document};
    for (const Block& block : document.blocks) {
        if (isBox(block) && !block.lines.empty()) {
            scanned.push_back(boxDocumentOf(block.lines, document));
        }
    }
    for (const Block& block : scanned | std::views::transform(&Document::blocks) | std::views::join) {
        for (const InlineSpan& span : block.spans) {
            if (span.kind == InlineKind::image) {
                wanted(span.target);
            }
        }
        if (block.kind == BlockKind::plot) {
            for (const std::string& line : block.lines) {
                if (const auto [key, value] = fieldOf(line); key == "source") {
                    wanted(value);
                }
            }
        }
        if (block.kind == BlockKind::directive && block.info == "video") {
            wanted(block.field("src"));
        }
        if (block.kind == BlockKind::directive && block.info == "gr4") {
            wanted(block.field("workflow"));
            wanted(block.field("standby"));
            wanted(block.field("fallback"));
        }
        if (block.kind == BlockKind::directive && (block.info == "regions" || block.info == "place" || block.info == "grid")) {
            // every other key of a regions directive names a region of the picture, not a file to fetch
            wanted(block.field("source"));
        }
        if (block.kind == BlockKind::directive && block.info == "layout") {
            for (const std::string_view key : {"transition", "background", "overlay"}) {
                wantedEffect(block.field(key));
            }
        }
        if (block.kind == BlockKind::directive && block.info == "shader") {
            wantedEffect(block.field("effect"));
            wanted(block.field("fallback"));
        }
        if (block.kind == BlockKind::directive && block.info == "stage") {
            for (const std::string& transition : parseNameList(block.field("transitions"))) {
                wantedEffect(transition);
            }
        }
        wantedEffect(block.reveal.kind);
        if (isBox(block)) {
            wantedEffect(block.field("in"));
            wantedEffect(block.field("overlay"));
        }
        if (block.kind == BlockKind::directive && block.info == "layout") {
            for (const auto& [key, value] : block.fields) {
                if (!isLayoutSetting(key)) {
                    wanted(schemeFilesOf(value).light);
                    wanted(schemeFilesOf(value).dark);
                }
            }
        }
    }
    for (const Document::StepReveal& stepReveal : document.stepReveals) {
        wantedEffect(stepReveal.reveal.kind);
    }
    for (const std::string_view value : {std::string_view{manifest.background}, std::string_view{manifest.pointer}, std::string_view{manifest.breakEffect}}) {
        wantedEffect(value);
    }
    for (const FontFamily& family : manifest.fonts | std::views::values) {
        for (const std::string_view file : {family.regular, family.bold, family.italic, family.boldItalic}) {
            wanted(file);
        }
    }
    return files;
}

void PresentationLoader::fillRequests() {
    while (!_pendingFigures.empty() && _inFlight.size() < kConcurrentFetches) {
        const std::string reference = _pendingFigures.front();
        _pendingFigures.erase(_pendingFigures.begin());
        // a package is resolved from its root, and a reference may not leave it: a remote package is untrusted
        const auto resolved = resolvePackagePath({}, reference);
        if (!resolved) {
            diagnostics.report(DiagnosticKind::missingResource, reference, std::string{message(resolved.error())});
            continue;
        }
        if (auto request = open(resolved->external ? resolved->path : resolve(resolved->path), reference); request) {
            _inFlight.push_back(std::move(request));
        } else {
            // the reader would not even start: the same outcome as a read that fails, and not fatal to the deck
            diagnostics.report(DiagnosticKind::missingResource, reference, "the reader could not be opened");
        }
    }
    if (_pendingFigures.empty() && _inFlight.empty()) {
        _state    = LoadState::ready;
        _progress = 1.0f;
        _step     = Step::done;
    }
}

void PresentationLoader::pollFigures() {
    for (auto& request : _inFlight) {
        request->collect();
    }

    for (auto& request : _inFlight) {
        if (request->error && std::ranges::contains(_optionalFigures, request->reference)) {
            continue; // the bundled effect of that name is drawn instead, or the viewer reports the name as unknown
        }
        if (request->error) {
            // a figure that cannot be read leaves the document loadable; DocumentView draws a labelled placeholder
            diagnostics.report(DiagnosticKind::missingResource, request->reference, *request->error);
        } else if (request->finished) {
            if (request->reference.ends_with(".glsl")) {
                const std::string_view text{reinterpret_cast<const char*>(request->bytes.data()), request->bytes.size()};
                for (const std::string& asset : effectAssetsOf(text)) {
                    const std::string path = std::format("effects/{}", asset);
                    if (!_figures.contains(path) && !std::ranges::contains(_pendingFigures, path)) {
                        _pendingFigures.push_back(path);
                        ++_figureCount;
                    }
                }
            }
            _figures.insert_or_assign(request->reference, std::move(request->bytes));
        }
    }
    std::erase_if(_inFlight, [](const std::unique_ptr<Request>& request) { return request->error || request->finished; });

    // a quarter of the bar is the figures, divided by how many the document named
    const std::size_t done = _figureCount - _pendingFigures.size() - _inFlight.size();
    _progress              = _figureCount == 0UZ ? 1.0f : 0.75f + 0.25f * static_cast<float>(done) / static_cast<float>(_figureCount);
    fillRequests();
}

std::span<const std::uint8_t> PresentationLoader::figureBytes(std::string_view reference) const noexcept {
    const auto figure = _figures.find(reference);
    return figure == _figures.end() ? std::span<const std::uint8_t>{} : std::span<const std::uint8_t>{figure->second};
}

} // namespace gr::present
