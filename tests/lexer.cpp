#include "bminor/lexer.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using std::cerr;
using std::exit;
using std::initializer_list;
using std::size_t;
using std::string;
using std::string_view;
using std::to_string;
using std::vector;

using Kind = bminor::TokenKind;

constexpr string_view filename = "lexer-fixture.bminor";

struct ExpectedToken {
    Kind kind;
    string_view lexeme;
};

void require(bool condition, string_view fixture, string_view check) {
    if (!condition) {
        cerr << fixture << ": " << check << '\n';
        exit(1);
    }
}

bminor::LexResult check_stream(string_view fixture, string_view source,
                             const vector<ExpectedToken>& expected) {
    auto result = bminor::lex(source, filename);
    require(result.diagnostics.empty(), fixture, "unexpected lexical diagnostic");
    require(result.tokens.size() == expected.size() + 1, fixture, "incorrect token count");

    size_t offset = 0;
    string reconstructed;
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto& token = result.tokens[i];
        require(token.kind == expected[i].kind && token.lexeme == expected[i].lexeme, fixture,
                "incorrect kind or lexeme for token " + to_string(i));
        require(token.span.begin == offset && token.span.end == offset + token.lexeme.size() &&
                    token.span.end > offset, fixture, "token coverage has a gap or overlap");
        require(token.filename == filename, fixture, "incorrect token filename");
        reconstructed += token.lexeme;
        offset = token.span.end;
    }
    require(reconstructed == source, fixture, "raw tokens do not reconstruct input");

    const auto& eof = result.tokens.back();
    require(eof.kind == Kind::EndOfFile && eof.lexeme.empty() && eof.filename == filename &&
                eof.span.begin == source.size() && eof.span.end == source.size(),
            fixture, "incorrect EOF token");
    return result;
}

void check_spaced(string_view fixture, initializer_list<ExpectedToken> entries) {
    string source;
    vector<ExpectedToken> expected;
    for (const auto& entry : entries) {
        if (!source.empty()) {
            source += ' ';
            expected.push_back({Kind::Whitespace, " "});
        }
        source += entry.lexeme;
        expected.push_back(entry);
    }
    check_stream(fixture, source, expected);
}

void check_reserved_words() {
    check_spaced("reserved words and identifiers", {
        {Kind::ArrayKeyword, "array"}, {Kind::BooleanKeyword, "boolean"},
        {Kind::CharKeyword, "char"}, {Kind::ElseKeyword, "else"},
        {Kind::ForKeyword, "for"}, {Kind::FunctionKeyword, "function"},
        {Kind::IfKeyword, "if"}, {Kind::IntegerKeyword, "integer"},
        {Kind::PrintKeyword, "print"}, {Kind::ReturnKeyword, "return"},
        {Kind::StringKeyword, "string"}, {Kind::VoidKeyword, "void"},
        {Kind::WhileKeyword, "while"}, {Kind::BooleanLiteral, "true"},
        {Kind::BooleanLiteral, "false"}, {Kind::Identifier, "return_"},
        {Kind::Identifier, "return2"}, {Kind::Identifier, "Array"},
        {Kind::Identifier, "_a0"}, {Kind::Identifier, "falsehood"},
    });
}

void check_operators_and_delimiters() {
    check_spaced("operators and delimiters", {
        {Kind::Increment, "++"}, {Kind::Decrement, "--"}, {Kind::LessEqual, "<="},
        {Kind::GreaterEqual, ">="}, {Kind::Equal, "=="}, {Kind::NotEqual, "!="},
        {Kind::And, "&&"}, {Kind::Or, "||"}, {Kind::Plus, "+"}, {Kind::Minus, "-"},
        {Kind::Star, "*"}, {Kind::Slash, "/"}, {Kind::Percent, "%"}, {Kind::Caret, "^"},
        {Kind::Bang, "!"}, {Kind::Less, "<"}, {Kind::Greater, ">"}, {Kind::Assign, "="},
        {Kind::LeftParen, "("}, {Kind::RightParen, ")"}, {Kind::LeftBracket, "["},
        {Kind::RightBracket, "]"}, {Kind::LeftBrace, "{"}, {Kind::RightBrace, "}"},
        {Kind::Comma, ","}, {Kind::Colon, ":"}, {Kind::Semicolon, ";"},
    });
    check_spaced("longest match and comment priority", {
        {Kind::ReturnKeyword, "return"}, {Kind::Identifier, "return_"},
        {Kind::LessEqual, "<="}, {Kind::Less, "<"}, {Kind::Equal, "=="},
        {Kind::Assign, "="}, {Kind::Slash, "/"}, {Kind::Comment, "//x"},
    });
    check_stream("comments", "/* outer /* inner */ // end here\r\n/", {
        {Kind::Comment, "/* outer /* inner */"}, {Kind::Whitespace, " "},
        {Kind::Comment, "// end here"}, {Kind::Whitespace, "\r\n"}, {Kind::Slash, "/"},
    });
    check_stream("minus is a separate token", "x-12", {
        {Kind::Identifier, "x"}, {Kind::Minus, "-"}, {Kind::IntegerLiteral, "12"},
    });
}

void check_literals() {
    constexpr string_view fixture = "literal decoding";
    auto result = check_stream(fixture, R"lit(007 true false "" "a\"b\n\0\qc" '\'' '\\' '\0')lit", {
        {Kind::IntegerLiteral, "007"}, {Kind::Whitespace, " "},
        {Kind::BooleanLiteral, "true"}, {Kind::Whitespace, " "},
        {Kind::BooleanLiteral, "false"}, {Kind::Whitespace, " "},
        {Kind::StringLiteral, "\"\""}, {Kind::Whitespace, " "},
        {Kind::StringLiteral, R"lit("a\"b\n\0\qc")lit"}, {Kind::Whitespace, " "},
        {Kind::CharLiteral, R"lit('\'')lit"}, {Kind::Whitespace, " "},
        {Kind::CharLiteral, R"lit('\\')lit"}, {Kind::Whitespace, " "},
        {Kind::CharLiteral, R"lit('\0')lit"},
    });
    require(!result.tokens[0].decoded && !result.tokens[2].decoded && !result.tokens[4].decoded,
            fixture, "only quoted tokens have a decoded payload");
    require(result.tokens[6].decoded == "", fixture, "incorrect empty string payload");
    require(result.tokens[8].decoded == string("a\"b\n\0qc", 7), fixture,
            "incorrect decoded string bytes");
    require(result.tokens[10].decoded == "'", fixture, "incorrect escaped quote");
    require(result.tokens[12].decoded == "\\", fixture, "incorrect escaped backslash");
    require(result.tokens[14].decoded == string(1, '\0'), fixture, "incorrect NUL character");
}

void check_positions() {
    constexpr string_view fixture = "byte positions with mixed newlines";
    auto result = check_stream(fixture, "a\r\n\tb\rc\nd /*z\r\nw*/\n", {
        {Kind::Identifier, "a"}, {Kind::Whitespace, "\r\n\t"}, {Kind::Identifier, "b"},
        {Kind::Whitespace, "\r"}, {Kind::Identifier, "c"}, {Kind::Whitespace, "\n"},
        {Kind::Identifier, "d"}, {Kind::Whitespace, " "}, {Kind::Comment, "/*z\r\nw*/"},
        {Kind::Whitespace, "\n"},
    });
    const bminor::Location expected[] = {
        {1, 1}, {1, 2}, {2, 2}, {2, 3}, {3, 1}, {3, 2},
        {4, 1}, {4, 2}, {4, 3}, {5, 4}, {6, 1},
    };
    for (size_t i = 0; i < result.tokens.size(); ++i) {
        const auto location = result.tokens[i].span.start;
        require(location.line == expected[i].line && location.column == expected[i].column,
                fixture, "incorrect start location for token " + to_string(i));
    }
    auto empty = check_stream("empty source", "", {});
    require(empty.tokens.front().span.start.line == 1 && empty.tokens.front().span.start.column == 1,
            "empty source", "incorrect EOF location");
}

}

int main() {
    check_reserved_words();
    check_operators_and_delimiters();
    check_literals();
    check_positions();
}
