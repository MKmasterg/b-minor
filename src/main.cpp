#include "bminor/lexer.hpp"
#include "bminor/parser.hpp"
#include "bminor/render.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using std::cerr;
using std::cout;
using std::flush;
using std::ifstream;
using std::ios;
using std::make_move_iterator;
using std::nullopt;
using std::optional;
using std::size_t;
using std::stable_sort;
using std::string;
using std::string_view;
using std::vector;

constexpr string_view usage = "Usage: bminor-highlight --format ansi|html INPUT\n";

optional<string> read_source(const char* filename) {
    ifstream input(filename, ios::binary);
    if (!input) {
        return nullopt;
    }

    string source;
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof buffer);
        source.append(buffer, static_cast<size_t>(input.gcount()));
    }
    if (input.bad() || !input.eof()) {
        return nullopt;
    }
    return source;
}

void print_diagnostics(const vector<bminor::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        const auto location = diagnostic.span.start;
        cerr << diagnostic.filename << ':' << location.line << ':' << location.column
                  << ": error [" << diagnostic.code << "]: " << diagnostic.message << '\n';
    }
}

}

int main(int argc, char** argv) {
    if (argc == 2 && string_view(argv[1]) == "--help") {
        cout << usage << flush;
        if (!cout) {
            cerr << "Could not write to stdout\n";
            return 2;
        }
        return 0;
    }
    if (argc != 4 || string_view(argv[1]) != "--format") {
        cerr << usage;
        return 2;
    }

    const string_view format = argv[2];
    if (format != "ansi" && format != "html") {
        cerr << "Unknown format '" << format << "'\n" << usage;
        return 2;
    }

    const char* filename = argv[3];
    const auto source = read_source(filename);
    if (!source) {
        cerr << "Could not read input '" << filename << "'\n";
        return 2;
    }

    auto lexical = bminor::lex(*source, filename);
    auto syntax = bminor::parse(lexical.tokens);
    auto& diagnostics = lexical.diagnostics;
    diagnostics.insert(diagnostics.end(), make_move_iterator(syntax.diagnostics.begin()),
                       make_move_iterator(syntax.diagnostics.end()));
    stable_sort(diagnostics.begin(), diagnostics.end(), [](const auto& left, const auto& right) {
        return left.span.begin < right.span.begin;
    });

    const auto highlights = bminor::classify(lexical.tokens, *syntax.root, diagnostics);
    print_diagnostics(diagnostics);
    cout << (format == "ansi" ? bminor::render_ansi(*source, highlights)
                                    : bminor::render_html(*source, highlights))
              << flush;
    if (!cout) {
        cerr << "Could not write to stdout\n";
        return 2;
    }
    if (!cerr) {
        return 2;
    }
    return diagnostics.empty() ? 0 : 1;
}
