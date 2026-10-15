#ifndef GR4_PRESENT_LOADING_SCREEN_HPP
#define GR4_PRESENT_LOADING_SCREEN_HPP

#include "Theme.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace gr::present {

/// stages stall for different reasons: runtime download (browser), content fetch (package), initialisation (app)
enum class LoadStage : std::size_t { application = 0UZ, content = 1UZ, initialisation = 2UZ };

inline constexpr std::size_t kLoadStageCount = 3UZ;

[[nodiscard]] std::string_view defaultStatus(LoadStage stage) noexcept;

struct LoadingScreen {
    std::array<float, kLoadStageCount> progress{}; // per stage, clamped to [0, 1]

    void setProgress(LoadStage stage, float fraction) noexcept;

    [[nodiscard]] bool      finished() const noexcept;
    [[nodiscard]] LoadStage activeStage() const noexcept; // first stage that is not complete

    /// seconds the splash, or the replaced deck's last frame, takes to fade once loading is done, the deck showing through it
    static constexpr float kFadeSeconds = 1.0f;
    /// the splash's opacity `seconds` after loading finished: 1 then, 0 from `kFadeSeconds` on
    [[nodiscard]] static constexpr float fadeOpacity(float seconds) noexcept { return std::clamp(1.0f - seconds / kFadeSeconds, 0.0f, 1.0f); }

    /// below an `opacity` of 1 the splash lies over the deck already drawn, in the background colour at that opacity
    void draw(ImTextureID logo, ImVec2 logoSize, const Theme& theme, float opacity) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_LOADING_SCREEN_HPP
