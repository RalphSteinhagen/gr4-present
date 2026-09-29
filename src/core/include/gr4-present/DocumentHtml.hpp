#ifndef GR4_PRESENT_DOCUMENT_HTML_HPP
#define GR4_PRESENT_DOCUMENT_HTML_HPP

#include <gr4-present/Markdown.hpp>

#include <span>
#include <string>

namespace gr::present {

/**
 * The deck as semantic HTML for the page's `<main>`: one `<section>` per view, its headings and paragraphs with their links, emphasis and
 * images' alternative text, escaped. The canvas stays what a person sees and uses; this is what a crawler, a screen
 * reader or a browser's Find reads, rendered in the page from the same model rather than stored as a file.
 *
 * Covers headings and paragraphs so far; lists, tables, code, formulas, figures and notes follow (spec F).
 */
[[nodiscard]] std::string documentHtml(std::span<const Section> sections);

} // namespace gr::present

#endif // GR4_PRESENT_DOCUMENT_HTML_HPP
