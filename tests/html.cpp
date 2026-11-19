#include "bminor/render.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using std::array;
using std::cerr;
using std::exit;
using std::size_t;
using std::string;
using std::string_view;
using std::vector;

using bminor::HighlightCategory;
using bminor::HighlightSpan;

void check(bool condition, string_view message) {
    if (!condition) {
        cerr << "HTML renderer: " << message << '\n';
        exit(1);
    }
}

size_t count(string_view text, string_view match) {
    size_t total = 0;
    auto position = text.find(match);
    while (position != string_view::npos) {
        ++total;
        position = text.find(match, position + match.size());
    }
    return total;
}

string_view source_region(string_view document) {
    constexpr string_view open = "<pre><code>";
    constexpr string_view close = "</code></pre>";
    const auto begin = document.find(open);
    const auto end = document.find(close);
    check(begin != string_view::npos && end != string_view::npos &&
              begin + open.size() <= end &&
              count(document, "<pre") == 1 && count(document, "<code") == 1 &&
              count(document, "</pre>") == 1 && count(document, "</code>") == 1,
          "expected one pre/code region");
    check(document.find("<script") == string_view::npos, "unexpected script element");
    return document.substr(begin + open.size(), end - begin - open.size());
}

char decode_entity(string_view entity) {
    if (entity == "amp") { return '&'; }
    if (entity == "lt") { return '<'; }
    if (entity == "gt") { return '>'; }
    if (entity == "quot") { return '"'; }
    if (entity == "#39" || entity == "apos") { return '\''; }
    check(false, "unrecognized entity in source text");
    return '\0';
}

string read_text(string_view document) {
    const auto region = source_region(document);
    string decoded;
    bool in_span = false;
    for (size_t position = 0; position < region.size();) {
        const char byte = region[position];
        if (byte == '<') {
            const auto end = region.find('>', position + 1);
            check(end != string_view::npos, "unterminated tag in source region");
            const auto tag = region.substr(position + 1, end - position - 1);
            if (in_span) {
                check(tag == "/span", "expected closing span");
            } else {
                check(tag.substr(0, 12) == "span class=\"" && tag.back() == '"',
                      "unexpected tag inside source region");
            }
            in_span = !in_span;
            position = end + 1;
        } else if (byte == '&') {
            const auto end = region.find(';', position + 1);
            check(end != string_view::npos, "unterminated entity in source region");
            decoded.push_back(decode_entity(region.substr(position + 1, end - position - 1)));
            position = end + 1;
        } else {
            check(byte != '>' && byte != '"' && byte != '\'', "unescaped source HTML character");
            decoded.push_back(byte);
            ++position;
        }
    }
    check(!in_span, "unclosed span");
    return decoded;
}

void check_escaping_and_newlines() {
    const string comment = u8"// café: \"<script>alert('x') & </script>\"";
    const string source = "\n\t" + comment + "\r\n\r\"'<>&\ttail";
    const vector<HighlightSpan> spans = {{{2, 2 + comment.size(), {}}, HighlightCategory::Comment}};
    const auto html = bminor::render_html(source, spans);
    check(html.find("<!doctype html>") == 0 &&
              html.find("<meta charset=\"utf-8\">") != string::npos,
          "missing HTML doctype or UTF-8 metadata");
    check(read_text(html) == "\n\t" + comment + "\n\n\"'<>&\ttail",
          "text changed after entity decoding and newline normalization");
    check(source_region(html).find("&lt;script&gt;") != string_view::npos,
          "angle brackets in comment were not escaped");
    check(source_region(html).front() == '\n', "initial LF missing from source region");
    check(read_text(bminor::render_html("", {})).empty(), "empty input produced source text");

    const auto styled_crlf = bminor::render_html("\r\n", {
        {{0, 1, {}}, HighlightCategory::Comment},
        {{1, 2, {}}, HighlightCategory::Type},
    });
    check(source_region(styled_crlf) == "<span class=\"comment\">\n</span>",
          "CRLF did not keep the style of its first byte");
}

void check_classes() {
    constexpr array categories = {
        HighlightCategory::Keyword, HighlightCategory::Type, HighlightCategory::Variable,
        HighlightCategory::Function, HighlightCategory::Integer, HighlightCategory::Boolean,
        HighlightCategory::String, HighlightCategory::Operator, HighlightCategory::Comment,
        HighlightCategory::Plain,
    };
    constexpr array<string_view, 10> classes = {
        "kw", "type", "var", "fn", "int", "bool", "str", "op", "comment", "err",
    };
    constexpr string_view source = "abcdefghijxy";
    vector<HighlightSpan> spans;
    for (size_t i = 0; i < categories.size(); ++i) {
        spans.push_back({{i, i + 1, {}}, categories[i], categories[i] == HighlightCategory::Plain});
    }
    spans.push_back({{10, 11, {}}, HighlightCategory::Function, true});
    spans.push_back({{11, 12, {}}, HighlightCategory::Plain});

    const auto html = bminor::render_html(source, spans);
    check(read_text(html) == source, "styling changed source text");
    const auto style_begin = html.find("<style>");
    const auto style_end = html.find("</style>");
    check(style_begin != string::npos && style_end > style_begin &&
              style_end != string::npos, "missing embedded stylesheet");
    const auto stylesheet = html.substr(style_begin, style_end - style_begin);
    const auto region = source_region(html);
    for (size_t i = 0; i < classes.size(); ++i) {
        const string name(classes[i]);
        check(stylesheet.find("." + name + " {") != string::npos,
              "missing stylesheet class " + name);
        check(region.find("<span class=\"" + name + "\">" + source[i] + "</span>") !=
                  string_view::npos, "missing source class " + name);
    }
    check(region.find("<span class=\"fn err\">x</span>y") != string_view::npos,
          "error class did not combine with its category or plain text gained styling");
}

void check_utf8_across_token_boundaries() {
    const string source = u8"// café\né€🧪";
    const auto lexed = bminor::lex(source, "utf8.bminor");
    check(lexed.diagnostics.size() == 9, "unquoted UTF-8 did not split into invalid bytes");
    vector<HighlightSpan> spans;
    for (const auto& token : lexed.tokens) {
        if (token.kind == bminor::TokenKind::Comment) {
            spans.push_back({token.span, HighlightCategory::Comment});
        } else if (token.kind == bminor::TokenKind::Invalid) {
            spans.push_back({token.span, HighlightCategory::Plain, true});
        }
    }
    const auto html = bminor::render_html(source, spans);
    check(read_text(html) == source, "well-formed UTF-8 changed across lexer token boundaries");
    check(source_region(html).find(u8"<span class=\"err\">🧪</span>") != string_view::npos,
          "UTF-8 code point was split by markup");

    const auto styled = bminor::render_html(u8"éé", {
        {{0, 1, {}}, HighlightCategory::Function},
        {{1, 2, {}}, HighlightCategory::Type},
        {{3, 4, {}}, HighlightCategory::Plain, true},
    });
    check(source_region(styled) == u8"<span class=\"fn\">é</span>é",
          "code point did not keep the style at its first byte");
}

void check_invalid_utf8() {
    struct InvalidBytes {
        string_view bytes;
        size_t replacements;
        string_view suffix;
    };
    const array malformed = {
        InvalidBytes{string_view("\0", 1), 1, ""},
        InvalidBytes{"\x80", 1, ""},
        InvalidBytes{"\xc0\xaf", 2, ""},
        InvalidBytes{"\xe0\x80\x80", 3, ""},
        InvalidBytes{"\xed\xa0\x80", 3, ""},
        InvalidBytes{"\xf0\x80\x80\x80", 4, ""},
        InvalidBytes{"\xf4\x90\x80\x80", 4, ""},
        InvalidBytes{"\xf5\x80\x80\x80", 4, ""},
        InvalidBytes{"\xe2\x82" "Z", 2, "Z"},
        InvalidBytes{"\xf0\x9f", 2, ""},
    };
    for (const auto& fixture : malformed) {
        string expected;
        for (size_t i = 0; i < fixture.replacements; ++i) {
            expected += u8"\uFFFD";
        }
        expected += fixture.suffix;
        check(read_text(bminor::render_html(fixture.bytes, {})) == expected,
              "expected one U+FFFD per invalid byte or NUL");
    }
    constexpr string_view valid_edges =
        "\xc2\x80" "\xdf\xbf" "\xe0\xa0\x80" "\xed\x9f\xbf" "\xee\x80\x80"
        "\xf0\x90\x80\x80" "\xf4\x8f\xbf\xbf";
    check(read_text(bminor::render_html(valid_edges, {})) == valid_edges,
          "valid UTF-8 at encoding boundaries was rejected");
}

}

int main() {
    check_escaping_and_newlines();
    check_classes();
    check_utf8_across_token_boundaries();
    check_invalid_utf8();
}
