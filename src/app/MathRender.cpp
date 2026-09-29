#include "MathRender.hpp"

#include "EmbeddedMathFont.hpp"

#include <graphic/graphic.h>
#include <microtex.h>
#include <plutovg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <vector>

namespace gr::present {

namespace {

/// a formula is laid out at this size and scaled by the caller; one size keeps the glyph cache small
constexpr float kRenderSize  = 64.0f;
constexpr int   kMargin      = 2; // pixels, so an anti-aliased outline is not clipped by its own bounding box
constexpr float kUnlimited   = 0.0f;
constexpr float kNoLineSpace = 0.0f;

/**
 * The half of `Graphics2D` that path rendering actually reaches.
 *
 * `createFont`, `drawGlyph` and everything else about typefaces is unreachable with `GLYPH_RENDER_TYPE=1`, so
 * those are defined to satisfy the interface and do nothing. The engine's own contract is that every path begins
 * with `beginPath` and ends with `fillPath`, which is exactly a plutovg path.
 */
class PathGraphics final : public microtex::Graphics2D {
public:
    explicit PathGraphics(plutovg_canvas_t* canvas) : _canvas(canvas) {}

    void setColor(microtex::color c) override {
        _colour            = c;
        const auto channel = [c](unsigned shift) { return static_cast<float>((c >> shift) & 0xFFU) / 255.0f; };
        plutovg_canvas_set_rgba(_canvas, channel(16U), channel(8U), channel(0U), channel(24U));
    }
    [[nodiscard]] microtex::color getColor() const override { return _colour; }

    void                                  setStroke(const microtex::Stroke& s) override { setStrokeWidth(s.lineWidth); }
    [[nodiscard]] const microtex::Stroke& getStroke() const override { return _stroke; }
    void                                  setStrokeWidth(float w) override {
        _stroke.lineWidth = w;
        plutovg_canvas_set_line_width(_canvas, w);
    }

    void                             setDash(const std::vector<float>& dash) override { plutovg_canvas_set_dash_array(_canvas, dash.data(), static_cast<int>(dash.size())); }
    [[nodiscard]] std::vector<float> getDash() override { return {}; }

    // a typeface is never asked for, because every glyph arrives as an outline
    [[nodiscard]] microtex::sptr<microtex::Font> getFont() const override { return nullptr; }
    void                                         setFont(const microtex::sptr<microtex::Font>&) override {}
    [[nodiscard]] float                          getFontSize() const override { return _fontSize; }
    void                                         setFontSize(float size) override { _fontSize = size; }
    void                                         drawGlyph(microtex::u16, float, float) override {}

    void translate(float dx, float dy) override { plutovg_canvas_translate(_canvas, dx, dy); }
    void scale(float sx, float sy) override {
        _scaleX *= sx;
        _scaleY *= sy;
        plutovg_canvas_scale(_canvas, sx, sy);
    }
    void rotate(float angle) override { plutovg_canvas_rotate(_canvas, angle); }
    void rotate(float angle, float px, float py) override {
        plutovg_canvas_translate(_canvas, px, py);
        plutovg_canvas_rotate(_canvas, angle);
        plutovg_canvas_translate(_canvas, -px, -py);
    }
    void reset() override {
        plutovg_canvas_reset_matrix(_canvas);
        _scaleX = 1.0f;
        _scaleY = 1.0f;
    }
    [[nodiscard]] float sx() const override { return _scaleX; }
    [[nodiscard]] float sy() const override { return _scaleY; }

    bool beginPath(microtex::i32) override { return false; } // nothing is cached, so the engine always rebuilds
    void moveTo(float x, float y) override { plutovg_canvas_move_to(_canvas, x, y); }
    void lineTo(float x, float y) override { plutovg_canvas_line_to(_canvas, x, y); }
    void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) override { plutovg_canvas_cubic_to(_canvas, x1, y1, x2, y2, x3, y3); }
    void quadTo(float x1, float y1, float x2, float y2) override { plutovg_canvas_quad_to(_canvas, x1, y1, x2, y2); }
    void closePath() override { plutovg_canvas_close_path(_canvas); }
    void fillPath(microtex::i32) override {
        // non-zero winding, so a counter cut the other way round leaves a hole rather than filling solid
        plutovg_canvas_set_fill_rule(_canvas, PLUTOVG_FILL_RULE_NON_ZERO);
        plutovg_canvas_fill(_canvas);
    }

    void drawLine(float x1, float y1, float x2, float y2) override {
        plutovg_canvas_move_to(_canvas, x1, y1);
        plutovg_canvas_line_to(_canvas, x2, y2);
        plutovg_canvas_stroke(_canvas);
    }
    void drawRect(float x, float y, float w, float h) override {
        plutovg_canvas_rect(_canvas, x, y, w, h);
        plutovg_canvas_stroke(_canvas);
    }
    void fillRect(float x, float y, float w, float h) override {
        plutovg_canvas_rect(_canvas, x, y, w, h);
        plutovg_canvas_fill(_canvas);
    }
    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        plutovg_canvas_round_rect(_canvas, x, y, w, h, rx, ry);
        plutovg_canvas_stroke(_canvas);
    }
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        plutovg_canvas_round_rect(_canvas, x, y, w, h, rx, ry);
        plutovg_canvas_fill(_canvas);
    }

private:
    plutovg_canvas_t* _canvas = nullptr;
    microtex::color   _colour = 0xFF000000U;
    microtex::Stroke  _stroke;
    float             _fontSize = kRenderSize;
    float             _scaleX   = 1.0f;
    float             _scaleY   = 1.0f;
};

/**
 * The same half of `Graphics2D`, collecting outlines instead of filling them: what an exported page draws in place
 * of the bitmap. Points are mapped through the transform the engine has set, so they land in the bitmap's pixels.
 */
class OutlineGraphics final : public microtex::Graphics2D {
public:
    explicit OutlineGraphics(VectorDrawing& drawing) : _drawing(drawing) { plutovg_matrix_init_identity(&_matrix); }

    void                          setColor(microtex::color c) override { _colour = c; }
    [[nodiscard]] microtex::color getColor() const override { return _colour; }

    void                                  setStroke(const microtex::Stroke& s) override { _stroke = s; }
    [[nodiscard]] const microtex::Stroke& getStroke() const override { return _stroke; }
    void                                  setStrokeWidth(float w) override { _stroke.lineWidth = w; }
    void                                  setDash(const std::vector<float>&) override {}
    [[nodiscard]] std::vector<float>      getDash() override { return {}; }

    [[nodiscard]] microtex::sptr<microtex::Font> getFont() const override { return nullptr; }
    void                                         setFont(const microtex::sptr<microtex::Font>&) override {}
    [[nodiscard]] float                          getFontSize() const override { return _fontSize; }
    void                                         setFontSize(float size) override { _fontSize = size; }
    void                                         drawGlyph(microtex::u16, float, float) override {}

    void translate(float dx, float dy) override { plutovg_matrix_translate(&_matrix, dx, dy); }
    void scale(float sx, float sy) override {
        _scaleX *= sx;
        _scaleY *= sy;
        plutovg_matrix_scale(&_matrix, sx, sy);
    }
    void rotate(float angle) override { plutovg_matrix_rotate(&_matrix, angle); }
    void rotate(float angle, float px, float py) override {
        plutovg_matrix_translate(&_matrix, px, py);
        plutovg_matrix_rotate(&_matrix, angle);
        plutovg_matrix_translate(&_matrix, -px, -py);
    }
    void reset() override {
        plutovg_matrix_init_identity(&_matrix);
        _scaleX = 1.0f;
        _scaleY = 1.0f;
    }
    [[nodiscard]] float sx() const override { return _scaleX; }
    [[nodiscard]] float sy() const override { return _scaleY; }

    bool beginPath(microtex::i32) override {
        _path = VectorPath{};
        return false;
    }
    void moveTo(float x, float y) override { step(VectorPath::Step::move, {x, y}); }
    void lineTo(float x, float y) override { step(VectorPath::Step::line, {x, y}); }
    void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) override { step(VectorPath::Step::cubic, {x1, y1, x2, y2, x3, y3}); }
    void quadTo(float x1, float y1, float x2, float y2) override {
        // a quadratic as the cubic with the same curve: its control points two thirds of the way to the quadratic's
        const float x0 = _last[0];
        const float y0 = _last[1];
        cubicTo(x0 + (x1 - x0) * 2.0f / 3.0f, y0 + (y1 - y0) * 2.0f / 3.0f, x2 + (x1 - x2) * 2.0f / 3.0f, y2 + (y1 - y2) * 2.0f / 3.0f, x2, y2);
    }
    void closePath() override { _path.steps.push_back(VectorPath::Step::close); }
    void fillPath(microtex::i32) override { finish(true); }

    void drawLine(float x1, float y1, float x2, float y2) override {
        beginPath(0);
        moveTo(x1, y1);
        lineTo(x2, y2);
        finish(false);
    }
    void drawRect(float x, float y, float w, float h) override { rectangle(x, y, w, h, false); }
    void fillRect(float x, float y, float w, float h) override { rectangle(x, y, w, h, true); }
    void drawRoundRect(float x, float y, float w, float h, float, float) override { rectangle(x, y, w, h, false); }
    void fillRoundRect(float x, float y, float w, float h, float, float) override { rectangle(x, y, w, h, true); }

private:
    void step(VectorPath::Step kind, std::initializer_list<float> coordinates) {
        _path.steps.push_back(kind);
        for (auto at = coordinates.begin(); at != coordinates.end(); at += 2) {
            const plutovg_point_t source{at[0], at[1]};
            plutovg_point_t       mapped{};
            plutovg_matrix_map_point(&_matrix, &source, &mapped);
            _path.points.push_back(mapped.x);
            _path.points.push_back(mapped.y);
        }
        _last = {*(coordinates.end() - 2), *(coordinates.end() - 1)};
    }
    void rectangle(float x, float y, float w, float h, bool filled) {
        beginPath(0);
        moveTo(x, y);
        lineTo(x + w, y);
        lineTo(x + w, y + h);
        lineTo(x, y + h);
        closePath();
        finish(filled);
    }
    void finish(bool filled) {
        // the engine speaks 0xAARRGGBB, a recording 0xAABBGGRR
        const std::uint32_t colour = (_colour & 0xFF00FF00U) | ((_colour & 0x00FF0000U) >> 16U) | ((_colour & 0x000000FFU) << 16U);
        _path.fill                 = filled ? colour : 0U;
        _path.stroke               = filled ? 0U : colour;
        _path.strokeWidth          = filled ? 0.0f : _stroke.lineWidth * std::sqrt(std::abs(_matrix.a * _matrix.d - _matrix.b * _matrix.c));
        _drawing.paths.push_back(std::move(_path));
        _path = VectorPath{};
    }

    VectorDrawing&       _drawing;
    VectorPath           _path;
    plutovg_matrix_t     _matrix{};
    std::array<float, 2> _last{0.0f, 0.0f};
    microtex::color      _colour = 0xFF000000U;
    microtex::Stroke     _stroke;
    float                _fontSize = kRenderSize;
    float                _scaleX   = 1.0f;
    float                _scaleY   = 1.0f;
};

/// loaded once; the metrics are in the binary, so this works the same natively and in a browser
[[nodiscard]] bool engineReady() {
    static const bool ready = [] {
        const microtex::FontSrcData source{kFiraMathClm2.size(), kFiraMathClm2.data(), "FiraMath"};
        microtex::MicroTeX::init(source);
        return microtex::MicroTeX::isInited();
    }();
    return ready;
}

} // namespace

namespace {
/// the engine's layout of a formula, or the reason it has none
[[nodiscard]] std::expected<std::unique_ptr<microtex::Render>, std::string> typeset(std::string_view latex, float pixels, std::uint32_t rgba, bool display) {
    if (latex.empty()) {
        return std::unexpected(std::string{"an empty formula"});
    }
    if (!engineReady()) {
        return std::unexpected(std::string{"the maths font could not be loaded"});
    }

    // MicroTeX throws on a formula it cannot parse; this project's own code must not, so it stops here and the
    // message becomes something the slide can show
    std::unique_ptr<microtex::Render> render;
    try {
        // the caller speaks ImGui's 0xAABBGGRR; MicroTeX wants 0xAARRGGBB
        const microtex::color            foreground = ((rgba & 0xFF000000U)) | ((rgba & 0x000000FFU) << 16U) | (rgba & 0x0000FF00U) | ((rgba & 0x00FF0000U) >> 16U);
        const microtex::OverrideTeXStyle style{display, display ? microtex::TexStyle::display : microtex::TexStyle::text};
        render.reset(microtex::MicroTeX::parse(std::string{latex}, kUnlimited, pixels, kNoLineSpace, foreground, true, style));
    } catch (const std::exception& failure) {
        return std::unexpected(std::string{failure.what()});
    } catch (...) {
        return std::unexpected(std::string{"the formula could not be parsed"});
    }
    if (render == nullptr) {
        return std::unexpected(std::string{"the formula produced nothing to draw"});
    }
    return render;
}
} // namespace

std::expected<Formula, std::string> renderFormula(std::string_view latex, float pixels, std::uint32_t rgba, bool display) {
    auto typeset = gr::present::typeset(latex, pixels, rgba, display);
    if (!typeset.has_value()) {
        return std::unexpected(typeset.error());
    }
    const std::unique_ptr<microtex::Render>& render = *typeset;

    const int width  = render->getWidth() + 2 * kMargin;
    const int height = render->getHeight() + 2 * kMargin;
    if (width <= 2 * kMargin || height <= 2 * kMargin) {
        return std::unexpected(std::string{"the formula has no extent"});
    }

    plutovg_surface_t* surface = plutovg_surface_create(width, height);
    plutovg_canvas_t*  canvas  = plutovg_canvas_create(surface);
    {
        PathGraphics graphics{canvas};
        render->draw(graphics, kMargin, kMargin);
    }

    Formula formula;
    formula.image.width  = static_cast<std::uint32_t>(width);
    formula.image.height = static_cast<std::uint32_t>(height);
    formula.image.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ);
    plutovg_convert_argb_to_rgba(formula.image.rgba.data(), plutovg_surface_get_data(surface), width, height, plutovg_surface_get_stride(surface));
    formula.baseline = render->getBaseline() * static_cast<float>(render->getHeight()) + static_cast<float>(kMargin);

    plutovg_canvas_destroy(canvas);
    plutovg_surface_destroy(surface);
    return formula;
}

std::expected<VectorDrawing, std::string> outlineFormula(std::string_view latex, float pixels, std::uint32_t rgba, bool display) {
    auto typeset = gr::present::typeset(latex, pixels, rgba, display);
    if (!typeset.has_value()) {
        return std::unexpected(typeset.error());
    }
    const std::unique_ptr<microtex::Render>& render = *typeset;
    if (render->getWidth() <= 0 || render->getHeight() <= 0) {
        return std::unexpected(std::string{"the formula has no extent"});
    }
    VectorDrawing   drawing{.width = static_cast<float>(render->getWidth() + 2 * kMargin), .height = static_cast<float>(render->getHeight() + 2 * kMargin), .paths = {}, .texts = {}};
    OutlineGraphics graphics{drawing};
    render->draw(graphics, kMargin, kMargin);
    return drawing;
}

} // namespace gr::present
