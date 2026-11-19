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
    Program,
    VarDecl,
    FuncDecl,
    Param,
    AtomType,
    ArrayType,
    InitializerList,
    BlockStmt,
    IfStmt,
    ForStmt,
    ReturnStmt,
    PrintStmt,
    ExprStmt,
    EmptyStmt,
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
    Node(Node&&) = default;
    Node& operator=(Node&&) = default;

    ~Node() {
        // Detach children before deleting them to bound teardown stack depth.
        Node* node = this;
        while (!node->children.empty() || node != this) {
            if (!node->children.empty()) {
                auto* child = node->children.back().release();
                node->children.pop_back();
                if (child) {
                    child->destruction_parent_ = node;
                    node = child;
                }
            } else {
                auto* parent = node->destruction_parent_;
                delete node;
                node = parent;
            }
        }
    }

    NodeKind kind;
    Span span;
    optional<string> type_annotation;
    optional<TokenKind> token_kind;
    string text;
    optional<string> decoded;
    vector<unique_ptr<Node>> children;

private:
    Node* destruction_parent_ = nullptr;
};

}
