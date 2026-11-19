#include "bminor/source.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using std::cerr;
using std::exit;
using std::size_t;
using std::string_view;

void expect_point(const char* name, bminor::Span point, size_t offset,
                  size_t line, size_t column) {
    if (point.begin == offset && point.end == offset &&
        point.start.line == line && point.start.column == column) {
        return;
    }
    cerr << name << " expected byte " << offset << " at " << line << ':' << column
              << ", got [" << point.begin << ',' << point.end << ") at "
              << point.start.line << ':' << point.start.column << '\n';
    exit(1);
}

}

int main() {
    constexpr string_view source = "a\r\n\tb\rc\nd";
    bminor::SourceCursor cursor;
    expect_point("empty input", cursor.point(), 0, 1, 1);

    for (const char byte : source) {
        switch (byte) {
        case 'b': expect_point("CRLF and tab", cursor.point(), 4, 2, 2); break;
        case 'c': expect_point("lone CR", cursor.point(), 6, 3, 1); break;
        case 'd': expect_point("lone LF", cursor.point(), 8, 4, 1); break;
        }
        cursor.advance(byte);
    }
    expect_point("end of input", cursor.point(), 9, 4, 2);
}
