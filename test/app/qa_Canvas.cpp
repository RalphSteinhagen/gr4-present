#include "Canvas.hpp"

#include <boost/ut.hpp>

using namespace boost::ut;
using namespace gr::present;

// Ground truth: by hand -- a rectangle at (10, 20), 30 wide and 40 high, spans (10, 20) to (40, 60)
const suite<"Canvas"> canvasTests = [] {
    "a rectangle's low corner is its origin and its high corner the origin plus its size"_test = [] {
        constexpr Rectangle kArea{.x = 10.0f, .y = 20.0f, .width = 30.0f, .height = 40.0f};
        expect(eq(lowCorner(kArea).x, 10.0f) && eq(lowCorner(kArea).y, 20.0f));
        expect(eq(highCorner(kArea).x, 40.0f) && eq(highCorner(kArea).y, 60.0f));
    };

    "an empty rectangle has both corners at its origin"_test = [] {
        constexpr Rectangle kPoint{.x = -5.0f, .y = 7.0f, .width = 0.0f, .height = 0.0f};
        expect(eq(highCorner(kPoint).x, lowCorner(kPoint).x) && eq(highCorner(kPoint).y, lowCorner(kPoint).y));
    };
};

int main() { return 0; }
