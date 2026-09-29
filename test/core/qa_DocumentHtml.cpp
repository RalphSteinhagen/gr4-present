#include <boost/ut.hpp>

#include <gr4-present/DocumentHtml.hpp>
#include <gr4-present/Markdown.hpp>

#include <string>

using namespace boost::ut;
using namespace gr::present;

// Ground truth is the HTML written out by hand here for each fixture: what a reader of the slide would mark up.
const boost::ut::suite<"DocumentHtml"> tests = [] {
    "a deck becomes one section per view, its headings and paragraphs in order"_test = [] {
        const Document deck = parseMarkdown("# Intro {#intro}\n\nFirst words.\n\n# Next\n\nMore words.\n");
        expect(documentHtml(sectionsOf(deck)) == std::string{R"(<section id="intro"><h1>Intro</h1><p>First words.</p></section><section id="next"><h1>Next</h1><p>More words.</p></section>)"}) << documentHtml(sectionsOf(deck));
    };

    "inline emphasis, code, links and an image's alternative text keep their meaning"_test = [] {
        const Document deck = parseMarkdown("# Slide {#s}\n\nA *spectrum* is **the** `FFT` of [GR4](https://gnuradio.org) ![a block diagram](figures/flow.svg).\n");
        expect(documentHtml(sectionsOf(deck)) == std::string{R"(<section id="s"><h1>Slide</h1><p>A <em>spectrum</em> is <strong>the</strong> <code>FFT</code> of <a href="https://gnuradio.org">GR4</a> <img src="figures/flow.svg" alt="a block diagram">.</p></section>)"}) << documentHtml(sectionsOf(deck));
    };

    "a formula is written as its LaTeX, as a reader copies it"_test = [] {
        const Document deck = parseMarkdown("# Maths {#m}\n\nEnergy $E = mc^2$ here.\n");
        expect(documentHtml(sectionsOf(deck)) == std::string{R"(<section id="m"><h1>Maths</h1><p>Energy <code>E = mc^2</code> here.</p></section>)"}) << documentHtml(sectionsOf(deck));
    };

    "text that looks like markup is escaped, not interpreted"_test = [] {
        const Document deck = parseMarkdown("# A < B & C {#e}\n\nUse \"quotes\" and <b>tags</b> as text.\n");
        expect(documentHtml(sectionsOf(deck)) == std::string{R"(<section id="e"><h1>A &lt; B &amp; C</h1><p>Use &quot;quotes&quot; and &lt;b&gt;tags&lt;/b&gt; as text.</p></section>)"}) << documentHtml(sectionsOf(deck));
    };

    "an empty deck gives no sections"_test = [] { expect(documentHtml({}).empty()); };
};

int main() { return 0; }
