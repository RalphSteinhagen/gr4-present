#include <boost/ut.hpp>

#include <gr4-present/Slice.hpp>

#include <string_view>

using namespace boost::ut;
using gr::present::slice;

const suite<"Slice"> sliceTests = [] {
    "inside the text, slice equals substr for every start and count"_test = [] {
        constexpr std::string_view kText = "camera ripple";
        for (std::size_t from = 0UZ; from <= kText.size(); ++from) {
            for (std::size_t count : {0UZ, 1UZ, 3UZ, kText.size(), std::string_view::npos}) {
                expect(slice(kText, from, count) == kText.substr(from, count)) << "from" << from << "count" << count;
            }
        }
    };

    "a start past the end gives an empty view where substr would throw"_test = [] {
        constexpr std::string_view kText = "push-left";
        expect(slice(kText, kText.size() + 1UZ).empty());
        expect(slice(kText, std::string_view::npos, 2UZ).empty());
        expect(slice({}, 0UZ).empty());
    };

    "a slice points into the text it was cut from"_test = [] {
        constexpr std::string_view kText = "key: value";
        expect(slice(kText, 5UZ).data() == kText.data() + 5UZ);
    };

    static_assert(slice("abcdef", 2UZ, 3UZ) == "cde");
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
