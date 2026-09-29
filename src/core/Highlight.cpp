#include <gr4-present/Highlight.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <string>

namespace gr::present {

namespace {

using namespace std::string_view_literals;

[[nodiscard]] bool isIdentifierStart(char c) noexcept { return (std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_'; }
[[nodiscard]] bool isIdentifierChar(char c) noexcept { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; }
[[nodiscard]] bool isDigit(char c) noexcept { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
[[nodiscard]] bool isSpace(char c) noexcept { return c == ' ' || c == '\t'; }

[[nodiscard]] bool holds(std::span<const std::string_view> words, std::string_view word) noexcept { return std::ranges::find(words, word) != words.end(); }

constexpr std::array kCppKeywords{"alignas"sv, "alignof"sv, "and"sv, "asm"sv, "auto"sv, "break"sv, "case"sv, "catch"sv, "class"sv, "co_await"sv, "co_return"sv, "co_yield"sv, "concept"sv, "const"sv, "consteval"sv, "constexpr"sv, "constinit"sv, "const_cast"sv, "continue"sv, "decltype"sv, "default"sv, "delete"sv, "do"sv, "dynamic_cast"sv, "else"sv, "enum"sv, "explicit"sv, "export"sv, "extern"sv, "false"sv, "final"sv, "for"sv, "friend"sv, "goto"sv, "if"sv, "inline"sv, "mutable"sv, "namespace"sv, "new"sv, "noexcept"sv, "nullptr"sv, "operator"sv, "override"sv, "private"sv, "protected"sv, "public"sv, "reinterpret_cast"sv, "requires"sv, "return"sv, "sizeof"sv, "static"sv, "static_assert"sv, "static_cast"sv, "struct"sv, "switch"sv, "template"sv, "this"sv, "thread_local"sv, "throw"sv, "true"sv, "try"sv, "typedef"sv, "typeid"sv, "typename"sv, "union"sv, "using"sv, "virtual"sv, "volatile"sv, "while"sv};

constexpr std::array kCppTypes{"bool"sv, "char"sv, "char8_t"sv, "char16_t"sv, "char32_t"sv, "double"sv, "float"sv, "int"sv, "long"sv, "short"sv, "signed"sv, "unsigned"sv, "void"sv, "wchar_t"sv, "size_t"sv, "ptrdiff_t"sv, "int8_t"sv, "int16_t"sv, "int32_t"sv, "int64_t"sv, "uint8_t"sv, "uint16_t"sv, "uint32_t"sv, "uint64_t"sv, "string"sv, "string_view"sv, "vector"sv, "array"sv, "span"sv, "optional"sv, "expected"sv, "unique_ptr"sv, "shared_ptr"sv};

constexpr std::array kPythonKeywords{"False"sv, "None"sv, "True"sv, "and"sv, "as"sv, "assert"sv, "async"sv, "await"sv, "break"sv, "class"sv, "continue"sv, "def"sv, "del"sv, "elif"sv, "else"sv, "except"sv, "finally"sv, "for"sv, "from"sv, "global"sv, "if"sv, "import"sv, "in"sv, "is"sv, "lambda"sv, "nonlocal"sv, "not"sv, "or"sv, "pass"sv, "raise"sv, "return"sv, "try"sv, "while"sv, "with"sv, "yield"sv};

constexpr std::array kPythonBuiltins{"abs"sv, "all"sv, "any"sv, "bool"sv, "bytes"sv, "cls"sv, "dict"sv, "enumerate"sv, "filter"sv, "float"sv, "format"sv, "frozenset"sv, "getattr"sv, "int"sv, "isinstance"sv, "len"sv, "list"sv, "map"sv, "max"sv, "min"sv, "object"sv, "open"sv, "print"sv, "range"sv, "repr"sv, "reversed"sv, "round"sv, "self"sv, "set"sv, "setattr"sv, "sorted"sv, "str"sv, "sum"sv, "super"sv, "tuple"sv, "type"sv, "zip"sv};

constexpr std::array kYamlConstants{"true"sv, "false"sv, "null"sv, "yes"sv, "no"sv, "on"sv, "off"sv, "True"sv, "False"sv, "Null"sv, "~"sv};

/// collects tokens, merging a run of the same kind so the stream stays readable in a test's expectation
struct Sink {
    TokenLine tokens;

    void add(TokenKind kind, std::string_view text) {
        if (text.empty()) {
            return;
        }
        if (!tokens.empty() && tokens.back().kind == kind) {
            tokens.back().text.append(text);
            return;
        }
        tokens.push_back(Token{.kind = kind, .text = std::string{text}});
    }
};

/// consumes a run of whitespace from `at`, emitting it unclassified
void takeSpace(Sink& sink, std::string_view line, std::size_t& at) {
    const std::size_t start = at;
    while (at < line.size() && isSpace(line[at])) {
        ++at;
    }
    sink.add(TokenKind::text, line.substr(start, at - start));
}

/// consumes a numeric literal, including digit separators and an exponent's sign
void takeNumber(Sink& sink, std::string_view line, std::size_t& at) {
    const std::size_t start = at;
    while (at < line.size()) {
        const char c = line[at];
        if (isIdentifierChar(c) || c == '.' || c == '\'') {
            ++at;
        } else if ((c == '+' || c == '-') && at > start && (line[at - 1UZ] == 'e' || line[at - 1UZ] == 'E' || line[at - 1UZ] == 'p' || line[at - 1UZ] == 'P')) {
            ++at;
        } else {
            break;
        }
    }
    sink.add(TokenKind::number, line.substr(start, at - start));
}

/// consumes a quoted run up to the unescaped closing `quote`; reports whether it closed on this line
bool takeQuoted(Sink& sink, std::string_view line, std::size_t& at, char quote, bool escapes) {
    const std::size_t start  = at;
    bool              closed = false;
    ++at; // the opening quote
    while (at < line.size()) {
        if (escapes && line[at] == '\\' && at + 1UZ < line.size()) {
            at += 2UZ;
            continue;
        }
        if (line[at] == quote) {
            ++at;
            closed = true;
            break;
        }
        ++at;
    }
    sink.add(TokenKind::string, line.substr(start, at - start));
    return closed;
}

struct CppState {
    bool        inComment = false;
    std::string rawTerminator; // non-empty while a raw string literal is open, e.g. )svg"
};

TokenLine lexCpp(std::string_view line, CppState& state) {
    Sink        sink;
    std::size_t at = 0UZ;

    if (!state.rawTerminator.empty()) {
        const std::size_t end = line.find(state.rawTerminator);
        if (end == std::string_view::npos) {
            sink.add(TokenKind::string, line);
            return std::move(sink.tokens);
        }
        at = end + state.rawTerminator.size();
        sink.add(TokenKind::string, line.substr(0UZ, at));
        state.rawTerminator.clear();
    }
    if (state.inComment) {
        const std::size_t end = line.find("*/", at);
        if (end == std::string_view::npos) {
            sink.add(TokenKind::comment, line.substr(at));
            return std::move(sink.tokens);
        }
        sink.add(TokenKind::comment, line.substr(at, end + 2UZ - at));
        at              = end + 2UZ;
        state.inComment = false;
    }

    // a preprocessor line is coloured whole, except that the header it names reads as a string
    std::size_t probe = at;
    while (probe < line.size() && isSpace(line[probe])) {
        ++probe;
    }
    if (probe < line.size() && line[probe] == '#') {
        sink.add(TokenKind::text, line.substr(at, probe - at));
        std::size_t end = probe + 1UZ;
        while (end < line.size() && isIdentifierChar(line[end])) {
            ++end;
        }
        sink.add(TokenKind::directive, line.substr(probe, end - probe));
        at = end;
        while (at < line.size() && isSpace(line[at])) {
            ++at;
        }
        sink.add(TokenKind::text, line.substr(end, at - end));
        if (at < line.size() && (line[at] == '<' || line[at] == '"')) {
            sink.add(TokenKind::string, line.substr(at));
            return std::move(sink.tokens);
        }
    }

    while (at < line.size()) {
        const char c = line[at];
        if (isSpace(c)) {
            takeSpace(sink, line, at);
        } else if (c == '/' && at + 1UZ < line.size() && line[at + 1UZ] == '/') {
            sink.add(TokenKind::comment, line.substr(at));
            at = line.size();
        } else if (c == '/' && at + 1UZ < line.size() && line[at + 1UZ] == '*') {
            const std::size_t end = line.find("*/", at + 2UZ);
            if (end == std::string_view::npos) {
                sink.add(TokenKind::comment, line.substr(at));
                state.inComment = true;
                at              = line.size();
            } else {
                sink.add(TokenKind::comment, line.substr(at, end + 2UZ - at));
                at = end + 2UZ;
            }
        } else if (c == 'R' && at + 1UZ < line.size() && line[at + 1UZ] == '"') {
            // R"delim( ... )delim" -- the delimiter is whatever sits between the quote and the parenthesis
            const std::size_t open = line.find('(', at + 2UZ);
            if (open == std::string_view::npos) {
                sink.add(TokenKind::string, line.substr(at));
                at = line.size();
            } else {
                const std::string terminator = ")" + std::string{line.substr(at + 2UZ, open - at - 2UZ)} + "\"";
                const std::size_t end        = line.find(terminator, open);
                if (end == std::string_view::npos) {
                    sink.add(TokenKind::string, line.substr(at));
                    state.rawTerminator = terminator;
                    at                  = line.size();
                } else {
                    sink.add(TokenKind::string, line.substr(at, end + terminator.size() - at));
                    at = end + terminator.size();
                }
            }
        } else if (c == '"' || c == '\'') {
            takeQuoted(sink, line, at, c, true);
        } else if (isDigit(c) || (c == '.' && at + 1UZ < line.size() && isDigit(line[at + 1UZ]))) {
            takeNumber(sink, line, at);
        } else if (isIdentifierStart(c)) {
            const std::size_t start = at;
            while (at < line.size() && isIdentifierChar(line[at])) {
                ++at;
            }
            const std::string_view word = line.substr(start, at - start);
            sink.add(holds(kCppKeywords, word) ? TokenKind::keyword : holds(kCppTypes, word) ? TokenKind::type : TokenKind::text, word);
        } else {
            sink.add(TokenKind::punctuation, line.substr(at, 1UZ));
            ++at;
        }
    }
    return std::move(sink.tokens);
}

struct PythonState {
    char triple = '\0'; // the quote character of an open triple-quoted string, else nul
};

TokenLine lexPython(std::string_view line, PythonState& state) {
    Sink        sink;
    std::size_t at = 0UZ;

    if (state.triple != '\0') {
        const std::string fence(3UZ, state.triple);
        const std::size_t end = line.find(fence);
        if (end == std::string_view::npos) {
            sink.add(TokenKind::string, line);
            return std::move(sink.tokens);
        }
        at = end + 3UZ;
        sink.add(TokenKind::string, line.substr(0UZ, at));
        state.triple = '\0';
    }

    const auto openTriple = [&](std::size_t start, char quote) {
        const std::string fence(3UZ, quote);
        const std::size_t end = line.find(fence, start + 3UZ);
        if (end == std::string_view::npos) {
            sink.add(TokenKind::string, line.substr(start));
            state.triple = quote;
            at           = line.size();
        } else {
            at = end + 3UZ;
            sink.add(TokenKind::string, line.substr(start, at - start));
        }
    };

    while (at < line.size()) {
        const char c = line[at];
        if (isSpace(c)) {
            takeSpace(sink, line, at);
        } else if (c == '#') {
            sink.add(TokenKind::comment, line.substr(at));
            at = line.size();
        } else if (c == '@' && at + 1UZ < line.size() && isIdentifierStart(line[at + 1UZ])) {
            std::size_t end = at + 1UZ;
            while (end < line.size() && (isIdentifierChar(line[end]) || line[end] == '.')) {
                ++end;
            }
            sink.add(TokenKind::directive, line.substr(at, end - at));
            at = end;
        } else if (c == '"' || c == '\'') {
            if (line.compare(at, 3UZ, std::string(3UZ, c)) == 0) {
                openTriple(at, c);
            } else {
                takeQuoted(sink, line, at, c, true);
            }
        } else if (isDigit(c) || (c == '.' && at + 1UZ < line.size() && isDigit(line[at + 1UZ]))) {
            takeNumber(sink, line, at);
        } else if (isIdentifierStart(c)) {
            const std::size_t start = at;
            while (at < line.size() && isIdentifierChar(line[at])) {
                ++at;
            }
            const std::string_view word = line.substr(start, at - start);
            // a string prefix such as f, r or rb belongs to the literal that follows it, not to the identifiers
            if (at < line.size() && (line[at] == '"' || line[at] == '\'') && word.size() <= 2UZ) {
                const char quote = line[at];
                if (line.compare(at, 3UZ, std::string(3UZ, quote)) == 0) {
                    openTriple(start, quote); // start, so the prefix is part of the literal
                } else {
                    sink.add(TokenKind::string, word); // the literal that follows merges onto this
                    takeQuoted(sink, line, at, quote, true);
                }
                continue;
            }
            sink.add(holds(kPythonKeywords, word) ? TokenKind::keyword : holds(kPythonBuiltins, word) ? TokenKind::type : TokenKind::text, word);
        } else {
            sink.add(TokenKind::punctuation, line.substr(at, 1UZ));
            ++at;
        }
    }
    return std::move(sink.tokens);
}

struct YamlState {
    int blockIndent = -1; // indentation of the key that opened a `|` or `>` scalar, else -1
};

[[nodiscard]] std::size_t indentOf(std::string_view line) noexcept {
    std::size_t at = 0UZ;
    while (at < line.size() && isSpace(line[at])) {
        ++at;
    }
    return at;
}

/// a YAML scalar is a number when the whole of it is one: JSON's grammar, which YAML 1.2 adopted
[[nodiscard]] bool isNumericScalar(std::string_view value) noexcept {
    std::size_t at     = 0UZ;
    bool        digits = false;
    if (at < value.size() && (value[at] == '-' || value[at] == '+')) {
        ++at;
    }
    for (; at < value.size() && isDigit(value[at]); ++at) {
        digits = true;
    }
    if (at < value.size() && value[at] == '.') {
        for (++at; at < value.size() && isDigit(value[at]); ++at) {
            digits = true;
        }
    }
    if (digits && at < value.size() && (value[at] == 'e' || value[at] == 'E')) {
        ++at;
        if (at < value.size() && (value[at] == '-' || value[at] == '+')) {
            ++at;
        }
        bool exponent = false;
        for (; at < value.size() && isDigit(value[at]); ++at) {
            exponent = true;
        }
        if (!exponent) {
            return false;
        }
    }
    return digits && at == value.size();
}

/// a scalar after `key:` or `- `: quoted text, a number, a constant, or plain text up to a trailing comment
void takeYamlValue(Sink& sink, std::string_view line, std::size_t& at, YamlState& state) {
    if (at >= line.size()) {
        return;
    }
    const char c = line[at];
    if (c == '"' || c == '\'') {
        takeQuoted(sink, line, at, c, c == '"');
        return;
    }
    if (c == '|' || c == '>') {
        sink.add(TokenKind::punctuation, line.substr(at));
        at                = line.size();
        state.blockIndent = static_cast<int>(indentOf(line));
        return;
    }
    if (c == '&' || c == '*' || c == '!') {
        std::size_t end = at;
        while (end < line.size() && !isSpace(line[end])) {
            ++end;
        }
        sink.add(TokenKind::directive, line.substr(at, end - at));
        at = end;
        takeSpace(sink, line, at);
        takeYamlValue(sink, line, at, state);
        return;
    }

    std::size_t end = line.size();
    if (const std::size_t hash = line.find(" #", at); hash != std::string_view::npos) {
        end = hash + 1UZ; // a comment may follow a value, but only after whitespace, which is not part of either
    }
    std::string_view value = line.substr(at, end - at);
    while (!value.empty() && isSpace(value.back())) {
        value.remove_suffix(1UZ);
    }
    if (!value.empty()) {
        sink.add(isNumericScalar(value) ? TokenKind::number : holds(kYamlConstants, value) ? TokenKind::keyword : TokenKind::string, value);
        at += value.size();
    }
    sink.add(TokenKind::text, line.substr(at, end - at));
    at = end;
    if (at < line.size()) {
        sink.add(TokenKind::comment, line.substr(at));
        at = line.size();
    }
}

TokenLine lexYaml(std::string_view line, YamlState& state) {
    Sink              sink;
    const std::size_t indent = indentOf(line);

    // a block scalar runs until a line indented no further than the key that opened it
    if (state.blockIndent >= 0) {
        if (indent == line.size() || static_cast<int>(indent) > state.blockIndent) {
            sink.add(TokenKind::string, line);
            return std::move(sink.tokens);
        }
        state.blockIndent = -1;
    }

    std::size_t at = 0UZ;
    takeSpace(sink, line, at);
    if (at >= line.size()) {
        return std::move(sink.tokens);
    }
    if (line[at] == '#') {
        sink.add(TokenKind::comment, line.substr(at));
        return std::move(sink.tokens);
    }
    if (line.substr(at) == "---" || line.substr(at) == "...") {
        sink.add(TokenKind::punctuation, line.substr(at));
        return std::move(sink.tokens);
    }
    while (at < line.size() && line[at] == '-' && (at + 1UZ == line.size() || isSpace(line[at + 1UZ]))) {
        sink.add(TokenKind::punctuation, line.substr(at, 1UZ));
        ++at;
        takeSpace(sink, line, at);
    }

    // `key:` binds only when the colon is followed by whitespace or ends the line, so a URL value stays a value
    std::size_t colon = at;
    while (colon < line.size()) {
        if (line[colon] == '#') {
            break;
        }
        if (line[colon] == ':' && (colon + 1UZ == line.size() || isSpace(line[colon + 1UZ]))) {
            sink.add(TokenKind::key, line.substr(at, colon - at));
            sink.add(TokenKind::punctuation, ":");
            at = colon + 1UZ;
            takeSpace(sink, line, at);
            takeYamlValue(sink, line, at, state);
            return std::move(sink.tokens);
        }
        ++colon;
    }
    takeYamlValue(sink, line, at, state);
    return std::move(sink.tokens);
}

} // namespace

Language languageOf(std::string_view info) noexcept {
    std::string_view word = info.substr(0UZ, info.find_first_of(" \t"));
    std::string      lowered;
    lowered.reserve(word.size());
    for (char c : word) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lowered == "cpp" || lowered == "c++" || lowered == "cxx" || lowered == "cc" || lowered == "c" || lowered == "hpp" || lowered == "h") {
        return Language::cpp;
    }
    if (lowered == "python" || lowered == "py") {
        return Language::python;
    }
    if (lowered == "yaml" || lowered == "yml") {
        return Language::yaml;
    }
    return Language::plain;
}

std::vector<TokenLine> highlight(std::span<const std::string> lines, Language language) {
    std::vector<TokenLine> result;
    result.reserve(lines.size());

    CppState    cpp;
    PythonState python;
    YamlState   yaml;

    for (const std::string& line : lines) {
        switch (language) {
        case Language::cpp: result.push_back(lexCpp(line, cpp)); break;
        case Language::python: result.push_back(lexPython(line, python)); break;
        case Language::yaml: result.push_back(lexYaml(line, yaml)); break;
        case Language::plain: result.push_back(line.empty() ? TokenLine{} : TokenLine{Token{.kind = TokenKind::text, .text = line}}); break;
        }
    }
    return result;
}

} // namespace gr::present
