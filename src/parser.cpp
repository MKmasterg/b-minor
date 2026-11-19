#include "bminor/parser.hpp"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace bminor {

using std::find;
using std::initializer_list;
using std::make_unique;
using std::move;
using std::size_t;
using std::string;
using std::string_view;
using std::unique_ptr;
using std::vector;
namespace {

using Kind = TokenKind;
using NodePtr = unique_ptr<Node>;

struct ParseFailure {
    Diagnostic diagnostic;
};

Span through(Span first, Span last) {
    first.end = last.end;
    return first;
}

NodePtr leaf(NodeKind kind, const Token& token) {
    auto node = make_unique<Node>(kind, token.span);
    node->token_kind = token.kind;
    node->text = token.lexeme;
    node->decoded = token.decoded;
    return node;
}

NodePtr binary(NodeKind kind, const Token& op, NodePtr left, NodePtr right) {
    auto node = make_unique<Node>(kind, through(left->span, right->span));
    node->token_kind = op.kind;
    node->children.push_back(bminor::move(left));
    node->children.push_back(bminor::move(right));
    return node;
}

class Parser {
public:
    explicit Parser(const vector<Token>& tokens) {
        for (const auto& token : tokens) {
            if (token.kind != Kind::Whitespace && token.kind != Kind::Comment &&
                token.kind != Kind::Invalid) {
                tokens_.push_back(&token);
            }
        }
    }

    ParseResult expression_result() {
        try {
            auto root = expression();
            expect(Kind::EndOfFile);
            return {bminor::move(root), {}};
        } catch (ParseFailure& failure) {
            return {nullptr, {bminor::move(failure.diagnostic)}};
        }
    }

private:
    const Token& current() const { return *tokens_[cursor_]; }
    bool at(Kind kind) const { return current().kind == kind; }

    const Token& take() {
        const auto& token = current();
        if (token.kind != Kind::EndOfFile) {
            ++cursor_;
        }
        return token;
    }

    bool accept(Kind kind) {
        if (!at(kind)) {
            return false;
        }
        take();
        return true;
    }

    [[noreturn]] void fail(string_view expected) const {
        const auto& token = current();
        throw ParseFailure{{Phase::Syntax, "PARSE_EXPECTED", token.filename, token.span,
                            "Expected " + string(expected) + ", found " +
                                string(token_name(token.kind))}};
    }

    const Token& expect(Kind kind) {
        if (!at(kind)) {
            fail(token_name(kind));
        }
        return take();
    }

    NodePtr expression() { return assignment(); }

    NodePtr assignment() {
        auto left = logical_or();
        if (!at(Kind::Assign)) {
            return left;
        }
        const auto& op = take();
        return binary(NodeKind::Assignment, op, bminor::move(left), assignment());
    }

    using ExpressionLevel = NodePtr (Parser::*)();

    NodePtr fold_left(ExpressionLevel operand, initializer_list<Kind> operators) {
        auto left = (this->*operand)();
        while (find(operators.begin(), operators.end(), current().kind) != operators.end()) {
            const auto& op = take();
            auto right = (this->*operand)();
            left = binary(NodeKind::BinaryExpr, op, bminor::move(left), bminor::move(right));
        }
        return left;
    }

    NodePtr logical_or() { return fold_left(&Parser::logical_and, {Kind::Or}); }
    NodePtr logical_and() { return fold_left(&Parser::comparison, {Kind::And}); }

    NodePtr comparison() {
        return fold_left(&Parser::additive, {Kind::Less, Kind::LessEqual, Kind::Greater,
                                           Kind::GreaterEqual, Kind::Equal, Kind::NotEqual});
    }

    NodePtr additive() {
        return fold_left(&Parser::multiply, {Kind::Plus, Kind::Minus});
    }

    NodePtr multiply() {
        return fold_left(&Parser::power, {Kind::Star, Kind::Slash, Kind::Percent});
    }

    NodePtr power() {
        auto left = prefix();
        if (!at(Kind::Caret)) {
            return left;
        }
        const auto& op = take();
        return binary(NodeKind::BinaryExpr, op, bminor::move(left), power());
    }

    NodePtr prefix() {
        if (!at(Kind::Minus) && !at(Kind::Bang)) {
            return postfix();
        }
        const auto& op = take();
        auto operand = prefix();
        auto node = make_unique<Node>(NodeKind::PrefixExpr, through(op.span, operand->span));
        node->token_kind = op.kind;
        node->children.push_back(bminor::move(operand));
        return node;
    }

    NodePtr postfix() {
        auto value = primary();
        while (accept(Kind::LeftBracket)) {
            auto index = expression();
            const auto& close = expect(Kind::RightBracket);
            auto node = make_unique<Node>(NodeKind::IndexExpr, through(value->span, close.span));
            node->children.push_back(bminor::move(value));
            node->children.push_back(bminor::move(index));
            value = bminor::move(node);
        }
        if (at(Kind::Increment) || at(Kind::Decrement)) {
            const auto& op = take();
            auto node = make_unique<Node>(NodeKind::PostfixExpr, through(value->span, op.span));
            node->token_kind = op.kind;
            node->children.push_back(bminor::move(value));
            value = bminor::move(node);
        }
        return value;
    }

    void arguments(Node& node) {
        do {
            node.children.push_back(expression());
        } while (accept(Kind::Comma));
    }

    NodePtr identifier_or_call() {
        auto name = leaf(NodeKind::Identifier, expect(Kind::Identifier));
        if (!accept(Kind::LeftParen)) {
            return name;
        }
        auto call = make_unique<Node>(NodeKind::CallExpr, name->span);
        call->children.push_back(bminor::move(name));
        if (!at(Kind::RightParen)) {
            arguments(*call);
        }
        call->span = through(call->span, expect(Kind::RightParen).span);
        return call;
    }

    NodePtr primary() {
        switch (current().kind) {
        case Kind::Identifier:
            return identifier_or_call();
        case Kind::IntegerLiteral:
        case Kind::BooleanLiteral:
        case Kind::StringLiteral:
        case Kind::CharLiteral:
            return leaf(NodeKind::Literal, take());
        case Kind::LeftParen: {
            const auto& open = take();
            auto value = expression();
            const auto& close = expect(Kind::RightParen);
            auto node = make_unique<Node>(NodeKind::GroupExpr, through(open.span, close.span));
            node->children.push_back(bminor::move(value));
            return node;
        }
        default:
            fail("an expression");
        }
    }

    vector<const Token*> tokens_;
    size_t cursor_ = 0;
};

}

ParseResult parse_expression(const vector<Token>& tokens) {
    return Parser(tokens).expression_result();
}

}
