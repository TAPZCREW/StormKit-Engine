import std;

import stormkit.core;
import stormkit.log;

#include <stormkit/log/log_macro.hpp>
#include <stormkit/main/main_macro.hpp>

LOGGER("Events");

using namespace stormkit;
using namespace std::literals;

namespace stdr = std::ranges;

////////////////////////////////////////
////////////////////////////////////////
auto main(std::span<const std::string_view> args) -> int {
    auto logger = log::Logger::create_logger_instance<log::ConsoleLogger>();
    ilog("Hello world");

    return 0;
}
