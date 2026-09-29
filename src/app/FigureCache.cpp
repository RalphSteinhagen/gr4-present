#include "FigureCache.hpp"

#include "SvgImage.hpp"

#include <gr4-present/export/PageRecording.hpp>

#include <imgui.h>

#include <algorithm>
#include <cfloat>

namespace gr::present {

namespace {
constexpr std::uint32_t kWidthQuantum = 64U; // rasters are cached per this many pixels of width, not per pixel
} // namespace

const Texture* FigureCache::get(std::string_view reference, std::uint32_t targetWidth) { return get(reference, targetWidth, {}); }

const Texture* FigureCache::get(std::string_view reference, std::uint32_t targetWidth, std::span<const std::string> hiddenIds) {
    // Masked and unmasked rasters of one file are different pictures, so they cannot share an entry -- and neither
    // can two rasters of one drawing at different sizes. Keying on the reference alone left a master at whatever
    // width it was first drawn at, so rotating a tablet left every drawing soft. The width is rounded, or a window
    // dragged by a pixel would rasterise everything again.
    const std::uint32_t rounded = isSvgReference(reference) ? (std::max(targetWidth, 1U) + kWidthQuantum / 2U) / kWidthQuantum * kWidthQuantum : 0U;
    const std::string   key     = std::string{reference} + "\x1f" + std::to_string(rounded) + (hiddenIds.empty() ? "" : "\x1fmasked");

    const auto showing = [this](const Frames& frames) -> const Texture* {
        if (frames.textures.empty()) {
            return nullptr;
        }
        const Texture& texture = frames.textures[frameAt(frames.delaysMs, clock)];
        return texture.id == 0 ? nullptr : &texture;
    };

    if (const auto cached = _uploaded.find(key); cached != _uploaded.end()) {
        return showing(cached->second);
    }

    const std::span<const std::uint8_t> bytes = bytesFor ? bytesFor(reference) : std::span<const std::uint8_t>{};
    if (bytes.empty()) {
        return nullptr; // not fetched yet, or unreadable; either way it is not this cache's business to retry
    }

    Frames frames;
    if (isSvgReference(reference)) {
        if (const auto raster = rasteriseSvg(bytes, std::max(targetWidth, 1U), hiddenIds)) {
            frames.textures.push_back(Texture::loadRgba(std::span<const unsigned char>{raster->rgba}, static_cast<int>(raster->width), static_cast<int>(raster->height)));
            frames.delaysMs.push_back(0);
        }
    } else if (const auto decoded = decodeAnimation(bytes); decoded.has_value()) {
        for (const std::vector<std::uint8_t>& frame : decoded->frames) {
            frames.textures.push_back(Texture::loadRgba(std::span<const unsigned char>{frame}, static_cast<int>(decoded->width), static_cast<int>(decoded->height)));
        }
        frames.delaysMs = decoded->delaysMs;
    }
    if (frames.textures.empty()) {
        frames.textures.emplace_back(); // an empty texture, so the failure is remembered rather than retried
        frames.delaysMs.push_back(0);
    }
    return showing(_uploaded.emplace(key, std::move(frames)).first->second);
}

std::span<const SvgLink> FigureCache::links(std::string_view reference) {
    if (const auto found = _links.find(reference); found != _links.end()) {
        return found->second;
    }
    const std::span<const std::uint8_t> bytes = isSvgReference(reference) && bytesFor ? bytesFor(reference) : std::span<const std::uint8_t>{};
    if (bytes.empty()) {
        return {}; // not fetched yet, or not a drawing: asked again next frame rather than remembered as linkless
    }
    return _links.emplace(std::string{reference}, linksOfSvg(bytes)).first->second;
}

std::span<const DrawnLabel> FigureCache::labels(std::string_view reference, std::span<const std::string> hiddenIds) {
    std::string key{reference};
    for (const std::string& id : hiddenIds) {
        key += "\x1f" + id;
    }
    if (const auto found = _labels.find(key); found != _labels.end()) {
        return found->second;
    }
    const std::span<const std::uint8_t> bytes = isSvgReference(reference) && bytesFor ? bytesFor(reference) : std::span<const std::uint8_t>{};
    if (bytes.empty()) {
        return {}; // not fetched yet, or not a drawing
    }
    std::vector<DrawnLabel> found;
    if (const auto drawing = outlineSvg(bytes, hiddenIds); drawing.has_value() && drawing->width > 0.0f && drawing->height > 0.0f) {
        // measured in the deck's own face, which is what the drawing's text falls back to when it names no other
        ImFont* const font = ImGui::GetFont();
        for (const DrawingText& text : drawing->texts) {
            const float wide  = font->CalcTextSizeA(text.em, FLT_MAX, 0.0f, text.text.data(), text.text.data() + text.text.size()).x;
            const float start = text.anchor == DrawingText::Anchor::middle ? text.x - wide * 0.5f : (text.anchor == DrawingText::Anchor::end ? text.x - wide : text.x);
            found.push_back(DrawnLabel{.text = text.text, .area = Rectangle{.x = start / drawing->width, .y = (text.y - text.em * 0.8f) / drawing->height, .width = wide / drawing->width, .height = text.em / drawing->height}});
        }
    }
    return _labels.emplace(std::move(key), std::move(found)).first->second;
}

} // namespace gr::present
