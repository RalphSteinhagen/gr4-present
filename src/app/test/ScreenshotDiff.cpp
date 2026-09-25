#include "ScreenshotDiff.hpp"

#include <stb_image.h>

// imgui_test_engine bundles its own stb_image_write with the same external symbols, so ours is kept internal to
// this translation unit and reached through writePng()
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present::test {

namespace {

constexpr int kChannels = 4;

struct Image {
    int                        width  = 0;
    int                        height = 0;
    std::vector<unsigned char> pixels; // RGBA

    [[nodiscard]] std::size_t area() const noexcept { return static_cast<std::size_t>(width) * static_cast<std::size_t>(height); }
};

[[nodiscard]] std::optional<Image> readImage(const std::filesystem::path& path) {
    int   width    = 0;
    int   height   = 0;
    int   channels = 0;
    auto* decoded  = stbi_load(path.c_str(), &width, &height, &channels, kChannels);
    if (decoded == nullptr) {
        return std::nullopt;
    }
    const std::span<const unsigned char> bytes{decoded, static_cast<std::size_t>(width * height * kChannels)};
    Image                                image{.width = width, .height = height, .pixels = {bytes.begin(), bytes.end()}};
    stbi_image_free(decoded);
    return image;
}

// four-connected; iterative so a large differing region cannot overflow the stack
[[nodiscard]] std::size_t largestConnectedCluster(const std::vector<char>& differs, int width, int height) {
    std::vector<char>        visited(differs.size(), 0);
    std::vector<std::size_t> pending;
    std::size_t              largest = 0UZ;

    for (std::size_t seed = 0UZ; seed < differs.size(); ++seed) {
        if (differs[seed] == 0 || visited[seed] != 0) {
            continue;
        }
        pending.push_back(seed);
        visited[seed]    = 1;
        std::size_t size = 0UZ;
        while (!pending.empty()) {
            const std::size_t index = pending.back();
            pending.pop_back();
            ++size;
            const int                                    x = static_cast<int>(index % static_cast<std::size_t>(width));
            const int                                    y = static_cast<int>(index / static_cast<std::size_t>(width));
            constexpr std::array<std::pair<int, int>, 4> kNeighbours{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto& [dx, dy] : kNeighbours) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                    continue;
                }
                const auto neighbour = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
                if (differs[neighbour] != 0 && visited[neighbour] == 0) {
                    visited[neighbour] = 1;
                    pending.push_back(neighbour);
                }
            }
        }
        largest = std::max(largest, size);
    }
    return largest;
}

void writeDiffImage(const std::filesystem::path& destination, const Image& candidate, const Image& reference, const std::vector<char>& differs) {
    std::vector<unsigned char> diff(candidate.pixels.size());
    for (std::size_t pixel = 0UZ; pixel < candidate.area(); ++pixel) {
        const auto offset = pixel * static_cast<std::size_t>(kChannels);
        if (differs[pixel] != 0) {
            diff[offset + 0UZ] = 0xFF; // differing pixels in red over a dimmed reference
            diff[offset + 1UZ] = 0x00;
            diff[offset + 2UZ] = 0x00;
        } else {
            for (std::size_t channel = 0UZ; channel < 3UZ; ++channel) {
                diff[offset + channel] = static_cast<unsigned char>(reference.pixels[offset + channel] / 3U + 170U);
            }
        }
        diff[offset + 3UZ] = 0xFF;
    }
    stbi_write_png(destination.c_str(), candidate.width, candidate.height, kChannels, diff.data(), candidate.width * kChannels);
}

} // namespace

bool writePng(const std::filesystem::path& path, std::span<const unsigned char> rgba, int width, int height) { return stbi_write_png(path.c_str(), width, height, kChannels, rgba.data(), width * kChannels) != 0; }

bool updatingReferences() noexcept {
    const char* requested = std::getenv("GR4_PRESENT_UPDATE_REFERENCES");
    return requested != nullptr && std::string_view{requested} == "1";
}

ComparisonResult compareScreenshot(const std::filesystem::path& candidate, const std::filesystem::path& reference, const ImageTolerance& tolerance) {
    const auto captured = readImage(candidate);
    if (!captured) {
        return {.message = std::format("cannot read captured screenshot {}", candidate.string())};
    }
    const auto expected = readImage(reference);
    if (!expected) {
        return {.message = std::format("no reference {} — record it with GR4_PRESENT_UPDATE_REFERENCES=1", reference.string())};
    }
    if (captured->width != expected->width || captured->height != expected->height) {
        return {.message = std::format("size changed: captured {}x{}, reference {}x{}", captured->width, captured->height, expected->width, expected->height)};
    }

    std::vector<char> differs(captured->area(), 0);
    std::size_t       differingPixels = 0UZ;
    for (std::size_t pixel = 0UZ; pixel < captured->area(); ++pixel) {
        const auto offset = pixel * static_cast<std::size_t>(kChannels);
        int        worst  = 0;
        for (std::size_t channel = 0UZ; channel < 3UZ; ++channel) {
            worst = std::max(worst, std::abs(static_cast<int>(captured->pixels[offset + channel]) - static_cast<int>(expected->pixels[offset + channel])));
        }
        if (worst > tolerance.channelDelta) {
            differs[pixel] = 1;
            ++differingPixels;
        }
    }

    const double      share   = static_cast<double>(differingPixels) / static_cast<double>(captured->area());
    const std::size_t cluster = differingPixels == 0UZ ? 0UZ : largestConnectedCluster(differs, captured->width, captured->height);

    ComparisonResult result{
        .matches         = cluster < tolerance.clusterArea && share <= tolerance.differingFraction,
        .differingPixels = differingPixels,
        .largestCluster  = cluster,
        .differingShare  = share,
        .message         = {},
    };
    if (result.matches) {
        result.message = std::format("within tolerance ({} px differ, largest blob {} px)", differingPixels, cluster);
        return result;
    }

    std::filesystem::path diffPath = candidate;
    diffPath.replace_extension(".diff.png");
    writeDiffImage(diffPath, *captured, *expected, differs);
    result.message = std::format("{} px differ ({:.3f}%), largest coherent blob {} px (allowed {}); diff written to {}", differingPixels, 100.0 * share, cluster, tolerance.clusterArea, diffPath.string());
    return result;
}

} // namespace gr::present::test
