#include "SvgImage.hpp"

#include "BuiltinFace.hpp"

#include <gr4-present/Number.hpp>

#include <gr4-present/export/PageRecording.hpp>

#include <lunasvg.h>
#include <plutovg.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include <gnuradio-4.0/TriggerMatcher.hpp> // trim

namespace gr::present {

namespace {

/**
 * lunasvg draws a `<text>` element only if it can find a face, and silently draws nothing when it cannot. It does
 * load the system fonts, which is why a drawing keeps its labels natively -- and a browser has no system fonts, so
 * the demo deck's signal chain arrived there with its boxes and arrows and no labels at all.
 *
 * Registering with an empty family makes a face the fallback for every family an author might name, so a drawing
 * keeps its text whatever font it asks for. The faces are the ones already in the binary for the user interface,
 * so this costs nothing further, and they are static, so lunasvg is given no destructor to call.
 */
bool registerFallbackFonts() {
    static const bool registered = [] {
        const auto add = [](BuiltinFace builtin, bool bold, bool italic) {
            const std::span<const std::uint8_t> face = builtinFaceTtf(builtin);
            return lunasvg_add_font_face_from_data("", bold, italic, face.data(), face.size(), nullptr, nullptr);
        };
        return add(BuiltinFace::body, false, false) && add(BuiltinFace::bold, true, false) && add(BuiltinFace::italic, false, true) && add(BuiltinFace::boldItalic, true, true);
    }();
    return registered;
}

[[nodiscard]] std::optional<RasterImage> rasterise(const std::unique_ptr<lunasvg::Document>& document, std::uint32_t targetWidth) {
    static_cast<void>(registerFallbackFonts());
    if (!document) {
        return std::nullopt;
    }

    const float naturalWidth  = document->width();
    const float naturalHeight = document->height();
    if (naturalWidth <= 0.0f || naturalHeight <= 0.0f) {
        return std::nullopt;
    }

    // rasterised at the size it will be drawn at, because scaling a bitmap is what using SVG is meant to avoid
    const std::uint32_t width  = targetWidth;
    const std::uint32_t height = static_cast<std::uint32_t>(static_cast<float>(width) * naturalHeight / naturalWidth + 0.5f);
    lunasvg::Bitmap     bitmap = document->renderToBitmap(static_cast<int>(width), static_cast<int>(height));
    if (bitmap.isNull()) {
        return std::nullopt;
    }
    bitmap.convertToRGBA();

    RasterImage image{.width = width, .height = height, .rgba = {}};
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4UZ);
    for (std::uint32_t row = 0U; row < height; ++row) {
        std::memcpy(image.rgba.data() + static_cast<std::size_t>(row) * width * 4UZ, bitmap.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.stride()), static_cast<std::size_t>(width) * 4UZ);
    }
    return image;
}

} // namespace

bool svgTextHasAFont() { return registerFallbackFonts(); }

std::optional<RasterImage> rasteriseSvg(std::span<const std::uint8_t> source, std::uint32_t targetWidth) { return rasterise(lunasvg::Document::loadFromData(reinterpret_cast<const char*>(source.data()), source.size()), targetWidth); }

std::optional<RasterImage> rasteriseSvg(std::span<const std::uint8_t> source, std::uint32_t targetWidth, std::span<const std::string> hiddenIds) {
    auto document = lunasvg::Document::loadFromData(reinterpret_cast<const char*>(source.data()), source.size());
    if (!document) {
        return std::nullopt;
    }
    for (const std::string& id : hiddenIds) {
        // the boxes define where content goes; drawing them as well would put a visible frame behind every placement
        if (auto element = document->getElementById(id); !element.isNull()) {
            element.setAttribute("display", "none");
        }
    }
    return rasterise(document, targetWidth);
}

namespace {

/// `name="value"` pairs of one tag, with `style` declarations folded in as if they were attributes
[[nodiscard]] std::vector<std::pair<std::string, std::string>> attributesOf(std::string_view tag) {
    std::vector<std::pair<std::string, std::string>> attributes;
    std::size_t                                      at = tag.find_first_of(" \t\r\n");
    while (at != std::string_view::npos && at < tag.size()) {
        const std::size_t nameStart = tag.find_first_not_of(" \t\r\n/", at);
        const std::size_t equals    = tag.find('=', nameStart);
        if (nameStart == std::string_view::npos || equals == std::string_view::npos) {
            break;
        }
        const std::size_t open = tag.find_first_of("\"'", equals);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t close = tag.find(tag[open], open + 1UZ);
        if (close == std::string_view::npos) {
            break;
        }
        std::string name{tag.substr(nameStart, equals - nameStart)};
        std::erase_if(name, [](char letter) { return letter == ' ' || letter == '\t' || letter == '\n' || letter == '\r'; });
        std::string value{tag.substr(open + 1UZ, close - open - 1UZ)};
        if (name == "style") {
            for (const auto declaration : value | std::views::split(';')) {
                const std::string_view text{declaration.begin(), declaration.end()};
                const std::size_t      colon = text.find(':');
                if (colon == std::string_view::npos) {
                    continue;
                }
                attributes.emplace_back(gr::trigger::detail::trim(text.substr(0UZ, colon)), gr::trigger::detail::trim(text.substr(colon + 1UZ)));
            }
        } else {
            attributes.emplace_back(std::move(name), std::move(value));
        }
        at = close + 1UZ;
    }
    return attributes;
}

} // namespace

std::vector<SvgElementName> svgElementNames(std::string_view source) {
    std::vector<SvgElementName> names;
    // lunasvg keeps only the attributes it renders, so the `data-` ones an author marks a region with are read here
    for (std::size_t at = source.find('<'); at != std::string_view::npos; at = source.find('<', at + 1UZ)) {
        const std::size_t tagEnd     = source.find('>', at);
        const auto        attributes = attributesOf(source.substr(at, tagEnd == std::string_view::npos ? std::string_view::npos : tagEnd - at));
        const auto        valueOf    = [&attributes](std::string_view name) {
            const auto found = std::ranges::find(attributes, name, &std::pair<std::string, std::string>::first);
            return found == attributes.end() ? std::string{} : found->second;
        };
        if (std::string id = valueOf("id"); !id.empty()) {
            names.push_back(SvgElementName{.id = std::move(id), .kind = valueOf("data-present"), .step = parseNumber<int>(valueOf("data-step")).value_or(0)});
        }
    }
    return names;
}

std::vector<std::string> hiddenAt(const Layout& layout, int step) {
    const auto concealed = [step](const Area& area) { return !area.kind.empty() || area.step > step; };
    return layout.areas | std::views::filter(concealed) | std::views::transform(&Area::id) | std::ranges::to<std::vector>();
}

std::optional<Layout> layoutOfSvg(std::span<const std::uint8_t> source) {
    const std::string_view text{reinterpret_cast<const char*>(source.data()), source.size()};
    auto                   document = lunasvg::Document::loadFromData(text.data(), text.size());
    if (!document) {
        return std::nullopt;
    }

    Layout layout{.width = document->width(), .height = document->height(), .areas = {}};
    for (const SvgElementName& named : svgElementNames(text)) {
        auto element = document->getElementById(named.id);
        if (element.isNull()) {
            continue;
        }
        // after transforms, so this is where the box actually sits; it includes the stroke the author drew it with
        const lunasvg::Box box = element.getGlobalBoundingBox();
        layout.areas.push_back(Area{.id = named.id, .kind = named.kind, .step = named.step, .x = box.x, .y = box.y, .width = box.w, .height = box.h});
    }
    return layout;
}

std::vector<SvgLink> linksOfSvg(std::span<const std::uint8_t> source) {
    const auto document = lunasvg::Document::loadFromData(reinterpret_cast<const char*>(source.data()), source.size());
    if (!document || document->width() <= 0.0f || document->height() <= 0.0f) {
        return {};
    }
    std::vector<SvgLink> links;
    // lunasvg parses `<a>` as a group, and a group of its own has no href, so this is exactly the links
    for (const lunasvg::Element& link : document->querySelectorAll("g[href]")) {
        const lunasvg::Box box = link.getGlobalBoundingBox();
        links.push_back(SvgLink{.target = link.getAttribute("href"), .area = Rectangle{.x = box.x / document->width(), .y = box.y / document->height(), .width = box.w / document->width(), .height = box.h / document->height()}});
    }
    return links;
}

namespace {

/// the paint, line and type an element inherits from the elements around it, as SVG's presentation attributes say
struct SvgStyle {
    plutovg_matrix_t    matrix{};
    std::uint32_t       fill          = 0xFF000000U; // 0xAABBGGRR; black, SVG's default fill
    std::uint32_t       stroke        = 0U;          // none
    float               strokeWidth   = 1.0f;
    float               opacity       = 1.0f; // the element's own, multiplied down the tree
    float               fillOpacity   = 1.0f;
    float               strokeOpacity = 1.0f;
    bool                evenOdd       = false;
    float               fontSize      = 16.0f;
    RecordedFace        face          = RecordedFace::body;
    bool                bold          = false;
    bool                italic        = false;
    DrawingText::Anchor anchor        = DrawingText::Anchor::start;
    bool                skipped       = false; // hidden, or not drawn at all: a definition, a title
};

[[nodiscard]] float numberOf(std::string_view text, float fallback = 0.0f) {
    float      value = fallback;
    const auto first = text.find_first_not_of(" \t,");
    if (first == std::string_view::npos) {
        return fallback;
    }
    std::from_chars(text.data() + first, text.data() + text.size(), value);
    return value;
}

/// a colour as SVG writes it, 0xAABBGGRR; `none` is no paint, and a gradient or pattern is not one this can draw
[[nodiscard]] std::expected<std::uint32_t, std::string> paintOf(std::string_view text) {
    if (text == "none" || text == "transparent") {
        return 0U;
    }
    if (text.starts_with("url(")) {
        return std::unexpected(std::format("a paint server, {}", text));
    }
    if (text == "currentColor") {
        return 0xFF000000U;
    }
    plutovg_color_t colour{};
    if (plutovg_color_parse(&colour, text.data(), static_cast<int>(text.size())) <= 0) {
        return 0xFF000000U;
    }
    const auto channel = [](float value) { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
    return (channel(colour.a) << 24U) | (channel(colour.b) << 16U) | (channel(colour.g) << 8U) | channel(colour.r);
}

[[nodiscard]] std::uint32_t withOpacity(std::uint32_t colour, float opacity) {
    const float alpha = static_cast<float>(colour >> 24U) * std::clamp(opacity, 0.0f, 1.0f);
    return (colour & 0x00FFFFFFU) | (static_cast<std::uint32_t>(std::lround(alpha)) << 24U);
}

/// one shape of the drawing, as its outline in the drawing's coordinates, in the paint its style says
void addShape(VectorDrawing& drawing, plutovg_path_t* path, const SvgStyle& style) {
    plutovg_path_transform(path, &style.matrix);
    VectorPath              outline{.steps = {},
                     .points               = {},
                     .fill                 = withOpacity(style.fill, style.opacity * style.fillOpacity),
                     .stroke               = withOpacity(style.stroke, style.opacity * style.strokeOpacity), //
                     .strokeWidth          = style.strokeWidth * std::sqrt(std::abs(style.matrix.a * style.matrix.d - style.matrix.b * style.matrix.c)),
                     .evenOdd              = style.evenOdd};
    plutovg_path_iterator_t it{};
    plutovg_path_iterator_init(&it, path);
    plutovg_point_t points[3];
    while (plutovg_path_iterator_has_next(&it)) {
        switch (plutovg_path_iterator_next(&it, points)) {
        case PLUTOVG_PATH_COMMAND_MOVE_TO: outline.steps.push_back(VectorPath::Step::move), outline.points.insert(outline.points.end(), {points[0].x, points[0].y}); break;
        case PLUTOVG_PATH_COMMAND_LINE_TO: outline.steps.push_back(VectorPath::Step::line), outline.points.insert(outline.points.end(), {points[0].x, points[0].y}); break;
        case PLUTOVG_PATH_COMMAND_CUBIC_TO: outline.steps.push_back(VectorPath::Step::cubic), outline.points.insert(outline.points.end(), {points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y}); break;
        case PLUTOVG_PATH_COMMAND_CLOSE: outline.steps.push_back(VectorPath::Step::close); break;
        }
    }
    if (!outline.steps.empty() && ((outline.fill >> 24U) != 0U || (outline.stroke >> 24U) != 0U)) {
        drawing.paths.push_back(std::move(outline));
    }
}

/// &amp; and its kin, as an SVG's character data writes them
[[nodiscard]] std::string decoded(std::string_view text) {
    std::string plain;
    for (std::size_t at = 0UZ; at < text.size(); ++at) {
        if (text[at] != '&') {
            plain += text[at] == '\n' || text[at] == '\t' || text[at] == '\r' ? ' ' : text[at];
            continue;
        }
        const std::size_t end = text.find(';', at);
        if (end == std::string_view::npos) {
            plain += text[at];
            continue;
        }
        const std::string_view entity = text.substr(at + 1UZ, end - at - 1UZ);
        if (entity == "amp") {
            plain += '&';
        } else if (entity == "lt") {
            plain += '<';
        } else if (entity == "gt") {
            plain += '>';
        } else if (entity == "quot") {
            plain += '"';
        } else if (entity == "apos") {
            plain += '\'';
        } else if (entity.starts_with('#')) {
            const bool    hex  = entity.size() > 1UZ && (entity[1] == 'x' || entity[1] == 'X');
            std::uint32_t code = 0U;
            std::from_chars(entity.data() + (hex ? 2 : 1), entity.data() + entity.size(), code, hex ? 16 : 10);
            // UTF-8, so a numeric reference reads as the character it names
            if (code < 0x80U) {
                plain += static_cast<char>(code);
            } else if (code < 0x800U) {
                plain += static_cast<char>(0xC0U | (code >> 6U)), plain += static_cast<char>(0x80U | (code & 0x3FU));
            } else if (code < 0x10000U) {
                plain += static_cast<char>(0xE0U | (code >> 12U)), plain += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)), plain += static_cast<char>(0x80U | (code & 0x3FU));
            } else if (code <= 0x10FFFFU) {
                plain += static_cast<char>(0xF0U | (code >> 18U)), plain += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)), plain += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)), plain += static_cast<char>(0x80U | (code & 0x3FU));
            }
        }
        at = end;
    }
    return plain;
}

} // namespace

std::expected<VectorDrawing, std::string> outlineSvg(std::span<const std::uint8_t> source, std::span<const std::string> hiddenIds) {
    const std::string_view text{reinterpret_cast<const char*>(source.data()), source.size()};
    const auto             document = lunasvg::Document::loadFromData(text.data(), text.size());
    if (!document) {
        return std::unexpected(std::string{"not a drawing lunasvg can read"});
    }
    VectorDrawing drawing{.width = document->width(), .height = document->height(), .paths = {}, .texts = {}};

    // Drawn elements are what an export can make vectors of; anything else either draws nothing (a title, metadata)
    // or draws something this does not reproduce, and then the picture is better shown as the picture it is.
    constexpr std::array kShapes{std::string_view{"rect"}, std::string_view{"circle"}, std::string_view{"ellipse"}, std::string_view{"line"}, std::string_view{"polyline"}, std::string_view{"polygon"}, std::string_view{"path"}};
    constexpr std::array kSilent{std::string_view{"title"}, std::string_view{"desc"}, std::string_view{"metadata"}, std::string_view{"sodipodi:namedview"}, std::string_view{"defs"}, std::string_view{"style"}};
    constexpr std::array kContainers{std::string_view{"svg"}, std::string_view{"g"}, std::string_view{"a"}, std::string_view{"text"}, std::string_view{"tspan"}, std::string_view{"switch"}};

    std::vector<SvgStyle> styles(1UZ);
    plutovg_matrix_init_identity(&styles.front().matrix);
    std::vector<std::string>   open;    // the element names the scanner is inside
    std::optional<DrawingText> pending; // a `<text>` collecting its characters
    bool                       rootSeen = false;

    for (std::size_t at = 0UZ; at < text.size();) {
        const std::size_t lt = text.find('<', at);
        if (pending.has_value() && lt != at) {
            pending->text += decoded(text.substr(at, (lt == std::string_view::npos ? text.size() : lt) - at));
        }
        if (lt == std::string_view::npos) {
            break;
        }
        if (text.substr(lt).starts_with("<!--")) {
            const std::size_t end = text.find("-->", lt);
            at                    = end == std::string_view::npos ? text.size() : end + 3UZ;
            continue;
        }
        if (text.substr(lt).starts_with("<?") || text.substr(lt).starts_with("<!")) {
            const std::size_t end = text.find('>', lt);
            at                    = end == std::string_view::npos ? text.size() : end + 1UZ;
            continue;
        }
        const std::size_t gt = text.find('>', lt);
        if (gt == std::string_view::npos) {
            break;
        }
        at                         = gt + 1UZ;
        const std::string_view tag = text.substr(lt + 1UZ, gt - lt - 1UZ);
        if (tag.starts_with('/')) { // a closing tag: back to the style around it
            if (!open.empty()) {
                if (open.back() == "text" && pending.has_value()) {
                    const std::size_t first = pending->text.find_first_not_of(' ');
                    const std::size_t last  = pending->text.find_last_not_of(' ');
                    pending->text           = first == std::string::npos ? std::string{} : pending->text.substr(first, last - first + 1UZ);
                    if (!pending->text.empty()) {
                        drawing.texts.push_back(std::move(*pending));
                    }
                    pending.reset();
                }
                open.pop_back();
                styles.pop_back();
            }
            continue;
        }
        const bool        selfClosing = tag.ends_with('/');
        const std::size_t nameEnd     = tag.find_first_of(" \t\r\n/");
        const std::string name{tag.substr(0UZ, nameEnd)};
        const auto        attributes = attributesOf(selfClosing ? tag.substr(0UZ, tag.size() - 1UZ) : tag);
        const auto        valueOf    = [&attributes](std::string_view key) -> std::optional<std::string_view> {
            const auto found = std::ranges::find(attributes, key, &std::pair<std::string, std::string>::first);
            return found == attributes.end() ? std::nullopt : std::optional<std::string_view>{found->second};
        };

        SvgStyle   style  = styles.back();
        const bool silent = std::ranges::contains(kSilent, name);
        const bool known  = silent || std::ranges::contains(kShapes, name) || std::ranges::contains(kContainers, name);
        if (!known && !style.skipped) {
            return std::unexpected(std::format("it uses <{}>", name));
        }
        for (const std::string_view effect : {std::string_view{"clip-path"}, std::string_view{"mask"}, std::string_view{"filter"}, std::string_view{"marker-start"}, std::string_view{"marker-mid"}, std::string_view{"marker-end"}}) {
            if (const auto used = valueOf(effect); used.has_value() && *used != "none" && !style.skipped) {
                return std::unexpected(std::format("it uses {}", effect));
            }
        }

        if (name == "svg" && !rootSeen) {
            // the viewBox is mapped onto the drawing's width and height, which is what the texture was rasterised from
            rootSeen = true;
            if (const auto box = valueOf("viewBox"); box.has_value()) {
                std::array<float, 4> numbers{};
                std::string_view     rest = *box;
                for (float& number : numbers) {
                    const auto start = rest.find_first_not_of(" ,\t\r\n");
                    if (start == std::string_view::npos) {
                        break;
                    }
                    rest              = rest.substr(start);
                    const auto [p, e] = std::from_chars(rest.data(), rest.data() + rest.size(), number);
                    rest              = rest.substr(static_cast<std::size_t>(p - rest.data()));
                }
                if (numbers[2] > 0.0f && numbers[3] > 0.0f) {
                    plutovg_matrix_init_scale(&style.matrix, drawing.width / numbers[2], drawing.height / numbers[3]);
                    plutovg_matrix_translate(&style.matrix, -numbers[0], -numbers[1]);
                }
            }
        }
        if (const auto transform = valueOf("transform"); transform.has_value()) {
            plutovg_matrix_t local{};
            if (plutovg_matrix_parse(&local, transform->data(), static_cast<int>(transform->size()))) {
                plutovg_matrix_t combined{};
                plutovg_matrix_multiply(&combined, &local, &style.matrix);
                style.matrix = combined;
            }
        }
        const auto id = valueOf("id");
        style.skipped = style.skipped || silent || (id.has_value() && std::ranges::contains(hiddenIds, *id)) || valueOf("display") == std::optional<std::string_view>{"none"} || valueOf("visibility") == std::optional<std::string_view>{"hidden"};
        for (const auto& [key, value] : attributes) {
            if (key == "fill" || key == "stroke") {
                const auto paint = paintOf(value);
                if (!paint.has_value()) {
                    if (style.skipped) {
                        continue;
                    }
                    return std::unexpected(std::format("it fills or strokes with {}", paint.error()));
                }
                (key == "fill" ? style.fill : style.stroke) = *paint;
            } else if (key == "stroke-width") {
                style.strokeWidth = numberOf(value, 1.0f);
            } else if (key == "opacity") {
                style.opacity *= numberOf(value, 1.0f);
            } else if (key == "fill-opacity") {
                style.fillOpacity = numberOf(value, 1.0f);
            } else if (key == "stroke-opacity") {
                style.strokeOpacity = numberOf(value, 1.0f);
            } else if (key == "fill-rule") {
                style.evenOdd = value == "evenodd";
            } else if (key == "font-size") {
                style.fontSize = numberOf(value, style.fontSize);
            } else if (key == "font-weight") {
                style.bold = value == "bold" || value == "bolder" || numberOf(value, 400.0f) >= 600.0f;
            } else if (key == "font-style") {
                style.italic = value == "italic" || value == "oblique";
            } else if (key == "font-family") {
                const bool mono = value.contains("mono") || value.contains("Mono") || value.contains("Cousine") || value.contains("Courier");
                style.face      = mono ? RecordedFace::mono : RecordedFace::body;
            } else if (key == "text-anchor") {
                style.anchor = value == "middle" ? DrawingText::Anchor::middle : (value == "end" ? DrawingText::Anchor::end : DrawingText::Anchor::start);
            }
        }

        if (!style.skipped && std::ranges::contains(kShapes, name)) {
            plutovg_path_t* path   = plutovg_path_create();
            const auto      number = [&valueOf](std::string_view key) { return numberOf(valueOf(key).value_or(""), 0.0f); };
            if (name == "rect") {
                float rx = number("rx");
                float ry = number("ry");
                rx       = rx > 0.0f ? rx : ry;
                ry       = ry > 0.0f ? ry : rx;
                plutovg_path_add_round_rect(path, number("x"), number("y"), number("width"), number("height"), rx, ry);
            } else if (name == "circle") {
                plutovg_path_add_circle(path, number("cx"), number("cy"), number("r"));
            } else if (name == "ellipse") {
                plutovg_path_add_ellipse(path, number("cx"), number("cy"), number("rx"), number("ry"));
            } else if (name == "line") {
                plutovg_path_move_to(path, number("x1"), number("y1"));
                plutovg_path_line_to(path, number("x2"), number("y2"));
            } else if (name == "path") {
                const std::string_view data = valueOf("d").value_or("");
                plutovg_path_parse(path, data.data(), static_cast<int>(data.size()));
            } else { // polyline, polygon
                std::string_view rest  = valueOf("points").value_or("");
                bool             first = true;
                for (;;) {
                    float      x = 0.0f, y = 0.0f;
                    const auto startX = rest.find_first_not_of(" ,\t\r\n");
                    if (startX == std::string_view::npos) {
                        break;
                    }
                    const auto [px, ex] = std::from_chars(rest.data() + startX, rest.data() + rest.size(), x);
                    rest                = rest.substr(static_cast<std::size_t>(px - rest.data()));
                    const auto startY   = rest.find_first_not_of(" ,\t\r\n");
                    if (ex != std::errc{} || startY == std::string_view::npos) {
                        break;
                    }
                    const auto [py, ey] = std::from_chars(rest.data() + startY, rest.data() + rest.size(), y);
                    rest                = rest.substr(static_cast<std::size_t>(py - rest.data()));
                    if (ey != std::errc{}) {
                        break;
                    }
                    first ? plutovg_path_move_to(path, x, y) : plutovg_path_line_to(path, x, y);
                    first = false;
                }
                if (name == "polygon") {
                    plutovg_path_close(path);
                }
            }
            SvgStyle painted = style;
            if (name == "line" || name == "polyline") {
                painted.fill = name == "line" ? 0U : painted.fill; // a line has no inside; an open polyline is filled as if closed, as SVG does
            }
            addShape(drawing, path, painted);
            plutovg_path_destroy(path);
        }
        if (name == "text" && !style.skipped) {
            plutovg_point_t anchorAt{numberOf(valueOf("x").value_or(""), 0.0f), numberOf(valueOf("y").value_or(""), 0.0f)};
            plutovg_point_t mapped{};
            plutovg_matrix_map_point(&style.matrix, &anchorAt, &mapped);
            const RecordedFace face = style.face == RecordedFace::mono ? RecordedFace::mono : (style.bold && style.italic ? RecordedFace::boldItalic : (style.bold ? RecordedFace::bold : (style.italic ? RecordedFace::italic : RecordedFace::body)));
            pending                 = DrawingText{.text = {}, .face = face, .em = style.fontSize * std::sqrt(std::abs(style.matrix.a * style.matrix.d - style.matrix.b * style.matrix.c)), .x = mapped.x, .y = mapped.y, .anchor = style.anchor, .colour = withOpacity(style.fill, style.opacity * style.fillOpacity)};
        }
        if (!selfClosing) {
            open.push_back(name);
            styles.push_back(style);
        }
    }
    return drawing;
}

bool isSvgReference(std::string_view reference) noexcept { return reference.ends_with(".svg") || reference.ends_with(".SVG"); }

} // namespace gr::present
