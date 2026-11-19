#include "bminor/render.hpp"

namespace bminor {

using std::size_t;
using std::string;
using std::string_view;
using std::vector;
namespace {

constexpr string_view page_start = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>B-Minor source</title>
<style>
body { margin: 0; background: #111827; color: #e2e8f0; }
pre { margin: 0; padding: 1.5rem; overflow: auto; tab-size: 4; }
code { font: 1rem/1.55 ui-monospace, monospace; }
.kw { color: #93c5fd; font-weight: bold; }
.type { color: #67e8f9; }
.var { color: inherit; }
.fn { color: #facc15; }
.int { color: #fdba74; }
.bool { color: #fdba74; }
.str { color: #86efac; }
.op { color: #cbd5e1; }
.comment { color: #94a3b8; font-style: italic; }
.err { text-decoration: underline solid #f87171; text-underline-offset: 0.16em; }
</style>
</head>
<body>
<pre><code>)html";

constexpr string_view page_end = "</code></pre>\n</body>\n</html>\n";
constexpr string_view replacement = u8"\uFFFD";

string_view category_class(HighlightCategory category) {
    switch (category) {
    case HighlightCategory::Plain: return {};
    case HighlightCategory::Keyword: return "kw";
    case HighlightCategory::Type: return "type";
    case HighlightCategory::Variable: return "var";
    case HighlightCategory::Function: return "fn";
    case HighlightCategory::Integer: return "int";
    case HighlightCategory::Boolean: return "bool";
    case HighlightCategory::String: return "str";
    case HighlightCategory::Operator: return "op";
    case HighlightCategory::Comment: return "comment";
    }
    return {};
}

void open_span(string& output, const HighlightSpan& span) {
    const auto category = category_class(span.category);
    output.append("<span class=\"");
    output.append(category);
    if (span.error) {
        if (!category.empty()) {
            output.push_back(' ');
        }
        output.append("err");
    }
    output.append("\">");
}

size_t utf8_width(string_view rest) {
    const auto lead = static_cast<unsigned char>(rest.front());
    size_t width;
    if (lead >= 0xc2 && lead <= 0xdf) {
        width = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        width = 3;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        width = 4;
    } else {
        return 0;
    }
    if (rest.size() < width) {
        return 0;
    }
    for (size_t i = 1; i < width; ++i) {
        const auto byte = static_cast<unsigned char>(rest[i]);
        if (byte < 0x80 || byte > 0xbf) {
            return 0;
        }
    }

    // Reject overlong encodings, surrogates and values above U+10FFFF.
    const auto second = static_cast<unsigned char>(rest[1]);
    if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second > 0x9f) ||
        (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) {
        return 0;
    }
    return width;
}

size_t append_source_unit(string& output, string_view rest) {
    const char byte = rest.front();
    if (static_cast<unsigned char>(byte) >= 0x80) {
        const auto width = utf8_width(rest);
        if (width == 0) {
            output.append(replacement);
            return 1;
        }
        output.append(rest.substr(0, width));
        return width;
    }

    switch (byte) {
    case '\r':
        output.push_back('\n');
        return rest.size() > 1 && rest[1] == '\n' ? 2 : 1;
    case '\0': output.append(replacement); break;
    case '&': output.append("&amp;"); break;
    case '<': output.append("&lt;"); break;
    case '>': output.append("&gt;"); break;
    case '"': output.append("&quot;"); break;
    case '\'': output.append("&#39;"); break;
    default: output.push_back(byte); break;
    }
    return 1;
}

}

string render_html(string_view source, const vector<HighlightSpan>& spans) {
    string output(page_start);
    const HighlightSpan* active = nullptr;
    size_t next_span = 0;
    size_t offset = 0;
    while (offset < source.size()) {
        while (next_span < spans.size() && spans[next_span].span.end <= offset) {
            ++next_span;
        }
        const HighlightSpan* style = nullptr;
        if (next_span < spans.size() && spans[next_span].span.begin <= offset) {
            const auto& span = spans[next_span];
            if (span.category != HighlightCategory::Plain || span.error) {
                style = &span;
            }
        }

        if (style != active) {
            if (active) {
                output.append("</span>");
            }
            if (style) {
                open_span(output, *style);
            }
            active = style;
        }
        // Decode across span boundaries and retain the style at this byte.
        offset += append_source_unit(output, source.substr(offset));
    }
    if (active) {
        output.append("</span>");
    }
    output.append(page_end);
    return output;
}

}
