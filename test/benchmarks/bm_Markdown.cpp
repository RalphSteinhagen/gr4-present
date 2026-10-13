#include <benchmark.hpp>

#include "FileContents.hpp"

#include <gr4-present/Markdown.hpp>

#include <string>

inline const boost::ut::suite<"Markdown benchmarks"> markdownBenchmarks = [] {
    const std::string deck                                            = fileContents(GR4_PRESENT_DEFAULT_PRESENTATION "/talk.md");
    ::benchmark::benchmark<20>("parse the default deck", deck.size()) = [&deck] { [[maybe_unused]] const auto document = gr::present::parseMarkdown(deck); };
};

int main() { /* not needed by the UT framework */ }
