#ifndef GR4_PRESENT_ZOOM_PAN_HPP
#define GR4_PRESENT_ZOOM_PAN_HPP

namespace gr::present {

/**
 * How far into the slide the audience is looking: a uniform scale and the screen position of the slide's origin.
 *
 * A slide point `p` is shown at `offset + scale * p`. The slide always covers the whole viewport, so zooming out
 * to 1 brings back the untouched slide and panning never reveals the empty edge beyond it.
 */
struct ZoomPan {
    static constexpr float kMinimumScale = 1.0f;
    static constexpr float kMaximumScale = 4.0f;

    float scale   = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    [[nodiscard]] bool active() const noexcept { return scale > kMinimumScale; }

    /// multiplies the scale by `factor`, keeping the slide point under (focusX, focusY) where it is on screen
    void zoomAt(float focusX, float focusY, float factor, float viewportWidth, float viewportHeight) noexcept;

    void panBy(float dx, float dy, float viewportWidth, float viewportHeight) noexcept;

    void reset() noexcept { *this = ZoomPan{}; }

    [[nodiscard]] float screenX(float slideX) const noexcept { return offsetX + scale * slideX; }
    [[nodiscard]] float screenY(float slideY) const noexcept { return offsetY + scale * slideY; }
    [[nodiscard]] float slideX(float screenX) const noexcept { return (screenX - offsetX) / scale; }
    [[nodiscard]] float slideY(float screenY) const noexcept { return (screenY - offsetY) / scale; }
};

} // namespace gr::present

#endif // GR4_PRESENT_ZOOM_PAN_HPP
