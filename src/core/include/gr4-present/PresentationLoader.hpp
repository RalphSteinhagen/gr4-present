#ifndef GR4_PRESENT_PRESENTATION_LOADER_HPP
#define GR4_PRESENT_PRESENTATION_LOADER_HPP

#include <gr4-present/Manifest.hpp>

#include <gr4-present/Diagnostics.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gr::present {

/**
 * Fetches a presentation package from wherever it lives, without blocking.
 *
 * GR4's fileio reader serves file:, http: and https: alike, so the same code opens a directory on disk and a package
 * behind a URL. Its blocking read is forbidden on the browser's main thread, so this drives the reader in steps:
 * `advance()` is called once per frame and returns immediately, and the caller draws a launch screen until `state()`
 * leaves `loading`. Everything is resolved against a base URI, so relative references inside a package do not care
 * where the package came from.
 *
 * The manifest and the document are fetched one after the other, because each says what to ask for next. The
 * figures they name are fetched several at a time: a deck's assets are many and small, and a request spends most of
 * its life waiting.
 */
enum class LoadState { idle, loading, ready, failed };

/// a presentation is often served by something that starts late, so a failure is retried rather than final
struct RetryPolicy {
    bool                      enabled      = true;
    std::chrono::milliseconds initialDelay = std::chrono::seconds{1};
    std::chrono::milliseconds maximumDelay = std::chrono::seconds{30};
};

class PresentationLoader {
public:
    static constexpr std::string_view kManifestName = "index.yml";

    // the in-flight request is an incomplete type here, so these are defined where it is complete
    PresentationLoader();
    PresentationLoader(const PresentationLoader&)            = delete;
    PresentationLoader& operator=(const PresentationLoader&) = delete;
    ~PresentationLoader();

    RetryPolicy              retryPolicy;
    Diagnostics              diagnostics;    // everything that went wrong, for the presenter and for the log
    std::vector<std::string> builtInEffects; // effect names the viewer has itself: a bare one is never looked for in the deck

    void begin(std::string_view baseUri);
    void advance();

    [[nodiscard]] LoadState        state() const noexcept { return _state; }
    [[nodiscard]] float            progress() const noexcept { return _progress; }
    [[nodiscard]] const Manifest&  manifest() const noexcept { return _manifest; }
    [[nodiscard]] std::string_view diagnostic() const noexcept { return _diagnostic; }
    [[nodiscard]] std::string_view baseUri() const noexcept { return _baseUri; }
    /// the entry document's source, as named by the manifest
    [[nodiscard]] std::string_view documentSource() const noexcept { return _documentSource; }

    /// bytes of a figure the entry document referenced, empty when it was not fetched or could not be read
    [[nodiscard]] std::span<const std::uint8_t> figureBytes(std::string_view reference) const noexcept;

    /// non-empty when the manifest loaded but one of its assets did not
    [[nodiscard]] std::string_view          missingAsset() const noexcept { return _missingAsset; }
    [[nodiscard]] std::size_t               attempts() const noexcept { return _attempts; }
    [[nodiscard]] std::chrono::milliseconds untilRetry() const noexcept;

    /// joins a package-relative reference onto the base URI
    [[nodiscard]] std::string resolve(std::string_view reference) const;

private:
    enum class Step { manifest, document, figures, done };

    struct Request;

    LoadState                                                     _state    = LoadState::idle;
    Step                                                          _step     = Step::manifest;
    float                                                         _progress = 0.0f;
    std::string                                                   _baseUri;
    std::string                                                   _diagnostic;
    Manifest                                                      _manifest;
    std::string                                                   _missingAsset;
    std::string                                                   _documentSource;
    std::vector<std::string>                                      _pendingFigures;  // named by the document, not yet asked for
    std::vector<std::string>                                      _optionalFigures; // effect files a bundled effect stands in for when the deck has none
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> _figures;
    std::unique_ptr<Request>                                      _request;           // the manifest, then the document
    std::vector<std::unique_ptr<Request>>                         _inFlight;          // the figures, several at once
    std::size_t                                                   _figureCount = 0UZ; // how many the document named, for the progress bar
    std::size_t                                                   _attempts    = 0UZ;
    std::chrono::steady_clock::time_point                         _nextAttempt{};

    [[nodiscard]] std::unique_ptr<Request> open(std::string_view uri, std::string_view reference);
    void                                   startRequest(std::string_view uri);
    void                                   fail(std::string_view reason);
    void                                   fillRequests();
    void                                   pollFigures();
};

} // namespace gr::present

#endif // GR4_PRESENT_PRESENTATION_LOADER_HPP
