module;

#include <cstddef>

#include <stormkit/core/try_expected.hpp>

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;
import :ecs;
import :sprite_renderer;
import :renderer;

namespace sm = stormkit::monadic;

namespace stormkit::engine {
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
    } // namespace

    auto make_sprite(entities::EntityManager& manager, TextureID texture_id, const math::fbounding_rect& sprite_rect) {
        auto e = manager.make_entity();

        manager.add_component(e, bidim::StaticSpriteComponent { texture_id, sprite_rect });

        return e;
    }

    auto bind_bidim_components(sol::table& engine) noexcept -> void {
        engine.new_usertype<
          bidim::PositionComponent>("position_component",
                                    sol::constructors<bidim::PositionComponent(), bidim::PositionComponent(math::fvec2)> {},
                                    "position",
                                    &bidim::PositionComponent::position,
                                    "type",
                                    &bidim::PositionComponent::component_name);
        engine.new_usertype<
          bidim::StaticSpriteComponent>("texture_component",
                                        sol::constructors<bidim::StaticSpriteComponent(),
                                                          bidim::StaticSpriteComponent(TextureID, math::fbounding_rect)> {},
                                        "texture",
                                        &bidim::StaticSpriteComponent::texture_id,
                                        "type",
                                        &bidim::StaticSpriteComponent::component_name);
    }

    auto bind_bidim(sol::state& global_state) noexcept -> void {
        auto engine = global_state["stormkit"].get_or_create<sol::table>();
        engine.new_usertype<ResourceStore>(
          "resources_store",
          sol::no_constructor,
          "load_image",
          +[](ResourceStore* store, std::string_view path) static noexcept { return store->load_image(stdfs::path { path }); });
        auto bidim            = engine["2d"].get_or_create<sol::table>();
        engine["make_sprite"] = &make_sprite;

        bind_bidim_components(engine);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    BidimPipeline::BidimPipeline(Application& application, const math::fextent2& viewport, PrivateFuncTag) noexcept
        : m_viewport { viewport } {
        application.add_binder(&bind_bidim);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::do_init(Application& application) noexcept -> gpu::Expected<void> {
        const auto& renderer = application.renderer();
        auto&       world    = application.world();
        const auto& device   = renderer.device();

        m_render_data.vertex_shader = Try(gpu::Shader::load_from_bytes(device, QUAD_SPRITE_SHADER, gpu::ShaderStageFlag::VERTEX));
        m_render_data
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

        m_render_data.pipeline_state = {
                .input_assembly_state = { gpu::PrimitiveTopology::TRIANGLE_STRIP },
                .viewport_state       = { .viewports = { window_viewport },
                                         .scissors  = { scissor }, },
                .color_blend_state = { .attachments = { { .blend_enable           = true,
                                       .src_color_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                                       .dst_color_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                                       .src_alpha_blend_factor = gpu::BlendFactor::SRC_ALPHA,
                                       .dst_alpha_blend_factor = gpu::BlendFactor::ONE_MINUS_SRC_ALPHA,
                                       .alpha_blend_operation  = gpu::BlendOperation::ADD, }, }, },
                .shader_state  = to_refs(m_render_data.vertex_shader, m_render_data.fragment_shader),
                .vertex_input_state = {
                .binding_descriptions = SPRITE_VERTEX_BINDING_DESCRIPTIONS | stdr::to<std::vector>(),
                .input_attribute_descriptions= SPRITE_VERTEX_ATTRIBUTE_DESCRIPTIONS | stdr::to<std::vector>(),
            },
        };

        m_render_data.pipeline_layout = Try(gpu::PipelineLayout::create(device, {}));
        const auto rendering_info     = gpu::RasterPipelineRenderingInfo {
            .color_attachment_formats = { gpu::PixelFormat::RGBA8_UNORM }
        };

        m_render_data.pipeline = Try(gpu::Pipeline::create(device,
                                                           m_render_data.pipeline_state,
                                                           m_render_data.pipeline_layout,
                                                           rendering_info));
        world.add_system("StormKit:sprite_render_system",
                         { bidim::StaticSpriteComponent::type() },
                         {
                           .update              = monadic::noop(),
                           .on_message_received = bind_front(&BidimPipeline::on_message_received, this, std::ref(renderer)),
                         });
        Return {};
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto BidimPipeline::update_framegraph(const Renderer& renderer, FrameBuilder& graph) noexcept -> void {
        static constexpr auto UPDATE_VERTEX_TASK_NAME  = "StormKit:2d_pipeline:update_vertex_buffer";
        static constexpr auto RENDER_SPRITES_TASK_NAME = "StormKit:2d_pipeline:render_sprites";
        static constexpr auto BACKBUFFER_NAME          = "StormKit:2d_pipeline:backbuffer";
        static constexpr auto VERTEX_BUFFER_NAME       = "StormKit:2d_pipeline:render_sprites:vertex_buffer";
        static constexpr auto STAGING_BUFFER_NAME      = "StormKit:2d_pipeline:update_vertex_buffer:staging_buffer";

        const auto& device        = renderer.device();
        auto        should_upload = false;
        if (not m_render_data.vertex_buffer.initialized()) [[unlikely]] {
            m_render_data
              .vertex_buffer = TryAssert(gpu::Buffer::create(device,
                                                             {
                                                               .usages   = gpu::BufferUsageFlag::VERTEX
                                                                           | gpu::BufferUsageFlag::TRANSFER_DST,
                                                               .size     = SPRITE_VERTEX_BUFFER_SIZE,
                                                               .property = gpu::MemoryPropertyFlag::DEVICE_LOCAL,
                                                             }),
                                         "Failed to allocate vertex gpu buffer");
            should_upload    = true;
        }

        const auto vertex_buffer_id = graph.retain_buffer(VERTEX_BUFFER_NAME, *m_render_data.vertex_buffer);

        if (should_upload) {
            auto staging_buffer_id = FrameBuilder::ResourceID {};
            graph.add_transfer_task(
              UPDATE_VERTEX_TASK_NAME,
              [&](auto& builder) noexcept {
                  staging_buffer_id = builder.create_buffer(STAGING_BUFFER_NAME,
                                                            {
                                                              .usages = gpu::BufferUsageFlag::TRANSFER_SRC,
                                                              .size   = SPRITE_VERTEX_BUFFER_SIZE,
                                                            });

                  builder.write_buffer(vertex_buffer_id);
                  builder.write_buffer(staging_buffer_id);
              },
              [staging_buffer_id, this](auto& frame_resources, auto& cmb) noexcept {
                  const auto& staging_buffer = frame_resources.get_buffer(staging_buffer_id);
                  // staging_buffer.upload();

                  cmb.copy_buffer(staging_buffer, *m_render_data.vertex_buffer, SPRITE_VERTEX_BUFFER_SIZE);
              });
        }

        auto backbuffer_id = FrameBuilder::ResourceID {};
        graph.add_raster_task(
          RENDER_SPRITES_TASK_NAME,
          [&](auto& builder) noexcept {
              builder.read_buffer(vertex_buffer_id);

              backbuffer_id = builder.create_image(BACKBUFFER_NAME,
                                                   { .extent = m_viewport.to<u32>().to<3>(),
                                                     .format = gpu::PixelFormat::RGBA8_UNORM,
                                                     .layers = 1u,
                                                     .type   = gpu::ImageType::T2D,
                                                     .usages = gpu::ImageUsageFlag::COLOR_ATTACHMENT
                                                               | gpu::ImageUsageFlag::TRANSFER_SRC });

              builder.write_attachment(backbuffer_id, gpu::ClearColor {});
          },
          [this](const auto&, auto& cmb) noexcept {
              auto buffers = as_refs(m_render_data.vertex_buffer);

              cmb.bind_pipeline(m_render_data.pipeline);
              cmb.bind_vertex_buffers(buffers, std::array { 0_u64 });
              auto access = m_sprites.read(); // CONTINUER ICI
              for (const auto& _ : *access) cmb.draw(4);
          },
          FrameBuilder::ROOT);

        graph.set_backbuffer(backbuffer_id);
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

                sprites->emplace_back(e,
                                      Sprite {
                                        .texture = TryAssert(gpu::ImageView::create(renderer.device(),
                                                                                    renderer.resources()
                                                                                      .get_image(sprite_component.texture_id)),
                                                             std::format("Failed to create image view for sprite {}", e)),
                                      });
            }
        } else if (message.id == entities::EntityManager::REMOVED_ENTITY_MESSAGE_ID) {
            auto remove_begin = stdr::cend(*sprites);
            auto remove_end   = stdr::cend(*sprites);
            for (auto&& e : message.entities) {
                if (not entity_manager.has_component(e, bidim::StaticSpriteComponent::type())) continue;

                auto result  = stdr::remove_if(*sprites, [&e](const auto& pair) noexcept { return pair.first == e; });
                remove_begin = stdr::begin(result);
                remove_end   = stdr::end(result);
            }

            if (remove_begin != stdr::cend(*sprites)) sprites->erase(remove_begin, remove_end);
        }
    }
} // namespace stormkit::engine
