#include "bminor/highlight.hpp"
#include "bminor/parser.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using std::cerr;
using std::exit;
using std::find_if;
using std::size_t;
using std::string_view;
using std::vector;

using Category = bminor::HighlightCategory;

void expect(bool condition, string_view description) {
    if (!condition) {
        cerr << description << '\n';
        exit(1);
    }
}

vector<bminor::HighlightSpan> highlight(string_view source) {
    auto lexical = bminor::lex(source, "highlight.bminor");
    auto syntax = bminor::parse(lexical.tokens);
    lexical.diagnostics.insert(lexical.diagnostics.end(), syntax.diagnostics.begin(),
                               syntax.diagnostics.end());
    return bminor::classify(lexical.tokens, *syntax.root, lexical.diagnostics);
}

void expect_style(const vector<bminor::HighlightSpan>& spans, size_t offset,
                  Category category, bool error = false) {
    const auto found = find_if(spans.begin(), spans.end(), [offset](const auto& span) {
        return span.span.begin == offset;
    });
    if (found == spans.end() || found->category != category || found->error != error) {
        cerr << "Unexpected style at byte " << offset << '\n';
        exit(1);
    }
}

void test_token_and_ast_categories() {
    constexpr string_view source =
        "f: function integer (); main: function integer () = { f: integer; print f, f(); }";
    const auto spans = highlight(source);
    expect_style(spans, 0, Category::Function);
    expect_style(spans, source.find("main"), Category::Function);
    expect_style(spans, source.find("f: integer"), Category::Variable);
    expect_style(spans, source.find("f,"), Category::Variable);
    expect_style(spans, source.find("f()"), Category::Function);
    expect_style(spans, source.find("function"), Category::Keyword);
    expect_style(spans, source.find("integer"), Category::Type);
    expect_style(spans, source.find('='), Category::Operator);

    size_t end = 0;
    for (const auto& span : spans) {
        expect(end <= span.span.begin && span.span.begin < span.span.end &&
                   span.span.end <= source.size(),
               "Highlight spans must be ordered and disjoint");
        for (const char byte : source.substr(span.span.begin, span.span.end - span.span.begin)) {
            expect(byte != ':' && byte != '(' && byte != ')' && byte != ';' && byte != ' ',
                   "Whitespace and delimiters must remain plain");
        }
        end = span.span.end;
    }

    constexpr string_view literals =
        "m: function void () = { // note\n print 72, true, \"word\", 'x'; }";
    const auto literal_spans = highlight(literals);
    expect_style(literal_spans, literals.find("72"), Category::Integer);
    expect_style(literal_spans, literals.find("true"), Category::Boolean);
    expect_style(literal_spans, literals.find('"'), Category::String);
    expect_style(literal_spans, literals.find('\''), Category::String);
    expect_style(literal_spans, literals.find("//"), Category::Comment);
}

void test_errors_and_recovered_call() {
    constexpr string_view source = "m: function void () = { print ; @ print f(); }";
    const auto spans = highlight(source);
    expect_style(spans, source.find(';'), Category::Plain, true);
    expect_style(spans, source.find('@'), Category::Plain, true);
    expect_style(spans, source.find("f()"), Category::Function);

    constexpr string_view eof = "x: integer";
    for (const auto& span : highlight(eof)) {
        expect(!span.error && span.span.end <= eof.size(), "EOF cannot color source bytes");
    }

    constexpr string_view next_declaration = "oops: integer\nnext: integer;";
    expect_style(highlight(next_declaration), next_declaration.find("next"), Category::Variable, true);
}

}

int main() {
    test_token_and_ast_categories();
    test_errors_and_recovered_call();
}
