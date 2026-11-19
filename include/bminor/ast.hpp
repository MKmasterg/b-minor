#pragma once

#include "bminor/lexer.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bminor {

using std::optional;
using std::string;
using std::unique_ptr;
using std::vector;

enum class NodeKind {
    Identifier,
    Literal,
    GroupExpr,
    IndexExpr,
    CallExpr,
    PrefixExpr,
    PostfixExpr,
    BinaryExpr,
    Assignment,
};

struct Node {
    Node(NodeKind kind, Span span) : kind(kind), span(span) {}

    NodeKind kind;
    Span span;
    optional<string> type_annotation;
    optional<TokenKind> token_kind;
    string text;
    optional<string> decoded;
    vector<unique_ptr<Node>> children;
};

}
