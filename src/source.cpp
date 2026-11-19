#include "bminor/source.hpp"

namespace bminor {

Span SourceCursor::point() const {
    return {offset_, offset_, location_};
}

void SourceCursor::advance(char byte) {
    ++offset_;
    if (byte == '\r' || (byte == '\n' && !after_cr_)) {
        ++location_.line;
        location_.column = 1;
    } else if (byte != '\n') {
        ++location_.column;
    }
    after_cr_ = byte == '\r';
}

}
