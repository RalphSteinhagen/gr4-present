#ifndef GR4_PRESENT_LOADING_SCREEN_HPP
#define GR4_PRESENT_LOADING_SCREEN_HPP

#include "Theme.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gr::present {

/**
 * The three stages are separate because they stall for different reasons: the runtime download is the browser's, the
 * content fetch the presentation package's, and initialisation the application's own.
 */
enum class LoadStage : std::size_t { application = 0UZ, content = 1UZ, initialisation = 2UZ };

inline constexpr std::size_t kLoadStageCount = 3UZ;

[[nodiscard]] std::string_view defaultStatus(LoadStage stage) noexcept;

struct LoadingScreen {
    std::array<float, kLoadStageCount> progress{}; // per stage, clamped to [0, 1]
    std::optional<std::string>         statusOverride;

    void setProgress(LoadStage stage, float fraction) noexcept;
    void setStatus(std::string text) { statusOverride = std::move(text); }
    void clearStatus() noexcept { statusOverride.reset(); }

    [[nodiscard]] float            progressOf(LoadStage stage) const noexcept { return progress[std::to_underlying(stage)]; }
    [[nodiscard]] bool             finished() const noexcept;
    [[nodiscard]] LoadStage        activeStage() const noexcept; // first stage that is not complete
    [[nodiscard]] std::string_view statusText() const noexcept;

    void draw(ImTextureID logo, ImVec2 logoSize, const Theme& theme) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_LOADING_SCREEN_HPP
