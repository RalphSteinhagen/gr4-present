#include <boost/ut.hpp>

#include "SideMenu.hpp"

using namespace boost::ut;
using namespace gr::present;

// Ground truth: the author's list rules (2026-10-10), in interface points -- the shown slide bold at the controls' 15 pt;
// every other slide one size, 16 pt by default, or smaller to fit them all; never below 12.5 pt, the list scrolling
// instead. A line is 1.25 of its type size, so n - 1 names at s beside the shown one take 1.25 ((n - 1) s + 15).
const suite<"SideMenu"> sideMenuTests = [] {
    "a few slides are named at 16 pt without scrolling"_test = [] {
        const SideMenu::ListSizes sizes = SideMenu::listSizes(5UZ, 300.0f);
        expect(eq(sizes.otherPoints, 16.0f));
        expect(!sizes.scrolls);
    };

    "more slides share one smaller size that exactly fills the room"_test = [] {
        // 30 names in 500 pt: (500 / 1.25 - 15) / 29 = 13.2759 pt
        const SideMenu::ListSizes sizes = SideMenu::listSizes(30UZ, 500.0f);
        expect(approx(sizes.otherPoints, 13.2759f, 0.001f));
        expect(!sizes.scrolls);
        expect(approx(1.25f * (29.0f * sizes.otherPoints + 15.0f), 500.0f, 0.01f)) << "and the list fills the room";
    };

    "names that would fall below 12.5 pt stay at 12.5 pt and the list scrolls"_test = [] {
        // 60 names in 500 pt would need (400 - 15) / 59 = 6.53 pt
        const SideMenu::ListSizes sizes = SideMenu::listSizes(60UZ, 500.0f);
        expect(eq(sizes.otherPoints, 12.5f));
        expect(sizes.scrolls);
    };

    "exactly 12.5 pt still fits without scrolling"_test = [] {
        // 20 names at 12.5 pt beside the shown one at 15 pt take 1.25 (20 * 12.5 + 15) = 331.25 pt
        const SideMenu::ListSizes sizes = SideMenu::listSizes(21UZ, 331.25f);
        expect(approx(sizes.otherPoints, 12.5f, 1e-4f));
        expect(!sizes.scrolls);
    };

    "an empty deck and a single slide ask for the default size"_test = [] {
        for (const std::size_t entries : {0UZ, 1UZ}) {
            const SideMenu::ListSizes sizes = SideMenu::listSizes(entries, 100.0f);
            expect(eq(sizes.otherPoints, 16.0f)) << entries << " entries";
            expect(!sizes.scrolls) << entries << " entries";
        }
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
