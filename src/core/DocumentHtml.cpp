#include <gr4-present/DocumentHtml.hpp>
#include <gr4-present/Manifest.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>

namespace gr::present {

namespace {

void appendEscaped(std::string& html, std::string_view text) {
    for (const char letter : text) {
        switch (letter) {
        case '&': html += "&amp;"; break;
        case '<': html += "&lt;"; break;
        case '>': html += "&gt;"; break;
        case '"': html += "&quot;"; break;
        default: html += letter;
        }
    }
}

void appendSpans(std::string& html, std::span<const InlineSpan> spans) {
    const auto wrapped = [&html](std::string_view tag, std::string_view text) {
        html += std::format("<{}>", tag);
        appendEscaped(html, text);
        html += std::format("</{}>", tag);
    };
    for (const InlineSpan& span : spans) {
        const std::optional<Rgb> colour = parseHexColour(span.colour);
        if (colour) {
            html += std::format("<span style=\"color:#{:02x}{:02x}{:02x}\">", colour->red, colour->green, colour->blue);
        }
        switch (span.kind) {
        case InlineKind::text: appendEscaped(html, span.text); break;
        case InlineKind::emphasis: wrapped("em", span.text); break;
        case InlineKind::strong: wrapped("strong", span.text); break;
        case InlineKind::strongEmphasis:
            html += "<strong>";
            wrapped("em", span.text);
            html += "</strong>";
            break;
        case InlineKind::code: wrapped("code", span.text); break;
        case InlineKind::math: wrapped("code", span.text); break; // its LaTeX, as a reader copies it
        case InlineKind::lineBreak: html += "<br>"; break;
        case InlineKind::footnote: break;
        case InlineKind::link:
            html += "<a href=\"";
            appendEscaped(html, span.target);
            html += "\">";
            appendEscaped(html, span.text);
            html += "</a>";
            break;
        case InlineKind::image:
            html += "<img src=\"";
            appendEscaped(html, span.target);
            html += "\" alt=\"";
            appendEscaped(html, span.text);
            html += "\">";
            break;
        }
        if (colour) {
            html += "</span>";
        }
    }
}

} // namespace

std::string documentHtml(std::span<const Section> sections) {
    std::string html;
    for (const Section& section : sections) {
        html += "<section";
        if (!section.id.empty()) {
            html += " id=\"";
            appendEscaped(html, section.id);
            html += "\"";
        }
        html += ">";
        for (const Block& block : section.document.blocks) {
            if (block.kind == BlockKind::heading) {
                const int level = std::clamp(block.level, 1, 6);
                html += std::format("<h{}>", level);
                appendSpans(html, block.spans);
                html += std::format("</h{}>", level);
            } else if (block.kind == BlockKind::paragraph) {
                html += "<p>";
                appendSpans(html, block.spans);
                html += "</p>";
            }
        }
        html += "</section>";
    }
    return html;
}

} // namespace gr::present
