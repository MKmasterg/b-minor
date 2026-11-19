#include "bminor/parser.hpp"
#include "bminor/render.hpp"

#include <cstdlib>
#include <iostream>
#include <regex>
#include <string_view>

namespace {

using std::cerr;
using std::exit;
using std::regex;
using std::regex_replace;
using std::string;
using std::string_view;
using std::vector;

void expect(bool condition, string_view description) {
    if (!condition) {
        cerr << description << '\n';
        exit(1);
    }
}

}

int main() {
    constexpr string_view source =
        "x: function void () = {\r\n\t// note\r\n\r\n\tprint ; x();\r\n}";
    auto lexical = bminor::lex(source, "ansi.bminor");
    const auto syntax = bminor::parse(lexical.tokens);
    const auto spans = bminor::classify(lexical.tokens, *syntax.root, syntax.diagnostics);
    const string output = bminor::render_ansi(source, spans);

    expect(output.find("\x1b[1;34mfunction\x1b[0m") != string::npos,
           "Keyword must be bold blue and reset");
    expect(output.find("\x1b[93mx\x1b[0m:") != string::npos &&
               output.find("\x1b[93mx\x1b[0m();") != string::npos,
           "Function name and recovered callee must be gold and reset");
    expect(output.find("\x1b[2;3;90m// note\x1b[0m\r\n") != string::npos,
           "Comment must be dim gray, italic and reset");
    expect(output.find("\x1b[4;31m;\x1b[0m") != string::npos,
           "Syntax error on delimiter must be red, underlined and reset");

    const regex sgr("\x1b\\[[0-9;]*m");
    expect(regex_replace(output, sgr, "") == source,
           "Removing emitted SGR must reproduce the original source");
    expect(bminor::render_ansi("", {}).empty(), "Empty source must produce no ANSI bytes");

    const vector<bminor::HighlightSpan> marked_function = {
        {{0, 1, {}}, bminor::HighlightCategory::Function, true}
    };
    expect(bminor::render_ansi("x", marked_function) == "\x1b[4;31mx\x1b[0m",
           "Error styling must override the category");
}
