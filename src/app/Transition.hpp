#ifndef GR4_PRESENT_TRANSITION_HPP
#define GR4_PRESENT_TRANSITION_HPP

#include <gr4-present/Manifest.hpp>
#include <gr4-present/Navigation.hpp>
#include <gr4-present/RegionGeometry.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/// 0 to 1 eased so that it starts and stops without a jerk; `t` outside 0..1 is held at the ends
[[nodiscard]] constexpr float smoothstep(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// a move between views: sections on one master interpolate the camera, unrelated scenes cross-fade, `transition:`
/// overrides; a step within a section never transitions, as a reveal is not a move
struct Transition {
    enum class Kind { none, cut, fade, fadeThrough, camera, push, cover, uncover, zoom, shader, morph };
    /// `byOrder`: forward to the left, back to the right, unless the author says otherwise
    enum class Direction : std::uint8_t { byOrder, left, right, up, down };

    Kind                                                     kind = Kind::none;
    Cursor                                                   from;
    float                                                    elapsed    = 0.0f;
    float                                                    duration   = kDefaultTransitionSeconds; // seconds, the deck's or the slide's
    Direction                                                direction  = Direction::byOrder;
    std::string                                              effect     = {}; // the effect shader, when the kind is `shader`
    std::map<std::string, std::array<float, 4>, std::less<>> parameters = {}; // what the slide sets of the effect's parameters
    std::string                                              moveEffect = {}; // `camera|zoom <effect> [k=v …]`: the effect the move is seen through

    /// eased 0 to 1; smoothstep, so the move has no jerk at either end
    [[nodiscard]] float progress() const noexcept;
    [[nodiscard]] bool  running() const noexcept { return kind != Kind::none; }
};

/// `requested` is the author's `transition:` value; `sameMaster` says whether both sections draw on one layout
[[nodiscard]] Transition::Kind transitionKindFor(std::string_view requested, bool sameMaster) noexcept;

/// `push-left` and its kin: the direction after the kind's name, or with the deck's order when there is none
[[nodiscard]] Transition::Direction transitionDirectionFor(std::string_view requested) noexcept;

/// which element of the arriving slide each element of the leaving slide becomes under `transition: morph`, as index
/// pairs: the same key on both, and the n-th of a repeated key with the n-th on the other (Keynote: Magic Move,
/// PowerPoint: Morph)
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> matchedByKey(std::span<const std::string> leaving, std::span<const std::string> arriving);

/// transition length through an effect when neither slide nor effect says; longer than a usual move so the effect is
/// seen
inline constexpr float kShaderSeconds    = 1.0f;
inline constexpr float kZoomReach        = 0.1f; // share the leaving picture grows and the arriving one starts smaller
inline constexpr float kStageHoldSeconds = 1.0f; // a stage holds each picture this long between two plays

/// where a looping stage is `seconds` after its step began: the move from `from` to `to` over `durationSeconds` (eased),
/// a hold, the move back, a hold, and again
struct StagePlay {
    bool  backwards = false;
    float progress  = 0.0f;
};
[[nodiscard]] StagePlay stagePlayAt(float seconds, float durationSeconds) noexcept;

/// the `from=` values a rise or a wipe can start at
inline constexpr std::array<std::string_view, 4> kRevealDirections{"left", "right", "above", "below"};

[[nodiscard]] Rectangle interpolate(const Rectangle& from, const Rectangle& to, float t) noexcept;

/// the trade-off between zooming and panning the camera moves with; 1.42 is the mean of van Wijk and Nuij's user
/// experiment (section 6 of the paper cited at `zoomAndPan`)
inline constexpr double kZoomPanRho = 1.42;

/// the camera's optimal path from `from` to `to`, pulling back as far as the distance needs; `t` is the eased progress
/// J. J. van Wijk and W. A. A. Nuij, "Smooth and efficient zooming and panning," in Proc. IEEE Symp. Information
/// Visualization, 2003, pp. 15-22, equation (9) and its case for coinciding centres
[[nodiscard]] Rectangle zoomAndPan(const Rectangle& from, const Rectangle& to, float t, double rho = kZoomPanRho) noexcept;

/// the rectangle a frame stands for: an empty frame means the whole document, which cannot be interpolated as it would
/// drag a zoom towards the origin, so both ends of a camera move are resolved through this
[[nodiscard]] Rectangle wholeIfEmpty(const Rectangle& frame, float width, float height) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_TRANSITION_HPP
