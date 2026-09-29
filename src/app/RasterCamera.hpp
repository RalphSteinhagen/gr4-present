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

/**
 * Stepping through a photograph the way the camera steps through a drawing.
 *
 * An SVG master carries its own named boxes, so a section names one and the camera frames it. A raster has no
 * such thing, so the author declares the regions in a `:::regions` directive instead, one line per region:
 *
 *     :::regions
 *     source: media/Science_FAIR.webp
 *     fair-stand: 0.8125 0.37037 0.171875 0.212963
 *     :::
 *
 * The numbers are fractions of the image, never pixels. A pixel box is only correct for the file it was
 * measured on: re-encoding, resizing or cropping the picture moves it silently and the camera then frames the
 * wrong thing. Fractions survive all three.
 */

/// how much of the slide a move takes with it
enum class CameraScope {
    image, ///< only the picture pans and zooms; the title and footer stay where they are
    slide  ///< the whole slide is the viewport, as it is for an SVG master
};

/// `camera:` from the layout directive; anything unrecognised, including nothing, keeps the title still
[[nodiscard]] CameraScope cameraScopeOf(std::string_view requested) noexcept;

/// the regions a `:::regions` directive declares, in fractions of the image; malformed lines are skipped
[[nodiscard]] std::vector<Area> regionsOf(std::span<const std::pair<std::string, std::string>> fields);

/**
 * `frame` grown about its centre until it has the screen's shape, then slid back inside the picture.
 *
 * A master letterboxes, because its margins are part of the design and cropping them would cut the layout.
 * A photograph must not: black bars down two thirds of a phone held upright are worse than showing more of
 * the scene. So the frame only ever grows, and what it grows into is more photograph.
 *
 * Both aspects are width over height. `frame` is in fractions of the image, so its shape on screen is its
 * own ratio times the image's -- which is why the image's aspect has to be passed in rather than assumed.
 */
[[nodiscard]] Rectangle fittedToAspect(const Rectangle& frame, float screenAspect, float imageAspect) noexcept;

/// how far past the picture a move may pull back, as a multiple of the frame that just shows all of it in the screen's shape
inline constexpr float kMaxPullBack = 1.5f;

/**
 * `frame` grown about its centre to the screen's shape and left where it falls, which may run past the picture's
 * edge, up to `kMaxPullBack` times the frame that holds the whole picture. For the frames in the middle of a move,
 * where pushing each one back inside the picture makes the camera stick against an edge and then jump.
 */
[[nodiscard]] Rectangle fittedLoosely(const Rectangle& frame, float screenAspect, float imageAspect) noexcept;

/**
 * The frame to show partway through a move between two stops, as it should be drawn: the stops settled as they
 * would be at rest, the path between them interpolated through `waypoint` and fitted loosely. An empty `waypoint`
 * is the straight move. At `t` of zero and one this is the frame a stop shows at rest.
 */
[[nodiscard]] Rectangle cameraFrameAt(const Rectangle& from, const Rectangle& waypoint, const Rectangle& to, float t, float screenAspect, float imageAspect) noexcept;

/**
 * The frame partway through a move that passes through `waypoint`.
 *
 * Two small boxes at opposite ends of a picture cannot be joined by a straight line: the frame stays small
 * all the way across and the audience sees a smear rather than a journey. Naming the whole picture as the
 * waypoint turns it into what an author would do by hand -- pull back, travel, push in.
 *
 * The move is one smooth curve that reaches `waypoint` at the middle of `t`, so there is no pause and no corner
 * there: the zoom rate is continuous, and passes through zero where the pull-back ends.
 */
[[nodiscard]] Rectangle interpolateVia(const Rectangle& from, const Rectangle& waypoint, const Rectangle& to, float t) noexcept;

/**
 * Where a stop's info box goes: beside the framed region, never over it, and never off the screen.
 *
 * `side` is `left`, `right`, `above` or `below`; anything else, including nothing, takes the side with more room,
 * trying left and right before above and below because a face is taller than it is wide. Beside a region the box
 * is top-aligned with it; above or below, centred on it. Either way it is then slid inside the screen.
 */
[[nodiscard]] Rectangle besideRegion(const Rectangle& region, const Rectangle& screen, float width, float height, std::string_view side, float gap) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_RASTER_CAMERA_HPP
