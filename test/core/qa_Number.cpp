#include <boost/ut.hpp>

#include <gr4-present/Number.hpp>

#include <cstddef>
#include <optional>

using namespace boost::ut;
using namespace gr::present;

const suite<"Number"> numberTests = [] {
    "a number is read whole"_test = [] {
        expect(parseNumber<float>("0.25") == std::optional{0.25f});
        expect(parseNumber<float>("-3.5") == std::optional{-3.5f});
        expect(parseNumber<int>("42") == std::optional{42});
        expect(parseNumber<std::size_t>("7") == std::optional{std::size_t{7}});
    };

    "anything that is not part of the number makes it none"_test = [] {
        expect(parseNumber<float>("") == std::nullopt);
        expect(parseNumber<float>("   ") == std::nullopt);
        expect(parseNumber<float>(" 14 ") == std::nullopt) << "blanks are not part of a number";
        expect(parseNumber<float>("0.5x") == std::nullopt) << "trailing text";
        expect(parseNumber<float>("4:3") == std::nullopt) << "a ratio is two numbers";
        expect(parseNumber<int>("1.5") == std::nullopt) << "an integer has no fraction";
        expect(parseNumber<std::size_t>("-1") == std::nullopt) << "a count is not negative";
        expect(parseNumber<int>("99999999999") == std::nullopt) << "out of range";
    };

    "a duration is seconds, with or without its unit, and never negative"_test = [] {
        expect(parseSeconds("2") == std::optional{2.0f});
        expect(parseSeconds("0.5") == std::optional{0.5f});
        expect(parseSeconds("3s") == std::optional{3.0f});
        expect(parseSeconds("1.5s") == std::optional{1.5f});
        expect(parseSeconds(" 1.5s") == std::nullopt) << "blanks are not part of a duration";
        expect(parseSeconds("0") == std::optional{0.0f});
        expect(parseSeconds("-1") == std::nullopt);
        expect(parseSeconds("s") == std::nullopt);
        expect(parseSeconds("2ms") == std::nullopt) << "only seconds are a unit";
        expect(parseSeconds("") == std::nullopt);
    };
};

int main() { return 0; }
