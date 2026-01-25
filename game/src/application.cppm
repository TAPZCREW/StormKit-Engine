module;

#include <cstdlib>

export module application;

import std;

import stormkit.core;
import stormkit.log;
import stormkit.wsi;

using namespace stormkit;

export {
    class Application {
      public:
        ////////////////////////////////////////
        ////////////////////////////////////////
        auto run(std::span<const std::string_view> args) -> i32 {
            wsi::parse_args(args);

            auto logger_singleton = log::Logger::create_logger_instance<log::ConsoleLogger>();

            return EXIT_SUCCESS;
        }

      protected:
        DeferInit<wsi::Window> m_window;
    };
}
