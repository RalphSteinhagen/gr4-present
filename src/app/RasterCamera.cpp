#include "RasterCamera.hpp"

#include "Transition.hpp"

#include <gr4-present/Number.hpp>
#include <gr4-present/Slice.hpp>

#include <algorithm>
#include <cmath>

namespace gr::present {

namespace {

/// advances `text` past the next whitespace-separated number
[[nodiscard]] bool takeNumber(std::string_view& text, float& value) noexcept {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return false;
    }
    text              = slice(text, begin);
    const auto end    = text.find_first_of(" \t");
    const auto parsed = parseNumber<float>(slice(text, 0UZ, end));
    text              = end == std::string_view::npos ? std::string_view{} : slice(text, end);
    value             = parsed.value_or(value);
    return parsed.has_value();
}

} // namespace

CameraScope cameraScopeOf(std::string_view requested) noexcept { return requested == "slide" ? CameraScope::slide : CameraScope::image; }

std::vector<Area> regionsOf(std::span<const std::pair<std::string, std::string>> fields) {
    std::vector<Area> regions;
    for (const auto& [key, value] : fields) {
        if (key == "source") {
            continue;
        }
        std::string_view rest = value;
        float            x    = 0.0f;
        float            y    = 0.0f;
        float            w    = 0.0f;
        float            h    = 0.0f;
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
    // a frame's shape on screen is its own ratio times the image's, so this is the ratio to reach
    const float wanted = screenAspect / imageAspect;

    Rectangle fitted = frame;
    if (frame.width < wanted * frame.height) {
        fitted.width = wanted * frame.height;
    } else {
        fitted.height = frame.width / wanted;
    }
    fitted.x = frame.x + 0.5f * (frame.width - fitted.width);
    fitted.y = frame.y + 0.5f * (frame.height - fitted.height);

    // a frame wider than the picture shows the whole width
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
        return zoomAndPan(from, to, t);
    }
    if (t <= 0.0f) {
        return from;
    }
    if (t >= 1.0f) {
        return to;
    }
    // one parabola in log size and centre position through all three: two legs would meet at a corner and stutter
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
    // the ends settle like a stop, slid inside the picture; the path between does not, or the centre sticks at an edge
    const Rectangle settledFrom = fittedToAspect(from, screenAspect, imageAspect);
    const Rectangle settledTo   = fittedToAspect(to, screenAspect, imageAspect);
    // the waypoint may reach past the edge: that is how an author pulls back beyond the picture, up to `kMaxPullBack`
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

Rectangle CameraGeometry::onScreen(const Rectangle& part) const noexcept {
    if (uv.width <= 0.0f || uv.height <= 0.0f) {
        return Rectangle{};
    }
    const float scaleX = drawn.width / uv.width;
    const float scaleY = drawn.height / uv.height;
    return Rectangle{.x = drawn.x + (part.x - uv.x) * scaleX, .y = drawn.y + (part.y - uv.y) * scaleY, .width = part.width * scaleX, .height = part.height * scaleY};
}

Glass magnifierFor(const CameraGeometry& whole, const CameraGeometry& framed, bool regionGiven, const Rectangle& screen) noexcept {
    const float uvRatio = framed.frame.width > 0.0f ? whole.frame.width / framed.frame.width : 1.0f;
    const float ratio   = regionGiven && whole.drawn.width > 0.0f ? uvRatio * whole.box.width / whole.drawn.width : uvRatio;
    const float centreX = whole.drawn.x + whole.drawn.width * 0.5f;
    const float centreY = whole.drawn.y + whole.drawn.height * 0.5f;
    const float aimedX  = whole.drawn.x + (framed.frame.x + framed.frame.width * 0.5f - whole.frame.x) / whole.frame.width * whole.drawn.width;
    const float aimedY  = whole.drawn.y + (framed.frame.y + framed.frame.height * 0.5f - whole.frame.y) / whole.frame.height * whole.drawn.height;
    // snapped to exactly the identity, or a stop would differ from an image-scope one by a pixel row
    constexpr float kIdentityNoise = 1e-3f;
    if (std::abs(ratio - 1.0f) < kIdentityNoise && std::abs(aimedX - centreX) < kIdentityNoise && std::abs(aimedY - centreY) < kIdentityNoise) {
        return Glass{};
    }
    const float offsetX = centreX - aimedX * ratio;
    const float offsetY = centreY - aimedY * ratio;
    const auto  kept    = [](float wanted, float from, float extent, float drawn) { return drawn >= extent ? std::clamp(wanted, from + extent - drawn, from) : wanted; };
    const float shiftX  = kept(whole.drawn.x * ratio + offsetX, screen.x, screen.width, whole.drawn.width * ratio) - (whole.drawn.x * ratio + offsetX);
    const float shiftY  = kept(whole.drawn.y * ratio + offsetY, screen.y, screen.height, whole.drawn.height * ratio) - (whole.drawn.y * ratio + offsetY);
    return Glass{.factor = ratio, .offsetX = offsetX + shiftX, .offsetY = offsetY + shiftY};
}

} // namespace gr::present
