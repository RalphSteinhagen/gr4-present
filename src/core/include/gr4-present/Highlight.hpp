#ifndef GR4_PRESENT_HIGHLIGHT_HPP
#define GR4_PRESENT_HIGHLIGHT_HPP

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

enum class TokenKind : std::uint8_t {
    text, // anything the lexer does not classify, including whitespace
    keyword,
    type,   // a built-in type or a well-known library type
    number, // an integer, floating-point or character-count literal
    string, // a string or character literal, and a YAML block scalar's body
    comment,
    directive,   // a C preprocessor line, a Python decorator, a YAML anchor, alias or tag
    punctuation, // operators, separators, and YAML's structural characters
    key          // the left-hand side of a YAML mapping entry
};

struct Token {
    TokenKind   kind = TokenKind::text;
    std::string text;

    bool operator==(const Token&) const = default;
};

using TokenLine = std::vector<Token>;

enum class Language : std::uint8_t { plain, cpp, python, yaml };

/// the language a fenced block's info string names; anything unrecognised is `plain`
[[nodiscard]] Language languageOf(std::string_view info) noexcept;

/// splits each line into coloured tokens, taken together because block comments, raw strings and scalars span lines
/// concatenating a line's tokens reproduces it exactly: highlighting decides colour and nothing else
[[nodiscard]] std::vector<TokenLine> highlight(std::span<const std::string> lines, Language language);

} // namespace gr::present

#endif // GR4_PRESENT_HIGHLIGHT_HPP
