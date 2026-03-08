module;

#include <cstddef>

#include <stormkit/core/try_expected.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;
import :ecs;
import :sprite_renderer;
import :sprite_renderer_lua;
import :renderer;

namespace sm = stormkit::monadic;

namespace stormkit::engine {
    struct SpriteData {
        math::fmat4 model = math::fmat4::identity();

        static constexpr auto layout_binding() -> gpu::DescriptorSetLayoutBinding {
            return { .binding          = 0,
                     .type             = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
                     .stages           = gpu::ShaderStageFlag::VERTEX,
                     .descriptor_count = 1 };
        }
    };

    namespace {
        constexpr auto QUAD_SPRITE_SHADER = core::into_bytes({
        // clang-format off
#embed <quad_sprite.spv>
          // clang-format on
        });

        constexpr auto SPRITE_VERTEX_SIZE                 = sizeof(SpriteVertex);
        constexpr auto SPRITE_VERTEX_BINDING_DESCRIPTIONS = std::array {
            gpu::VertexBindingDescription {
                                           .binding = 0,
                                           .stride  = SPRITE_VERTEX_SIZE,
                                           },
        };
        constexpr auto SPRITE_VERTEX_ATTRIBUTE_DESCRIPTIONS = std::array {
            gpu::VertexInputAttributeDescription {
                                                  .location = 0,
                                                  .binding  = 0,
                                                  .format   = gpu::PixelFormat::RG32F,
                                                  .offset   = offsetof(SpriteVertex, position),
                                                  },
            gpu::VertexInputAttributeDescription {
                                                  .location = 1,
                                                  .binding  = 0,
                                                  .format   = gpu::PixelFormat::RG32F,
                                                  .offset   = offsetof(SpriteVertex,                     uv),
                                                  },
        };

        constexpr auto SPRITE_VERTEX_BUFFER_SIZE = SPRITE_VERTEX_SIZE * 4;

        constexpr auto UPDATE_VERTEX_TASK_NAME    = "StormKit:2d_pipeline:update_vertex_buffer";
        constexpr auto RENDER_SPRITES_TASK_NAME   = "StormKit:2d_pipeline:render_sprites";
        constexpr auto BACKBUFFER_NAME            = "StormKit:2d_pipeline:backbuffer";
        constexpr auto VERTEX_BUFFER_NAME         = "StormKit:2d_pipeline:render_sprites:vertex_buffer";
        constexpr auto VERTEX_STAGING_BUFFER_NAME = "StormKit:2d_pipeline:update_vertex_buffer:vertex_staging_buffer";
        constexpr auto CAMERA_BUFFER_NAME         = "StormKit:2d_pipeline:render_sprites:camera_buffer";
        constexpr auto CAMERA_STAGING_BUFFER_NAME = "StormKit:2d_pipeline:update_vertex_buffer:camera_staging_buffer";

        struct Camera {
            math::fmat4 projection;
            math::fmat4 view;
        };

        constexpr auto CAMERA_BUFFER_SIZE = sizeof(Camera);

        constexpr auto MAX_SPRITE_COUNT   = 100_usize;
        constexpr auto SPRITE_BUFFER_SIZE = sizeof(SpriteData) * MAX_SPRITE_COUNT;
    } // namespace

    //////////////////////////////////////
    //////////////////////////////////////
    BidimPipeline::BidimPipeline(Application& application, const math::fextent2& viewport, PrivateFuncTag) noexcept
        : m_viewport { viewport } {
        application.add_binder(&bind_bidim);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::do_init(Application& application) noexcept -> gpu::Expected<void> {
        Try(do_init_scene_data(application));
        Try(do_init_buffered_scene_data(application));

        Return {};
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::do_init_scene_data(Application& application) noexcept -> gpu::Expected<void> {
        const auto& renderer = application.renderer();
        const auto& device   = renderer.device();

        m_scene_data.vertex_shader = Try(gpu::Shader::load_from_bytes(device, QUAD_SPRITE_SHADER, gpu::ShaderStageFlag::VERTEX));
        m_scene_data
          .fragment_shader = Try(gpu::Shader::load_from_bytes(device, QUAD_SPRITE_SHADER, gpu::ShaderStageFlag::FRAGMENT));

        const auto window_viewport = gpu::Viewport {
            .position = { 0.f, 0.f },
            .extent   = m_viewport,
            .depth    = { 0.f, 1.f },
        };
        const auto scissor = gpu::Scissor {
            .offset = { 0, 0 },
            .extent = m_viewport.to<u32>(),
        };

        m_scene_data.pipeline_state = {
                .input_assembly_state = { gpu::PrimitiveTopology::TRIANGLE_STRIP },
                .viewport_state       = { .viewports = { window_viewport },
                                         .scissors  = { scissor }, },
                .rasterization_state = { .cull_mode = gpu::CullModeFlag::FRONT },
                .color_blend_state = { .attachments = { { .blend_enable           = true,
                                       .src_color_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                                       .dst_color_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                                       .src_alpha_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                                       .dst_alpha_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                                       .alpha_blend_operation  = gpu::BlendOperation::ADD, }, }, },
                .shader_state  = to_refs(m_scene_data.vertex_shader, m_scene_data.fragment_shader),
                .vertex_input_state = {
                .binding_descriptions = SPRITE_VERTEX_BINDING_DESCRIPTIONS | stdr::to<std::vector>(),
                .input_attribute_descriptions= SPRITE_VERTEX_ATTRIBUTE_DESCRIPTIONS | stdr::to<std::vector>(),
            },
        };

        m_scene_data
          .vertex_buffer = Try(gpu::Buffer::create(device,
                                                   {
                                                     .usages = gpu::BufferUsageFlag::VERTEX | gpu::BufferUsageFlag::TRANSFER_DST,
                                                     .size   = SPRITE_VERTEX_BUFFER_SIZE,
                                                     .property = gpu::MemoryPropertyFlag::DEVICE_LOCAL,
                                                   }));

        std::array<SpriteVertex, 4> vertices = {
            SpriteVertex { { 0.f, 0.f }, { 0.f, 0.f } },
            SpriteVertex { { 1.f, 0.f }, { 1.f, 0.f } },
            SpriteVertex { { 0.f, 1.f }, { 0.f, 1.f } },
            SpriteVertex { { 1.f, 1.f }, { 1.f, 1.f } },
        };
        auto staging_vertex_buffer = Try(gpu::Buffer::create(device,
                                                             {
                                                               .usages = gpu::BufferUsageFlag::VERTEX
                                                                         | gpu::BufferUsageFlag::TRANSFER_SRC,
                                                               .size   = SPRITE_VERTEX_BUFFER_SIZE,
                                                             }));
        staging_vertex_buffer.upload(as_bytes(vertices));

        auto cmb = Try(renderer.main_command_pool().create_command_buffer());
        Try(cmb.begin(true));
        cmb.copy_buffer(staging_vertex_buffer, m_scene_data.vertex_buffer, SPRITE_VERTEX_BUFFER_SIZE);
        Try(cmb.end());

        auto fence = Try(gpu::Fence::create(device));
        // renderer.transfer_queue().submit(cmb, {}, {}, {}, as_ref(fence));
        Try(cmb.submit(renderer.raster_queue(), {}, {}, {}, as_ref(fence)));

        m_buffered_scene_data
          .camera_descriptor_layout = Try(gpu::DescriptorSetLayout::
                                            create(device,
                                                   into_dyn_array<gpu::DescriptorSetLayoutBinding>(Camera::layout_binding())));
        m_buffered_scene_data.sprite_descriptor_layout
          = Try(gpu::DescriptorSetLayout::create(device,
                                                 into_dyn_array<gpu::DescriptorSetLayoutBinding>(SpriteData::layout_binding())));

        m_scene_data
          .pipeline_layout = Try(gpu::PipelineLayout::create(device,
                                                             { .descriptor_set_layouts = to_refs(m_buffered_scene_data
                                                                                                   .camera_descriptor_layout,
                                                                                                 m_buffered_scene_data
                                                                                                   .sprite_descriptor_layout) }));

        const auto rendering_info = gpu::RasterPipelineRenderingInfo {
            .color_attachment_formats = { gpu::PixelFormat::RGBA8_UNORM }
        };

        m_scene_data.pipeline = Try(gpu::Pipeline::create(device,
                                                          m_scene_data.pipeline_state,
                                                          m_scene_data.pipeline_layout,
                                                          rendering_info));

        const auto pool_sizes = to_array<gpu::DescriptorPool::Size>({
          {
           .type             = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
           .descriptor_count = 1,
           },
          {
           .type             = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
           .descriptor_count = 1,
           }
        });
        // {
        //   .type             = gpu::DescriptorType::COMBINED_IMAGE_SAMPLER,
        //   .descriptor_count = 1,
        // });

        m_scene_data.descriptor_pool = Try(gpu::DescriptorPool::create(device, pool_sizes, 2));

        Try(fence.wait());

        Return {};
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::do_init_buffered_scene_data(Application& application) noexcept -> gpu::Expected<void> {
        const auto& renderer = application.renderer();
        const auto& device   = renderer.device();

        m_buffered_scene_data
          .camera_descriptor_set = Try(m_scene_data.descriptor_pool
                                         ->create_descriptor_set(m_buffered_scene_data.camera_descriptor_layout));
        m_buffered_scene_data
          .sprite_descriptor_set = Try(m_scene_data.descriptor_pool
                                         ->create_descriptor_set(m_buffered_scene_data.sprite_descriptor_layout));

        const auto window_viewport = gpu::Viewport {
            .position = { 0.f, 0.f },
            .extent   = m_viewport,
            .depth    = { 0.f, 1.f },
        };
        m_camera.projection = math::orthographique(window_viewport.position.x,
                                                   window_viewport.extent.width,
                                                   window_viewport.position.y,
                                                   window_viewport.extent.height);

        m_buffered_scene_data
          .camera_buffer = Try(gpu::Buffer::create(device,
                                                   {
                                                     .usages = gpu::BufferUsageFlag::UNIFORM | gpu::BufferUsageFlag::TRANSFER_DST,
                                                     .size   = CAMERA_BUFFER_SIZE * renderer.buffering_count(),
                                                     .property = gpu::MemoryPropertyFlag::DEVICE_LOCAL,
                                                   }));
        const auto camera_sets = into_dyn_array<gpu::Descriptor>(gpu::BufferDescriptor {
          .type    = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
          .binding = 0,
          .buffer  = as_ref(m_buffered_scene_data.camera_buffer),
          .range   = CAMERA_BUFFER_SIZE,
          .offset  = 0,
        });

        m_buffered_scene_data.camera_descriptor_set->update(camera_sets);

        m_buffered_scene_data
          .sprite_buffer = Try(gpu::Buffer::create(device,
                                                   {
                                                     .usages = gpu::BufferUsageFlag::UNIFORM | gpu::BufferUsageFlag::TRANSFER_DST,
                                                     .size   = SPRITE_BUFFER_SIZE * renderer.buffering_count(),
                                                     .property = gpu::MemoryPropertyFlag::DEVICE_LOCAL,
                                                   }));
        const auto sprites_sets = into_dyn_array<gpu::Descriptor>(gpu::BufferDescriptor {
          .type    = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
          .binding = 0,
          .buffer  = as_ref(m_buffered_scene_data.sprite_buffer),
          .range   = SPRITE_BUFFER_SIZE,
          .offset  = 0,
        });

        m_buffered_scene_data.sprite_descriptor_set->update(sprites_sets);

        Return {};
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::init_ecs(Application& application) -> void {
        const auto& renderer = application.renderer();
        auto&       world    = application.world();

        world.add_system("StormKit:sprite_render_system",
                         { bidim::StaticSpriteComponent::type() },
                         {
                           .update              = monadic::noop(),
                           .on_message_received = bind_front(&BidimPipeline::on_message_received, this, std::ref(renderer)),
                         });
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::update_framegraph(const Renderer& renderer, FrameBuilder& graph) noexcept -> void {
        const auto vertex_buffer_id = graph.retain_buffer(VERTEX_BUFFER_NAME, *m_scene_data.vertex_buffer);
        const auto camera_buffer_id = graph.retain_buffer(CAMERA_BUFFER_NAME, *m_buffered_scene_data.camera_buffer);

        if (m_dirty.camera) insert_update_camera_task(renderer, graph, camera_buffer_id);
        // if (m_dirty.sprites) insert_update_sprite_task(renderer, graph, sprite_buffer_id);
        insert_render_sprites_task(renderer, graph, vertex_buffer_id, camera_buffer_id);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::insert_update_camera_task(const Renderer&          renderer,
                                                  FrameBuilder&            graph,
                                                  FrameBuilder::ResourceID camera_buffer_id) noexcept -> void {
        struct UpdateCameraTaskData {
            FrameBuilder::ResourceID camera_staging_buffer_id = {};
            FrameBuilder::ResourceID camera_buffer_id         = {};
        };

        graph.add_transfer_task<UpdateCameraTaskData>(
          UPDATE_VERTEX_TASK_NAME,
          [&](auto& builder, auto& data) mutable noexcept {
              data.camera_staging_buffer_id = builder.create_buffer(CAMERA_STAGING_BUFFER_NAME,
                                                                    {
                                                                      .usages = gpu::BufferUsageFlag::TRANSFER_SRC,
                                                                      .size   = CAMERA_BUFFER_SIZE,
                                                                    });
              data.camera_buffer_id         = camera_buffer_id;

              builder.write_buffer(data.camera_buffer_id);
              builder.write_buffer(data.camera_staging_buffer_id);
          },
          [this, &renderer](auto& frame_resources, auto& cmb, const auto& data) noexcept {
              auto&       camera_staging_buffer = frame_resources.get_buffer(data.camera_staging_buffer_id);
              const auto& camera_buffer         = frame_resources.get_buffer(data.camera_buffer_id);

              auto camera       = Camera {};
              camera.projection = math::transpose(m_camera.projection);
              camera.view       = math::transpose(m_camera.view);
              camera_staging_buffer.upload(as_bytes(camera));

              m_buffered_scene_data
                .camera_current_offset = (renderer.current_frame() * CAMERA_BUFFER_SIZE) % renderer.buffering_count();

              cmb.copy_buffer(camera_staging_buffer,
                              camera_buffer,
                              CAMERA_BUFFER_SIZE,
                              m_buffered_scene_data.camera_current_offset);

              m_dirty.camera = false;
          });
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::insert_update_sprites_task(const Renderer& renderer, FrameBuilder& graph) noexcept -> void {
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::insert_render_sprites_task(const Renderer&          renderer,
                                                   FrameBuilder&            graph,
                                                   FrameBuilder::ResourceID vertex_buffer_id,
                                                   FrameBuilder::ResourceID camera_buffer_id) noexcept -> void {
        struct RenderSpriteTaskData {
            FrameBuilder::ResourceID vertex_buffer_id = {};
            FrameBuilder::ResourceID camera_buffer_id = {};
            FrameBuilder::ResourceID backbuffer_id    = {};
        };

        const auto& [_, render_sprite_data] = graph.add_raster_task<RenderSpriteTaskData>(
          RENDER_SPRITES_TASK_NAME,
          [&](auto& builder, auto& data) noexcept {
              data.vertex_buffer_id = vertex_buffer_id;
              data.camera_buffer_id = camera_buffer_id;
              data.backbuffer_id    = builder.create_image(BACKBUFFER_NAME,
                                                           { .extent = m_viewport.to<u32>().to<3>(),
                                                             .format = gpu::PixelFormat::RGBA8_UNORM,
                                                             .layers = 1u,
                                                             .type   = gpu::ImageType::T2D,
                                                             .usages = gpu::ImageUsageFlag::COLOR_ATTACHMENT
                                                                       | gpu::ImageUsageFlag::TRANSFER_SRC });

              builder.read_buffer(data.vertex_buffer_id);
              builder.read_buffer(data.camera_buffer_id);
              builder.write_attachment(data.backbuffer_id, gpu::ClearColor {});
          },
          [this](const auto& frame_resources, auto& cmb, const auto& data) noexcept {
              const auto& vertex_buffer = frame_resources.get_buffer(data.vertex_buffer_id);

              auto buffers = as_refs(vertex_buffer);

              cmb.bind_pipeline(m_scene_data.pipeline)
                .bind_descriptor_sets(m_scene_data.pipeline,
                                      m_scene_data.pipeline_layout,
                                      as_refs(m_buffered_scene_data.camera_descriptor_set,
                                              m_buffered_scene_data.sprite_descriptor_set),
                                      { m_buffered_scene_data.camera_current_offset,
                                        m_buffered_scene_data.sprite_current_offset })
                .bind_vertex_buffers(buffers, std::array { 0_u64 });
              auto access = m_sprites.read();
              for (const auto& _ : *access) cmb.draw(4);
          },
          FrameBuilder::ROOT);

        graph.set_backbuffer(render_sprite_data->backbuffer_id);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::on_message_received(const Renderer&                renderer,
                                            const entities::EntityManager& entity_manager,
                                            const entities::Message&       message,
                                            const entities::Entities&) noexcept -> void {
        auto sprites = m_sprites.write();
        if (message.id == entities::EntityManager::ADDED_ENTITY_MESSAGE_ID) {
            for (auto&& e : message.entities) {
                if (not entity_manager.has_component(e, bidim::StaticSpriteComponent::type())) continue;

                const auto& sprite_component = entity_manager.template get_component<
                  bidim::StaticSpriteComponent>(e, bidim::StaticSpriteComponent::type());

                sprites
                  ->emplace_back(e,
                                 Sprite { .texture = TryAssert(gpu::ImageView::create(renderer.device(),
                                                                                      renderer.resources()
                                                                                        .get_image(sprite_component.texture_id)),
                                                               std::format("Failed to create image view for sprite {}", e)),
                                          .sampler = TryAssert(gpu::Sampler::create(renderer.device(), {}),
                                                               std::format("Failed to create sampler for sprite {}", e)) });
                m_dirty.sprites = true;
            }

        } else if (message.id == entities::EntityManager::REMOVED_ENTITY_MESSAGE_ID) {
            auto&& [remove_begin, remove_end] = stdr::remove_if(*sprites, [&entity_manager, &message](const auto& pair) noexcept {
                auto e = pair.first;
                return entity_manager.has_component(e, bidim::StaticSpriteComponent::type())
                       and stdr::any_of(message.entities, monadic::is_equal(e));
            });

            if (remove_begin != remove_end) {
                sprites->erase(remove_begin, remove_end);

                m_dirty.sprites = true;
            }
        }
    }
} // namespace stormkit::engine
