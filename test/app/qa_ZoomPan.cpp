#include <boost/ut.hpp>

#include "ZoomPan.hpp"

using namespace boost::ut;
using namespace gr::present;

namespace {
constexpr float kWidth  = 1280.0f;
constexpr float kHeight = 720.0f;
} // namespace

// Ground truth is what a person expects of a map: the point under the fingers stays under the fingers while
// zooming, the picture never slides away from the edges of the screen, and there is a limit either way.
const suite<"ZoomPan"> zoomPanTests = [] {
    "a fresh view is the untouched slide"_test = [] {
        const ZoomPan view;
        expect(!view.active());
        expect(eq(view.screenX(300.0f), 300.0f));
        expect(eq(view.screenY(200.0f), 200.0f));
    };

    "zooming keeps the point under the cursor where it is"_test = [] {
        ZoomPan view;
        view.zoomAt(800.0f, 300.0f, 2.0f, kWidth, kHeight);
        expect(eq(view.scale, 2.0f));
        // slide point (800, 300) was at screen (800, 300); after doubling it must still be there
        expect(eq(view.screenX(800.0f), 800.0f));
        expect(eq(view.screenY(300.0f), 300.0f));
        expect(view.active());
    };

    "zooming in on the top-left corner needs no offset"_test = [] {
        ZoomPan view;
        view.zoomAt(0.0f, 0.0f, 3.0f, kWidth, kHeight);
        expect(eq(view.offsetX, 0.0f));
        expect(eq(view.offsetY, 0.0f));
    };

    "the slide keeps covering the viewport, so the far corner cannot be pulled inside it"_test = [] {
        ZoomPan view;
        view.zoomAt(kWidth, kHeight, 2.0f, kWidth, kHeight);
        expect(eq(view.screenX(kWidth), kWidth)) << "the bottom-right corner stays in the bottom-right corner";
        view.panBy(-5000.0f, -5000.0f, kWidth, kHeight);
        expect(eq(view.screenX(kWidth), kWidth));
        expect(eq(view.screenY(kHeight), kHeight));
        view.panBy(5000.0f, 5000.0f, kWidth, kHeight);
        expect(eq(view.screenX(0.0f), 0.0f));
        expect(eq(view.screenY(0.0f), 0.0f));
    };

    "panning moves the slide with the finger while there is room"_test = [] {
        ZoomPan view;
        view.zoomAt(640.0f, 360.0f, 2.0f, kWidth, kHeight);
        const float before = view.screenX(100.0f);
        view.panBy(-40.0f, 0.0f, kWidth, kHeight);
        expect(eq(view.screenX(100.0f), before - 40.0f));
    };

    "zoom is limited to one and four"_test = [] {
        ZoomPan view;
        view.zoomAt(100.0f, 100.0f, 100.0f, kWidth, kHeight);
        expect(eq(view.scale, ZoomPan::kMaximumScale));
        view.zoomAt(100.0f, 100.0f, 0.001f, kWidth, kHeight);
        expect(eq(view.scale, 1.0f));
        expect(eq(view.offsetX, 0.0f)) << "zooming fully out returns the slide to the origin wherever it was panned";
        expect(eq(view.offsetY, 0.0f));
    };

    "panning an unzoomed slide does nothing"_test = [] {
        ZoomPan view;
        view.panBy(50.0f, -50.0f, kWidth, kHeight);
        expect(eq(view.offsetX, 0.0f));
        expect(eq(view.offsetY, 0.0f));
    };

    "resetting returns to the untouched slide"_test = [] {
        ZoomPan view;
        view.zoomAt(900.0f, 500.0f, 3.0f, kWidth, kHeight);
        view.reset();
        expect(!view.active());
        expect(eq(view.offsetX, 0.0f));
    };

    "a shrinking viewport re-clamps the offset on the next pan"_test = [] {
        ZoomPan view;
        view.zoomAt(kWidth, kHeight, 2.0f, kWidth, kHeight); // offset -1280, -720
        view.panBy(0.0f, 0.0f, 640.0f, 360.0f);
        expect(eq(view.screenX(640.0f), 640.0f));
    };
};

int main() { return 0; }
