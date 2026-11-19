#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    using std::cerr;
    using std::cout;
    using std::string_view;

    constexpr string_view usage = "Usage: bminor-highlight --format ansi|html INPUT\n";
    if (argc == 2 && string_view(argv[1]) == "--help") {
        cout << usage;
        return 0;
    }
    cerr << usage;
    return 2;
}
