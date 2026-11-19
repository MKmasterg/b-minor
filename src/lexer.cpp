#include "bminor/lexer.hpp"

#include <utility>

namespace bminor {

using std::move;
using std::optional;
using std::size_t;
using std::string;
using std::string_view;
namespace {

using Kind = TokenKind;

struct Spelling {
    string_view text;
    Kind kind;
};

constexpr Spelling keywords[] = {
    {"array", Kind::ArrayKeyword},
    {"boolean", Kind::BooleanKeyword},
    {"char", Kind::CharKeyword},
    {"else", Kind::ElseKeyword},
    {"for", Kind::ForKeyword},
    {"function", Kind::FunctionKeyword},
    {"if", Kind::IfKeyword},
    {"integer", Kind::IntegerKeyword},
    {"print", Kind::PrintKeyword},
    {"return", Kind::ReturnKeyword},
    {"string", Kind::StringKeyword},
    {"void", Kind::VoidKeyword},
    {"while", Kind::WhileKeyword},
    {"true", Kind::BooleanLiteral},
    {"false", Kind::BooleanLiteral},
};

constexpr Spelling symbols[] = {
    {"++", Kind::Increment}, {"--", Kind::Decrement},
    {"<=", Kind::LessEqual}, {">=", Kind::GreaterEqual},
    {"==", Kind::Equal}, {"!=", Kind::NotEqual},
    {"&&", Kind::And}, {"||", Kind::Or},
    {"+", Kind::Plus}, {"-", Kind::Minus}, {"*", Kind::Star},
    {"/", Kind::Slash}, {"%", Kind::Percent}, {"^", Kind::Caret},
    {"!", Kind::Bang}, {"<", Kind::Less}, {">", Kind::Greater},
    {"=", Kind::Assign}, {"(", Kind::LeftParen}, {")", Kind::RightParen},
    {"[", Kind::LeftBracket}, {"]", Kind::RightBracket},
    {"{", Kind::LeftBrace}, {"}", Kind::RightBrace},
    {",", Kind::Comma}, {":", Kind::Colon}, {";", Kind::Semicolon},
};

bool is_newline(char byte) {
    return byte == '\r' || byte == '\n';
}

bool is_whitespace(char byte) {
    return byte == ' ' || byte == '\t' || is_newline(byte);
}

bool is_digit(char byte) {
    return byte >= '0' && byte <= '9';
}

bool is_identifier_start(char byte) {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || byte == '_';
}

char decode_escape(char byte) {
    switch (byte) {
    case 'n': return '\n';
    case '0': return '\0';
    default: return byte;
    }
}

class Scanner {
public:
    Scanner(string_view source, string_view filename)
        : source_(source), filename_(filename) {}

    LexResult run() {
        LexResult result;
        while (!at_end()) {
            Span span = cursor_.point();
            optional<string> decoded;
            const Kind kind = scan_kind(decoded);
            span.end = offset();
            result.tokens.push_back({kind, string(source_.substr(span.begin, span.end - span.begin)),
                                     string(filename_), span, bminor::move(decoded)});
        }
        result.tokens.push_back({Kind::EndOfFile, "", string(filename_), cursor_.point(), {}});
        return result;
    }

private:
    size_t offset() const { return cursor_.point().begin; }
    bool at_end() const { return offset() == source_.size(); }
    char peek() const { return at_end() ? '\0' : source_[offset()]; }

    char take() {
        const char byte = source_[offset()];
        cursor_.advance(byte);
        return byte;
    }

    bool starts_with(string_view text) const {
        return source_.substr(offset(), text.size()) == text;
    }

    Kind scan_identifier() {
        const size_t begin = offset();
        do {
            take();
        } while (is_identifier_start(peek()) || is_digit(peek()));

        const auto text = source_.substr(begin, offset() - begin);
        for (const auto& keyword : keywords) {
            if (text == keyword.text) {
                return keyword.kind;
            }
        }
        return Kind::Identifier;
    }

    Kind scan_comment() {
        const bool block = starts_with("/*");
        take();
        take();
        if (!block) {
            while (!at_end() && !is_newline(peek())) {
                take();
            }
            return Kind::Comment;
        }

        while (!at_end() && !starts_with("*/")) {
            take();
        }
        if (at_end()) {
            return Kind::Invalid;
        }
        take();
        take();
        return Kind::Comment;
    }

    Kind scan_quoted(optional<string>& decoded) {
        const char quote = take();
        string payload;
        while (!at_end() && !is_newline(peek())) {
            const char byte = take();
            if (byte == quote) {
                decoded = bminor::move(payload);
                return quote == '"' ? Kind::StringLiteral : Kind::CharLiteral;
            }
            if (byte == '\\') {
                if (at_end() || is_newline(peek())) {
                    break;
                }
                payload += decode_escape(take());
            } else {
                payload += byte;
            }
        }
        return Kind::Invalid;
    }

    Kind scan_kind(optional<string>& decoded) {
        if (is_whitespace(peek())) {
            do {
                take();
            } while (is_whitespace(peek()));
            return Kind::Whitespace;
        }
        if (is_identifier_start(peek())) {
            return scan_identifier();
        }
        if (is_digit(peek())) {
            do {
                take();
            } while (is_digit(peek()));
            return Kind::IntegerLiteral;
        }
        if (peek() == '"' || peek() == '\'') {
            return scan_quoted(decoded);
        }
        if (starts_with("//") || starts_with("/*")) {
            return scan_comment();
        }
        for (const auto& symbol : symbols) {
            if (starts_with(symbol.text)) {
                for (size_t i = 0; i < symbol.text.size(); ++i) {
                    take();
                }
                return symbol.kind;
            }
        }
        take();
        return Kind::Invalid;
    }

    string_view source_;
    string_view filename_;
    SourceCursor cursor_;
};

}

string_view token_name(TokenKind kind) {
    switch (kind) {
    case Kind::EndOfFile: return "end of file";
    case Kind::Invalid: return "invalid token";
    case Kind::Whitespace: return "whitespace";
    case Kind::Comment: return "comment";
    case Kind::Identifier: return "identifier";
    case Kind::IntegerLiteral: return "integer literal";
    case Kind::BooleanLiteral: return "boolean literal";
    case Kind::StringLiteral: return "string literal";
    case Kind::CharLiteral: return "character literal";
    default: break;
    }
    for (const auto& keyword : keywords) {
        if (kind == keyword.kind) {
            return keyword.text;
        }
    }
    for (const auto& symbol : symbols) {
        if (kind == symbol.kind) {
            return symbol.text;
        }
    }
    return "token";
}

LexResult lex(string_view source, string_view filename) {
    return Scanner(source, filename).run();
}

}
