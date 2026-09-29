#include "QrCode.hpp"

#include <qrcodegen.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>
#include <vector>

namespace gr::present {

std::expected<RasterImage, std::string> renderQr(std::string_view text, int pixels) {
    if (text.empty()) {
        return std::unexpected(std::string{"a qr directive needs a `url:`"});
    }
    try {
        // medium error correction: the usual choice for a code on a slide, which is never damaged but is often
        // photographed at an angle from the back of a room
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(std::string{text}.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

        const int modules = code.getSize() + 2 * kQrQuietModules;
        const int scale   = std::max(1, pixels / modules);
        const int side    = modules * scale;

        RasterImage image{.width = static_cast<std::uint32_t>(side), .height = static_cast<std::uint32_t>(side), .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4UZ, 0xFFU)};
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                if (!code.getModule(x / scale - kQrQuietModules, y / scale - kQrQuietModules)) {
                    continue;
                }
                const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(side) + static_cast<std::size_t>(x)) * 4UZ;
                image.rgba[offset]       = 0x00U;
                image.rgba[offset + 1UZ] = 0x00U;
                image.rgba[offset + 2UZ] = 0x00U;
            }
        }
        return image;
    } catch (const std::exception& failure) {
        return std::unexpected(std::string{failure.what()});
    } catch (...) {
        return std::unexpected(std::string{"this text cannot be encoded as a QR code"});
    }
}

} // namespace gr::present
