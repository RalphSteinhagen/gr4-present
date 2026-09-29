#ifndef GR4_PRESENT_QR_CODE_HPP
#define GR4_PRESENT_QR_CODE_HPP

#include "SvgImage.hpp"
#include "Texture.hpp"

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace gr::present {

/// the quiet zone ISO/IEC 18004 requires around a symbol; without it many scanners will not lock on
inline constexpr int kQrQuietModules = 4;

/**
 * Encodes `text` as a QR code, drawn dark on light at about `pixels` across.
 *
 * Dark on light whatever the slide's scheme is. Inverting the modules is legal and many scanners cope, but not
 * all of them do, and a code nobody in the room can scan is worse than one that looks out of place.
 *
 * The encoder is Nayuki's, vendored under `third_party/qrcodegen`. It throws when the text will not fit the
 * largest version, which is caught here: this project's own code must not throw.
 */
[[nodiscard]] std::expected<RasterImage, std::string> renderQr(std::string_view text, int pixels);

struct PlacedQrCode {
    Texture     texture;
    std::string problem; // when set, nothing could be encoded and this says why
};

/// uploads a code once and keeps it, as the figure and formula caches do
struct QrCache {
    [[nodiscard]] const PlacedQrCode& get(std::string_view text, float pixels);

private:
    std::map<std::pair<std::string, int>, PlacedQrCode, std::less<>> _encoded;
};

} // namespace gr::present

#endif // GR4_PRESENT_QR_CODE_HPP
