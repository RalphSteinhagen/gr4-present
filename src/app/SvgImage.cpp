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
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include <gnuradio-4.0/TriggerMatcher.hpp> // trim

namespace gr::present {

namespace {

// lunasvg silently draws no `<text>` without a face and a browser has no system fonts; an empty family registers the
// built-in faces as the fallback for every family, and they are static so lunasvg needs no destructor
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

void hide(lunasvg::Document& document, std::span<const std::string> hiddenIds) {
    for (const std::string& id : hiddenIds) {
        if (auto element = document.getElementById(id); !element.isNull()) {
            element.setAttribute("display", "none");
        }
    }
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

    // rasterised at the drawn size, as scaling a bitmap is what SVG avoids
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
    hide(*document, hiddenIds); // the boxes only place content; drawing them would frame every placement
    return rasterise(document, targetWidth);
}

namespace {

using Attributes = std::vector<std::pair<std::string, std::string>>;

/// `name="value"` pairs of one tag, with `style` declarations folded in as if they were attributes
[[nodiscard]] Attributes attributesOf(std::string_view tag) {
    Attributes  attributes;
    std::size_t at = tag.find_first_of(" \t\r\n");
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

[[nodiscard]] std::optional<std::string_view> valueOf(const Attributes& attributes, std::string_view key) {
    const auto found = std::ranges::find(attributes, key, &Attributes::value_type::first);
    return found == attributes.end() ? std::nullopt : std::optional<std::string_view>{found->second};
}

} // namespace

std::vector<SvgElementName> svgElementNames(std::string_view source) {
    std::vector<SvgElementName> names;
    // lunasvg keeps only the attributes it renders, so `data-` attributes are read from the text
    for (std::size_t at = source.find('<'); at != std::string_view::npos; at = source.find('<', at + 1UZ)) {
        const std::size_t tagEnd     = source.find('>', at);
        const auto        attributes = attributesOf(source.substr(at, tagEnd == std::string_view::npos ? std::string_view::npos : tagEnd - at));
        if (const std::string_view id = valueOf(attributes, "id").value_or(""); !id.empty()) {
            names.push_back(SvgElementName{.id = std::string{id}, .kind = std::string{valueOf(attributes, "data-present").value_or("")}, .step = parseNumber<int>(valueOf(attributes, "data-step").value_or("")).value_or(0)});
        }
    }
    return names;
}

std::string drawingKey(std::string_view reference, std::span<const std::string> hiddenIds) {
    std::string key{reference};
    for (const std::string& id : hiddenIds) {
        key += '\n' + id;
    }
    return key;
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
        // after transforms, including the stroke
        const lunasvg::Box box = element.getGlobalBoundingBox();
        layout.areas.push_back(Area{.id = named.id, .kind = named.kind, .step = named.step, .x = box.x, .y = box.y, .width = box.w, .height = box.h});
    }
    // brightness of the master under each area, to pick a picture suited to its backdrop
    constexpr std::uint32_t kProbeWidth = 256U;
    if (const std::optional<RasterImage> probe = rasterise(document, kProbeWidth); probe.has_value() && layout.width > 0.0f) {
        const float perUnit = static_cast<float>(probe->width) / layout.width;
        double      whole = 0.0, wholeCover = 0.0;
        for (std::size_t at = 0UZ; at + 3UZ < probe->rgba.size(); at += 4UZ) {
            whole += (0.2126 * probe->rgba[at] + 0.7152 * probe->rgba[at + 1UZ] + 0.0722 * probe->rgba[at + 2UZ]) / 255.0;
            wholeCover += probe->rgba[at + 3UZ] / 255.0;
        }
        const double pixels = static_cast<double>(probe->rgba.size()) / 4.0;
        if (pixels > 0.0 && wholeCover / pixels >= 0.5) {
            layout.backdropLight = static_cast<float>(whole / pixels);
        }
        for (Area& area : layout.areas) {
            const auto  first = [perUnit](float at, std::uint32_t limit) { return std::min(static_cast<std::uint32_t>(std::max(at * perUnit, 0.0f)), limit); };
            const auto  left = first(area.x, probe->width), right = first(area.x + area.width, probe->width), top = first(area.y, probe->height), bottom = first(area.y + area.height, probe->height);
            double      light = 0.0, cover = 0.0;
            std::size_t count = 0UZ;
            for (std::uint32_t y = top; y < bottom; ++y) {
                for (std::uint32_t x = left; x < right; ++x) {
                    const std::size_t at = (static_cast<std::size_t>(y) * probe->width + x) * 4UZ;
                    // Rec. 709 luma of the sRGB values: a threshold, not a colour measurement
                    light += (0.2126 * probe->rgba[at] + 0.7152 * probe->rgba[at + 1UZ] + 0.0722 * probe->rgba[at + 2UZ]) / 255.0;
                    cover += probe->rgba[at + 3UZ] / 255.0;
                    ++count;
                }
            }
            if (count > 0UZ && cover / static_cast<double>(count) >= 0.5) {
                area.backdropLight = static_cast<float>(light / static_cast<double>(count));
            }
        }
    }
    return layout;
}

std::vector<SvgLink> linksOfSvg(std::span<const std::uint8_t> source) {
    const auto document = lunasvg::Document::loadFromData(reinterpret_cast<const char*>(source.data()), source.size());
    if (!document || document->width() <= 0.0f || document->height() <= 0.0f) {
        return {};
    }
    std::vector<SvgLink> links;
    // lunasvg parses `<a>` as a group, and only a link group has an href
    for (const lunasvg::Element& link : document->querySelectorAll("g[href]")) {
        const lunasvg::Box box = link.getGlobalBoundingBox();
        links.push_back(SvgLink{.target = link.getAttribute("href"), .area = Rectangle{.x = box.x / document->width(), .y = box.y / document->height(), .width = box.w / document->width(), .height = box.h / document->height()}});
    }
    return links;
}

namespace {

// SVG's words for the enums, in the enums' order; a word not listed is the first, SVG's default
constexpr std::array<std::string_view, 3UZ> kJoinWords{"miter", "round", "bevel"}; // `miter-clip` and `arcs` as miter
constexpr std::array<std::string_view, 3UZ> kCapWords{"butt", "round", "square"};
constexpr std::array<std::string_view, 3UZ> kAnchorWords{"start", "middle", "end"};

template<typename TEnum, std::size_t N>
[[nodiscard]] constexpr TEnum enumOf(const std::array<std::string_view, N>& words, std::string_view word) {
    const auto found = std::ranges::find(words, word);
    return static_cast<TEnum>(found == words.end() ? 0 : found - words.begin());
}

struct SvgStyle {
    plutovg_matrix_t           matrix{};
    std::uint32_t              fill          = 0xFF000000U; // 0xAABBGGRR, SVG's default black
    std::uint32_t              stroke        = 0U;
    float                      strokeWidth   = 1.0f;
    float                      opacity       = 1.0f; // the element's own, multiplied down the tree
    float                      fillOpacity   = 1.0f;
    float                      strokeOpacity = 1.0f;
    bool                       evenOdd       = false;
    float                      fontSize      = 16.0f;
    RecordedFace               face          = RecordedFace::body;
    bool                       bold          = false;
    bool                       italic        = false;
    DrawingText::Anchor        anchor        = DrawingText::Anchor::start;
    bool                       skipped       = false; // hidden, or not drawn at all: a definition, a title
    std::vector<std::uint32_t> clips         = {};    // the drawing's clips its paths are drawn inside
    std::string                fillServer    = {};    // `url(#gradient)` where a paint server fills or strokes, drawn as pixels
    std::string                strokeServer  = {};
    VectorPath::Join           join          = VectorPath::Join::miter;
    VectorPath::Cap            cap           = VectorPath::Cap::butt;
    float                      miterLimit    = 4.0f;
    float                      viewportWide  = 0.0f; // the nearest svg's user space, which a length in percent is of
    float                      viewportTall  = 0.0f;
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

[[nodiscard]] float lengthOf(const Attributes& attributes, std::string_view key, float whole, float fallback) {
    const std::string_view trimmed = gr::trigger::detail::trim(valueOf(attributes, key).value_or(""));
    const bool             percent = trimmed.ends_with('%');
    const float            number  = numberOf(percent ? trimmed.substr(0UZ, trimmed.size() - 1UZ) : trimmed, std::numeric_limits<float>::quiet_NaN());
    return std::isnan(number) ? fallback : (percent ? number * whole / 100.0f : number);
}

/// a colour as 0xAABBGGRR; `none` is no paint, and a gradient or pattern is refused
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

/// a subpath closing where it began, but for float rounding, ends exactly there: a PDF draws the sliver's join as a spike
void appendSteps(const plutovg_path_t* path, std::vector<VectorPath::Step>& steps, std::vector<float>& points) {
    plutovg_path_iterator_t it{};
    plutovg_path_iterator_init(&it, path);
    plutovg_point_t at[3];
    std::size_t     subpathStartX = points.size();
    while (plutovg_path_iterator_has_next(&it)) {
        switch (plutovg_path_iterator_next(&it, at)) {
        case PLUTOVG_PATH_COMMAND_MOVE_TO:
            subpathStartX = points.size();
            steps.push_back(VectorPath::Step::move), points.insert(points.end(), {at[0].x, at[0].y});
            break;
        case PLUTOVG_PATH_COMMAND_LINE_TO: steps.push_back(VectorPath::Step::line), points.insert(points.end(), {at[0].x, at[0].y}); break;
        case PLUTOVG_PATH_COMMAND_CUBIC_TO: steps.push_back(VectorPath::Step::cubic), points.insert(points.end(), {at[0].x, at[0].y, at[1].x, at[1].y, at[2].x, at[2].y}); break;
        case PLUTOVG_PATH_COMMAND_CLOSE:
            if (const std::size_t last = points.size() - 2UZ; points.size() >= subpathStartX + 4UZ) {
                constexpr float kRounding = 1e-4f; // of the coordinate's size: float's sums, not a drawn segment
                const auto      near      = [](float a, float b) { return std::abs(a - b) <= kRounding * (1.0f + std::max(std::abs(a), std::abs(b))); };
                if (near(points[last], points[subpathStartX]) && near(points[last + 1UZ], points[subpathStartX + 1UZ])) {
                    points[last]       = points[subpathStartX];
                    points[last + 1UZ] = points[subpathStartX + 1UZ];
                }
            }
            steps.push_back(VectorPath::Step::close);
            break;
        }
    }
}

void addShape(VectorDrawing& drawing, plutovg_path_t* path, const SvgStyle& style) {
    plutovg_path_transform(path, &style.matrix);
    const std::uint32_t fillColour   = withOpacity(style.fill, style.opacity * style.fillOpacity);
    const std::uint32_t strokeColour = withOpacity(style.stroke, style.opacity * style.strokeOpacity);
    const float         strokeWidth  = style.strokeWidth * uniformScaleOf(style.matrix);
    VectorPath          outline{.fill = fillColour, .stroke = strokeColour, .strokeWidth = strokeWidth, .evenOdd = style.evenOdd, .clips = style.clips, .join = style.join, .cap = style.cap, .miterLimit = style.miterLimit};
    appendSteps(path, outline.steps, outline.points);
    if (!outline.steps.empty() && ((outline.fill >> 24U) != 0U || (outline.stroke >> 24U) != 0U)) {
        drawing.paths.push_back(std::move(outline));
    }
}

constexpr std::array kShapes{std::string_view{"rect"}, std::string_view{"circle"}, std::string_view{"ellipse"}, std::string_view{"line"}, std::string_view{"polyline"}, std::string_view{"polygon"}, std::string_view{"path"}};

/// the outline of a basic shape or path element, untransformed; the caller destroys it
[[nodiscard]] plutovg_path_t* shapePath(std::string_view name, const Attributes& attributes) {
    plutovg_path_t* path   = plutovg_path_create();
    const auto      number = [&attributes](std::string_view key) { return numberOf(valueOf(attributes, key).value_or(""), 0.0f); };
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
        const std::string_view data = valueOf(attributes, "d").value_or("");
        plutovg_path_parse(path, data.data(), static_cast<int>(data.size()));
    } else {
        // pairs; an odd last number has no partner and is left out, as in SVG
        const std::vector<float> points = numbersOf(valueOf(attributes, "points").value_or(""));
        for (std::size_t index = 0UZ; index + 1UZ < points.size(); index += 2UZ) {
            const float x = points[index];
            const float y = points[index + 1UZ];
            index == 0UZ ? plutovg_path_move_to(path, x, y) : plutovg_path_line_to(path, x, y);
        }
        if (name == "polygon") {
            plutovg_path_close(path);
        }
    }
    return path;
}

[[nodiscard]] plutovg_matrix_t transformedBy(std::optional<std::string_view> transform, const plutovg_matrix_t& outer) {
    plutovg_matrix_t local{};
    if (!transform.has_value() || !plutovg_matrix_parse(&local, transform->data(), static_cast<int>(transform->size()))) {
        return outer;
    }
    plutovg_matrix_t combined{};
    plutovg_matrix_multiply(&combined, &local, &outer);
    return combined;
}

struct Definition {
    std::string_view startTag; // between its `<` and `>`
    std::string_view body;
    std::string_view markup;
};

using Definitions = std::map<std::string, Definition, std::less<>>;

/// where the element whose start tag closes at `gt` ends, just past its end tag, counting elements of its name inside it
[[nodiscard]] std::size_t elementEnd(std::string_view text, std::string_view name, std::size_t gt) {
    const std::string open  = std::format("<{}", name);
    const std::string close = std::format("</{}>", name);
    int               depth = 1;
    for (std::size_t at = gt + 1UZ; at < text.size();) {
        const std::size_t opened = text.find(open, at);
        const std::size_t closed = text.find(close, at);
        if (closed == std::string_view::npos) {
            return std::string_view::npos;
        }
        if (opened != std::string_view::npos && opened < closed) {
            const std::size_t tagEnd = text.find('>', opened);
            const char        after  = opened + open.size() < text.size() ? text[opened + open.size()] : '>';
            if (tagEnd != std::string_view::npos && std::string_view{" \t\r\n/>"}.contains(after) && text[tagEnd - 1UZ] != '/') {
                ++depth;
            }
            at = opened + open.size();
            continue;
        }
        if (--depth == 0) {
            return closed + close.size();
        }
        at = closed + close.size();
    }
    return std::string_view::npos;
}

[[nodiscard]] Definitions definitionsOf(std::string_view text, std::string_view element) {
    Definitions       found;
    const std::string open  = std::format("<{}", element);
    const std::string close = std::format("</{}>", element);
    for (std::size_t at = text.find(open); at != std::string_view::npos; at = text.find(open, at + 1UZ)) {
        const std::size_t gt = text.find('>', at);
        if (gt == std::string_view::npos) {
            break;
        }
        const std::string_view tag = text.substr(at + 1UZ, gt - at - 1UZ);
        if (tag.size() > element.size() && !std::string_view{" \t\r\n/"}.contains(tag[element.size()])) {
            continue; // another element whose name begins the same
        }
        const bool        selfClosing = tag.ends_with('/');
        const std::size_t markupEnd   = selfClosing ? gt + 1UZ : elementEnd(text, element, gt);
        if (markupEnd == std::string_view::npos) {
            break;
        }
        const std::size_t bodyEnd    = selfClosing ? gt + 1UZ : markupEnd - close.size();
        const auto        attributes = attributesOf(tag);
        if (const auto id = valueOf(attributes, "id"); id.has_value()) {
            found.insert_or_assign(std::string{*id}, Definition{.startTag = tag, .body = text.substr(gt + 1UZ, bodyEnd - gt - 1UZ), .markup = text.substr(at, markupEnd - at)});
        }
    }
    return found;
}

/// the id `url(#id)` names; empty for anything else
[[nodiscard]] std::string_view urlId(std::string_view reference) {
    reference = gr::trigger::detail::trim(reference);
    return reference.starts_with("url(#") && reference.ends_with(')') ? reference.substr(5UZ, reference.size() - 6UZ) : std::string_view{};
}

/// a marker's place on a path, in the path's own coordinates, turned to the path's direction there in radians
struct MarkerPlace {
    plutovg_point_t at;
    float           radians;
};

struct MarkerPlaces {
    std::vector<MarkerPlace> start;
    std::vector<MarkerPlace> mid;
    std::vector<MarkerPlace> end;
};

[[nodiscard]] MarkerPlaces markerPlacesOf(const plutovg_path_t* path) {
    struct Vertex {
        plutovg_point_t at;
        plutovg_point_t arriving{0.0f, 0.0f};
        plutovg_point_t leaving{0.0f, 0.0f};
    };
    std::vector<Vertex> vertices;
    plutovg_point_t     start{0.0f, 0.0f};
    const auto          towards = [](plutovg_point_t from, plutovg_point_t to) { return plutovg_point_t{to.x - from.x, to.y - from.y}; };
    const auto          still   = [](plutovg_point_t a, plutovg_point_t b) { return a.x == b.x && a.y == b.y; };
    const auto          reach   = [&vertices](plutovg_point_t to, plutovg_point_t leaving, plutovg_point_t arriving) {
        if (!vertices.empty() && vertices.back().leaving.x == 0.0f && vertices.back().leaving.y == 0.0f) {
            vertices.back().leaving = leaving;
        }
        vertices.push_back(Vertex{.at = to, .arriving = arriving});
    };
    plutovg_path_iterator_t it{};
    plutovg_path_iterator_init(&it, path);
    plutovg_point_t points[3];
    while (plutovg_path_iterator_has_next(&it)) {
        const plutovg_point_t here = vertices.empty() ? start : vertices.back().at;
        switch (plutovg_path_iterator_next(&it, points)) {
        case PLUTOVG_PATH_COMMAND_MOVE_TO:
            start = points[0];
            vertices.push_back(Vertex{.at = points[0]});
            break;
        case PLUTOVG_PATH_COMMAND_LINE_TO: reach(points[0], towards(here, points[0]), towards(here, points[0])); break;
        case PLUTOVG_PATH_COMMAND_CUBIC_TO: reach(points[2], towards(here, still(here, points[0]) ? points[1] : points[0]), towards(still(points[2], points[1]) ? points[0] : points[1], points[2])); break;
        case PLUTOVG_PATH_COMMAND_CLOSE:
            if (!still(here, start)) {
                reach(start, towards(here, start), towards(here, start));
            }
            break;
        }
    }
    MarkerPlaces places;
    if (vertices.empty()) {
        return places;
    }
    const auto angle = [](plutovg_point_t direction) { return std::atan2(direction.y, direction.x); };
    places.start.push_back(MarkerPlace{.at = vertices.front().at, .radians = angle(vertices.front().leaving)});
    for (std::size_t index = 1UZ; index + 1UZ < vertices.size(); ++index) { // between: the bisector of arriving and leaving
        const float arriving = angle(vertices[index].arriving);
        const float leaving  = angle(vertices[index].leaving);
        places.mid.push_back(MarkerPlace{.at = vertices[index].at, .radians = std::atan2(std::sin(arriving) + std::sin(leaving), std::cos(arriving) + std::cos(leaving))});
    }
    places.end.push_back(MarkerPlace{.at = vertices.back().at, .radians = angle(vertices.back().arriving)});
    return places;
}

/// a nested `<svg>`'s viewport, as SVG 1.1 establishes one; a length in percent is of the enclosing svg's user space
struct NestedViewport {
    plutovg_matrix_t          matrix;
    std::optional<VectorClip> clip;
    float                     wide; // the user space it opens
    float                     tall;
};

[[nodiscard]] NestedViewport nestedViewport(const Attributes& attributes, const plutovg_matrix_t& outer, float outerWide, float outerTall) {
    const float x    = lengthOf(attributes, "x", outerWide, 0.0f);
    const float y    = lengthOf(attributes, "y", outerTall, 0.0f);
    const float wide = lengthOf(attributes, "width", outerWide, outerWide);
    const float tall = lengthOf(attributes, "height", outerTall, outerTall);

    NestedViewport viewport{.matrix = outer, .clip = std::nullopt, .wide = wide, .tall = tall};
    plutovg_matrix_translate(&viewport.matrix, x, y);
    const std::vector<float> box = numbersOf(valueOf(attributes, "viewBox").value_or(""));
    if (box.size() >= 4UZ && box[2] > 0.0f && box[3] > 0.0f) {
        viewport.wide                 = box[2];
        viewport.tall                 = box[3];
        const std::string_view aspect = gr::trigger::detail::trim(valueOf(attributes, "preserveAspectRatio").value_or("xMidYMid meet"));
        float                  scaleX = wide / box[2];
        float                  scaleY = tall / box[3];
        if (!aspect.starts_with("none")) {
            scaleX = aspect.contains("slice") ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
            scaleY = scaleX;
        }
        const auto aligned = [&aspect](std::string_view axis, float room) { return aspect.contains(std::format("{}Max", axis)) ? room : (aspect.contains(std::format("{}Min", axis)) ? 0.0f : room / 2.0f); };
        plutovg_matrix_translate(&viewport.matrix, aligned("x", wide - box[2] * scaleX), aligned("Y", tall - box[3] * scaleY));
        plutovg_matrix_scale(&viewport.matrix, scaleX, scaleY);
        plutovg_matrix_translate(&viewport.matrix, -box[0], -box[1]);
    }
    if (const auto overflow = valueOf(attributes, "overflow"); overflow != "visible" && overflow != "auto") {
        plutovg_path_t* outline = plutovg_path_create();
        plutovg_path_add_rect(outline, x, y, wide, tall);
        plutovg_path_transform(outline, &outer);
        VectorClip cut;
        appendSteps(outline, cut.steps, cut.points);
        plutovg_path_destroy(outline);
        viewport.clip = std::move(cut);
    }
    return viewport;
}

struct ShapeElement {
    std::string_view name;
    Attributes       attributes;
};

/// each shape with what the groups around it give it: their transforms before its own, their other attributes where it sets none
[[nodiscard]] std::vector<ShapeElement> shapesIn(std::string_view body) {
    std::vector<ShapeElement> shapes;
    std::vector<Attributes>   groups(1UZ);
    for (std::size_t lt = body.find('<'); lt != std::string_view::npos; lt = body.find('<', lt + 1UZ)) {
        const std::size_t gt = body.find('>', lt);
        if (gt == std::string_view::npos) {
            break;
        }
        std::string_view tag         = body.substr(lt + 1UZ, gt - lt - 1UZ);
        const bool       selfClosing = tag.ends_with('/');
        tag                          = selfClosing ? tag.substr(0UZ, tag.size() - 1UZ) : tag;
        const std::string_view name  = tag.substr(0UZ, tag.find_first_of(" \t\r\n"));
        if (name == "/g" || name == "/a") {
            if (groups.size() > 1UZ) {
                groups.pop_back();
            }
            continue;
        }
        const bool group = name == "g" || name == "a";
        if (!group && !std::ranges::contains(kShapes, name)) {
            continue;
        }
        Attributes        attributes = attributesOf(tag);
        const std::string transform  = std::format("{} {}", valueOf(groups.back(), "transform").value_or(""), valueOf(attributes, "transform").value_or(""));
        std::erase_if(attributes, [](const auto& attribute) { return attribute.first == "transform"; });
        for (const auto& [key, value] : groups.back()) {
            if (key != "transform" && key != "id" && !valueOf(attributes, key).has_value()) {
                attributes.emplace_back(key, value);
            }
        }
        attributes.emplace_back("transform", transform);
        if (!group) {
            shapes.push_back(ShapeElement{.name = name, .attributes = std::move(attributes)});
        } else if (!selfClosing) {
            groups.push_back(std::move(attributes));
        }
    }
    return shapes;
}

/// the paths `marker` (`url(#id)`) draws at each of `places`, as SVG 1.1 §11.6 places them
[[nodiscard]] std::expected<void, std::string> addMarkers(VectorDrawing& drawing, std::string_view marker, const Definitions& markers, std::span<const MarkerPlace> places, const SvgStyle& path, bool atStart) {
    const auto found = markers.find(urlId(marker));
    if (found == markers.end()) {
        return std::unexpected(std::format("it uses a marker it does not define, {}", marker));
    }
    const auto                      own     = attributesOf(found->second.startTag);
    const std::string_view          orient  = valueOf(own, "orient").value_or("0");
    const float                     wide    = numberOf(valueOf(own, "markerWidth").value_or("3"), 3.0f);
    const float                     tall    = numberOf(valueOf(own, "markerHeight").value_or("3"), 3.0f);
    const float                     units   = valueOf(own, "markerUnits") == "userSpaceOnUse" ? 1.0f : path.strokeWidth;
    const std::vector<float>        viewBox = numbersOf(valueOf(own, "viewBox").value_or(""));
    const bool                      boxed   = viewBox.size() >= 4UZ && viewBox[2] > 0.0f && viewBox[3] > 0.0f;
    const float                     fit     = boxed ? std::min(wide / viewBox[2], tall / viewBox[3]) : 1.0f; // `xMidYMid meet`
    const float                     referX  = numberOf(valueOf(own, "refX").value_or("0"));
    const float                     referY  = numberOf(valueOf(own, "refY").value_or("0"));
    const std::vector<ShapeElement> shapes  = shapesIn(found->second.body);
    for (const MarkerPlace& place : places) {
        float turn = 0.0f;
        if (orient == "auto" || orient == "auto-start-reverse") {
            turn = place.radians + (orient == "auto-start-reverse" && atStart ? std::numbers::pi_v<float> : 0.0f);
        } else {
            turn = numberOf(orient) * std::numbers::pi_v<float> / 180.0f;
        }
        plutovg_matrix_t placed = path.matrix;
        plutovg_matrix_translate(&placed, place.at.x, place.at.y);
        plutovg_matrix_rotate(&placed, turn);
        plutovg_matrix_scale(&placed, units, units);
        plutovg_matrix_scale(&placed, fit, fit); // the reference point, mapped as the viewBox maps it, is the origin
        plutovg_matrix_translate(&placed, -referX, -referY);
        for (const auto& [name, attributes] : shapes) {
            SvgStyle drawn{.matrix = transformedBy(valueOf(attributes, "transform"), placed), .clips = path.clips};
            for (const auto& [key, value] : attributes) {
                if (key == "fill" || key == "stroke") {
                    const auto paint = value == "context-stroke" ? std::expected<std::uint32_t, std::string>{path.stroke} : (value == "context-fill" ? std::expected<std::uint32_t, std::string>{path.fill} : paintOf(value));
                    if (!paint.has_value()) {
                        return std::unexpected(std::format("a marker fills or strokes with {}", paint.error()));
                    }
                    (key == "fill" ? drawn.fill : drawn.stroke) = *paint;
                } else if (key == "stroke-width") {
                    drawn.strokeWidth = numberOf(value, 1.0f);
                } else if (key == "opacity") {
                    drawn.opacity = numberOf(value, 1.0f);
                } else if (key == "fill-opacity") {
                    drawn.fillOpacity = numberOf(value, 1.0f);
                } else if (key == "stroke-opacity") {
                    drawn.strokeOpacity = numberOf(value, 1.0f);
                } else if (key == "fill-rule") {
                    drawn.evenOdd = value == "evenodd";
                }
            }
            drawn.opacity *= path.opacity;
            plutovg_path_t* outline = shapePath(name, attributes);
            addShape(drawing, outline, drawn);
            plutovg_path_destroy(outline);
        }
    }
    return {};
}

/// the clip `reference` names (`url(#id)`), its shapes in the drawing's coordinates through `matrix`; none for one in
/// object units, which this does not reproduce, or one that is not defined
[[nodiscard]] std::optional<VectorClip> clipOf(std::string_view reference, const Definitions& clipPaths, const plutovg_matrix_t& matrix) {
    const auto found = clipPaths.find(urlId(reference));
    if (found == clipPaths.end()) {
        return std::nullopt;
    }
    const auto own = attributesOf(found->second.startTag);
    if (valueOf(own, "clipPathUnits") == "objectBoundingBox") {
        return std::nullopt;
    }
    const plutovg_matrix_t outer = transformedBy(valueOf(own, "transform"), matrix);
    VectorClip             clip;
    for (const auto& [name, attributes] : shapesIn(found->second.body)) {
        clip.evenOdd                   = clip.evenOdd || valueOf(attributes, "clip-rule") == "evenodd";
        const plutovg_matrix_t placed  = transformedBy(valueOf(attributes, "transform"), outer);
        plutovg_path_t*        outline = shapePath(name, attributes);
        plutovg_path_transform(outline, &placed);
        appendSteps(outline, clip.steps, clip.points);
        plutovg_path_destroy(outline);
    }
    return clip;
}

/// the markup of the element `id` names, from its start tag to its end tag; empty where there is none
[[nodiscard]] std::string_view elementMarkup(std::string_view text, std::string_view id) {
    for (const std::string_view quote : {std::string_view{"\""}, std::string_view{"'"}}) {
        const std::string attribute = std::format("id={0}{1}{0}", quote, id);
        std::size_t       named     = text.find(attribute);
        while (named != std::string_view::npos && named > 0UZ && !std::string_view{" \t\r\n"}.contains(text[named - 1UZ])) {
            named = text.find(attribute, named + 1UZ); // `data-id=` is another attribute
        }
        if (named == std::string_view::npos) {
            continue;
        }
        const std::size_t start = text.rfind('<', named);
        const std::size_t gt    = text.find('>', named);
        if (start == std::string_view::npos || gt == std::string_view::npos) {
            return {};
        }
        if (text[gt - 1UZ] == '/') {
            return text.substr(start, gt + 1UZ - start);
        }
        const std::string_view name = text.substr(start + 1UZ, text.find_first_of(" \t\r\n>", start) - start - 1UZ);
        const std::size_t      end  = elementEnd(text, name, gt);
        return end == std::string_view::npos ? std::string_view{} : text.substr(start, end - start);
    }
    return {};
}

[[nodiscard]] std::string withUsesInlined(std::string_view text) {
    constexpr int kLevels = 4;
    std::string   expanded{text};
    for (int level = 0; level < kLevels && expanded.contains("<use"); ++level) {
        std::string next;
        std::size_t copied = 0UZ;
        for (std::size_t at = expanded.find("<use"); at != std::string::npos; at = expanded.find("<use", at + 1UZ)) {
            const std::size_t gt = expanded.find('>', at);
            if (gt == std::string::npos) {
                break;
            }
            const bool             selfClosing = expanded[gt - 1UZ] == '/';
            const auto             attributes  = attributesOf(std::string_view{expanded}.substr(at + 1UZ, gt - at - (selfClosing ? 2UZ : 1UZ)));
            const std::string_view href        = valueOf(attributes, "href").value_or("");
            const std::string_view reference   = href.empty() ? valueOf(attributes, "xlink:href").value_or("") : href;
            const std::size_t      end         = selfClosing ? gt + 1UZ : expanded.find("</use>", gt);
            if (!reference.starts_with('#') || end == std::string::npos) {
                continue;
            }
            const std::string_view copy = elementMarkup(expanded, reference.substr(1UZ));
            if (copy.empty()) {
                continue;
            }
            std::string group = std::format("<g transform=\"{} translate({} {})\"", valueOf(attributes, "transform").value_or(""), numberOf(valueOf(attributes, "x").value_or("")), numberOf(valueOf(attributes, "y").value_or("")));
            for (const auto& [key, value] : attributes) { // what the use itself paints with passes to the copy
                if (key != "href" && key != "xlink:href" && key != "x" && key != "y" && key != "transform" && key != "id" && key != "width" && key != "height") {
                    group += std::format(" {}=\"{}\"", key, value);
                }
            }
            next += std::string_view{expanded}.substr(copied, at - copied);
            std::string body{copy};
            if (copy.starts_with("<symbol")) { // drawn where it is used as an svg of the use's size, its viewBox fitted to it
                constexpr std::string_view kSymbolEnd = "</symbol>";
                const std::string          size       = std::format(R"( width="{}" height="{}")", valueOf(attributes, "width").value_or("100%"), valueOf(attributes, "height").value_or("100%"));
                body                                  = copy.ends_with(kSymbolEnd) ? std::format("<svg{}{}</svg>", size, copy.substr(7UZ, copy.size() - 7UZ - kSymbolEnd.size())) : std::format("<svg{}{}", size, copy.substr(7UZ));
            }
            next += std::format("{}>{}</g>", group, body);
            copied = selfClosing ? end : end + 6UZ;
            at     = copied - 1UZ;
        }
        next += std::string_view{expanded}.substr(std::min(copied, expanded.size()));
        expanded = std::move(next);
    }
    return expanded;
}

/// `colour` (0xAABBGGRR) as SVG paint: `#rrggbb` and its opacity, or `none`
[[nodiscard]] std::string paintAttributes(std::string_view key, std::uint32_t colour) {
    if ((colour >> 24U) == 0U) {
        return std::format(R"( {}="none")", key);
    }
    return std::format(R"( {0}="#{1:02x}{2:02x}{3:02x}" {0}-opacity="{4}")", key, colour & 0xFFU, (colour >> 8U) & 0xFFU, (colour >> 16U) & 0xFFU, static_cast<float>(colour >> 24U) / 255.0f);
}

/// the paint an element inherits, as attributes of the group it is rasterised or measured in
[[nodiscard]] std::string paintingOf(const SvgStyle& style) {
    std::string painting = style.fillServer.empty() ? paintAttributes("fill", style.fill) : std::format(" fill=\"{}\"", style.fillServer);
    painting += style.strokeServer.empty() ? paintAttributes("stroke", style.stroke) : std::format(" stroke=\"{}\"", style.strokeServer);
    return painting + std::format(R"( stroke-width="{}" stroke-linejoin="{}" stroke-linecap="{}" stroke-miterlimit="{}" fill-rule="{}")", style.strokeWidth, kJoinWords[static_cast<std::size_t>(style.join)], kCapWords[static_cast<std::size_t>(style.cap)], style.miterLimit, style.evenOdd ? "evenodd" : "nonzero");
}

/// what outlines cannot paint stays pixels, and only as many as `element` covers
[[nodiscard]] std::optional<VectorPicture> pictureOf(std::string_view element, std::string_view paints, std::span<const std::string> hiddenIds, const plutovg_matrix_t& parent, const SvgStyle& style, const plutovg_rect_t& bounds, bool photograph) {
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) {
        return std::nullopt;
    }
    const std::string markup   = std::format(R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{2}" height="{3}" viewBox="{0} {1} {2} {3}"><defs>{4}</defs><g transform="matrix({5} {6} {7} {8} {9} {10})" opacity="{11}"{12}>{13}</g></svg>)svg", //
          bounds.x, bounds.y, bounds.w, bounds.h, paints, parent.a, parent.b, parent.c, parent.d, parent.e, parent.f, style.opacity, paintingOf(style), element);
    const auto        width    = static_cast<std::uint32_t>(std::clamp(std::ceil(bounds.w * kSvgOversampling), 1.0f, kSvgMostPixelsWide));
    auto              document = lunasvg::Document::loadFromData(markup.data(), markup.size());
    if (document) {
        hide(*document, hiddenIds);
    }
    auto raster = rasterise(document, width);
    if (!raster.has_value()) {
        return std::nullopt;
    }
    return VectorPicture{.pixels = RecordedPixels{.width = raster->width, .height = raster->height, .rgba = std::move(raster->rgba)}, .at = Rectangle{.x = bounds.x, .y = bounds.y, .width = bounds.w, .height = bounds.h}, .photograph = photograph};
}

struct Effects {
    std::string_view transform;
    std::string_view maskBody;
    std::string_view filterTag;
};

/// lunasvg's box ignores effects: a mask is cut to its content's box, a filter widened to its region (by default 10 % per side)
[[nodiscard]] std::optional<plutovg_rect_t> paintedBounds(std::string_view element, std::string_view paints, const plutovg_matrix_t& parent, const SvgStyle& style, const Effects& effects, float width, float height) {
    static_cast<void>(registerFallbackFonts());
    const std::string matrix   = std::format("matrix({} {} {} {} {} {})", parent.a, parent.b, parent.c, parent.d, parent.e, parent.f);
    const std::string masking  = effects.maskBody.empty() ? std::string{} : std::format(R"svg(<g id="gr4-present-mask" transform="{}"><g transform="{}">{}</g></g>)svg", matrix, effects.transform, effects.maskBody);
    const std::string markup   = std::format(R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{}" height="{}"><defs>{}</defs><g id="gr4-present-probe" transform="{}"{}>{}</g>{}</svg>)svg", //
          width, height, paints, matrix, paintingOf(style), element, masking);
    const auto        document = lunasvg::Document::loadFromData(markup.data(), markup.size());
    if (!document) {
        return std::nullopt;
    }
    const lunasvg::Element probe = document->getElementById("gr4-present-probe");
    if (probe.isNull()) {
        return std::nullopt;
    }
    lunasvg::Box box = probe.getGlobalBoundingBox();
    if (!effects.filterTag.empty()) {
        const auto attributes = attributesOf(effects.filterTag);
        if (valueOf(attributes, "filterUnits") == "userSpaceOnUse") {
            box = lunasvg::Box{0.0f, 0.0f, width, height}; // a region of its own, which this does not follow: all of it
        } else {
            // fractions of the element's box, 100 % being 1
            const auto fraction = [&attributes](std::string_view key, float fallback) { return lengthOf(attributes, key, 1.0f, fallback); };
            box                 = lunasvg::Box{box.x + fraction("x", -0.1f) * box.w, box.y + fraction("y", -0.1f) * box.h, fraction("width", 1.2f) * box.w, fraction("height", 1.2f) * box.h};
        }
    }
    if (!effects.maskBody.empty()) {
        if (const lunasvg::Element covered = document->getElementById("gr4-present-mask"); !covered.isNull()) {
            const lunasvg::Box content = covered.getGlobalBoundingBox();
            const float        left    = std::max(box.x, content.x);
            const float        top     = std::max(box.y, content.y);
            box                        = lunasvg::Box{left, top, std::min(box.x + box.w, content.x + content.w) - left, std::min(box.y + box.h, content.y + content.h) - top};
        }
    }
    const float left   = std::max(box.x, 0.0f);
    const float top    = std::max(box.y, 0.0f);
    const float right  = std::min(box.x + box.w, width);
    const float bottom = std::min(box.y + box.h, height);
    if (right <= left || bottom <= top) {
        return std::nullopt;
    }
    return plutovg_rect_t{left, top, right - left, bottom - top};
}

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

std::expected<VectorDrawing, std::string> outlineSvg(std::span<const std::uint8_t> source, std::span<const std::string> hiddenIds, SvgPictures pictures) {
    std::string unprefixed{reinterpret_cast<const char*>(source.data()), source.size()}; // SVG's own namespace spelt out, `<svg:rect>`
    for (const auto& [prefixed, plain] : {std::pair{std::string_view{"<svg:"}, std::string_view{"<"}}, std::pair{std::string_view{"</svg:"}, std::string_view{"</"}}}) {
        for (std::size_t at = unprefixed.find(prefixed); at != std::string::npos; at = unprefixed.find(prefixed, at + plain.size())) {
            unprefixed.replace(at, prefixed.size(), plain);
        }
    }
    const std::string      inlined = withUsesInlined(unprefixed);
    const std::string_view text{inlined};
    const auto             document = lunasvg::Document::loadFromData(text.data(), text.size());
    if (!document) {
        return std::unexpected(std::string{"not a drawing lunasvg can read"});
    }
    VectorDrawing drawing{.width = document->width(), .height = document->height(), .paths = {}, .texts = {}};

    // anything not listed either draws nothing or draws something this does not reproduce, so it is refused
    constexpr std::array<std::string_view, 11UZ> kSilent{"title", "desc", "metadata", "namedview", "defs", "style", "clipPath", "marker", "mask", "filter", "symbol"};
    const Definitions                            clipPaths       = definitionsOf(text, "clipPath");
    const Definitions                            markers         = definitionsOf(text, "marker");
    const Definitions                            masks           = definitionsOf(text, "mask");
    const Definitions                            filters         = definitionsOf(text, "filter");
    const Definitions                            linearGradients = definitionsOf(text, "linearGradient");
    const Definitions                            radialGradients = definitionsOf(text, "radialGradient");
    const Definitions                            patterns        = definitionsOf(text, "pattern");
    std::string                                  paints;
    for (const Definitions* definitions : {&linearGradients, &radialGradients, &patterns, &clipPaths, &markers, &masks, &filters}) {
        for (const Definition& definition : *definitions | std::views::values) {
            paints += definition.markup;
        }
    }
    constexpr std::array kContainers{std::string_view{"svg"}, std::string_view{"g"}, std::string_view{"a"}, std::string_view{"text"}, std::string_view{"tspan"}, std::string_view{"switch"}};

    std::vector<SvgStyle> styles(1UZ);
    plutovg_matrix_init_identity(&styles.front().matrix);
    std::vector<std::string>   openElements;
    std::optional<DrawingText> pendingText;
    bool                       rootSeen = false;

    for (std::size_t at = 0UZ; at < text.size();) {
        const std::size_t lt = text.find('<', at);
        if (pendingText.has_value() && lt != at) {
            pendingText->text += decoded(text.substr(at, (lt == std::string_view::npos ? text.size() : lt) - at));
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
        if (tag.starts_with('/')) {
            if (!openElements.empty()) {
                if (openElements.back() == "text" && pendingText.has_value()) {
                    const std::size_t first = pendingText->text.find_first_not_of(' ');
                    const std::size_t last  = pendingText->text.find_last_not_of(' ');
                    pendingText->text       = first == std::string::npos ? std::string{} : pendingText->text.substr(first, last - first + 1UZ);
                    if (!pendingText->text.empty()) {
                        drawing.texts.push_back(std::move(*pendingText));
                    }
                    pendingText.reset();
                }
                openElements.pop_back();
                styles.pop_back();
            }
            continue;
        }
        const bool             selfClosing = tag.ends_with('/');
        const std::size_t      nameEnd     = tag.find_first_of(" \t\r\n/");
        const std::string_view name        = tag.substr(0UZ, nameEnd);
        const auto             attributes  = attributesOf(selfClosing ? tag.substr(0UZ, tag.size() - 1UZ) : tag);

        SvgStyle style = styles.back();
        // an editor's own elements (`sodipodi:namedview`, `inkscape:grid`, `rdf:RDF`) draw nothing
        const bool silent = std::ranges::contains(kSilent, name) || name.contains(':');
        const bool shape  = std::ranges::contains(kShapes, name);
        const bool known  = silent || name == "image" || shape || std::ranges::contains(kContainers, name);
        if (!known && !style.skipped) {
            return std::unexpected(std::format("it uses <{}>", name));
        }
        const bool effect = std::ranges::any_of(std::array{std::string_view{"mask"}, std::string_view{"filter"}}, [&attributes](std::string_view kind) {
            const auto used = valueOf(attributes, kind);
            return used.has_value() && *used != "none";
        });

        if (name == "svg" && !rootSeen) {
            // the viewBox maps onto the drawing's width and height, as in the rasterised texture
            rootSeen           = true;
            style.viewportWide = drawing.width;
            style.viewportTall = drawing.height;
            if (const auto box = valueOf(attributes, "viewBox"); box.has_value()) {
                const std::vector<float> numbers = numbersOf(*box);
                if (numbers.size() >= 4UZ && numbers[2] > 0.0f && numbers[3] > 0.0f) {
                    plutovg_matrix_init_scale(&style.matrix, drawing.width / numbers[2], drawing.height / numbers[3]);
                    plutovg_matrix_translate(&style.matrix, -numbers[0], -numbers[1]);
                    style.viewportWide = numbers[2];
                    style.viewportTall = numbers[3];
                }
            }
        } else if (name == "svg") {
            NestedViewport viewport = nestedViewport(attributes, style.matrix, style.viewportWide, style.viewportTall);
            style.matrix            = viewport.matrix;
            style.viewportWide      = viewport.wide;
            style.viewportTall      = viewport.tall;
            if (viewport.clip.has_value()) {
                style.clips.push_back(static_cast<std::uint32_t>(drawing.clips.size()));
                drawing.clips.push_back(std::move(*viewport.clip));
            }
        }
        style.matrix = transformedBy(valueOf(attributes, "transform"), style.matrix);
        if (const auto clipped = valueOf(attributes, "clip-path"); clipped.has_value() && *clipped != "none" && !style.skipped) {
            std::optional<VectorClip> clip = clipOf(*clipped, clipPaths, style.matrix);
            if (!clip.has_value()) {
                return std::unexpected(std::format("it uses clip-path {}", *clipped));
            }
            style.clips.push_back(static_cast<std::uint32_t>(drawing.clips.size()));
            drawing.clips.push_back(std::move(*clip));
        }
        const auto id           = valueOf(attributes, "id");
        const bool hiddenById   = id.has_value() && std::ranges::contains(hiddenIds, *id);
        const bool notDisplayed = valueOf(attributes, "display") == "none";
        const bool invisible    = valueOf(attributes, "visibility") == "hidden";
        style.skipped           = style.skipped || silent || hiddenById || notDisplayed || invisible;
        for (const auto& [key, value] : attributes) {
            if (key == "fill" || key == "stroke") {
                const auto   paint  = paintOf(value);
                std::string& server = key == "fill" ? style.fillServer : style.strokeServer;
                if (!paint.has_value()) {
                    if (style.skipped) {
                        continue;
                    }
                    if (!urlId(value).empty()) {
                        server = value; // a gradient or pattern: the shapes it paints are drawn as pixels
                        continue;
                    }
                    return std::unexpected(std::format("it fills or strokes with {}", paint.error()));
                }
                server.clear();
                (key == "fill" ? style.fill : style.stroke) = *paint;
            } else if (key == "stroke-width") {
                style.strokeWidth = numberOf(value, 1.0f);
            } else if (key == "stroke-linejoin") {
                style.join = enumOf<VectorPath::Join>(kJoinWords, value);
            } else if (key == "stroke-linecap") {
                style.cap = enumOf<VectorPath::Cap>(kCapWords, value);
            } else if (key == "stroke-miterlimit") {
                style.miterLimit = std::max(numberOf(value, 4.0f), 1.0f);
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
                style.anchor = enumOf<DrawingText::Anchor>(kAnchorWords, value);
            }
        }

        const bool byServer = !style.fillServer.empty() || !style.strokeServer.empty();
        if (name == "tspan" && byServer && !style.skipped) {
            return std::unexpected(std::string{"it paints part of a text with a gradient or pattern"}); // placed by its text, so not a picture of its own
        }
        const bool asPixels = !style.skipped && (effect || name == "image" || ((shape || name == "text") && byServer));
        if (asPixels && pictures == SvgPictures::rasterised) {
            const std::size_t      end     = selfClosing ? gt + 1UZ : elementEnd(text, name, gt);
            const std::string_view element = text.substr(lt, (end == std::string_view::npos ? gt + 1UZ : end) - lt);
            const SvgStyle&        parent  = styles.back();
            Effects                effects{};
            if (effect) {
                const auto mask   = masks.find(urlId(valueOf(attributes, "mask").value_or("")));
                const auto filter = filters.find(urlId(valueOf(attributes, "filter").value_or("")));
                effects           = Effects{.transform = valueOf(attributes, "transform").value_or(""), .maskBody = mask == masks.end() ? std::string_view{} : mask->second.body, .filterTag = filter == filters.end() ? std::string_view{} : filter->second.startTag};
            }
            const std::optional<plutovg_rect_t> bounds     = paintedBounds(element, paints, parent.matrix, parent, effects, drawing.width, drawing.height);
            const bool                          photograph = effect ? element.contains("data:image/jpeg") : name == "image";
            if (std::optional<VectorPicture> picture = bounds.has_value() ? pictureOf(element, paints, hiddenIds, parent.matrix, parent, *bounds, photograph) : std::nullopt; picture.has_value()) {
                drawing.paths.push_back(VectorPath{.clips = style.clips, .picture = std::move(picture)});
            }
        }
        style.skipped = style.skipped || effect || (asPixels && name == "text");
        if (!asPixels && !style.skipped && shape) {
            plutovg_path_t* path    = shapePath(name, attributes);
            SvgStyle        painted = style;
            if (name == "line" || name == "polyline") {
                painted.fill = name == "line" ? 0U : painted.fill; // a line has no inside; an open polyline fills as if closed, as in SVG
            }
            const MarkerPlaces places = markerPlacesOf(path); // in the shape's own coordinates, before it is placed
            addShape(drawing, path, painted);
            plutovg_path_destroy(path);
            const std::array<std::pair<std::string_view, std::span<const MarkerPlace>>, 3UZ> markerSlots{{{"marker-start", places.start}, {"marker-mid", places.mid}, {"marker-end", places.end}}};
            for (const auto& [property, placed] : markerSlots) {
                const auto marker = valueOf(attributes, property).or_else([&attributes] { return valueOf(attributes, "marker"); });
                if (marker.has_value() && *marker != "none") {
                    if (auto added = addMarkers(drawing, *marker, markers, placed, style, property == "marker-start"); !added.has_value()) {
                        return std::unexpected(added.error());
                    }
                }
            }
        }
        if (name == "text" && !style.skipped) {
            plutovg_point_t anchorAt{numberOf(valueOf(attributes, "x").value_or(""), 0.0f), numberOf(valueOf(attributes, "y").value_or(""), 0.0f)};
            plutovg_point_t mapped{};
            plutovg_matrix_map_point(&style.matrix, &anchorAt, &mapped);
            const RecordedFace  upright      = style.bold ? RecordedFace::bold : RecordedFace::body;
            const RecordedFace  slanted      = style.bold ? RecordedFace::boldItalic : RecordedFace::italic;
            const RecordedFace  styledFace   = style.italic ? slanted : upright;
            const RecordedFace  face         = style.face == RecordedFace::mono ? RecordedFace::mono : styledFace;
            const std::uint32_t fillColour   = withOpacity(style.fill, style.opacity * style.fillOpacity);
            const float         angleDegrees = std::atan2(-style.matrix.b, style.matrix.a) * 180.0f / std::numbers::pi_v<float>;
            pendingText                      = DrawingText{.text = {}, .face = face, .em = style.fontSize * uniformScaleOf(style.matrix), .x = mapped.x, .y = mapped.y, .anchor = style.anchor, .colour = fillColour, .angleDegrees = angleDegrees};
        }
        if (!selfClosing) {
            openElements.emplace_back(name);
            styles.push_back(style);
        }
    }
    return drawing;
}

std::vector<float> numbersOf(std::string_view list) {
    constexpr std::string_view kSeparators = " ,\t\r\n";
    std::vector<float>         numbers;
    for (std::size_t at = list.find_first_not_of(kSeparators); at != std::string_view::npos; at = list.find_first_not_of(kSeparators, at)) {
        float             number = 0.0f;
        const std::size_t digits = list[at] == '+' ? at + 1UZ : at; // SVG allows a plus sign, std::from_chars does not
        const auto [end, read]   = std::from_chars(list.data() + digits, list.data() + list.size(), number);
        if (read != std::errc{}) {
            break;
        }
        numbers.push_back(number);
        at = static_cast<std::size_t>(end - list.data());
    }
    return numbers;
}

bool isSvgReference(std::string_view reference) noexcept { return reference.ends_with(".svg") || reference.ends_with(".SVG"); }

} // namespace gr::present
