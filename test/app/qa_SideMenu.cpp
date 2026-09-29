#include <boost/ut.hpp>

#include "SideMenu.hpp"

using namespace boost::ut;
using namespace gr::present;

// Ground truth is the author's specification of the lens: the slide shown at 18 pt, and its neighbours shrinking to
// 8 pt within three or four entries either side, so that most of a deck stays within reach of the menu.
const suite<"SideMenu"> sideMenuTests = [] {
    "the slide being shown is named at 18 pt"_test = [] { expect(eq(SideMenu::lensPoints(0UZ), 18.0f)); };

    "names four or more entries away are at 8 pt"_test = [] {
        for (const std::size_t distance : {4UZ, 5UZ, 12UZ, 40UZ}) {
            expect(eq(SideMenu::lensPoints(distance), 8.0f)) << "at distance " << distance;
        }
    };

    "names shrink with every step away from the centre until they reach 8 pt"_test = [] {
        for (std::size_t distance = 0UZ; distance < 4UZ; ++distance) {
            expect(gt(SideMenu::lensPoints(distance), SideMenu::lensPoints(distance + 1UZ))) << "from " << distance << " to " << distance + 1UZ;
        }
        // a quarter turn of the drum over 3.5 entries: one entry out is at 8 + 10 cos(pi/7) = 17.01 pt
        expect(approx(SideMenu::lensPoints(1UZ), 17.01f, 0.01f));
    };
};

int main() { return 0; }
