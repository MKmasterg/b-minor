#include "bminor/parser.hpp"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace bminor {

using std::exchange;
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

    ParseResult program() {
        auto root = make_unique<Node>(NodeKind::Program, Span{});
        root->span.end = tokens_.back()->span.end;
        while (!at(Kind::EndOfFile)) {
            const auto entry = cursor_;
            try {
                root->children.push_back(global_declaration());
            } catch (ParseFailure& failure) {
                diagnostics_.push_back(bminor::move(failure.diagnostic));
                recover(entry, false);
            }
        }
        return {bminor::move(root), bminor::move(diagnostics_)};
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

    Diagnostic expected(string_view description) const {
        const auto& token = current();
        return {Phase::Syntax, "PARSE_EXPECTED", token.filename, token.span,
                "Expected " + string(description) + ", found " +
                    string(token_name(token.kind))};
    }

    [[noreturn]] void fail(string_view description) const {
        throw ParseFailure{expected(description)};
    }

    const Token& expect(Kind kind) {
        if (!at(kind)) {
            fail(token_name(kind));
        }
        return take();
    }

    bool declaration_start() const {
        return at(Kind::Identifier) && tokens_[cursor_ + 1]->kind == Kind::Colon;
    }

    bool statement_restart() const {
        switch (current().kind) {
        case Kind::IfKeyword:
        case Kind::ForKeyword:
        case Kind::PrintKeyword:
        case Kind::ReturnKeyword:
        case Kind::LeftBrace:
            return true;
        default:
            return false;
        }
    }

    void recover(size_t entry, bool in_block) {
        auto depth = exchange(initializer_depth_, 0);
        while (!at(Kind::EndOfFile)) {
            if (depth == 0) {
                if (accept(Kind::Semicolon)) {
                    return;
                }
                if (at(Kind::RightBrace)) {
                    // The enclosing block still owns its closing brace.
                    if (!in_block) {
                        take();
                    }
                    return;
                }
                if (cursor_ > entry && (declaration_start() || (in_block && statement_restart()))) {
                    return;
                }
            }
            if (at(Kind::LeftBrace)) {
                ++depth;
            } else if (at(Kind::RightBrace)) {
                --depth;
            }
            take();
        }
    }

    NodePtr global_declaration() {
        const auto& name = expect(Kind::Identifier);
        expect(Kind::Colon);
        if (accept(Kind::FunctionKeyword)) {
            return function_tail(name);
        }
        return variable_tail(name);
    }

    NodePtr function_tail(const Token& name) {
        auto node = make_unique<Node>(NodeKind::FuncDecl, name.span);
        node->children.push_back(leaf(NodeKind::Identifier, name));
        if (at(Kind::VoidKeyword)) {
            node->children.push_back(leaf(NodeKind::AtomType, take()));
        } else {
            node->children.push_back(atom_type());
        }
        expect(Kind::LeftParen);
        if (!at(Kind::RightParen)) {
            do {
                node->children.push_back(parameter());
            } while (accept(Kind::Comma));
        }
        expect(Kind::RightParen);
        if (at(Kind::Semicolon)) {
            node->span.end = take().span.end;
            node->children.push_back(nullptr);
        } else {
            expect(Kind::Assign);
            auto body = block();
            node->span.end = body->span.end;
            node->children.push_back(bminor::move(body));
        }
        return node;
    }

    NodePtr parameter() {
        const auto& name = expect(Kind::Identifier);
        expect(Kind::Colon);
        auto type = compound_type(true);
        auto node = make_unique<Node>(NodeKind::Param, through(name.span, type->span));
        node->children.push_back(leaf(NodeKind::Identifier, name));
        node->children.push_back(bminor::move(type));
        return node;
    }

    NodePtr atom_type() {
        switch (current().kind) {
        case Kind::IntegerKeyword:
        case Kind::BooleanKeyword:
        case Kind::CharKeyword:
        case Kind::StringKeyword:
            return leaf(NodeKind::AtomType, take());
        default:
            fail("integer, boolean, char or string");
        }
    }

    NodePtr compound_type(bool allow_unsized_array) {
        if (!at(Kind::ArrayKeyword)) {
            return atom_type();
        }
        const auto& keyword = take();
        expect(Kind::LeftBracket);
        NodePtr bound;
        if (!allow_unsized_array || !at(Kind::RightBracket)) {
            bound = leaf(NodeKind::Literal, expect(Kind::IntegerLiteral));
        }
        expect(Kind::RightBracket);
        auto element = compound_type(allow_unsized_array);
        auto node = make_unique<Node>(NodeKind::ArrayType, through(keyword.span, element->span));
        node->token_kind = keyword.kind;
        node->children.push_back(bminor::move(bound));
        node->children.push_back(bminor::move(element));
        return node;
    }

    NodePtr variable_tail(const Token& name) {
        auto node = make_unique<Node>(NodeKind::VarDecl, name.span);
        node->children.push_back(leaf(NodeKind::Identifier, name));
        node->children.push_back(compound_type(false));
        node->children.push_back(accept(Kind::Assign) ? initializer() : nullptr);
        node->span.end = expect(Kind::Semicolon).span.end;
        return node;
    }

    NodePtr initializer() {
        if (!at(Kind::LeftBrace)) {
            return expression();
        }
        auto node = make_unique<Node>(NodeKind::InitializerList, take().span);
        ++initializer_depth_;
        do {
            node->children.push_back(initializer());
        } while (accept(Kind::Comma));
        node->span.end = expect(Kind::RightBrace).span.end;
        --initializer_depth_;
        return node;
    }

    NodePtr block() {
        auto node = make_unique<Node>(NodeKind::BlockStmt, expect(Kind::LeftBrace).span);
        while (!at(Kind::RightBrace) && !at(Kind::EndOfFile)) {
            const auto entry = cursor_;
            try {
                node->children.push_back(statement());
            } catch (ParseFailure& failure) {
                diagnostics_.push_back(bminor::move(failure.diagnostic));
                recover(entry, true);
            }
        }
        if (at(Kind::EndOfFile)) {
            diagnostics_.push_back(expected(token_name(Kind::RightBrace)));
        }
        node->span.end = take().span.end;
        return node;
    }

    NodePtr statement() {
        switch (current().kind) {
        case Kind::LeftBrace:
            return block();
        case Kind::IfKeyword:
            return if_statement();
        case Kind::ForKeyword:
            return for_statement();
        case Kind::ReturnKeyword:
            return return_statement();
        case Kind::PrintKeyword:
            return print_statement();
        case Kind::Semicolon:
            return make_unique<Node>(NodeKind::EmptyStmt, take().span);
        default:
            if (declaration_start()) {
                const auto& name = take();
                expect(Kind::Colon);
                return variable_tail(name);
            }
            auto value = expression();
            auto node = make_unique<Node>(NodeKind::ExprStmt,
                                                through(value->span, expect(Kind::Semicolon).span));
            node->children.push_back(bminor::move(value));
            return node;
        }
    }

    NodePtr if_statement() {
        auto node = make_unique<Node>(NodeKind::IfStmt, expect(Kind::IfKeyword).span);
        expect(Kind::LeftParen);
        node->children.push_back(expression());
        expect(Kind::RightParen);
        node->children.push_back(statement());
        node->children.push_back(accept(Kind::ElseKeyword) ? statement() : nullptr);
        const auto& last_branch = node->children[2] ? node->children[2] : node->children[1];
        node->span.end = last_branch->span.end;
        return node;
    }

    NodePtr optional_expression(Kind delimiter) {
        return at(delimiter) ? nullptr : expression();
    }

    NodePtr for_statement() {
        auto node = make_unique<Node>(NodeKind::ForStmt, expect(Kind::ForKeyword).span);
        expect(Kind::LeftParen);
        node->children.push_back(optional_expression(Kind::Semicolon));
        expect(Kind::Semicolon);
        node->children.push_back(optional_expression(Kind::Semicolon));
        expect(Kind::Semicolon);
        node->children.push_back(optional_expression(Kind::RightParen));
        expect(Kind::RightParen);
        node->children.push_back(statement());
        node->span.end = node->children.back()->span.end;
        return node;
    }

    NodePtr return_statement() {
        auto node = make_unique<Node>(NodeKind::ReturnStmt, expect(Kind::ReturnKeyword).span);
        node->children.push_back(optional_expression(Kind::Semicolon));
        node->span.end = expect(Kind::Semicolon).span.end;
        return node;
    }

    NodePtr print_statement() {
        auto node = make_unique<Node>(NodeKind::PrintStmt, expect(Kind::PrintKeyword).span);
        arguments(*node);
        node->span.end = expect(Kind::Semicolon).span.end;
        return node;
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
    vector<Diagnostic> diagnostics_;
    size_t cursor_ = 0;
    size_t initializer_depth_ = 0;
};

}

ParseResult parse(const vector<Token>& tokens) {
    return Parser(tokens).program();
}

}
