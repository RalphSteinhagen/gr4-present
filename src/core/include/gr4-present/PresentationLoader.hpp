#ifndef GR4_PRESENT_PRESENTATION_LOADER_HPP
#define GR4_PRESENT_PRESENTATION_LOADER_HPP

#include <gr4-present/Manifest.hpp>

#include <chrono>
#include <cstdint>
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

    RetryPolicy retryPolicy;

    void begin(std::string_view baseUri);
    void advance();

    [[nodiscard]] LoadState                     state() const noexcept { return _state; }
    [[nodiscard]] float                         progress() const noexcept { return _progress; }
    [[nodiscard]] const Manifest&               manifest() const noexcept { return _manifest; }
    [[nodiscard]] std::string_view              diagnostic() const noexcept { return _diagnostic; }
    [[nodiscard]] std::string_view              baseUri() const noexcept { return _baseUri; }
    [[nodiscard]] std::span<const std::uint8_t> imageBytes() const noexcept { return _imageBytes; }

    /// non-empty when the manifest loaded but one of its assets did not
    [[nodiscard]] std::string_view          missingAsset() const noexcept { return _missingAsset; }
    [[nodiscard]] std::size_t               attempts() const noexcept { return _attempts; }
    [[nodiscard]] std::chrono::milliseconds untilRetry() const noexcept;

    /// joins a package-relative reference onto the base URI
    [[nodiscard]] std::string resolve(std::string_view reference) const;

private:
    enum class Step { manifest, image, done };

    struct Request;

    LoadState                             _state    = LoadState::idle;
    Step                                  _step     = Step::manifest;
    float                                 _progress = 0.0f;
    std::string                           _baseUri;
    std::string                           _diagnostic;
    Manifest                              _manifest;
    std::string                           _missingAsset;
    std::vector<std::uint8_t>             _imageBytes;
    std::unique_ptr<Request>              _request;
    std::size_t                           _attempts = 0UZ;
    std::chrono::steady_clock::time_point _nextAttempt{};

    void startRequest(std::string_view uri);
    void fail(std::string_view reason);
};

} // namespace gr::present

#endif // GR4_PRESENT_PRESENTATION_LOADER_HPP
