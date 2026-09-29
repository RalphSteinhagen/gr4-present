#include "ZoomPan.hpp"

#include <algorithm>

namespace gr::present {

namespace {
// the offset that keeps the slide covering the viewport lies between "slide's far edge at the viewport's far edge" and 0
float clampedOffset(float offset, float scale, float viewportExtent) noexcept { return std::clamp(offset, viewportExtent * (1.0f - scale), 0.0f); }
} // namespace

void ZoomPan::zoomAt(float focusX, float focusY, float factor, float viewportWidth, float viewportHeight) noexcept {
    const float wanted = std::clamp(scale * factor, kMinimumScale, kMaximumScale);
    const float ratio  = wanted / scale;
    offsetX            = focusX - (focusX - offsetX) * ratio;
    offsetY            = focusY - (focusY - offsetY) * ratio;
    scale              = wanted;
    panBy(0.0f, 0.0f, viewportWidth, viewportHeight);
}

void ZoomPan::panBy(float dx, float dy, float viewportWidth, float viewportHeight) noexcept {
    offsetX = clampedOffset(offsetX + dx, scale, viewportWidth);
    offsetY = clampedOffset(offsetY + dy, scale, viewportHeight);
}

} // namespace gr::present
