#ifndef GR4_PRESENT_TRANSITION_HPP
#define GR4_PRESENT_TRANSITION_HPP

#include <gr4-present/Manifest.hpp>
#include <gr4-present/Navigation.hpp>
#include <gr4-present/RegionGeometry.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/// the transitions drawn by a fragment shader from pictures of both slides, rather than by moving their vertices
enum class ShaderEffect : std::uint8_t { fire, crt, glitch, waterfall, sine, disintegrate, ripple, curl, cube, flip, fire2 };

inline constexpr std::array<std::string_view, 11> kShaderEffectNames{"fire", "crt", "glitch", "waterfall", "sine", "disintegrate", "ripple", "curl", "cube", "flip", "fire2"};

/// 0 to 1 eased so that it starts and stops without a jerk; `t` outside 0..1 is held at the ends
[[nodiscard]] constexpr float smoothstep(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * A move between views.
 *
 * Sections drawn on the same master interpolate the camera between their anchors, because the audience is looking at
 * one continuous picture; unrelated scenes cross-fade. An author overrides either with `transition:` in the layout
 * directive. A step within a section never transitions: a reveal is not a move.
 */
struct Transition {
    enum class Kind { none, cut, fade, fadeThrough, camera, push, cover, uncover, zoom, shader, morph };
    /// which way the slides move: with the deck's order (forward to the left, back to the right) unless the author says
    enum class Direction : std::uint8_t { byOrder, left, right, up, down };

    Kind         kind = Kind::none;
    Cursor       from;
    float        elapsed   = 0.0f;
    float        duration  = kDefaultTransitionSeconds; // seconds, the deck's or the slide's
    Direction    direction = Direction::byOrder;
    ShaderEffect effect    = ShaderEffect::fire; // which, when the kind is `shader`

    /// eased 0 to 1; smoothstep, so the move has no jerk at either end
    [[nodiscard]] float progress() const noexcept;
    [[nodiscard]] bool  running() const noexcept { return kind != Kind::none; }
};

/// `requested` is the author's `transition:` value; `sameMaster` says whether both sections draw on one layout
[[nodiscard]] Transition::Kind transitionKindFor(std::string_view requested, bool sameMaster) noexcept;

/// `push-left` and its kin: the direction after the kind's name, or with the deck's order when there is none
[[nodiscard]] Transition::Direction transitionDirectionFor(std::string_view requested) noexcept;

/**
 * Which element of the slide arriving each element of the slide leaving becomes under `transition: morph`, as pairs of
 * indices: the same key on both, and the n-th of a key repeated on a slide with the n-th on the other. Keynote calls
 * the effect Magic Move, PowerPoint Morph.
 */
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> matchedByKey(std::span<const std::string> leaving, std::span<const std::string> arriving);

/// how long a shader transition runs when its slide does not say: longer than a slide's usual move, so the effect is
/// seen, and longest for a fire, which has to burn
inline constexpr float kShaderSeconds = 1.0f;
inline constexpr float kFireSeconds   = 2.0f;

/// `fire` and its kin: the effect a shader transition draws
[[nodiscard]] ShaderEffect shaderEffectFor(std::string_view requested) noexcept;

/// the `transition:` values there are; anything else falls back to the default, and is reported
inline constexpr std::array<std::string_view, 32> kTransitionNames{"morph", "fire", "fire2", "crt", "glitch", "waterfall", "sine", "disintegrate", "ripple", "curl", "cube", "flip", "cut", "fade", "fade-through", "camera", "zoom", "push", "push-left", "push-right", "push-up", "push-down", "cover", "cover-left", "cover-right", "cover-up", "cover-down", "uncover", "uncover-left", "uncover-right", "uncover-up", "uncover-down"};

/// the `in=` values a step or a box can arrive with; anything else arrives as `fade`, and is reported
inline constexpr std::array<std::string_view, 4> kRevealNames{"fade", "rise", "wipe", "grow"};

/// the `from=` values a rise or a wipe can start at
inline constexpr std::array<std::string_view, 4> kRevealDirections{"left", "right", "above", "below"};

[[nodiscard]] Rectangle interpolate(const Rectangle& from, const Rectangle& to, float t) noexcept;

/// the trade-off between zooming and panning the camera moves with; 1.42 is the mean of van Wijk and Nuij's user
/// experiment (section 6 of the paper cited at `zoomAndPan`)
inline constexpr double kZoomPanRho = 1.42;

/**
 * The camera's optimal path from `from` to `to`: it pulls back while it travels, as far as the distance needs, so a
 * long move is a short flight over the picture rather than a slow slide along it. `t` is the eased progress; the
 * frame's aspect changes geometrically along it.
 *
 * J. J. van Wijk and W. A. A. Nuij, "Smooth and efficient zooming and panning," in Proc. IEEE Symp. Information
 * Visualization, 2003, pp. 15-22, equation (9) and its case for coinciding centres.
 */
[[nodiscard]] Rectangle zoomAndPan(const Rectangle& from, const Rectangle& to, float t, double rho = kZoomPanRho) noexcept;

/**
 * The rectangle a frame stands for, given the document it frames.
 *
 * A section with no anchor records an empty frame and means the whole document by it. That is fine to draw, because
 * the placement resolves it, but it cannot be interpolated: the empty rectangle drags a zoom towards the origin
 * instead of towards the full picture. Both ends of a camera move are resolved through this first.
 */
[[nodiscard]] Rectangle wholeIfEmpty(const Rectangle& frame, float width, float height) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_TRANSITION_HPP
