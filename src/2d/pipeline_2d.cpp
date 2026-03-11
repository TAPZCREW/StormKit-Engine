module;

#include <cstddef>

#include <stormkit/core/try_expected.hpp>

#include <stormkit/log/log_macro.hpp>

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;
import :ecs;
import :pipeline_2d;
import :renderer;

namespace sm = stormkit::monadic;

namespace stormkit::engine {
    LOGGER_FUNC(PIPELINE_2D_LOGGER)

    namespace {
        constexpr auto BACKBUFFER_NAME = "StormKit:2d_pipeline:backbuffer";

        constexpr auto CREATE_BACKBUFFER_TASK_NAME = "StormKit:2d_pipeline:create_backbuffer";
        constexpr auto UPDATE_CAMERA_TASK_NAME     = "StormKit:2d_pipeline:update_camera_buffer";
        constexpr auto CAMERA_BUFFER_NAME          = "StormKit:2d_pipeline:render_sprites:camera_buffer";
        constexpr auto CAMERA_STAGING_BUFFER_NAME  = "StormKit:2d_pipeline:update_camera_buffer:camera_staging_buffer";
        constexpr auto CAMERA_BUFFER_SIZE          = sizeof(Camera);
    } // namespace

    extern auto bind_pipeline_2d(sol::state& global_state) noexcept -> void;

    //////////////////////////////////////
    //////////////////////////////////////
    Pipeline2D::Pipeline2D(Application& application, const math::fextent2& viewport, PrivateTag) noexcept
        : m_world { as_ref(application.world()) }, m_view { ViewData::create_dirty() }, m_sprite_render_system {} {
        m_view.write().viewport = viewport;
        application.append_binder(&bind_pipeline_2d);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto Pipeline2D::do_init(Application& application) noexcept -> gpu::Expected<void> {
        const auto& renderer = application.renderer();
        const auto& device   = renderer.device();

        const auto window_viewport = gpu::Viewport {
            .position = { 0.f, 0.f },
            .extent   = m_view.read().viewport,
            .depth    = { 0.f, 1.f },
        };
        const auto scissor = gpu::Scissor {
            .offset = { 0, 0 },
            .extent = m_view.read().viewport.to<u32>(),
        };

        m_scene_data.pipeline_state = {
                .input_assembly_state = { gpu::PrimitiveTopology::TRIANGLE_STRIP },
                .viewport_state       = { .viewports = { window_viewport },
                                         .scissors  = { scissor }, },
                .rasterization_state = { .cull_mode = gpu::CullModeFlag::BACK, },
                .color_blend_state = { .attachments = { { .blend_enable           = true,
                                       .src_color_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                                       .dst_color_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                                       .src_alpha_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                                       .dst_alpha_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                                       .alpha_blend_operation  = gpu::BlendOperation::ADD, }, }, },
        };

        m_scene_data
          .camera_descriptor_layout = Try(gpu::DescriptorSetLayout::
                                            create(device,
                                                   into_dyn_array<gpu::DescriptorSetLayoutBinding>(Camera::layout_binding())));

        const auto pool_sizes              = to_array<gpu::DescriptorPool::Size>({
          {
           .type             = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
           .descriptor_count = 1,
           },
        });
        m_scene_data.descriptor_pool       = Try(gpu::DescriptorPool::create(device, pool_sizes, 1));
        m_scene_data.camera_descriptor_set = Try(m_scene_data.descriptor_pool
                                                   ->create_descriptor_set(m_scene_data.camera_descriptor_layout));

        m_view.write()
          .camera
          .projection = math::orthographique(0.f,
                                             m_view.read().viewport.width,
                                             0.f,
                                             m_view.read().viewport.height,
                                             0.001f,
                                             100.f);

        m_scene_data
          .camera_buffer = Try(gpu::Buffer::create(device,
                                                   {
                                                     .usages = gpu::BufferUsageFlag::UNIFORM | gpu::BufferUsageFlag::TRANSFER_DST,
                                                     .size   = CAMERA_BUFFER_SIZE * renderer.buffering_count(),
                                                     .property = gpu::MemoryPropertyFlag::DEVICE_LOCAL,
                                                   }));
        const auto camera_sets = into_dyn_array<gpu::Descriptor>(gpu::BufferDescriptor {
          .type    = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
          .binding = 0,
          .buffer  = as_ref(m_scene_data.camera_buffer),
          .range   = CAMERA_BUFFER_SIZE,
          .offset  = 0,
        });

        m_scene_data.camera_descriptor_set->update(camera_sets);

        m_sprite_render_system.unsafe() = Try(pipeline_2d::SpriteRenderSystem::create(renderer,
                                                                                      m_scene_data.pipeline_state,
                                                                                      *m_scene_data.camera_descriptor_layout));

        Return {};
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto Pipeline2D::init_ecs(Application& application) -> void {
        const auto& renderer = application.renderer();
        auto&       _world   = application.world();

        auto world = _world.write();
        world->add_system("StormKit:sprite_render_system",
                          { pipeline_2d::StaticSpriteComponent::type() },
                          entities::System::Closures {
                            .update =
                              [this](auto& world, auto delta, const auto& entities) noexcept {
                                  auto render_system = m_sprite_render_system.write();
                                  render_system->update(world, delta, entities);
                              },
                            .on_message_received =
                              [this, &renderer](auto& world, const auto& message, const auto& entities) noexcept {
                                  auto render_system = m_sprite_render_system.write();
                                  render_system->on_message_received(renderer, world, message, entities);
                              },
                          });
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto Pipeline2D::update_framegraph(const Application& application, FrameBuilder& graph) noexcept -> void {
        const auto& renderer = application.renderer();

        const auto camera_buffer_id = graph.retain_buffer(CAMERA_BUFFER_NAME, *m_scene_data.camera_buffer);

        const auto& [_, backbuffer_id] = graph.add_transfer_task<FrameBuilder::ResourceID>(
          CREATE_BACKBUFFER_TASK_NAME,
          [this](auto& builder, auto& backbuffer_id) noexcept {
              backbuffer_id = builder.create_image(BACKBUFFER_NAME,
                                                   { .extent = m_view.read().viewport.to<u32>().to<3>(),
                                                     .format = gpu::PixelFormat::RGBA8_UNORM,
                                                     .layers = 1u,
                                                     .type   = gpu::ImageType::T2D,
                                                     .usages = gpu::ImageUsageFlag::COLOR_ATTACHMENT
                                                               | gpu::ImageUsageFlag::TRANSFER_SRC });
          },
          [](auto&, auto&, const auto&) static noexcept {},
          FrameBuilder::ROOT);

        if (m_view.dirty()) update_task(renderer, graph, camera_buffer_id);

        m_sprite_render_system.write()
          ->insert_tasks(application,
                         graph,
                         *backbuffer_id,
                         camera_buffer_id,
                         *m_scene_data.camera_descriptor_set,
                         m_scene_data.camera_current_offset);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto Pipeline2D::update_task(const Renderer&          renderer,
                                 FrameBuilder&            graph,
                                 FrameBuilder::ResourceID camera_buffer_id) noexcept -> void {
        m_view.mark_not_dirty();

        struct UpdateCameraTaskData {
            FrameBuilder::ResourceID camera_staging_buffer_id = {};
            FrameBuilder::ResourceID camera_buffer_id         = {};
        };

        graph.add_transfer_task<UpdateCameraTaskData>(
          UPDATE_CAMERA_TASK_NAME,
          [&](auto& builder, auto& data) mutable noexcept {
              data.camera_staging_buffer_id = builder.create_buffer(CAMERA_STAGING_BUFFER_NAME,
                                                                    {
                                                                      .usages = gpu::BufferUsageFlag::TRANSFER_SRC,
                                                                      .size   = CAMERA_BUFFER_SIZE,
                                                                    });
              data.camera_buffer_id         = camera_buffer_id;

              m_scene_data.camera_current_offset = (renderer.current_frame() * CAMERA_BUFFER_SIZE) % renderer.buffering_count();

              builder.write_buffer(data.camera_buffer_id);
              builder.write_buffer(data.camera_staging_buffer_id);
          },
          [this, &renderer](auto& frame_resources, auto& cmb, const auto& data) noexcept {
              auto&       camera_staging_buffer = frame_resources.get_buffer(data.camera_staging_buffer_id);
              const auto& camera_buffer         = frame_resources.get_buffer(data.camera_buffer_id);

              auto camera       = Camera {};
              camera.projection = math::transpose(m_view.read().camera.projection);
              camera.view       = math::transpose(m_view.read().camera.view);
              camera_staging_buffer.upload(as_bytes(camera));

              cmb.copy_buffer(camera_staging_buffer, camera_buffer, CAMERA_BUFFER_SIZE, m_scene_data.camera_current_offset);

              m_view.mark_not_dirty();
          });
    }
} // namespace stormkit::engine
