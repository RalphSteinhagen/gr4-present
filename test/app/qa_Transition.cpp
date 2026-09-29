#include <boost/ut.hpp>

#include "Transition.hpp"

#include <cmath>

using namespace boost::ut;
using namespace gr::present;

namespace {
/// float comparison at the precision these expectations are written to
[[nodiscard]] bool approx(float value, float wanted) noexcept { return std::abs(value - wanted) < 0.01f; }
} // namespace

const suite<"Transition"> transitionTests = [] {
    "sections on one master move the camera, unrelated ones cross-fade"_test = [] {
        expect(transitionKindFor("", true) == Transition::Kind::camera) << "one picture, so the move is a camera move";
        expect(transitionKindFor("", false) == Transition::Kind::fade) << "unrelated scenes cross-fade";
    };

    "an author's choice overrides the automatic one"_test = [] {
        expect(transitionKindFor("cut", true) == Transition::Kind::cut);
        expect(transitionKindFor("fade", true) == Transition::Kind::fade) << "camera would have been automatic here";
        expect(transitionKindFor("camera", false) == Transition::Kind::camera) << "fade would have been automatic here";
        expect(transitionKindFor("push", true) == Transition::Kind::push) << "an author's push wins over the automatic camera";
        expect(transitionKindFor("zoom", false) == Transition::Kind::zoom);
    };

    // a name the viewer does not move by itself names an effect shader; whether the deck or the viewer has it is found
    // out when the move begins, which falls back to the automatic choice when neither does
    "an unrecognised value names an effect, with or without its settings"_test = [] {
        expect(transitionKindFor("dissolve", true) == Transition::Kind::shader);
        expect(transitionKindFor("dissolve grain=40", false) == Transition::Kind::shader);
        expect(transitionKindFor("", true) == Transition::Kind::camera);
        expect(transitionKindFor("", false) == Transition::Kind::fade);
    };

    "progress is eased and clamped"_test = [] {
        Transition move{.kind = Transition::Kind::fade, .from = {}, .elapsed = 0.0f, .duration = 0.4f};
        expect(eq(move.progress(), 0.0f));
        move.elapsed = 0.2f;
        expect(std::abs(move.progress() - 0.5f) < 0.001f) << "smoothstep is symmetric about the midpoint";
        move.elapsed = 0.4f;
        expect(eq(move.progress(), 1.0f));
        move.elapsed = 4.0f;
        expect(eq(move.progress(), 1.0f)) << "overshooting the duration must not overshoot the move";
    };

    "easing starts and ends flat, so a move has no jerk"_test = [] {
        Transition move{.kind = Transition::Kind::camera, .from = {}, .elapsed = 0.0f, .duration = 1.0f};
        move.elapsed          = 0.05f;
        const float nearStart = move.progress();
        move.elapsed          = 0.5f;
        const float atMiddle  = move.progress();
        move.elapsed          = 0.95f;
        const float nearEnd   = move.progress();
        expect(lt(nearStart, 0.05f)) << "the first twentieth should cover less than a twentieth of the move";
        expect(gt(1.0f - nearEnd, 0.0f) && lt(1.0f - nearEnd, 0.05f));
        expect(std::abs(atMiddle - 0.5f) < 0.001f);
    };

    "a zero duration completes immediately rather than dividing by zero"_test = [] {
        const Transition move{.kind = Transition::Kind::fade, .from = {}, .elapsed = 0.0f, .duration = 0.0f};
        expect(eq(move.progress(), 1.0f));
    };

    "a running move is distinguishable from none"_test = [] {
        expect(!Transition{}.running());
        expect(Transition{.kind = Transition::Kind::fade, .from = {}, .elapsed = 0.0f, .duration = 0.4f}.running());
    };

    // Ground truth is the arithmetic of a zoom done by hand: the centre travels linearly, so halfway is the
    // average of the two centres; the size travels geometrically, so halfway is the geometric mean of the two
    // widths -- sqrt(100 x 40) = 63.2456, not the arithmetic 70.
    "the camera frame moves its centre linearly and its size geometrically"_test = [] {
        const Rectangle from{.x = 0.0f, .y = 0.0f, .width = 100.0f, .height = 50.0f};
        const Rectangle to{.x = 200.0f, .y = 100.0f, .width = 40.0f, .height = 20.0f};
        expect(interpolate(from, to, 0.0f) == from) << "the ends are exact";
        expect(interpolate(from, to, 1.0f) == to);

        const Rectangle middle = interpolate(from, to, 0.5f);
        expect(approx(middle.width, 63.2456f)) << "geometric mean of 100 and 40";
        expect(approx(middle.height, 31.6228f)) << "geometric mean of 50 and 20";
        expect(approx(middle.x + 0.5f * middle.width, 135.0f)) << "centre halfway between 50 and 220";
        expect(approx(middle.y + 0.5f * middle.height, 67.5f)) << "centre halfway between 25 and 110";
    };

    // A linear width would put halfway at 2270 and so spend the first half of the move barely zooming and the
    // second half rushing. The geometric mean of 3840 and 700 is 1639.51: the picture is already a little over
    // twice as close by halfway, and doubles again over the second half.
    "a zoom covers equal ratios in equal times, not equal widths"_test = [] {
        const Rectangle whole{.x = 0.0f, .y = 0.0f, .width = 3840.0f, .height = 2160.0f};
        const Rectangle detail{.x = 3000.0f, .y = 800.0f, .width = 700.0f, .height = 500.0f};
        const Rectangle middle = interpolate(whole, detail, 0.5f);
        expect(approx(middle.width, 1639.512f));
        expect(approx(middle.height, 1039.230f));
        expect(lt(middle.width, 2270.0f)) << "closer in by halfway than a linear zoom would be";
    };

    // A section with no anchor frames the whole document, and "the whole document" is a rectangle the size of
    // the master -- not the empty rectangle the absence of an anchor is recorded as. Interpolating the empty one
    // is what a zoom out of a detail did: the frame kept a positive width all the way to t=1, so the camera
    // crawled towards the origin and only then snapped to the full picture.
    "a frame with no size means the whole document"_test = [] {
        const Rectangle whole{.x = 0.0f, .y = 0.0f, .width = 3840.0f, .height = 2160.0f};
        expect(wholeIfEmpty(Rectangle{}, 3840.0f, 2160.0f) == whole) << "an absent anchor is the whole picture";
        const Rectangle detail{.x = 800.0f, .y = 400.0f, .width = 600.0f, .height = 300.0f};
        expect(wholeIfEmpty(detail, 3840.0f, 2160.0f) == detail) << "an anchor that has a size keeps it";
    };

    "zooming in from the whole picture starts at the whole picture"_test = [] {
        const Rectangle detail{.x = 3000.0f, .y = 800.0f, .width = 700.0f, .height = 500.0f};
        const Rectangle from   = wholeIfEmpty(Rectangle{}, 3840.0f, 2160.0f);
        const Rectangle middle = interpolate(from, detail, 0.5f);
        expect(eq(interpolate(from, detail, 0.0f).width, 3840.0f)) << "at the start the whole picture is in shot";
        expect(approx(middle.x + 0.5f * middle.width, 2635.0f)) << "the centre travels from 1920 to 3350";
        expect(interpolate(from, detail, 1.0f) == detail);
    };

    "zooming back out ends on the whole picture, not on the origin"_test = [] {
        const Rectangle detail{.x = 3000.0f, .y = 800.0f, .width = 700.0f, .height = 500.0f};
        const Rectangle to     = wholeIfEmpty(Rectangle{}, 3840.0f, 2160.0f);
        const Rectangle middle = interpolate(detail, to, 0.5f);
        expect(approx(middle.width, 1639.512f)) << "the frame grows towards the full width";
        expect(approx(middle.x + 0.5f * middle.width, 2635.0f)) << "and the centre walks back towards the middle";
        expect(interpolate(detail, to, 1.0f) == to);
    };

    // Ground truth: van Wijk and Nuij's closed form worked by hand for a symmetric pan. With rho = 1, widths 10 and
    // a distance of 40, b0 = 1600 / 800 = 2 and b1 = -2, so r0 = asinh(-2) = -r1; halfway rho s + r0 = 0 and the
    // width is 10 cosh(asinh 2) = 10 sqrt(5) = 22.3607, with the centre halfway, by symmetry.
    "a long pan pulls back by the amount van Wijk and Nuij's optimal path gives"_test = [] {
        const Rectangle from{.x = -5.0f, .y = -5.0f, .width = 10.0f, .height = 10.0f};
        const Rectangle to{.x = 35.0f, .y = -5.0f, .width = 10.0f, .height = 10.0f};
        const Rectangle middle = zoomAndPan(from, to, 0.5f, 1.0);
        expect(approx(middle.width, 22.3607f)) << "the width halfway, " << middle.width;
        expect(approx(middle.x + 0.5f * middle.width, 20.0f)) << "and the centre halfway";
        expect(zoomAndPan(from, to, 0.0f, 1.0) == from && zoomAndPan(from, to, 1.0f, 1.0) == to) << "the ends are exact";
    };

    "with the centres together the optimal path is the plain geometric zoom"_test = [] {
        const Rectangle whole{.x = 0.0f, .y = 0.0f, .width = 400.0f, .height = 200.0f};
        const Rectangle close{.x = 150.0f, .y = 75.0f, .width = 100.0f, .height = 50.0f};
        const Rectangle middle = zoomAndPan(whole, close, 0.5f);
        expect(approx(middle.width, 200.0f)) << "geometric mean of 400 and 100";
        expect(approx(middle.x + 0.5f * middle.width, 200.0f) && approx(middle.y + 0.5f * middle.height, 100.0f)) << "and the centre stays";
        const Rectangle nudged{.x = 150.001f, .y = 75.0f, .width = 100.0f, .height = 50.0f};
        expect(std::abs(zoomAndPan(whole, nudged, 0.5f).width - middle.width) < 0.01f) << "a centre moved by a thousandth of a pixel changes nothing visible";
    };

    "the farther the move, the more the camera pulls back"_test = [] {
        const Rectangle from{.x = 0.0f, .y = 0.0f, .width = 100.0f, .height = 50.0f};
        const auto      widthHalfway = [&from](float distance) { return zoomAndPan(from, Rectangle{.x = distance, .y = 0.0f, .width = 100.0f, .height = 50.0f}, 0.5f).width; };
        expect(gt(widthHalfway(800.0f), widthHalfway(200.0f)) and gt(widthHalfway(200.0f), 100.0f));
    };

    "a transition's name says what moves and which way"_test = [] {
        expect(transitionKindFor("push-left", false) == Transition::Kind::push && transitionDirectionFor("push-left") == Transition::Direction::left);
        expect(transitionKindFor("cover-up", false) == Transition::Kind::cover && transitionDirectionFor("cover-up") == Transition::Direction::up);
        expect(transitionKindFor("uncover-down", false) == Transition::Kind::uncover && transitionDirectionFor("uncover-down") == Transition::Direction::down);
        expect(transitionKindFor("fade-through", true) == Transition::Kind::fadeThrough) << "not a fade with a direction";
        expect(transitionDirectionFor("push") == Transition::Direction::byOrder) << "no direction follows the deck's order";
        expect(transitionKindFor("sideways", true) == Transition::Kind::shader) << "an unknown name is an effect's";
        expect(transitionKindFor("camera ripple", true) == Transition::Kind::camera) << "a camera move seen through an effect is still a camera move";
        expect(transitionKindFor("zoom lens strength=2", false) == Transition::Kind::zoom);
    };

    // Ground truth: Keynote's Magic Move as the author specified it -- the same key is the same thing, and a key
    // repeated on a slide pairs in order
    "a morph pairs what has the same key, the n-th repeat with the n-th, and leaves the rest"_test = [] {
        const std::vector<std::string>                         leaving{"#title", "text:a", "text:b", "text:a"};
        const std::vector<std::string>                         arriving{"text:a", "#title", "text:c", "text:a"};
        const auto                                             pairs = matchedByKey(leaving, arriving);
        const std::vector<std::pair<std::size_t, std::size_t>> expected{{0UZ, 1UZ}, {1UZ, 0UZ}, {3UZ, 3UZ}};
        expect(pairs == expected) << "the title, then each `text:a` in turn; `text:b` and `text:c` have no partner";
        expect(matchedByKey(leaving, std::vector<std::string>{}).empty());
    };
};

int main() { return 0; }
