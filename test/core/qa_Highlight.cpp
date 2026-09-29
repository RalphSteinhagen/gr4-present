#include <boost/ut.hpp>

#include <gr4-present/Highlight.hpp>

#include <string>
#include <vector>

using namespace gr::present;
using namespace boost::ut;

namespace {

[[nodiscard]] std::vector<TokenLine> lex(std::vector<std::string> lines, Language language) { return highlight(lines, language); }

/// the text of the first token whose kind is `kind`, so an expectation names what it means rather than an index
[[nodiscard]] std::string first(const TokenLine& line, TokenKind kind) {
    for (const Token& token : line) {
        if (token.kind == kind) {
            return token.text;
        }
    }
    return {};
}

[[nodiscard]] std::size_t count(const TokenLine& line, TokenKind kind) {
    std::size_t total = 0UZ;
    for (const Token& token : line) {
        total += token.kind == kind ? 1UZ : 0UZ;
    }
    return total;
}

[[nodiscard]] std::string joined(const TokenLine& line) {
    std::string text;
    for (const Token& token : line) {
        text.append(token.text);
    }
    return text;
}

[[nodiscard]] bool has(const TokenLine& line, TokenKind kind, std::string_view text) {
    for (const Token& token : line) {
        if (token.kind == kind && token.text == text) {
            return true;
        }
    }
    return false;
}

} // namespace

// Ground truth is each language's own lexical grammar -- the C++ standard's phrase-structure rules, the Python
// reference's lexical analysis chapter and the YAML 1.2 specification -- applied by hand to the samples below.
// The highlighter is never consulted about what its answer ought to be.
const suite<"Highlight"> highlightTests = [] {
    "an info string names the language, whatever its spelling"_test = [] {
        expect(languageOf("cpp") == Language::cpp);
        expect(languageOf("C++") == Language::cpp);
        expect(languageOf("c") == Language::cpp);
        expect(languageOf("Python") == Language::python);
        expect(languageOf("py") == Language::python);
        expect(languageOf("yml") == Language::yaml);
        expect(languageOf("yaml") == Language::yaml);
        expect(languageOf("cpp title=demo") == Language::cpp) << "only the first word names the language";
        expect(languageOf("") == Language::plain);
        expect(languageOf("rust") == Language::plain) << "an unsupported language must not be guessed at";
    };

    "tokens concatenate back into the line they came from"_test = [] {
        // the renderer lays out from the tokens alone, so anything lost here is lost on the slide
        const std::vector<std::string> sources{"  int x = 0; // a trailing comment", "\tstd::string s = \"a\\\"b\";", "", "#include <vector>", "auto f = [](int n) { return n * 2; };"};
        for (const Language language : {Language::cpp, Language::python, Language::yaml, Language::plain}) {
            const auto lines = lex(sources, language);
            expect(eq(lines.size(), sources.size()));
            for (std::size_t index = 0UZ; index < sources.size(); ++index) {
                expect(eq(joined(lines[index]), sources[index])) << "language " << static_cast<int>(language) << " altered line " << index;
            }
        }
    };

    "C++ keywords, types, literals and comments are told apart"_test = [] {
        const auto  lines = lex({"constexpr std::size_t kMax = 42UZ; // the cap"}, Language::cpp);
        const auto& line  = lines.front();
        expect(has(line, TokenKind::keyword, "constexpr"));
        expect(has(line, TokenKind::type, "size_t")) << "size_t is a type even behind a namespace qualifier";
        expect(has(line, TokenKind::number, "42UZ")) << "a literal's suffix belongs to the literal";
        expect(eq(first(line, TokenKind::comment), std::string{"// the cap"}));
        expect(!has(line, TokenKind::keyword, "kMax")) << "an ordinary identifier is not a keyword";
    };

    "a C++ block comment spans lines and ends where it closes"_test = [] {
        const auto lines = lex({"int a = 1; /* opens", "still comment; int b = 2;", "closes */ int c = 3;"}, Language::cpp);
        expect(has(lines[0], TokenKind::number, "1"));
        expect(eq(first(lines[0], TokenKind::comment), std::string{"/* opens"}));
        expect(eq(lines[1].size(), 1UZ)) << "an interior line is comment and nothing else";
        expect(lines[1].front().kind == TokenKind::comment);
        expect(eq(first(lines[2], TokenKind::comment), std::string{"closes */"}));
        expect(has(lines[2], TokenKind::type, "int")) << "code after the close is code again";
    };

    "a raw string literal keeps its contents literal across lines"_test = [] {
        // the delimiter matters: the closing sequence is )svg" and nothing shorter ends it
        const auto lines = lex({"const char* s = R\"svg(<rect id=\"a\"/>", "// not a comment", ")svg\"; int n = 0;"}, Language::cpp);
        expect(has(lines[0], TokenKind::string, "R\"svg(<rect id=\"a\"/>"));
        expect(eq(lines[1].size(), 1UZ));
        expect(lines[1].front().kind == TokenKind::string) << "a comment marker inside a raw string is text";
        expect(has(lines[2], TokenKind::type, "int")) << "the literal ended at its delimiter";
    };

    "a preprocessor line is a directive and the header it names is a string"_test = [] {
        const auto angled = lex({"#include <vector>"}, Language::cpp);
        expect(eq(first(angled.front(), TokenKind::directive), std::string{"#include"}));
        expect(eq(first(angled.front(), TokenKind::string), std::string{"<vector>"}));

        const auto quoted = lex({"  #  define NOTHING"}, Language::cpp);
        expect(eq(first(quoted.front(), TokenKind::directive), std::string{"#"})) << "a space after the hash still opens a directive";
    };

    "Python keywords, builtins, decorators and comments are told apart"_test = [] {
        const auto lines = lex({"@property", "def total(self, values: list) -> int:  # sums"}, Language::python);
        expect(eq(first(lines[0], TokenKind::directive), std::string{"@property"}));
        expect(has(lines[1], TokenKind::keyword, "def"));
        expect(has(lines[1], TokenKind::type, "self"));
        expect(has(lines[1], TokenKind::type, "list"));
        expect(eq(first(lines[1], TokenKind::comment), std::string{"# sums"}));
        expect(!has(lines[1], TokenKind::keyword, "total"));
    };

    "a Python triple-quoted string runs to its closing fence"_test = [] {
        const auto lines = lex({"doc = \"\"\"first", "def not_a_keyword():", "\"\"\"", "x = 1"}, Language::python);
        expect(has(lines[0], TokenKind::string, "\"\"\"first"));
        expect(eq(lines[1].size(), 1UZ));
        expect(lines[1].front().kind == TokenKind::string) << "def inside a docstring is not a keyword";
        expect(has(lines[3], TokenKind::number, "1")) << "code after the fence is code again";
    };

    "a Python string prefix belongs to its literal"_test = [] {
        const auto lines = lex({"name = f\"{count} items\""}, Language::python);
        expect(has(lines.front(), TokenKind::string, "f\"{count} items\"")) << "the f must not be read as an identifier";
        expect(!has(lines.front(), TokenKind::text, "f"));
    };

    "YAML separates a key from its value"_test = [] {
        const auto lines = lex({"title: A talk", "count: 12", "enabled: true", "url: https://example.org/a:b"}, Language::yaml);
        expect(eq(first(lines[0], TokenKind::key), std::string{"title"}));
        expect(eq(first(lines[0], TokenKind::string), std::string{"A talk"}));
        expect(eq(first(lines[1], TokenKind::number), std::string{"12"}));
        expect(eq(first(lines[2], TokenKind::keyword), std::string{"true"})) << "a YAML boolean is a constant, not a string";
        expect(eq(first(lines[3], TokenKind::key), std::string{"url"})) << "a colon inside a value does not open a new key: only one followed by a space does";
        expect(eq(first(lines[3], TokenKind::string), std::string{"https://example.org/a:b"}));
    };

    "YAML sequence markers, comments and anchors are structural"_test = [] {
        const auto lines = lex({"# a whole-line comment", "- first", "- second   # trailing", "base: &anchor 3", "other: *anchor"}, Language::yaml);
        expect(lines[0].front().kind == TokenKind::comment);
        expect(eq(first(lines[1], TokenKind::punctuation), std::string{"-"}));
        expect(eq(first(lines[2], TokenKind::comment), std::string{"# trailing"}));
        expect(eq(first(lines[2], TokenKind::string), std::string{"second"})) << "the comment must not be swallowed into the value";
        expect(eq(first(lines[3], TokenKind::directive), std::string{"&anchor"}));
        expect(eq(first(lines[3], TokenKind::number), std::string{"3"}));
        expect(eq(first(lines[4], TokenKind::directive), std::string{"*anchor"}));
    };

    "a YAML block scalar holds everything indented under it"_test = [] {
        const auto lines = lex({"script: |", "  key: not a key", "  # not a comment", "next: value"}, Language::yaml);
        expect(eq(first(lines[0], TokenKind::key), std::string{"script"}));
        expect(eq(first(lines[0], TokenKind::punctuation), std::string{":"}));
        expect(eq(lines[1].size(), 1UZ));
        expect(lines[1].front().kind == TokenKind::string);
        expect(eq(count(lines[2], TokenKind::comment), 0UZ)) << "a hash inside a block scalar is content";
        expect(eq(first(lines[3], TokenKind::key), std::string{"next"})) << "dedenting ends the scalar";
    };

    "YAML numbers are numbers only when the whole scalar is one"_test = [] {
        const auto lines = lex({"a: 1.5e-3", "b: -42", "c: 3 apples", "d: 1.2.3"}, Language::yaml);
        expect(eq(first(lines[0], TokenKind::number), std::string{"1.5e-3"}));
        expect(eq(first(lines[1], TokenKind::number), std::string{"-42"}));
        expect(eq(count(lines[2], TokenKind::number), 0UZ)) << "3 apples is a string";
        expect(eq(count(lines[3], TokenKind::number), 0UZ)) << "a version is not a number";
    };

    "a plain block is one token per line and an empty line has none"_test = [] {
        const auto lines = lex({"anything at all", ""}, Language::plain);
        expect(eq(lines[0].size(), 1UZ));
        expect(lines[0].front().kind == TokenKind::text);
        expect(lines[1].empty());
    };
};

int main() { return 0; }
