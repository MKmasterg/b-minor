#pragma once

#include "bminor/highlight.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace bminor {

using std::string;
using std::string_view;
using std::vector;

string render_ansi(string_view source, const vector<HighlightSpan>& spans);

}
