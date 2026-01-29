#include <cstdlib>

import std;

import stormkit.core;
import stormkit.log;
import stormkit.gpu;
import stormkit.wsi;
import stormkit.engine;

#include <stormkit/core/try_expected.hpp>
#include <stormkit/main/main_macro.hpp>

using namespace stormkit;

using namespace stormkit::literals;

////////////////////////////////////////
////////////////////////////////////////
auto main(std::span<const std::string_view> args) -> int {
    wsi::parse_args(args);

    auto logger_singleton = log::Logger::create_logger_instance<log::ConsoleLogger>();

    auto application = TryAssert(engine::Application::create("Game", "lua/boot.lua", { 800, 600 }, "Game"),
                                 "Failed to initialize Game");
    application.set_frame_builder([&application](auto&& builder) noexcept {
        auto gbuffer = engine::GraphID {};

        builder.add_raster_task(
          "gen_gbuffer",
          [&](auto& task_builder) mutable noexcept {
              gbuffer = task_builder
                          .create_image({ .name        = "gbuffer",
                                          .extent      = application.window().extent(),
                                          .type        = gpu::ImageType::T2D,
                                          .format      = gpu::PixelFormat::RGBA8_UNORM,
                                          .layers      = 1u,
                                          .clear_value = gpu::ClearColor {},
                                          .cull_imune  = true });
          },
          []() {

          });

        auto buffer = engine::GraphID {};
        builder.add_raster_task(
          "render",
          [&](auto& task_builder) mutable noexcept {
              auto backbuffer = task_builder.create_image({ .name        = "backbuffer",
                                                            .extent      = application.window().extent(),
                                                            .type        = gpu::ImageType::T2D,
                                                            .format      = gpu::PixelFormat::RGBA8_UNORM,
                                                            .layers      = 1u,
                                                            .clear_value = gpu::ClearColor {},
                                                            .cull_imune  = true });
              buffer          = task_builder.create_buffer({
                .name = "buffer",
                .size = 8_kb,
              });

              task_builder.read_image(gbuffer);
          },
          []() {

          });
        builder.add_raster_task(
          "useless",
          [&](auto& task_builder) noexcept {
              task_builder.read_image(gbuffer);
              task_builder.read_buffer(buffer);
          },
          []() {

          });
    });
    application.run();

    return EXIT_SUCCESS;
}
