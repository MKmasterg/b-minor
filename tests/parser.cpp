#include "bminor/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using std::cerr;
using std::exit;
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

unique_ptr<Node> expression(string_view source) {
    const auto input = bminor::lex(source, "expression.bminor");
    check(input.diagnostics.empty(), "expression fixture should lex cleanly");
    auto result = bminor::parse_expression(input.tokens);
    if (!result.diagnostics.empty()) {
        cerr << result.diagnostics.front().message << " in " << source << '\n';
        exit(1);
    }
    check(result.root != nullptr, "expression root should exist");
    return ::move(result.root);
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
        check_annotations(*child);
    }
}

void check_shape(string_view source, string_view expected) {
    const auto root = expression(source);
    const auto actual = shape(*root);
    if (actual != expected) {
        cerr << source << ": expected " << expected << ", got " << actual << '\n';
        exit(1);
    }
    check(root->span.begin == 0 && root->span.end == source.size(),
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
    check_span(*call, 1, 19, 1, 2, "call span should include its closing parenthesis");
    check_span(*call->children[0], 1, 2, 1, 2, "callee should carry its token span");
    check_span(*call->children[1], 6, 10, 2, 2, "index span should include its closing bracket");
    check_span(*call->children[2], 13, 18, 3, 2, "group span should include both parentheses");
    check_span(*call->children[2]->children[0], 14, 17, 3, 3, "binary expression inside group span");
    check_annotations(*call);
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
}
