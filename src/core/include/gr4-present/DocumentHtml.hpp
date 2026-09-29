#ifndef GR4_PRESENT_DOCUMENT_HTML_HPP
#define GR4_PRESENT_DOCUMENT_HTML_HPP

#include <gr4-present/Markdown.hpp>

#include <span>
#include <string>

namespace gr::present {

/// the deck as semantic HTML for the page's `<main>`, one `<section>` per view: what a crawler, a screen reader or
/// Find reads; covers headings and paragraphs so far; lists, tables, code, formulas, figures and notes follow (spec F)
[[nodiscard]] std::string documentHtml(std::span<const Section> sections);

} // namespace gr::present

#endif // GR4_PRESENT_DOCUMENT_HTML_HPP
