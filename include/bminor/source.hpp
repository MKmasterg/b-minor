#pragma once

#include <cstddef>
#include <string>

namespace bminor {

using std::size_t;
using std::string;

struct Location {
    size_t line = 1;
    size_t column = 1;
};

struct Span {
    size_t begin = 0;
    size_t end = 0;
    Location start;
};

class SourceCursor {
public:
    Span point() const;
    void advance(char byte);

private:
    size_t offset_ = 0;
    Location location_;
    bool after_cr_ = false;
};

enum class Phase { Lexical, Syntax };

struct Diagnostic {
    Phase phase;
    string code;
    string filename;
    Span span;
    string message;
};

}
