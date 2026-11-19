#include "bminor/parser.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

using std::cerr;
using std::exit;
using std::ifstream;
using std::ios;
using std::istreambuf_iterator;
using std::make_unique;
using std::move;
using std::size_t;
using std::string;
using std::string_view;
using std::unique_ptr;

using bminor::Node;
using bminor::NodeKind;

void check(bool passed, string_view description) {
    if (!passed) {
        cerr << description << '\n';
        exit(1);
    }
}

unique_ptr<Node> program(string_view source) {
    const auto input = bminor::lex(source, "parser.bminor");
    check(input.diagnostics.empty(), "parser fixture should lex cleanly");
    auto result = bminor::parse(input.tokens);
    if (!result.diagnostics.empty()) {
        cerr << result.diagnostics.front().message << " in " << source << '\n';
        exit(1);
    }
    check(result.root && result.root->kind == NodeKind::Program, "program root should exist");
    return ::move(result.root);
}

constexpr string_view expression_prefix = "value: integer = ";

unique_ptr<Node> expression(string_view source) {
    auto root = program(string(expression_prefix) + string(source) + ";");
    check(root->children.size() == 1, "expression fixture should have one declaration");
    auto& decl = *root->children.front();
    check(decl.kind == NodeKind::VarDecl && decl.children.size() == 3 && decl.children[2],
          "expression fixture declaration should have an initializer");
    return ::move(decl.children[2]);
}

string shape(const Node& node) {
    string name;
    switch (node.kind) {
    case NodeKind::Identifier:
    case NodeKind::Literal:
        check(node.children.empty(), "leaves should have no children");
        return node.text;
    case NodeKind::GroupExpr: name = "group"; break;
    case NodeKind::CallExpr: name = "call"; break;
    case NodeKind::IndexExpr: name = "index"; break;
    case NodeKind::PrefixExpr: name = "prefix "; break;
    case NodeKind::PostfixExpr: name = "postfix "; break;
    case NodeKind::BinaryExpr:
    case NodeKind::Assignment:
        break;
    default:
        check(false, "expected an expression node");
    }
    if (node.token_kind) {
        name += bminor::token_name(*node.token_kind);
    }
    name += '(';
    bool first = true;
    for (const auto& child : node.children) {
        check(child != nullptr, "expression children should exist");
        if (!first) {
            name += ", ";
        }
        name += shape(*child);
        first = false;
    }
    name += ')';
    return name;
}

void check_annotations(const Node& node) {
    check(!node.type_annotation, "type annotations should start empty");
    for (const auto& child : node.children) {
        if (child) {
            check_annotations(*child);
        }
    }
}

void check_shape(string_view source, string_view expected) {
    const auto root = expression(source);
    const auto actual = shape(*root);
    if (actual != expected) {
        cerr << source << ": expected " << expected << ", got " << actual << '\n';
        exit(1);
    }
    check(root->span.begin == expression_prefix.size() &&
          root->span.end == expression_prefix.size() + source.size(),
          "expression span should cover the entire fixture");
    check_annotations(*root);
}

void check_span(const Node& node, size_t begin, size_t end,
                size_t line, size_t column, string_view description) {
    check(node.span.begin == begin && node.span.end == end &&
          node.span.start.line == line && node.span.start.column == column, description);
}

void check_multiline_locations() {
    const auto call = expression(" f(\r\n\tx[1],\n (2+3)) ");
    check(shape(*call) == "call(f, index(x, 1), group(+(2, 3)))", "multiline call shape");
    const auto offset = expression_prefix.size();
    check_span(*call, offset + 1, offset + 19, 1, offset + 2, "call span includes its close");
    check_span(*call->children[0], offset + 1, offset + 2, 1, offset + 2,
               "callee carries its own token span");
    check_span(*call->children[1], offset + 6, offset + 10, 2, 2, "index span includes its close");
    check_span(*call->children[2], offset + 13, offset + 18, 3, 2, "group includes both parentheses");
    check_span(*call->children[2]->children[0], offset + 14, offset + 17, 3, 3,
               "binary expression inside group span");
    check_annotations(*call);
}

const Node& child(const Node& node, size_t index, NodeKind kind,
                  string_view description) {
    check(index < node.children.size() && node.children[index] &&
          node.children[index]->kind == kind, description);
    return *node.children[index];
}

void absent(const Node& node, size_t index, string_view description) {
    check(index < node.children.size() && !node.children[index], description);
}

void check_program_shapes() {
    const auto empty = program("");
    check(empty->children.empty(), "empty program has no declarations");
    check_span(*empty, 0, 0, 1, 1, "empty program span");

    ifstream demo("examples/demo.bminor", ios::binary);
    check(demo.is_open(), "demo fixture should open");
    const string source((istreambuf_iterator<char>(demo)), {});
    const auto root = program(source);
    check(root->children.size() == 4, "demo has four global declarations");
    check_span(*root, 0, source.size(), 1, 1, "program spans full input");
    check_annotations(*root);

    const auto& seed = child(*root, 0, NodeKind::VarDecl, "scalar declaration");
    const auto& seed_name = child(seed, 0, NodeKind::Identifier, "scalar name");
    const auto seed_begin = source.find("seed:");
    check_span(seed_name, seed_begin, seed_begin + 4, 2, 1, "scalar name span");
    check_span(seed, seed_begin, seed_begin + 18, 2, 1, "scalar declaration span");
    check(child(seed, 1, NodeKind::AtomType, "scalar type").text == "integer", "scalar type name");
    check(child(seed, 2, NodeKind::Literal, "scalar initializer").text == "3",
          "scalar initializer value");

    const auto& matrix = child(*root, 1, NodeKind::VarDecl, "matrix declaration");
    const auto& outer = child(matrix, 1, NodeKind::ArrayType, "outer array type");
    check(child(outer, 0, NodeKind::Literal, "outer bound").text == "2", "outer array bound value");
    const auto& inner = child(outer, 1, NodeKind::ArrayType, "inner array type");
    check(child(inner, 0, NodeKind::Literal, "inner bound").text == "2", "inner array bound value");
    check(child(inner, 1, NodeKind::AtomType, "matrix element type").text == "integer",
          "matrix element type name");
    const auto& list = child(matrix, 2, NodeKind::InitializerList, "matrix initializer list");
    check(list.children.size() == 2, "matrix initializer row count");
    const auto& first_row = child(list, 0, NodeKind::InitializerList, "first initializer row");
    const auto& second_row = child(list, 1, NodeKind::InitializerList, "second initializer row");
    check(first_row.children.size() == 2 && second_row.children.size() == 2,
          "initializer column count");
    check(child(first_row, 1, NodeKind::Literal, "first row element").text == "2",
          "list stores elements in source order");
    check(child(second_row, 1, NodeKind::Identifier, "second row element").text == "seed",
          "nested initializer accepts an expression");

    const auto& proto = child(*root, 2, NodeKind::FuncDecl, "function prototype");
    check(proto.children.size() == 5, "prototype layout");
    const auto& name = child(proto, 0, NodeKind::Identifier, "prototype name");
    const auto name_begin = source.find("read_row:");
    check_span(name, name_begin, name_begin + 8, 4, 1, "prototype name span");
    check(child(proto, 1, NodeKind::AtomType, "prototype return type").text == "integer",
          "prototype return type name");
    const auto& row_param = child(proto, 2, NodeKind::Param, "first parameter");
    check(child(row_param, 0, NodeKind::Identifier, "first parameter name").text == "row",
          "first parameter name spelling");
    const auto& row_type = child(row_param, 1, NodeKind::ArrayType, "unsized array parameter type");
    absent(row_type, 0, "unsized parameter array has no bound");
    const auto& flags_param = child(proto, 3, NodeKind::Param, "second parameter");
    check(child(flags_param, 0, NodeKind::Identifier, "second parameter name").text == "flags",
          "second parameter name spelling");
    const auto& flags_type = child(flags_param, 1, NodeKind::ArrayType, "sized array parameter type");
    check(child(flags_type, 0, NodeKind::Literal, "parameter bound").text == "2",
          "sized parameter array bound value");
    absent(proto, 4, "prototype has no body");

    const auto& main = child(*root, 3, NodeKind::FuncDecl, "function definition");
    check(main.children.size() == 3 &&
          child(main, 0, NodeKind::Identifier, "definition name").text == "main" &&
          child(main, 1, NodeKind::AtomType, "definition type").text == "void",
          "definition name, result type, and empty parameter list");
    const auto& body = child(main, 2, NodeKind::BlockStmt, "definition body");
    check(body.children.size() == 8, "body contains every statement in source order");

    const auto& index = child(body, 0, NodeKind::VarDecl, "local scalar declaration");
    check(child(index, 2, NodeKind::Literal, "local initializer").text == "0",
          "local scalar initializer value");
    const auto& flags = child(body, 1, NodeKind::VarDecl, "local array declaration");
    check(child(flags, 2, NodeKind::InitializerList, "local initializer list").children.size() == 2,
          "local array has both initializers");

    const auto& block = child(body, 2, NodeKind::BlockStmt, "nested block");
    const auto& print = child(block, 0, NodeKind::PrintStmt, "print statement");
    check(print.children.size() == 3, "print argument count");
    check(child(print, 0, NodeKind::Literal, "print string").decoded == "row <tag>",
          "string literal should retain its decoded payload");
    const auto& call = child(print, 2, NodeKind::CallExpr, "call inside print");
    check(shape(call) == "call(read_row, index(matrix, 0), flags)", "demo call and arguments");

    const auto& outer_if = child(body, 3, NodeKind::IfStmt, "outer if");
    check(shape(child(outer_if, 0, NodeKind::BinaryExpr, "outer condition")) == ">(seed, 0)",
          "outer if has its own condition");
    const auto& inner_if = child(outer_if, 1, NodeKind::IfStmt, "inner if");
    absent(outer_if, 2, "else should skip the outer if");
    child(inner_if, 1, NodeKind::ExprStmt, "inner then branch");
    child(inner_if, 2, NodeKind::ExprStmt, "else attaches to nearest if");

    const auto& loop = child(body, 4, NodeKind::ForStmt, "for with expressions");
    child(loop, 0, NodeKind::Assignment, "for initial expression");
    child(loop, 1, NodeKind::BinaryExpr, "for condition");
    child(loop, 2, NodeKind::PostfixExpr, "for step");
    child(loop, 3, NodeKind::BlockStmt, "for block body");

    const auto& empty_loop = child(body, 5, NodeKind::ForStmt, "for without expressions");
    absent(empty_loop, 0, "omitted for initial expression");
    absent(empty_loop, 1, "omitted for condition");
    absent(empty_loop, 2, "omitted for step");
    child(empty_loop, 3, NodeKind::EmptyStmt, "empty for body");
    child(body, 6, NodeKind::EmptyStmt, "standalone empty statement");
    absent(child(body, 7, NodeKind::ReturnStmt, "return statement"), 0,
           "omitted return expression");

    const auto other = program("m: function integer () = { x: integer; if (a) if (b) x; else y; return 4; }");
    const auto& other_body = child(*other->children[0], 2, NodeKind::BlockStmt, "second function body");
    absent(child(other_body, 0, NodeKind::VarDecl, "uninitialized declaration"), 2,
           "variable initializer may be absent");
    const auto& first_if = child(other_body, 1, NodeKind::IfStmt, "nearest-if fixture outer branch");
    const auto& second_if = child(first_if, 1, NodeKind::IfStmt, "nearest-if fixture inner branch");
    absent(first_if, 2, "nearest-if fixture outer else omitted");
    const auto& else_branch = child(second_if, 2, NodeKind::ExprStmt, "nearest-if fixture inner else");
    check(child(else_branch, 0, NodeKind::Identifier, "else expression").text == "y",
          "nearest-if fixture else expression is y");
    const auto& ret = child(other_body, 2, NodeKind::ReturnStmt, "return with expression");
    check(child(ret, 0, NodeKind::Literal, "returned expression").text == "4", "returned literal");
    check_annotations(*other);
}

void check_deep_tree_destruction() {
    auto root = make_unique<Node>(NodeKind::Literal, bminor::Span{});
    for (size_t depth = 0; depth < 300'000; ++depth) {
        auto parent = make_unique<Node>(NodeKind::GroupExpr, bminor::Span{});
        parent->children.push_back(::move(root));
        root = ::move(parent);
    }
    root.reset();
}

}

int main() {
    check_shape("a = b = 2", "=(a, =(b, 2))");
    check(expression("a = b")->kind == NodeKind::Assignment, "assignment and binary nodes should differ");
    check_shape("2^3^4", "^(2, ^(3, 4))");
    check_shape("-2^3*4+5", "+(*(^(prefix -(2), 3), 4), 5)");
    check_shape("a + b + c", "+(+(a, b), c)");
    check_shape("a < b == c || d && e", "||(==(<(a, b), c), &&(d, e))");
    check_shape("f(a[1], (2+3))", "call(f, index(a, 1), group(+(2, 3)))");
    check_shape("a[0][1]++", "postfix ++(index(index(a, 0), 1))");
    check_multiline_locations();
    check_program_shapes();
    check_deep_tree_destruction();
}
