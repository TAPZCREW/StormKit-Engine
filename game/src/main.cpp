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

namespace stdfs = std::filesystem;

#ifndef SHADER_DIR
    #define SHADER_DIR "../share/shaders/"
#endif

#ifndef LUA_DIR
    #define LUA_DIR "../share/lua"
#endif

namespace {
    const auto SHADER   = stdfs::path { SHADER_DIR } / "triangle.spv";
    const auto BOOT_LUA = stdfs::path { LUA_DIR } / "boot.lua";
} // namespace

////////////////////////////////////////
////////////////////////////////////////
auto main(std::span<const std::string_view> args) -> int {
    wsi::parse_args(args);

    auto logger_singleton = log::Logger::create_logger_instance<log::ConsoleLogger>();

    auto application = TryAssert(engine::Application::create("Game", BOOT_LUA, { 800, 600 }, "Game"),
                                 "Failed to initialize Game");

    auto& device        = application.renderer().device();
    auto  vertex_shader = TryAssert(gpu::Shader::load_from_file(device, SHADER, gpu::ShaderStageFlag::VERTEX),
                                    std::format("Failed to load vertex shader {}", SHADER.string()));

    auto fragment_shader = TryAssert(gpu::Shader::load_from_file(device, SHADER, gpu::ShaderStageFlag::FRAGMENT),
                                     std::format("Failed to load fragment shader {}", SHADER.string()));

    auto pipeline_layout = TryAssert(gpu::PipelineLayout::create(device, {}), "Failed to create pipeline layout");

    const auto window_extent = application.window().extent();

    // initialize render pipeline
    const auto window_viewport = gpu::Viewport {
        .position = { 0.f, 0.f },
        .extent   = window_extent.to<f32>(),
        .depth    = { 0.f, 1.f },
    };
    const auto scissor = gpu::Scissor {
        .offset = { 0, 0 },
        .extent = window_extent,
    };

    const auto state = gpu::RasterPipelineState {
        .input_assembly_state = { .topology = gpu::PrimitiveTopology::TRIANGLE_LIST, },
        .viewport_state       = { .viewports = { window_viewport },
                                 .scissors  = { scissor }, },
        .color_blend_state
        = { .attachments = { { .blend_enable           = true,
                               .src_color_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                               .dst_color_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                               .src_alpha_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                               .dst_alpha_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                               .alpha_blend_operation  = gpu::BlendOperation::ADD, }, }, },
        .shader_state  = to_refs(vertex_shader, fragment_shader),
    };

    const auto rendering_info = gpu::RasterPipelineRenderingInfo {
        .color_attachment_formats = { gpu::PixelFormat::RGBA8_UNORM }
    };

    auto pipeline = TryAssert(gpu::Pipeline::create(device, state, pipeline_layout, rendering_info),
                              "Failed to create raster pipeline");

    application.set_frame_builder([&](auto&& builder) noexcept {
        auto gbuffer    = engine::GraphID {};
        auto backbuffer = engine::GraphID {};

        builder.add_raster_task(
          "gen_gbuffer",
          [&](auto& task_builder) mutable noexcept {
              gbuffer = task_builder.create_image({ .name       = "gbuffer",
                                                    .extent     = application.window().extent().to<3>(),
                                                    .type       = gpu::ImageType::T2D,
                                                    .format     = gpu::PixelFormat::RGBA8_UNORM,
                                                    .layers     = 1u,
                                                    .cull_imune = true });
          },
          [](auto&) {

          });

        auto buffer = engine::GraphID {};
        builder.add_raster_task(
          "render",
          [&](auto& task_builder) mutable noexcept {
              backbuffer = task_builder
                             .create_image({ .name   = "backbuffer",
                                             .extent = application.window().extent().to<3>(),
                                             .type   = gpu::ImageType::T2D,
                                             .format = gpu::PixelFormat::RGBA8_UNORM,
                                             .usages = gpu::ImageUsageFlag::COLOR_ATTACHMENT | gpu::ImageUsageFlag::TRANSFER_SRC,
                                             .layers = 1u,
                                             .cull_imune = true });
              buffer     = task_builder.create_buffer({
                .name = "buffer",
                .size = 8_kb,
              });

              task_builder.write_image(backbuffer, gpu::ImageViewType::T2D, gpu::ClearColor {});
              task_builder.read_image(gbuffer, gpu::ImageViewType::T2D);
          },
          [&pipeline](auto& cmb) noexcept { cmb.bind_pipeline(pipeline).draw(3); });
        builder.add_raster_task(
          "useless",
          [&](auto& task_builder) noexcept {
              task_builder.read_image(gbuffer, gpu::ImageViewType::T2D);
              task_builder.read_buffer(buffer);
          },
          [](auto&) {

          });

        builder.set_backbuffer(backbuffer);
    });
    application.run();

    return EXIT_SUCCESS;
}
