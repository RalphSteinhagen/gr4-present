#include "RasterCamera.hpp"

#include "Transition.hpp"

#include <gr4-present/Number.hpp>

#include <algorithm>
#include <cmath>

namespace gr::present {

namespace {

/// the reserved keys of a `:::regions` directive; everything else names a region
[[nodiscard]] bool isReservedRegionKey(std::string_view key) noexcept { return key == "source"; }

/// the next whitespace-separated number of `text`, advancing it past what was read
[[nodiscard]] bool takeNumber(std::string_view& text, float& value) noexcept {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return false;
    }
    text              = text.substr(begin);
    const auto end    = text.find_first_of(" \t");
    const auto parsed = parseNumber<float>(text.substr(0UZ, end));
    text              = end == std::string_view::npos ? std::string_view{} : text.substr(end);
    value             = parsed.value_or(value);
    return parsed.has_value();
}

} // namespace

CameraScope cameraScopeOf(std::string_view requested) noexcept { return requested == "slide" ? CameraScope::slide : CameraScope::image; }

std::vector<Area> regionsOf(std::span<const std::pair<std::string, std::string>> fields) {
    std::vector<Area> regions;
    for (const auto& [key, value] : fields) {
        if (isReservedRegionKey(key)) {
            continue;
        }
        std::string_view rest = value;
        float            x    = 0.0f;
        float            y    = 0.0f;
        float            w    = 0.0f;
        float            h    = 0.0f;
        // a region is four numbers; anything else is a typo the author should see rather than a silent zero box
        if (!takeNumber(rest, x) || !takeNumber(rest, y) || !takeNumber(rest, w) || !takeNumber(rest, h) || w <= 0.0f || h <= 0.0f) {
            continue;
        }
        regions.push_back(Area{.id = key, .kind = {}, .step = 0, .x = x, .y = y, .width = w, .height = h});
    }
    return regions;
}

Rectangle fittedToAspect(const Rectangle& frame, float screenAspect, float imageAspect) noexcept {
    if (screenAspect <= 0.0f || imageAspect <= 0.0f || frame.width <= 0.0f || frame.height <= 0.0f) {
        return frame;
    }
    // in fraction space a frame's shape on screen is its own ratio times the image's, so this is the ratio to reach
    const float wanted = screenAspect / imageAspect;

    Rectangle fitted = frame;
    if (frame.width < wanted * frame.height) {
        fitted.width = wanted * frame.height; // too tall for the screen: widen it
    } else {
        fitted.height = frame.width / wanted; // too wide: take in more above and below
    }
    fitted.x = frame.x + 0.5f * (frame.width - fitted.width); // grown about the centre, so the subject stays put
    fitted.y = frame.y + 0.5f * (frame.height - fitted.height);

    // a frame wider than the picture cannot be satisfied; showing the whole width is the closest thing to it
    if (fitted.width >= 1.0f) {
        fitted.width = 1.0f;
        fitted.x     = 0.0f;
    } else {
        fitted.x = std::clamp(fitted.x, 0.0f, 1.0f - fitted.width);
    }
    if (fitted.height >= 1.0f) {
        fitted.height = 1.0f;
        fitted.y      = 0.0f;
    } else {
        fitted.y = std::clamp(fitted.y, 0.0f, 1.0f - fitted.height);
    }
    return fitted;
}

Rectangle interpolateVia(const Rectangle& from, const Rectangle& waypoint, const Rectangle& to, float t) noexcept {
    if (waypoint.width <= 0.0f || waypoint.height <= 0.0f) {
        return zoomAndPan(from, to, t); // without a waypoint the camera pulls back as far as the distance needs
    }
    if (t <= 0.0f) {
        return from;
    }
    if (t >= 1.0f) {
        return to;
    }
    // One parabola through from, waypoint and to, in the quantities the eye reads: the logarithm of the size and
    // the position of the centre. Two legs joined at the waypoint would each be smooth but meet at a corner -- the
    // zoom rate flips sign and is at its largest right there -- which reads as a stutter. The parabola passes
    // through the waypoint at the middle of the move and its zoom rate goes through zero at the top of the pull-back.
    const float s       = 2.0f * t - 1.0f;
    const auto  through = [s](float a, float w, float b) { return w + 0.5f * s * (b - a) + 0.5f * s * s * (a + b - 2.0f * w); };
    const auto  sizeOf  = [&through](float a, float w, float b) { return a > 0.0f && w > 0.0f && b > 0.0f ? std::exp(through(std::log(a), std::log(w), std::log(b))) : through(a, w, b); };
    const float width   = sizeOf(from.width, waypoint.width, to.width);
    const float height  = sizeOf(from.height, waypoint.height, to.height);
    const float centreX = through(from.x + 0.5f * from.width, waypoint.x + 0.5f * waypoint.width, to.x + 0.5f * to.width);
    const float centreY = through(from.y + 0.5f * from.height, waypoint.y + 0.5f * waypoint.height, to.y + 0.5f * to.height);
    return Rectangle{.x = centreX - 0.5f * width, .y = centreY - 0.5f * height, .width = width, .height = height};
}

Rectangle fittedLoosely(const Rectangle& frame, float screenAspect, float imageAspect) noexcept {
    if (screenAspect <= 0.0f || imageAspect <= 0.0f || frame.width <= 0.0f || frame.height <= 0.0f) {
        return frame;
    }
    const float wanted = screenAspect / imageAspect;

    Rectangle fitted = frame;
    if (frame.width < wanted * frame.height) {
        fitted.width = wanted * frame.height;
    } else {
        fitted.height = frame.width / wanted;
    }
    // the smallest frame of the screen's shape that holds the whole picture, which is what pulling back ends at
    const float widest = kMaxPullBack * std::max(1.0f, wanted);
    if (fitted.width > widest) {
        fitted.height *= widest / fitted.width;
        fitted.width = widest;
    }
    fitted.x = frame.x + 0.5f * (frame.width - fitted.width);
    fitted.y = frame.y + 0.5f * (frame.height - fitted.height);
    return fitted;
}

Rectangle cameraFrameAt(const Rectangle& from, const Rectangle& waypoint, const Rectangle& to, float t, float screenAspect, float imageAspect) noexcept {
    // The ends are settled the way a stop is, slid inside the picture. The path between them is not: sliding every
    // frame of it back inside made the centre stick at an edge and then jump, which is the stutter this replaces.
    const Rectangle settledFrom = fittedToAspect(from, screenAspect, imageAspect);
    const Rectangle settledTo   = fittedToAspect(to, screenAspect, imageAspect);
    // the waypoint is not a stop, so it is not pushed back inside the picture: one that reaches past the edge is
    // how an author asks to pull back further than the whole picture, up to the allowed limit
    const Rectangle settledVia = waypoint.width > 0.0f && waypoint.height > 0.0f ? fittedLoosely(waypoint, screenAspect, imageAspect) : Rectangle{};
    return fittedLoosely(interpolateVia(settledFrom, settledVia, settledTo, t), screenAspect, imageAspect);
}

Rectangle besideRegion(const Rectangle& region, const Rectangle& screen, float width, float height, std::string_view side, float gap) noexcept {
    const float roomLeft  = region.x - screen.x;
    const float roomRight = screen.x + screen.width - (region.x + region.width);
    const float roomAbove = region.y - screen.y;
    const float roomBelow = screen.y + screen.height - (region.y + region.height);
    if (side != "left" && side != "right" && side != "above" && side != "below") {
        const bool sideways = std::max(roomLeft, roomRight) >= width + 2.0f * gap;
        side                = sideways ? (roomRight >= roomLeft ? "right" : "left") : (roomBelow >= roomAbove ? "below" : "above");
    }
    Rectangle box{.x = 0.0f, .y = 0.0f, .width = width, .height = height};
    if (side == "left" || side == "right") {
        box.x = side == "right" ? region.x + region.width + gap : region.x - gap - width;
        box.y = region.y;
    } else {
        box.x = region.x + (region.width - width) * 0.5f;
        box.y = side == "below" ? region.y + region.height + gap : region.y - gap - height;
    }
    box.x = std::clamp(box.x, screen.x + gap, std::max(screen.x + screen.width - gap - width, screen.x + gap));
    box.y = std::clamp(box.y, screen.y + gap, std::max(screen.y + screen.height - gap - height, screen.y + gap));
    return box;
}

} // namespace gr::present
