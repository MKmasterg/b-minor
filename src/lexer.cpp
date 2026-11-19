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

struct Match {
    Kind kind;
    optional<string> decoded = {};
    string_view code = {};
    string message = {};
};

Match invalid(string_view code, string message) {
    return {Kind::Invalid, {}, code, bminor::move(message)};
}

Match unknown_byte(unsigned char byte) {
    constexpr string_view hex = "0123456789ABCDEF";
    string message = "unknown byte 0x";
    message += hex[byte >> 4];
    message += hex[byte & 0x0f];
    return invalid("LEX_UNKNOWN_BYTE", bminor::move(message));
}

class Scanner {
public:
    Scanner(string_view source, string_view filename)
        : source_(source), filename_(filename) {}

    LexResult run() {
        LexResult result;
        while (!at_end()) {
            Span span = cursor_.point();
            auto match = scan_token();
            span.end = offset();
            if (match.kind == Kind::Invalid) {
                result.diagnostics.push_back({Phase::Lexical, string(match.code),
                                              string(filename_), span, bminor::move(match.message)});
            }
            const auto lexeme = source_.substr(span.begin, span.end - span.begin);
            result.tokens.push_back({match.kind, string(lexeme), string(filename_),
                                     span, bminor::move(match.decoded)});
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

    Match scan_identifier() {
        const size_t begin = offset();
        do {
            take();
        } while (is_identifier_start(peek()) || is_digit(peek()));

        const auto text = source_.substr(begin, offset() - begin);
        if (text.size() > 256) {
            return invalid("LEX_IDENT_LENGTH", "identifier exceeds 256 bytes");
        }
        for (const auto& keyword : keywords) {
            if (text == keyword.text) {
                return {keyword.kind};
            }
        }
        return {Kind::Identifier};
    }

    Match scan_comment() {
        const bool block = starts_with("/*");
        take();
        take();
        if (!block) {
            while (!at_end() && !is_newline(peek())) {
                take();
            }
            return {Kind::Comment};
        }

        while (!at_end() && !starts_with("*/")) {
            take();
        }
        if (at_end()) {
            return invalid("LEX_UNTERMINATED_COMMENT", "expected '*/' to close block comment");
        }
        take();
        take();
        return {Kind::Comment};
    }

    Match scan_quoted() {
        const char quote = take();
        const bool string_literal = quote == '"';
        const size_t limit = string_literal ? 256 : 1;
        string payload;
        bool ascii_payload = true;

        while (!at_end() && !is_newline(peek())) {
            char byte = take();
            if (byte == quote) {
                if (!ascii_payload) {
                    return invalid("LEX_BAD_LITERAL", "quoted literal contains a non-ASCII byte");
                }
                if (string_literal && payload.size() > limit) {
                    return invalid("LEX_BAD_LITERAL", "string literal exceeds 256 decoded bytes");
                }
                if (!string_literal && payload.size() != limit) {
                    return invalid("LEX_BAD_LITERAL", "character literal requires one decoded byte");
                }
                return {string_literal ? Kind::StringLiteral : Kind::CharLiteral, bminor::move(payload)};
            }
            if (byte == '\\') {
                if (at_end() || is_newline(peek())) {
                    break;
                }
                byte = decode_escape(take());
            }
            ascii_payload = ascii_payload && static_cast<unsigned char>(byte) <= 0x7f;
            // Keep one extra byte to detect overflow while looking for the closing quote.
            if (payload.size() <= limit) {
                payload += byte;
            }
        }
        if (string_literal) {
            return invalid("LEX_UNTERMINATED_STRING", "missing closing double quote");
        }
        return invalid("LEX_UNTERMINATED_CHAR", "missing closing single quote");
    }

    Match scan_token() {
        if (is_whitespace(peek())) {
            do {
                take();
            } while (is_whitespace(peek()));
            return {Kind::Whitespace};
        }
        if (is_identifier_start(peek())) {
            return scan_identifier();
        }
        if (is_digit(peek())) {
            do {
                take();
            } while (is_digit(peek()));
            return {Kind::IntegerLiteral};
        }
        if (peek() == '"' || peek() == '\'') {
            return scan_quoted();
        }
        if (starts_with("//") || starts_with("/*")) {
            return scan_comment();
        }
        for (const auto& symbol : symbols) {
            if (starts_with(symbol.text)) {
                for (size_t i = 0; i < symbol.text.size(); ++i) {
                    take();
                }
                return {symbol.kind};
            }
        }
        return unknown_byte(static_cast<unsigned char>(take()));
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
