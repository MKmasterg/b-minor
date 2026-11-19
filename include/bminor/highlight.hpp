#pragma once

#include "bminor/ast.hpp"
#include "bminor/lexer.hpp"

#include <vector>

namespace bminor {

using std::vector;

enum class HighlightCategory {
    Plain, Keyword, Type, Variable, Function, Integer, Boolean, String, Operator, Comment
};

struct HighlightSpan {
    Span span;
    HighlightCategory category;
    bool error = false;
};

vector<HighlightSpan> classify(const vector<Token>& tokens, const Node& root,
                                    const vector<Diagnostic>& diagnostics);

}
