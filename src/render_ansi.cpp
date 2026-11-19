#include "bminor/render.hpp"

namespace bminor {

using std::size_t;
using std::string;
using std::string_view;
using std::vector;
namespace {

string_view sgr(const HighlightSpan& span) {
    if (span.error) {
        return "\x1b[4;31m";
    }
    switch (span.category) {
    case HighlightCategory::Keyword: return "\x1b[1;34m";
    case HighlightCategory::Type: return "\x1b[36m";
    case HighlightCategory::Variable: return "\x1b[39m";
    case HighlightCategory::Function: return "\x1b[93m";
    case HighlightCategory::Integer:
    case HighlightCategory::Boolean:
        return "\x1b[38;5;208m";
    case HighlightCategory::String: return "\x1b[32m";
    case HighlightCategory::Operator: return "\x1b[37m";
    case HighlightCategory::Comment: return "\x1b[2;3;90m";
    case HighlightCategory::Plain: return {};
    }
    return {};
}

}

string render_ansi(string_view source, const vector<HighlightSpan>& spans) {
    string output;
    size_t offset = 0;
    for (const HighlightSpan& span : spans) {
        output.append(source.substr(offset, span.span.begin - offset));
        const auto style = sgr(span);
        output.append(style);
        output.append(source.substr(span.span.begin, span.span.end - span.span.begin));
        if (!style.empty()) {
            output.append("\x1b[0m");
        }
        offset = span.span.end;
    }
    output.append(source.substr(offset));
    return output;
}

}
