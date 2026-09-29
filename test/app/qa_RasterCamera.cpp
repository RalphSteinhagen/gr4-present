#include <boost/ut.hpp>

#include "RasterCamera.hpp"
#include "Transition.hpp"

#include <cmath>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] bool approx(float value, float wanted, float tolerance = 0.0005f) noexcept { return std::abs(value - wanted) < tolerance; }

using Fields = std::vector<std::pair<std::string, std::string>>;

/// the regions of the demonstration image, measured on it and written as fractions. Immortal, because the suite
/// below reads it from a static destructor and a container destroyed first is a container read after free.
const Fields& kScienceFair = *new Fields{
    {"source", "media/Science_FAIR.webp"},
    {"fair-stand", "0.8125 0.37037 0.171875 0.212963"},
    {"charlemagne", "0.369792 0.092593 0.109375 0.324074"},
    {"wally", "0.242188 0.333333 0.078125 0.148148"},
};

} // namespace

// Ground truth is measurement and arithmetic, both independent of the code: the fractions were read off the
// 3840x2160 image with a crop tool and are checked here by multiplying them back up to the pixel boxes they
// came from, and the aspect arithmetic is worked out by hand from the ratio it has to reach.
const suite<"RasterCamera"> rasterCameraTests = [] {
    "a regions directive names boxes and skips its own source"_test = [] {
        const std::vector<Area> regions = regionsOf(kScienceFair);
        expect(eq(regions.size(), 3UZ)) << "source configures the directive, it does not name a region";
        expect(regions[0].id == "fair-stand");
        expect(regions[2].id == "wally");
    };

    "fractions are the picture's own coordinates, whatever it was last encoded as"_test = [] {
        const std::vector<Area> regions = regionsOf(kScienceFair);
        // 0.8125 x 3840 = 3120 and 0.37037 x 2160 = 800, which is the crop the region was measured from. The
        // tolerance is a hundredth of a pixel: the fractions are written to six places, so they reproduce the
        // measurement to well under a pixel but not to the last bit of a float.
        constexpr float kPixel = 0.01f;
        expect(approx(regions[0].x * 3840.0f, 3120.0f, kPixel));
        expect(approx(regions[0].y * 2160.0f, 800.0f, kPixel));
        expect(approx(regions[0].width * 3840.0f, 660.0f, kPixel));
        expect(approx(regions[0].height * 2160.0f, 460.0f, kPixel));
        // and the same fractions address the same subject on a half-size copy of the picture
        expect(approx(regions[0].x * 1920.0f, 1560.0f, kPixel)) << "a re-encode at another size must not move the camera";
    };

    "a malformed region is skipped rather than framing nothing"_test = [] {
        const Fields            broken{{"three-numbers", "0.1 0.2 0.3"}, {"not-numbers", "left top wide high"}, {"zero-sized", "0.1 0.2 0.0 0.4"}, {"good", "0.1 0.2 0.3 0.4"}};
        const std::vector<Area> regions = regionsOf(broken);
        expect(eq(regions.size(), 1UZ)) << "only the well-formed one survives";
        expect(regions[0].id == "good");
    };

    "the camera keeps the title still unless the author asks otherwise"_test = [] {
        expect(cameraScopeOf("slide") == CameraScope::slide);
        expect(cameraScopeOf("image") == CameraScope::image);
        expect(cameraScopeOf("") == CameraScope::image) << "the default preserves the title and footer";
        expect(cameraScopeOf("everything") == CameraScope::image) << "an unrecognised value is not a third behaviour";
    };

    // Charlemagne is a tall box -- 0.109375 x 0.324074 of a 16:9 picture, which is 420 x 700 pixels, taller
    // than it is wide. On a 16:9 screen it must be widened, not letterboxed: the wanted ratio in fraction
    // space is 1.7778 / 1.7778 = 1, so the width must grow to equal the height, 0.324074.
    "a tall region is widened to the screen rather than letterboxed"_test = [] {
        const Rectangle charlemagne{.x = 0.369792f, .y = 0.092593f, .width = 0.109375f, .height = 0.324074f};
        const Rectangle fitted = fittedToAspect(charlemagne, 16.0f / 9.0f, 16.0f / 9.0f);
        expect(approx(fitted.width, 0.324074f)) << "widened until it has the screen's shape";
        expect(approx(fitted.height, 0.324074f)) << "and never cropped to get there";
        expect(approx(fitted.x + 0.5f * fitted.width, 0.369792f + 0.5f * 0.109375f)) << "grown about its centre";
        expect(approx(fitted.y, charlemagne.y)) << "the dimension that already fitted is untouched";
    };

    // A phone held upright wants a frame much taller than it is wide. On a 16:9 picture the ratio to reach in
    // fraction space is (9/16) / (16/9) = 0.316406, so a band 0.6 wide would need to be 0.6 / 0.316406 = 1.8963
    // deep -- nearly twice the height the picture has. The frame takes all the height there is and stops; the
    // shortfall that remains is the drawing's to letterbox, because no framing of this picture can fill that
    // screen.
    "a wide region takes all the depth the picture has and no more"_test = [] {
        const Rectangle band{.x = 0.2f, .y = 0.4f, .width = 0.6f, .height = 0.1f};
        const Rectangle fitted = fittedToAspect(band, 9.0f / 16.0f, 16.0f / 9.0f);
        expect(approx(fitted.width, 0.6f)) << "the width already covered enough";
        expect(approx(fitted.height, 1.0f)) << "1.8963 was wanted and 1.0 is all there is";
        expect(approx(fitted.y, 0.0f)) << "so the frame sits against the top edge";
    };

    "a region on a screen it can be fitted to is deepened rather than letterboxed"_test = [] {
        const Rectangle band{.x = 0.2f, .y = 0.4f, .width = 0.3f, .height = 0.1f};
        // a 4:3 screen on a 16:9 picture wants 1.3333 / 1.7778 = 0.75, so 0.3 wide needs 0.4 deep
        const Rectangle fitted = fittedToAspect(band, 4.0f / 3.0f, 16.0f / 9.0f);
        expect(approx(fitted.width, 0.3f));
        expect(approx(fitted.height, 0.4f)) << "0.3 / 0.75, and the picture has room for it";
        expect(approx(fitted.y + 0.5f * fitted.height, 0.45f)) << "grown about the band's own centre";
    };

    "a frame cannot be grown outside the picture"_test = [] {
        const Rectangle corner{.x = 0.9f, .y = 0.9f, .width = 0.1f, .height = 0.1f};
        const Rectangle fitted = fittedToAspect(corner, 16.0f / 9.0f, 1.0f);
        expect(ge(fitted.x, 0.0f)) << "never off the left edge";
        expect(le(fitted.x + fitted.width, 1.0001f)) << "and never off the right";
        expect(le(fitted.height, 1.0f));
    };

    "a region that is already the screen's shape is left alone"_test = [] {
        const Rectangle square{.x = 0.25f, .y = 0.25f, .width = 0.5f, .height = 0.5f};
        const Rectangle fitted = fittedToAspect(square, 1.0f, 1.0f);
        expect(approx(fitted.x, 0.25f) && approx(fitted.y, 0.25f));
        expect(approx(fitted.width, 0.5f) && approx(fitted.height, 0.5f));
    };

    // The move the author asked for: from Charlemagne in the middle of the square out to the whole picture and
    // back in to Wally on the left. Halfway is the waypoint exactly, because each leg gets half the time.
    "a waypoint makes the move pull back before it travels"_test = [] {
        const Rectangle charlemagne{.x = 0.369792f, .y = 0.092593f, .width = 0.109375f, .height = 0.324074f};
        const Rectangle wally{.x = 0.242188f, .y = 0.333333f, .width = 0.078125f, .height = 0.148148f};
        const Rectangle whole{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};

        expect(interpolateVia(charlemagne, whole, wally, 0.0f) == charlemagne);
        expect(interpolateVia(charlemagne, whole, wally, 1.0f) == wally);
        const Rectangle halfway = interpolateVia(charlemagne, whole, wally, 0.5f);
        expect(approx(halfway.x, whole.x, 1e-5f) && approx(halfway.y, whole.y, 1e-5f) && approx(halfway.width, whole.width, 1e-5f) && approx(halfway.height, whole.height, 1e-5f)) << "halfway is the waypoint itself";

        const Rectangle quarter = interpolateVia(charlemagne, whole, wally, 0.25f);
        expect(gt(quarter.width, charlemagne.width)) << "the first leg pulls back";
        expect(lt(quarter.width, whole.width));
        const Rectangle threeQuarters = interpolateVia(charlemagne, whole, wally, 0.75f);
        expect(lt(threeQuarters.width, whole.width)) << "and the second leg pushes in again";
        expect(gt(threeQuarters.width, wally.width));
    };

    // A corner at the waypoint was what made the move read as a stutter: two smooth legs whose zoom rates flip from
    // zooming out to zooming in, at the moment the move is fastest. The ground truth is calculus -- the slope of the
    // logarithm of the width, taken either side of the middle, must agree.
    "the pull-back turns round without a corner"_test = [] {
        const Rectangle charlemagne{.x = 0.369792f, .y = 0.092593f, .width = 0.109375f, .height = 0.324074f};
        const Rectangle wally{.x = 0.242188f, .y = 0.333333f, .width = 0.078125f, .height = 0.148148f};
        const Rectangle whole{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};

        constexpr float h        = 0.02f;
        const auto      logWidth = [&](float t) { return std::log(interpolateVia(charlemagne, whole, wally, t).width); };
        const float     before   = (logWidth(0.5f) - logWidth(0.5f - h)) / h;
        const float     after    = (logWidth(0.5f + h) - logWidth(0.5f)) / h;
        const float     outward  = (logWidth(0.25f) - logWidth(0.0f)) / 0.25f; // the average rate of the pull-back

        expect(gt(outward, 1.0f)) << "the pull-back is a real zoom, not a nudge";
        expect(lt(std::abs(after - before), 0.15f * outward)) << "the zoom rate is continuous through the waypoint";
    };

    // The frames of the Wally move as they are drawn on a wide screen, sampled as often as a display would. Ground
    // truth is what the audience sees: the centre accelerates and brakes smoothly instead of jumping, the camera
    // never shows a frame smaller than the stops or wider than the allowed pull-back, and at the top of the
    // pull-back the whole picture is in view.
    "the move to Wally is one smooth pull-back, travel and push-in"_test = [] {
        const Rectangle charlemagne{.x = 0.369792f, .y = 0.092593f, .width = 0.109375f, .height = 0.324074f};
        const Rectangle wally{.x = 0.242188f, .y = 0.333333f, .width = 0.078125f, .height = 0.148148f};
        const Rectangle whole{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};
        constexpr float screenAspect = 2.4f; // the room below the heading and the words of a 16:9 window
        constexpr float imageAspect  = 16.0f / 9.0f;
        constexpr int   kSteps       = 120;

        const Rectangle start = cameraFrameAt(charlemagne, whole, wally, 0.0f, screenAspect, imageAspect);
        const Rectangle end   = cameraFrameAt(charlemagne, whole, wally, 1.0f, screenAspect, imageAspect);
        const Rectangle restA = fittedToAspect(charlemagne, screenAspect, imageAspect);
        const Rectangle restB = fittedToAspect(wally, screenAspect, imageAspect);
        expect(start == restA) << "the move begins on the frame the first stop shows at rest";
        expect(end == restB) << "and ends on the frame the second shows";

        std::vector<Rectangle> frames;
        for (int i = 0; i <= kSteps; ++i) {
            frames.push_back(cameraFrameAt(charlemagne, whole, wally, static_cast<float>(i) / kSteps, screenAspect, imageAspect));
        }

        float widest        = 0.0f;
        int   widestAt      = 0;
        float largestStep   = 0.0f;
        float largestChange = 0.0f;
        for (int i = 1; i <= kSteps; ++i) {
            const float stepX = (frames[static_cast<std::size_t>(i)].x + 0.5f * frames[static_cast<std::size_t>(i)].width) - (frames[static_cast<std::size_t>(i - 1)].x + 0.5f * frames[static_cast<std::size_t>(i - 1)].width);
            largestStep       = std::max(largestStep, std::abs(stepX));
            if (i >= 2) {
                const float previousStepX = (frames[static_cast<std::size_t>(i - 1)].x + 0.5f * frames[static_cast<std::size_t>(i - 1)].width) - (frames[static_cast<std::size_t>(i - 2)].x + 0.5f * frames[static_cast<std::size_t>(i - 2)].width);
                largestChange             = std::max(largestChange, std::abs(stepX - previousStepX));
            }
            if (frames[static_cast<std::size_t>(i)].width > widest) {
                widest   = frames[static_cast<std::size_t>(i)].width;
                widestAt = i;
            }
        }
        expect(lt(largestChange, 0.15f * largestStep)) << "the centre speeds up and slows down gradually; a stick-and-jump is a step that changes by most of itself";

        const Rectangle& apex = frames[static_cast<std::size_t>(widestAt)];
        expect(ge(widestAt, kSteps / 3) && le(widestAt, 2 * kSteps / 3)) << "the widest frame is in the middle of the move";
        const Rectangle& halfway = frames[kSteps / 2];
        expect(le(halfway.x, 0.0f) && ge(halfway.x + halfway.width, 1.0f - 1e-4f) && le(halfway.y, 0.0f) && ge(halfway.y + halfway.height, 1.0f - 1e-4f)) << "halfway the whole picture is in view";
        expect(ge(apex.width, halfway.width)) << "and the pull-back goes no less far than that";
        expect(le(widest, kMaxPullBack * screenAspect / imageAspect + 1e-4f)) << "the pull-back stays within what is allowed";

        // one rise and one fall: the width never turns round except at the top
        for (int i = 1; i <= kSteps; ++i) {
            const float change = frames[static_cast<std::size_t>(i)].width - frames[static_cast<std::size_t>(i - 1)].width;
            expect(i <= widestAt ? ge(change, -1e-6f) : le(change, 1e-6f)) << "step" << i;
        }
    };

    "a waypoint far beyond the picture is held to the allowed pull-back"_test = [] {
        const Rectangle near{.x = 0.4f, .y = 0.4f, .width = 0.1f, .height = 0.1f};
        const Rectangle vast{.x = -2.0f, .y = -2.0f, .width = 5.0f, .height = 5.0f};
        constexpr float screenAspect = 2.4f;
        constexpr float imageAspect  = 16.0f / 9.0f;

        const Rectangle apex      = cameraFrameAt(near, vast, near, 0.5f, screenAspect, imageAspect);
        const float     wholeFits = screenAspect / imageAspect; // the width of the smallest screen-shaped frame holding the picture
        expect(approx(apex.width, kMaxPullBack * wholeFits, 1e-4f)) << "the widest frame is 50 % beyond the one that just holds the picture";
        expect(approx(apex.width / apex.height, screenAspect / imageAspect, 1e-4f)) << "and keeps the screen's shape";
    };

    // without a waypoint the camera takes van Wijk and Nuij's path, whose numbers qa_Transition checks; with one it
    // keeps the author's pull-back rather than adding its own
    "without a waypoint the move pulls back by itself, as the optimal path does"_test = [] {
        const Rectangle from{.x = 0.1f, .y = 0.1f, .width = 0.2f, .height = 0.2f};
        const Rectangle to{.x = 0.7f, .y = 0.7f, .width = 0.2f, .height = 0.2f};
        const Rectangle middle = interpolateVia(from, Rectangle{}, to, 0.5f);
        expect(middle == zoomAndPan(from, to, 0.5f));
        expect(gt(middle.width, 0.2f)) << "a move across the picture pulls back on the way";
    };

    // a 1280x720 screen with a 300x400 face in its middle: 490 px either side, 160 above and below
    static constexpr Rectangle kScreen{.x = 0.0f, .y = 0.0f, .width = 1280.0f, .height = 720.0f};
    static constexpr Rectangle kFace{.x = 490.0f, .y = 160.0f, .width = 300.0f, .height = 400.0f};

    "an info box goes beside the face on the side with more room, top-aligned with it"_test = [] {
        constexpr Rectangle offCentre{.x = 400.0f, .y = 160.0f, .width = 300.0f, .height = 400.0f}; // 400 left, 580 right
        const Rectangle     box = besideRegion(offCentre, kScreen, 360.0f, 200.0f, "", 16.0f);
        expect(eq(box.x, 716.0f)) << "right of the face, one gap away";
        expect(eq(box.y, 160.0f));
        const Rectangle mirrored = besideRegion(Rectangle{.x = 580.0f, .y = 160.0f, .width = 300.0f, .height = 400.0f}, kScreen, 360.0f, 200.0f, "", 16.0f);
        expect(eq(mirrored.x, 580.0f - 16.0f - 360.0f)) << "left when the left has more room";
    };

    "a stated side wins over the roomier one"_test = [] {
        const Rectangle box = besideRegion(kFace, kScreen, 360.0f, 120.0f, "below", 16.0f);
        expect(eq(box.y, 576.0f));
        expect(eq(box.x, 490.0f + (300.0f - 360.0f) * 0.5f)) << "centred under the face";
    };

    "too wide for either side, the box goes above or below"_test = [] {
        const Rectangle box = besideRegion(kFace, kScreen, 600.0f, 100.0f, "", 16.0f);
        expect(box.y + box.height <= kFace.y || box.y >= kFace.y + kFace.height) << "not over the face";
    };

    "a box never leaves the screen"_test = [] {
        const Rectangle box = besideRegion(Rectangle{.x = 900.0f, .y = 600.0f, .width = 300.0f, .height = 100.0f}, kScreen, 360.0f, 300.0f, "right", 16.0f);
        expect(ge(box.x, 16.0f) && le(box.x + box.width, 1280.0f - 16.0f));
        expect(ge(box.y, 16.0f) && le(box.y + box.height, 720.0f - 16.0f));
    };
};

int main() { return 0; }
