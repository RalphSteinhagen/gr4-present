#include <gr4-present/PresentationLoader.hpp>

#include <gnuradio-4.0/algorithm/fileio/FileIo.hpp>

#include <algorithm>
#include <format>
#include <optional>

namespace gr::present {

namespace {

// fileio dispatches on the scheme, and a bare path has none; anything without one is a local directory
[[nodiscard]] std::string withScheme(std::string_view uri) { return uri.contains("://") || uri.starts_with("file:") || uri.starts_with("dialog:") ? std::string{uri} : std::format("file:{}", uri); }

[[nodiscard]] std::string_view withoutTrailingSlash(std::string_view uri) { return uri.ends_with('/') ? uri.substr(0UZ, uri.size() - 1UZ) : uri; }

} // namespace

struct PresentationLoader::Request {
    gr::algorithm::fileio::Reader reader;
    std::vector<std::uint8_t>     bytes;
    std::optional<std::string>    error;
    bool                          finished = false;
};

PresentationLoader::PresentationLoader()  = default;
PresentationLoader::~PresentationLoader() = default;

std::string PresentationLoader::resolve(std::string_view reference) const { return std::format("{}/{}", withoutTrailingSlash(_baseUri), reference); }

void PresentationLoader::fail(std::string_view reason) {
    _state      = LoadState::failed;
    _diagnostic = std::string{reason};
    _request.reset();

    if (!retryPolicy.enabled) {
        return;
    }
    // exponential backoff: a server that is not up yet is the common case, and hammering it helps nobody
    auto delay = retryPolicy.initialDelay;
    for (std::size_t doubling = 1UZ; doubling < _attempts && delay < retryPolicy.maximumDelay; ++doubling) {
        delay *= 2;
    }
    _nextAttempt = std::chrono::steady_clock::now() + std::min(delay, retryPolicy.maximumDelay);
}

std::chrono::milliseconds PresentationLoader::untilRetry() const noexcept {
    if (_state != LoadState::failed || !retryPolicy.enabled) {
        return std::chrono::milliseconds::zero();
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(_nextAttempt - std::chrono::steady_clock::now());
    return std::max(remaining, std::chrono::milliseconds::zero());
}

void PresentationLoader::startRequest(std::string_view uri) {
    auto reader = gr::algorithm::fileio::readAsync(withScheme(uri));
    if (!reader) {
        fail(std::format("cannot read {}: {}", uri, reader.error().message));
        return;
    }
    _request = std::make_unique<Request>(std::move(*reader));
}

void PresentationLoader::begin(std::string_view baseUri) {
    _baseUri  = std::string{baseUri};
    _state    = LoadState::loading;
    _step     = Step::manifest;
    _progress = 0.0f;
    _diagnostic.clear();
    _manifest = {};
    _missingAsset.clear();
    _imageBytes.clear();
    ++_attempts;
    startRequest(resolve(kManifestName));
}

void PresentationLoader::advance() {
    if (_state == LoadState::failed && retryPolicy.enabled && std::chrono::steady_clock::now() >= _nextAttempt) {
        const std::size_t attempts = _attempts;
        begin(_baseUri);
        _attempts = attempts + 1UZ;
        return;
    }
    if (_state != LoadState::loading || !_request) {
        return;
    }

    _request->reader.poll([this](const gr::algorithm::fileio::Reader::PollResult& result) {
        if (!result.data) {
            _request->error = result.data.error().message;
        } else {
            _request->bytes.insert(_request->bytes.end(), result.data->begin(), result.data->end());
        }
        if (result.isFinal) {
            _request->finished = true;
        }
    });

    if (_request->error) {
        if (_step == Step::image) {
            _missingAsset = _manifest.views.front().image;
            _state        = LoadState::ready;
            _progress     = 1.0f;
            _step         = Step::done;
            _request.reset();
            return;
        }
        fail(std::format("cannot read presentation: {}", *_request->error));
        return;
    }
    if (!_request->finished) {
        return;
    }

    if (_step == Step::manifest) {
        const std::string_view text{reinterpret_cast<const char*>(_request->bytes.data()), _request->bytes.size()};
        const auto             parsed = parseManifest(text);
        if (!parsed) {
            fail(parsed.error().message());
            return;
        }
        _manifest = *parsed;
        _progress = 0.5f;

        const ManifestView* view = _manifest.views.empty() ? nullptr : &_manifest.views.front();
        if (view == nullptr || view->image.empty()) {
            _state    = LoadState::ready;
            _progress = 1.0f;
            _request.reset();
            return;
        }
        _step = Step::image;
        startRequest(resolve(view->image));
        return;
    }

    _imageBytes = std::move(_request->bytes);
    _state      = LoadState::ready;
    _progress   = 1.0f;
    _step       = Step::done;
    _request.reset();
}

} // namespace gr::present
