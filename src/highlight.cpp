#include "bminor/highlight.hpp"

#include <algorithm>
#include <unordered_map>

namespace bminor {

using std::remove_if;
using std::size_t;
using std::unordered_map;
using std::vector;
namespace {

HighlightCategory token_category(TokenKind kind) {
    switch (kind) {
    case TokenKind::ArrayKeyword:
    case TokenKind::BooleanKeyword:
    case TokenKind::CharKeyword:
    case TokenKind::IntegerKeyword:
    case TokenKind::StringKeyword:
    case TokenKind::VoidKeyword:
        return HighlightCategory::Type;
    case TokenKind::ElseKeyword:
    case TokenKind::ForKeyword:
    case TokenKind::FunctionKeyword:
    case TokenKind::IfKeyword:
    case TokenKind::PrintKeyword:
    case TokenKind::ReturnKeyword:
    case TokenKind::WhileKeyword:
        return HighlightCategory::Keyword;
    case TokenKind::Identifier: return HighlightCategory::Variable;
    case TokenKind::IntegerLiteral: return HighlightCategory::Integer;
    case TokenKind::BooleanLiteral: return HighlightCategory::Boolean;
    case TokenKind::StringLiteral:
    case TokenKind::CharLiteral:
        return HighlightCategory::String;
    case TokenKind::Comment: return HighlightCategory::Comment;
    case TokenKind::Increment:
    case TokenKind::Decrement:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
    case TokenKind::Equal:
    case TokenKind::NotEqual:
    case TokenKind::And:
    case TokenKind::Or:
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
    case TokenKind::Caret:
    case TokenKind::Bang:
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::Assign:
        return HighlightCategory::Operator;
    case TokenKind::EndOfFile:
    case TokenKind::Invalid:
    case TokenKind::Whitespace:
    case TokenKind::LeftParen:
    case TokenKind::RightParen:
    case TokenKind::LeftBracket:
    case TokenKind::RightBracket:
    case TokenKind::LeftBrace:
    case TokenKind::RightBrace:
    case TokenKind::Comma:
    case TokenKind::Colon:
    case TokenKind::Semicolon:
        return HighlightCategory::Plain;
    }
    return HighlightCategory::Plain;
}

}

vector<HighlightSpan> classify(const vector<Token>& tokens, const Node& root,
                                    const vector<Diagnostic>& diagnostics) {
    vector<HighlightSpan> spans;
    unordered_map<size_t, size_t> index_by_offset;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Whitespace || token.span.begin == token.span.end) {
            continue;
        }
        index_by_offset.emplace(token.span.begin, spans.size());
        spans.push_back({token.span, token_category(token.kind), token.kind == TokenKind::Invalid});
    }

    vector<const Node*> pending{&root};
    while (!pending.empty()) {
        const Node& node = *pending.back();
        pending.pop_back();
        if (node.kind == NodeKind::FuncDecl || node.kind == NodeKind::CallExpr) {
            const auto name = index_by_offset.find(node.children.front()->span.begin);
            if (name != index_by_offset.end()) {
                spans[name->second].category = HighlightCategory::Function;
            }
        }
        for (const auto& child : node.children) {
            if (child) {
                pending.push_back(child.get());
            }
        }
    }

    for (const Diagnostic& diagnostic : diagnostics) {
        const auto found = index_by_offset.find(diagnostic.span.begin);
        if (found != index_by_offset.end()) {
            spans[found->second].error = true;
        }
    }

    spans.erase(remove_if(spans.begin(), spans.end(), [](const HighlightSpan& span) {
        return span.category == HighlightCategory::Plain && !span.error;
    }), spans.end());
    return spans;
}

}
