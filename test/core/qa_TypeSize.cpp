#include <boost/ut.hpp>

#include <gr4-present/TypeSize.hpp>

#include <cmath>
#include <string_view>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] bool approx(float value, float wanted) noexcept { return std::abs(value - wanted) < 1e-4f; }

} // namespace

const suite<"TypeSize"> typeSizeTests = [] {
    "points are written with or without their unit"_test = [] {
        expect(parseTypeSize("14pt") == TypeSize{.unit = TypeSize::Unit::points, .value = 14.0f});
        expect(parseTypeSize("14") == TypeSize{.unit = TypeSize::Unit::points, .value = 14.0f}) << "a bare number is points, as `{size=14}` always read";
        expect(parseTypeSize("10.5pt") == TypeSize{.unit = TypeSize::Unit::points, .value = 10.5f});
    };

    "a percentage and an em both scale the size around them"_test = [] {
        const auto percent = parseTypeSize("80%");
        const auto em      = parseTypeSize("1.2em");
        expect(percent.has_value() && percent->unit == TypeSize::Unit::relative && approx(percent->value, 0.8f));
        expect(em.has_value() && em->unit == TypeSize::Unit::relative && approx(em->value, 1.2f));
        expect(approx(percent->pointsWithin(20.0f), 16.0f)) << "80 % of 20 pt";
        expect(approx(em->pointsWithin(20.0f), 24.0f)) << "1.2 em of 20 pt";
        expect(approx(parseTypeSize("14pt")->pointsWithin(20.0f), 14.0f)) << "points ignore what is around them";
    };

    "anything that is no size is refused rather than guessed"_test = [] {
        for (const std::string_view nonsense : {"", "pt", "%", "big", "14px", "14 pt", "1.2.3em", "-3pt", "0", "0%", "nan", "inf"}) {
            expect(!parseTypeSize(nonsense).has_value()) << "'" << nonsense << "' was taken as a size";
        }
    };

    "a deck that says nothing keeps the type it was designed with"_test = [] {
        constexpr TypeScale kDefault{};
        expect(eq(kDefault.title, 36.0f) and eq(kDefault.body, 18.0f) and eq(kDefault.floor, 12.0f));
    };
};

int main() { return 0; }
