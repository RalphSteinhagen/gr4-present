#include "Transition.hpp"

#include <cmath>

#include <algorithm>
#include <array>

namespace gr::present {

float Transition::progress() const noexcept {
    if (duration <= 0.0f) {
        return 1.0f;
    }
    return smoothstep(elapsed / duration);
}

Transition::Direction transitionDirectionFor(std::string_view requested) noexcept {
    const std::string_view direction = requested.substr(std::min(requested.find('-'), requested.size()));
    return direction == "-left" ? Transition::Direction::left : direction == "-right" ? Transition::Direction::right : direction == "-up" ? Transition::Direction::up : direction == "-down" ? Transition::Direction::down : Transition::Direction::byOrder;
}

std::vector<std::pair<std::size_t, std::size_t>> matchedByKey(std::span<const std::string> leaving, std::span<const std::string> arriving) {
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    std::vector<bool>                                taken(arriving.size(), false);
    for (std::size_t from = 0UZ; from < leaving.size(); ++from) {
        for (std::size_t to = 0UZ; to < arriving.size(); ++to) {
            if (!taken[to] && arriving[to] == leaving[from]) {
                taken[to] = true;
                pairs.emplace_back(from, to);
                break;
            }
        }
    }
    return pairs;
}

Transition::Kind transitionKindFor(std::string_view requested, bool sameMaster) noexcept {
    if (requested == "morph") {
        return Transition::Kind::morph;
    }
    requested = requested.substr(0UZ, requested.find(' ')); // `ripple speed=2` is the effect ripple
    if (!requested.empty() && !std::ranges::contains(kTransitionNames, requested)) {
        return Transition::Kind::shader; // whether the effect exists is the viewer's to find out
    }
    if (requested == "fade-through") {
        return Transition::Kind::fadeThrough;
    }
    requested = requested.substr(0UZ, requested.find('-')); // `push-left` is a push
    if (requested == "cover") {
        return Transition::Kind::cover;
    }
    if (requested == "uncover") {
        return Transition::Kind::uncover;
    }
    if (requested == "cut") {
        return Transition::Kind::cut;
    }
    if (requested == "fade") {
        return Transition::Kind::fade;
    }
    if (requested == "camera") {
        return Transition::Kind::camera;
    }
    if (requested == "push") {
        return Transition::Kind::push;
    }
    if (requested == "zoom") {
        return Transition::Kind::zoom;
    }
    // one master means one picture, so moving between its parts is a camera move rather than a change of scene
    return sameMaster ? Transition::Kind::camera : Transition::Kind::fade;
}

Rectangle interpolate(const Rectangle& from, const Rectangle& to, float t) noexcept {
    if (t <= 0.0f) {
        return from; // and the ends are exact, which a pow() of a ratio would not quite be
    }
    if (t >= 1.0f) {
        return to;
    }
    // The centre travels linearly and the size geometrically. A zoom whose width is interpolated linearly appears
    // to accelerate, because what the eye reads is the ratio between one frame and the next rather than their
    // difference -- over the 5.8x zoom this deck asks for, that is the difference between a move and a lurch.
    // J. J. van Wijk and W. A. A. Nuij, "Smooth and efficient zooming and panning", IEEE Symposium on Information
    // Visualization, 2003, pp. 15-22.
    const auto mix    = [t](float a, float b) { return a + (b - a) * t; };
    const auto scaled = [t](float a, float b) { return a > 0.0f && b > 0.0f ? a * std::pow(b / a, t) : a + (b - a) * t; };

    const float width  = scaled(from.width, to.width);
    const float height = scaled(from.height, to.height);
    return Rectangle{.x = mix(from.x + 0.5f * from.width, to.x + 0.5f * to.width) - 0.5f * width, .y = mix(from.y + 0.5f * from.height, to.y + 0.5f * to.height) - 0.5f * height, .width = width, .height = height};
}

Rectangle zoomAndPan(const Rectangle& from, const Rectangle& to, float t, double rho) noexcept {
    if (t <= 0.0f) {
        return from;
    }
    if (t >= 1.0f) {
        return to;
    }
    const auto wide             = [](const Rectangle& r) { return std::array{static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.width), static_cast<double>(r.height)}; };
    const auto [x0, y0, w0, h0] = wide(from);
    const auto [x1, y1, w1, h1] = wide(to);
    if (w0 <= 0.0 || w1 <= 0.0 || h0 <= 0.0 || h1 <= 0.0) {
        return interpolate(from, to, t);
    }
    const double dx       = (x1 + 0.5 * w1) - (x0 + 0.5 * w0);
    const double dy       = (y1 + 0.5 * h1) - (y0 + 0.5 * h0);
    const double distance = std::hypot(dx, dy);
    const double rho2     = rho * rho;
    double       u        = 0.0; // along the line between the centres
    double       w        = w0;  // the frame's width
    if (rho2 * distance < 1e-6 * std::max(w0, w1)) {
        // the centres coincide: a pure zoom, w(s) = w0 exp(k rho s) with S = |ln(w1/w0)| / rho
        w = w0 * std::pow(w1 / w0, static_cast<double>(t));
    } else {
        const auto   branch = [](double b) { return std::log(-b + std::sqrt(b * b + 1.0)); };
        const double r0     = branch((w1 * w1 - w0 * w0 + rho2 * rho2 * distance * distance) / (2.0 * w0 * rho2 * distance));
        const double r1     = branch((w1 * w1 - w0 * w0 - rho2 * rho2 * distance * distance) / (2.0 * w1 * rho2 * distance));
        const double s      = static_cast<double>(t) * (r1 - r0) / rho;
        u                   = w0 / rho2 * std::cosh(r0) * std::tanh(rho * s + r0) - w0 / rho2 * std::sinh(r0);
        w                   = w0 * std::cosh(r0) / std::cosh(rho * s + r0);
    }
    const double aspect0 = h0 / w0;
    const double aspect  = aspect0 * std::pow((h1 / w1) / aspect0, static_cast<double>(t));
    const double along   = distance > 0.0 ? u / distance : 0.0;
    const double cx      = x0 + 0.5 * w0 + dx * along;
    const double cy      = y0 + 0.5 * h0 + dy * along;
    const double h       = w * aspect;
    return Rectangle{.x = static_cast<float>(cx - 0.5 * w), .y = static_cast<float>(cy - 0.5 * h), .width = static_cast<float>(w), .height = static_cast<float>(h)};
}

Rectangle wholeIfEmpty(const Rectangle& frame, float width, float height) noexcept { return frame.width > 0.0f && frame.height > 0.0f ? frame : Rectangle{.x = 0.0f, .y = 0.0f, .width = width, .height = height}; }

} // namespace gr::present
