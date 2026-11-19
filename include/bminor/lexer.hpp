#pragma once

#include "bminor/source.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bminor {

using std::optional;
using std::string;
using std::string_view;
using std::vector;

enum class TokenKind {
    EndOfFile,
    Invalid,
    Whitespace,
    Comment,
    Identifier,
    IntegerLiteral,
    BooleanLiteral,
    StringLiteral,
    CharLiteral,
    ArrayKeyword,
    BooleanKeyword,
    CharKeyword,
    ElseKeyword,
    ForKeyword,
    FunctionKeyword,
    IfKeyword,
    IntegerKeyword,
    PrintKeyword,
    ReturnKeyword,
    StringKeyword,
    VoidKeyword,
    WhileKeyword,
    Increment,
    Decrement,
    LessEqual,
    GreaterEqual,
    Equal,
    NotEqual,
    And,
    Or,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Caret,
    Bang,
    Less,
    Greater,
    Assign,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Colon,
    Semicolon,
};

struct Token {
    TokenKind kind;
    string lexeme;
    string filename;
    Span span;
    optional<string> decoded;
};

struct LexResult {
    vector<Token> tokens;
    vector<Diagnostic> diagnostics;
};

string_view token_name(TokenKind kind);
LexResult lex(string_view source, string_view filename);

}
