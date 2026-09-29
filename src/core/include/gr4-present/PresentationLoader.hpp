#ifndef GR4_PRESENT_PRESENTATION_LOADER_HPP
#define GR4_PRESENT_PRESENTATION_LOADER_HPP

#include <gr4-present/Manifest.hpp>
#include <gr4-present/Markdown.hpp>

#include <gr4-present/Diagnostics.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gr::present {

/// fetches a presentation package without blocking: `advance()` runs once per frame and returns at once, as GR4's
/// fileio read may not block the browser's main thread; manifest and document load in turn, their figures several at
/// a time
enum class LoadState { idle, loading, ready, failed };

/// a presentation is often served by something that starts late, so a failure is retried rather than final
struct RetryPolicy {
    bool                      enabled      = true;
    std::chrono::milliseconds initialDelay = std::chrono::seconds{1};
    std::chrono::milliseconds maximumDelay = std::chrono::seconds{30};
};

/// the package files an entry document makes the loader fetch, each once, in the order the document names them, then the
/// manifest's effects and fonts; a reference starting with `http` is not fetched, an effect the viewer has itself is not
/// looked for, and one named bare (`effects/<name>.glsl`) is optional: a deck need not have its own
struct FilesToFetch {
    std::vector<std::string> wanted;
    std::vector<std::string> optional;
};
[[nodiscard]] FilesToFetch filesToFetch(const Document& document, const Manifest& manifest, std::span<const std::string> builtInEffects);

class PresentationLoader {
public:
    static constexpr std::string_view kManifestName = "index.yml";

    // the in-flight request is an incomplete type here, so these are defined where it is complete
    PresentationLoader();
    PresentationLoader(const PresentationLoader&)            = delete;
    PresentationLoader& operator=(const PresentationLoader&) = delete;
    ~PresentationLoader();

    RetryPolicy              retryPolicy;
    Diagnostics              diagnostics;
    std::vector<std::string> builtInEffects; // effect names the viewer has itself: a bare one is never looked for in the deck

    // `now` is the frame's time: a failed load is retried once `now` reaches the end of its back-off
    void begin(std::string_view baseUri, std::chrono::steady_clock::time_point now);
    void advance(std::chrono::steady_clock::time_point now);

    [[nodiscard]] LoadState        state() const noexcept { return _state; }
    [[nodiscard]] float            progress() const noexcept { return _progress; }
    [[nodiscard]] const Manifest&  manifest() const noexcept { return _manifest; }
    [[nodiscard]] std::string_view diagnostic() const noexcept { return _diagnostic; }
    [[nodiscard]] std::string_view baseUri() const noexcept { return _baseUri; }
    /// the entry document's source, as named by the manifest
    [[nodiscard]] std::string_view documentSource() const noexcept { return _documentSource; }

    /// bytes of a figure the entry document referenced, empty when it was not fetched or could not be read
    [[nodiscard]] std::span<const std::uint8_t> figureBytes(std::string_view reference) const noexcept;

    [[nodiscard]] std::size_t               attempts() const noexcept { return _attempts; }
    [[nodiscard]] std::chrono::milliseconds untilRetry(std::chrono::steady_clock::time_point now) const noexcept;

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
    std::string                                                   _documentSource;
    std::vector<std::string>                                      _pendingFigures;
    std::vector<std::string>                                      _optionalFigures; // effect files a bundled effect stands in for when the deck has none
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> _figures;
    std::unique_ptr<Request>                                      _request;
    std::vector<std::unique_ptr<Request>>                         _inFlight;
    std::size_t                                                   _figureCount = 0UZ;
    std::size_t                                                   _attempts    = 0UZ;
    std::chrono::steady_clock::time_point                         _nextAttempt{};

    [[nodiscard]] std::unique_ptr<Request> open(std::string_view uri, std::string_view reference);
    void                                   startRequest(std::string_view uri, std::chrono::steady_clock::time_point now);
    void                                   fail(std::string_view reason, std::chrono::steady_clock::time_point now);
    void                                   fillRequests();
    void                                   pollFigures();
};

} // namespace gr::present

#endif // GR4_PRESENT_PRESENTATION_LOADER_HPP
