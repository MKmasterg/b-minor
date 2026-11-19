#include "bminor/parser.hpp"

#include <array>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace {

using std::array;
using std::cerr;
using std::exit;
using std::initializer_list;
using std::mt19937;
using std::size_t;
using std::string;
using std::string_view;

using bminor::Diagnostic;
using bminor::Node;
using bminor::NodeKind;
using bminor::ParseResult;
using bminor::Phase;
using bminor::TokenKind;

constexpr string_view filename = "recovery.bminor";

void check(bool passed, string_view description) {
    if (!passed) {
        cerr << description << '\n';
        exit(1);
    }
}

ParseResult parse_clean_tokens(string_view source) {
    const auto lexical = bminor::lex(source, filename);
    check(lexical.diagnostics.empty(), "recovery fixture should lex cleanly");
    return bminor::parse(lexical.tokens);
}

void anchor(const Diagnostic& diagnostic, size_t begin, size_t length,
            size_t line, size_t column, string_view description) {
    const auto span = diagnostic.span;
    if (diagnostic.phase != Phase::Syntax || diagnostic.code != "PARSE_EXPECTED" ||
        diagnostic.filename != filename || span.begin != begin || span.end != begin + length ||
        span.start.line != line || span.start.column != column ||
        diagnostic.message.find("Expected ") != 0) {
        cerr << description << ": unexpected diagnostic " << diagnostic.filename << ':'
                  << span.start.line << ':' << span.start.column << " [" << span.begin << ','
                  << span.end << ") " << diagnostic.code << ' ' << diagnostic.message << '\n';
        exit(1);
    }
}

void one_error(const ParseResult& parsed, size_t begin, size_t length,
               size_t line, size_t column, string_view description) {
    if (parsed.diagnostics.size() != 1) {
        cerr << description << ": expected one diagnostic, got "
                  << parsed.diagnostics.size() << '\n';
        exit(1);
    }
    anchor(parsed.diagnostics.front(), begin, length, line, column, description);
}

const Node& child(const Node& parent, size_t index, NodeKind kind,
                  string_view description) {
    check(index < parent.children.size() && parent.children[index] &&
          parent.children[index]->kind == kind, description);
    return *parent.children[index];
}

void globals(const ParseResult& parsed, initializer_list<string_view> names) {
    check(parsed.root && parsed.root->kind == NodeKind::Program, "recovered program root");
    check(parsed.root->children.size() == names.size(), "recovered declaration count");
    auto expected_name = names.begin();
    for (const auto& decl : parsed.root->children) {
        check(decl && (decl->kind == NodeKind::VarDecl || decl->kind == NodeKind::FuncDecl),
              "each recovered global is a declaration");
        check(child(*decl, 0, NodeKind::Identifier, "recovered declaration name").text ==
                  *expected_name, "recovered global name and order");
        ++expected_name;
    }
}

const Node& function_body(const ParseResult& parsed, string_view name) {
    globals(parsed, {name});
    const auto& function = child(*parsed.root, 0, NodeKind::FuncDecl, "recovered function");
    return child(function, 2, NodeKind::BlockStmt, "recovered function body");
}

void check_missing_semicolon() {
    constexpr string_view source = "first: integer;\nbad: integer\nnext: integer;";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find("next"), 4, 3, 1, "missing declaration semicolon");
    globals(parsed, {"first", "next"});
}

void check_missing_parenthesis() {
    constexpr string_view source = "m: function void () = { if (x { print f(); } return; }";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find("{ print"), 1, 1, 31, "missing condition parenthesis");
    const auto& body = function_body(parsed, "m");
    check(body.children.size() == 2, "missing condition parenthesis retains two siblings");
    const auto& standalone = child(body, 0, NodeKind::BlockStmt, "resumed standalone block");
    const auto& print = child(standalone, 0, NodeKind::PrintStmt, "resumed print statement");
    const auto& call = child(print, 0, NodeKind::CallExpr, "resumed call");
    check(child(call, 0, NodeKind::Identifier, "resumed callee").text == "f",
          "call callee after failed if head");
    child(body, 1, NodeKind::ReturnStmt, "resumed return statement");
}

void check_missing_brace() {
    constexpr string_view source = "m: function void () = {\n print f();\n return;\n";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.size(), 0, 4, 1, "missing closing brace at EOF");
    const auto& body = function_body(parsed, "m");
    check(body.span.end == source.size() && body.children.size() == 2,
          "partial block includes complete statements and spans through EOF");
    child(body, 0, NodeKind::PrintStmt, "completed print before EOF");
    child(body, 1, NodeKind::ReturnStmt, "completed return before EOF");
}

void check_trailing_junk() {
    constexpr string_view source = "good: integer;\ntrue";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find("true"), 4, 2, 1, "trailing junk");
    globals(parsed, {"good"});
}

void check_local_function() {
    constexpr string_view source =
        "demo: function void () = {\n local: function void ();\n return;\n}";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find("function", source.find("local")), 8, 2, 9,
              "local function is a syntax error");
    const auto& body = function_body(parsed, "demo");
    check(body.children.size() == 1, "discard local function declaration");
    child(body, 0, NodeKind::ReturnStmt, "return after rejected local function");
}

void check_reserved_while() {
    constexpr string_view source = "m: function void () = {\n  while(x);\n  return;\n}";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find("while"), 5, 2, 3, "while is reserved");
    const auto& body = function_body(parsed, "m");
    check(body.children.size() == 1, "discard reserved while statement");
    child(body, 0, NodeKind::ReturnStmt, "return after rejected while");
}

void check_bad_initializer() {
    constexpr string_view source = "bad: integer = ; good: integer = 2;";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find(';'), 1, 1, 16, "missing initializer expression");
    globals(parsed, {"good"});
}

void check_bad_print() {
    constexpr string_view source = "demo: function integer () = {\n  print ;\n  return 7;\n}";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find(';'), 1, 2, 9, "missing print argument");
    const auto& body = function_body(parsed, "demo");
    check(body.children.size() == 1, "discard incomplete print");
    const auto& ret = child(body, 0, NodeKind::ReturnStmt, "return after bad print");
    check(child(ret, 0, NodeKind::Literal, "retained return value").text == "7",
          "return value remains available after recovery");
}

void check_independent_errors() {
    constexpr string_view source =
        "m: function void () = {\n print ;\n x: integer = ;\n return;\n}";
    const auto parsed = parse_clean_tokens(source);
    check(parsed.diagnostics.size() == 2, "two independent statements report exactly two errors");
    anchor(parsed.diagnostics[0], source.find(';'), 1, 2, 8, "first independent error");
    anchor(parsed.diagnostics[1], source.find(';', source.find("x:")), 1, 3, 15,
           "second independent error");
    const auto& body = function_body(parsed, "m");
    check(body.children.size() == 1, "discard both malformed statements");
    child(body, 0, NodeKind::ReturnStmt, "return after independent errors");
}

void check_invalid_bytes() {
    const auto lexical = bminor::lex("a: integer;\n@\nb: integer;", filename);
    check(lexical.diagnostics.size() == 1 && lexical.diagnostics[0].code == "LEX_UNKNOWN_BYTE",
          "invalid global byte is diagnosed by the lexer");
    const auto parsed = bminor::parse(lexical.tokens);
    check(parsed.diagnostics.empty(), "no syntax diagnostic for an invalid byte between globals");
    globals(parsed, {"a", "b"});

    const auto in_body = bminor::lex("m: function void () = { print f(); @ return; }", filename);
    check(in_body.diagnostics.size() == 1 && in_body.diagnostics[0].code == "LEX_UNKNOWN_BYTE",
          "invalid statement byte is diagnosed by the lexer");
    const auto statements = bminor::parse(in_body.tokens);
    check(statements.diagnostics.empty(), "no duplicate diagnostic between statements");
    const auto& body = function_body(statements, "m");
    check(body.children.size() == 2, "invalid byte does not discard adjacent statements");
    child(body, 0, NodeKind::PrintStmt, "print before invalid byte");
    child(body, 1, NodeKind::ReturnStmt, "return after invalid byte");
}

void check_stray_brace() {
    constexpr string_view source = "}\na: integer;";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, 0, 1, 1, 1, "stray top-level brace");
    globals(parsed, {"a"});
}

void check_truncated_call() {
    constexpr string_view source = "good: integer;\nbad: integer = f(";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.size(), 0, 2, 18, "truncated call");
    globals(parsed, {"good"});
}

void check_initializer_brace_depth() {
    constexpr string_view source = "bad: array [2] integer = {1, ; 2}; good: integer;";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find(';'), 1, 1, 30, "malformed initializer retains its brace");
    globals(parsed, {"good"});

    constexpr string_view nested =
        "bad: array [2] array [2] integer = {{1, ; ghost: integer}, {3, 4}}; good: integer;";
    const auto nested_result = parse_clean_tokens(nested);
    one_error(nested_result, nested.find(';'), 1, 1, nested.find(';') + 1,
              "nested malformed initializer ignores declaration inside braces");
    globals(nested_result, {"good"});

    constexpr string_view skipped =
        "bad: integer unexpected { ghost: integer; { print; } }; good: integer;";
    const auto skipped_result = parse_clean_tokens(skipped);
    one_error(skipped_result, skipped.find("unexpected"), 10, 1, 14,
              "skipped brace group ignores semicolons and restart tokens");
    globals(skipped_result, {"good"});

    constexpr string_view ambiguous =
        "m: function void () = { print f();\n  bad: array [2] integer = {1, ; return; }";
    const auto ambiguous_result = parse_clean_tokens(ambiguous);
    check(ambiguous_result.diagnostics.size() == 2,
          "ambiguous closing brace closes initializer and leaves the block unclosed");
    const auto bad_token = ambiguous.find(';', ambiguous.find("bad:"));
    anchor(ambiguous_result.diagnostics[0], bad_token, 1, 2, 32, "malformed local initializer");
    anchor(ambiguous_result.diagnostics[1], ambiguous.size(), 0, 2, 43, "local block missing brace");
    const auto& block = function_body(ambiguous_result, "m");
    check(block.children.size() == 1, "ignore restart tokens inside unfinished local initializer");
    child(block, 0, NodeKind::PrintStmt, "print kept in partial local block");
}

void check_inner_expression_boundaries() {
    constexpr string_view source = "m: function void () = { print a[3; return; }";
    const auto parsed = parse_clean_tokens(source);
    one_error(parsed, source.find(';'), 1, 1, 34, "statement semicolon after missing bracket");
    const auto& body = function_body(parsed, "m");
    check(body.children.size() == 1, "missing bracket keeps later statement");
    child(body, 0, NodeKind::ReturnStmt, "return after missing bracket");

    constexpr string_view before_close = "m: function void () = { print a(3 } good: integer;";
    const auto close_result = parse_clean_tokens(before_close);
    one_error(close_result, before_close.find('}'), 1, 1, 35, "owning brace after missing parenthesis");
    globals(close_result, {"m", "good"});
    const auto& function = child(*close_result.root, 0, NodeKind::FuncDecl, "function retained");
    check(child(function, 2, NodeKind::BlockStmt, "block retained").children.empty(),
          "failed child before owning brace is discarded");
}

void check_corpus() {
    constexpr array<string_view, 25> fragments{
        "x", "demo", ": integer", "{", "}", ";", "[", "]", "(", ")", ",",
        "++", ">=", "&&", " = ", "if ", "return ", "for ", "print ", "\t\r\n", "@",
        "\xFF\x80", "row: array [2] integer = {1,2};", "f: function void () = { print g(); }",
        "bad: array [2] integer = {1, ; 2};"
    };
    mt19937 random(0xB017);
    for (unsigned i = 0; i < 160; ++i) {
        string source;
        const size_t limit = random() % 257;
        while (source.size() < limit) {
            auto fragment = fragments[random() % fragments.size()];
            if (random() % 3 == 0) {
                fragment = fragment.substr(0, 1 + random() % fragment.size());
            }
            source.append(fragment.substr(0, limit - source.size()));
        }
        const auto lexical = bminor::lex(source, filename);
        check(!lexical.tokens.empty(), "generated input token stream includes EOF");
        size_t offset = 0;
        for (size_t j = 0; j + 1 < lexical.tokens.size(); ++j) {
            const auto& token = lexical.tokens[j];
            check(token.kind != TokenKind::EndOfFile && token.span.begin == offset &&
                  token.span.begin < token.span.end && token.span.end <= source.size(),
                  "generated input token spans are contiguous and nonempty");
            check(token.lexeme == source.substr(offset, token.span.end - offset),
                  "generated input raw token slices match source bytes");
            offset = token.span.end;
        }
        const auto& eof = lexical.tokens.back();
        check(offset == source.size() && eof.kind == TokenKind::EndOfFile &&
              eof.span.begin == offset && eof.span.end == offset && eof.lexeme.empty(),
              "generated input lexer covers all bytes and has one EOF");
        const auto parsed = bminor::parse(lexical.tokens);
        check(parsed.root && parsed.root->kind == NodeKind::Program, "generated input always parses to a program");
        for (const auto* diagnostics : {&lexical.diagnostics, &parsed.diagnostics}) {
            for (const auto& diagnostic : *diagnostics) {
                check(diagnostic.span.begin <= diagnostic.span.end && diagnostic.span.end <= source.size() &&
                      diagnostic.span.start.line > 0 && diagnostic.span.start.column > 0 &&
                      diagnostic.filename == filename, "generated input diagnostic bounds");
            }
        }
    }
}

}

int main() {
    check_missing_semicolon();
    check_missing_parenthesis();
    check_missing_brace();
    check_trailing_junk();
    check_local_function();
    check_reserved_while();
    check_bad_initializer();
    check_bad_print();
    check_independent_errors();
    check_invalid_bytes();
    check_stray_brace();
    check_truncated_call();
    check_initializer_brace_depth();
    check_inner_expression_boundaries();
    check_corpus();
}
