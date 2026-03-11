#include <cstdlib>

import std;

import stormkit;
import stormkit.engine;

#include <stormkit/core/try_expected.hpp>
#include <stormkit/main/main_macro.hpp>

using namespace stormkit;
using namespace stormkit::literals;

namespace stdfs = std::filesystem;

#ifndef LUA_DIR
static constexpr auto LUA_DIR = "../share/lua";
#endif

////////////////////////////////////////
////////////////////////////////////////
auto main(std::span<const std::string_view> args) -> int {
    wsi::parse_args(args);
    log::parse_args(args);

    auto logger_singleton = log::Logger::create_logger_instance<log::ConsoleLogger>();

    auto application = TryAssert(engine::Application::create("Game", LUA_DIR, { 800, 600 }, "Game"), "Failed to initialize Game");
    auto render_pipeline = TryAssert(engine::Pipeline2D::create(application, application.window().extent().to<f32>()),
                                     "Failed to create 2D render pipeline");
    application.set_frame_builder(bind_front(&engine::Pipeline2D::update_framegraph, &render_pipeline, as_ref(application)));
    render_pipeline.init_ecs(application);

    application.run();

    return EXIT_SUCCESS;
}
