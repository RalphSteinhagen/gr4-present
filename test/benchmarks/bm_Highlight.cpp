#include <benchmark.hpp>

#include "FileContents.hpp"

#include <gr4-present/Highlight.hpp>
#include <gr4-present/Markdown.hpp>

#include <string>
#include <vector>

// the deck's fenced code blocks, highlighted as the layout pass does on every frame a code slide is shown
inline const boost::ut::suite<"Highlight benchmarks"> highlightBenchmarks = [] {
    const std::string                      deck     = fileContents(GR4_PRESENT_DEFAULT_PRESENTATION "/talk.md");
    const gr::present::Document            document = gr::present::parseMarkdown(deck);
    std::vector<const gr::present::Block*> codeBlocks;
    std::size_t                            lineCount = 0UZ;
    for (const gr::present::Block& block : document.blocks) {
        if (block.kind == gr::present::BlockKind::codeBlock && gr::present::languageOf(block.info) != gr::present::Language::plain) {
            codeBlocks.push_back(&block);
            lineCount += block.lines.size();
        }
    }
    ::benchmark::benchmark<200>("highlight the deck's code blocks", lineCount) = [&codeBlocks] {
        for (const gr::present::Block* block : codeBlocks) {
            [[maybe_unused]] const auto tokens = gr::present::highlight(block->lines, gr::present::languageOf(block->info));
        }
    };
};

int main() { /* not needed by the UT framework */ }
