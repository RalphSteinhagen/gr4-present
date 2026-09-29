#ifndef GR4_PRESENT_RASTER_CAMERA_HPP
#define GR4_PRESENT_RASTER_CAMERA_HPP

#include "SvgImage.hpp"

#include <gr4-present/RegionGeometry.hpp>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/// regions of a photograph, declared by the author where an SVG master has named boxes, one line per region:
///
///     :::regions
///     source: media/Science_FAIR.webp
///     fair-stand: 0.8125 0.37037 0.171875 0.212963
///     :::
///
/// the numbers are fractions of the image, never pixels, so re-encoding, resizing or cropping does not move them

/// how much of the slide a move takes with it
enum class CameraScope {
    image, ///< only the picture pans and zooms; the title and footer stay where they are
    slide  ///< the whole slide is the viewport, as it is for an SVG master
};

/// `camera:` from the layout directive; anything unrecognised, including nothing, keeps the title still
[[nodiscard]] CameraScope cameraScopeOf(std::string_view requested) noexcept;

/// the regions a `:::regions` directive declares, in fractions of the image; malformed lines are skipped
[[nodiscard]] std::vector<Area> regionsOf(std::span<const std::pair<std::string, std::string>> fields);

/// `frame` grown about its centre to the screen's shape, then slid inside the picture; aspects are width over height
/// it only grows, because a photograph with letterbox bars is worse than showing more of the scene
[[nodiscard]] Rectangle fittedToAspect(const Rectangle& frame, float screenAspect, float imageAspect) noexcept;

/// the furthest a move may pull back, as a multiple of the frame that just shows the whole picture
inline constexpr float kMaxPullBack = 1.5f;

/// where the picture lands: `box` is the room it was given, `drawn` the rectangle it covers after fitting
struct CameraGeometry {
    Rectangle box;
    Rectangle drawn;
    Rectangle uv;    ///< the part of the texture that fills `drawn`, never outside the picture
    Rectangle frame; ///< the frame the camera was aimed at; during a move it may run past the picture's edge

    /// where `part`, in fractions of the picture, lands on screen
    [[nodiscard]] Rectangle onScreen(const Rectangle& part) const noexcept;
};

/// the slide-scope camera: the whole slide scaled by `factor` and moved by the offsets, in screen pixels
struct Glass {
    float factor  = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

/// the glass that shows `framed` as the image scope would, magnifying `whole`: the framed region fills the screen's
/// width (`regionGiven`), its centre comes to the picture's centre, and a picture larger than `screen` is slid to
/// cover it rather than show black beyond its edge
[[nodiscard]] Glass magnifierFor(const CameraGeometry& whole, const CameraGeometry& framed, bool regionGiven, const Rectangle& screen) noexcept;

/// `frame` grown to the screen's shape and left where it falls, possibly past the picture's edge, up to `kMaxPullBack`
/// for the frames in the middle of a move, where sliding each one back inside makes the camera stick at an edge
[[nodiscard]] Rectangle fittedLoosely(const Rectangle& frame, float screenAspect, float imageAspect) noexcept;

/// the frame partway through a move between two stops: stops settled as at rest, the path between interpolated through
/// `waypoint` (empty is a straight move) and fitted loosely; at `t` of zero and one it is the frame a stop shows
[[nodiscard]] Rectangle cameraFrameAt(const Rectangle& from, const Rectangle& waypoint, const Rectangle& to, float t, float screenAspect, float imageAspect) noexcept;

/// the frame partway through a move via `waypoint`: one smooth curve that reaches it at the middle of `t`
/// the whole picture as waypoint gives pull back, travel, push in, instead of a smear between small distant boxes
[[nodiscard]] Rectangle interpolateVia(const Rectangle& from, const Rectangle& waypoint, const Rectangle& to, float t) noexcept;

/// where a stop's info box goes: beside the framed region, never over it, always inside the screen
/// `side` is `left`, `right`, `above` or `below`; anything else takes the side with more room, left and right first
/// the box is top-aligned beside a region and centred above or below it
[[nodiscard]] Rectangle besideRegion(const Rectangle& region, const Rectangle& screen, float width, float height, std::string_view side, float gap) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_RASTER_CAMERA_HPP
