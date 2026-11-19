#pragma once

#include "bminor/ast.hpp"

namespace bminor {

using std::unique_ptr;
using std::vector;

struct ParseResult {
    unique_ptr<Node> root;
    vector<Diagnostic> diagnostics;
};

ParseResult parse(const vector<Token>& tokens);

}
