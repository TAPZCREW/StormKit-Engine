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

namespace {
    const auto BOOT_LUA = stdfs::path { LUA_DIR } / "boot.lua";
} // namespace

////////////////////////////////////////
////////////////////////////////////////
auto main(std::span<const std::string_view> args) -> int {
    wsi::parse_args(args);
    log::parse_args(args);

    auto logger_singleton = log::Logger::create_logger_instance<log::ConsoleLogger>();

    auto application     = TryAssert(engine::Application::create("Game", { 800, 600 }, "Game"), "Failed to initialize Game");
    auto render_pipeline = TryAssert(engine::BidimPipeline::create(application, application.window().extent().to<f32>()),
                                     "Failed to create 2D render pipeline");
    application.set_frame_builder(bind_front(&engine::BidimPipeline::update_framegraph,
                                             &render_pipeline,
                                             std::ref(application.renderer())));
    render_pipeline.init_ecs(application);

    application.run(BOOT_LUA);

    return EXIT_SUCCESS;
}
