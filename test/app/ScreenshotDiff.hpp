#ifndef GR4_PRESENT_SCREENSHOT_DIFF_HPP
#define GR4_PRESENT_SCREENSHOT_DIFF_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gr::present::test {

/**
 * Exact equality is the wrong test for rendered UI: anti-aliasing, font hinting and GPU or driver differences move
 * individual pixels without anything being wrong, so a byte comparison fails on a new machine and teaches the team
 * to ignore it. A regression instead shows up as a *coherent* change, so the largest connected blob of differing
 * pixels is the primary criterion and the differing fraction only a backstop.
 */
struct ImageTolerance {
    int         channelDelta      = 12;   // per-channel difference treated as rendering noise
    std::size_t clusterArea       = 24UZ; // smallest connected blob of differing pixels that counts as a regression
    double      differingFraction = 0.02; // share of differing pixels that fails regardless of clustering
};

struct ComparisonResult {
    bool        matches         = false;
    std::size_t differingPixels = 0UZ;
    std::size_t largestCluster  = 0UZ;
    double      differingShare  = 0.0;
    std::string message;

    explicit operator bool() const noexcept { return matches; }
};

/// a decoded capture, RGBA, so a test can assert about what was actually drawn rather than only about sameness
struct Screenshot {
    int                        width  = 0;
    int                        height = 0;
    std::vector<unsigned char> rgba;

    [[nodiscard]] std::size_t area() const noexcept { return static_cast<std::size_t>(width) * static_cast<std::size_t>(height); }

    /// the pixel at (x, y) packed as 0xAABBGGRR, which is what IM_COL32 produces
    [[nodiscard]] std::uint32_t at(int x, int y) const noexcept {
        const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4UZ;
        return static_cast<std::uint32_t>(rgba[offset]) | static_cast<std::uint32_t>(rgba[offset + 1UZ]) << 8U | static_cast<std::uint32_t>(rgba[offset + 2UZ]) << 16U | static_cast<std::uint32_t>(rgba[offset + 3UZ]) << 24U;
    }
};

[[nodiscard]] std::optional<Screenshot> readScreenshot(const std::filesystem::path& path);

/// on mismatch a diff image is written beside `candidate`
[[nodiscard]] ComparisonResult compareScreenshot(const std::filesystem::path& candidate, const std::filesystem::path& reference, const ImageTolerance& tolerance = {});

/// the only PNG writer: imgui_test_engine bundles a colliding stb_image_write
[[nodiscard]] bool writePng(const std::filesystem::path& path, std::span<const unsigned char> rgba, int width, int height);

/// set by GR4_PRESENT_UPDATE_REFERENCES=1
[[nodiscard]] bool updatingReferences() noexcept;

} // namespace gr::present::test

#endif // GR4_PRESENT_SCREENSHOT_DIFF_HPP
