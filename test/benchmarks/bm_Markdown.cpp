#include <benchmark.hpp>

#include <gr4-present/Markdown.hpp>

#include <fstream>
#include <iterator>
#include <string>

inline const boost::ut::suite<"Markdown benchmarks"> markdownBenchmarks = [] {
    std::ifstream     file{GR4_PRESENT_DEFAULT_PRESENTATION "/talk.md"};
    const std::string deck{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    ::benchmark::benchmark<20>("parse the default deck", deck.size()) = [&deck] { [[maybe_unused]] const auto document = gr::present::parseMarkdown(deck); };
};

int main() { /* not needed by the UT framework */ }
