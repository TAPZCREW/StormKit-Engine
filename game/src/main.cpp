import std;

import application;

#include <stormkit/main/main_macro.hpp>

////////////////////////////////////////
////////////////////////////////////////
auto main(std::span<const std::string_view> args) -> int {
    return Application {}.run(args);
}
