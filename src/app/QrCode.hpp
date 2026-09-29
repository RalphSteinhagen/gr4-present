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

/// encodes `text` as a QR code about `pixels` across, always dark on light as not every scanner reads inverted codes
/// encoder: Nayuki's qrcodegen, vendored under `third_party/qrcodegen`; its exception on oversize text is caught here
[[nodiscard]] std::expected<RasterImage, std::string> renderQr(std::string_view text, int pixels);

struct PlacedQrCode {
    Texture     texture;
    std::string problem; // when set, nothing could be encoded and this says why
};

/// uploads a code once and keeps it
struct QrCache {
    [[nodiscard]] const PlacedQrCode& get(std::string_view text, float pixels);

private:
    std::map<std::pair<std::string, int>, PlacedQrCode, std::less<>> _encoded;
};

} // namespace gr::present

#endif // GR4_PRESENT_QR_CODE_HPP
