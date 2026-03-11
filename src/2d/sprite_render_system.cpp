module;

#include <stormkit/core/try_expected.hpp>

#include <stormkit/log/log_macro.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;
import :ecs;
// import :pipeline_2d;

namespace sm = stormkit::monadic;

namespace stormkit::engine::pipeline_2d {
    LOGGER("sprite render system")

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

        constexpr auto RENDER_SPRITES_TASK_NAME = "StormKit:2d_pipeline:render_sprites";
        constexpr auto VERTEX_BUFFER_NAME       = "StormKit:2d_pipeline:render_sprites:vertex_buffer";

        constexpr auto UPDATE_SPRITES_TASK_NAME    = "StormKit:2d_pipeline:update_sprites_buffer";
        constexpr auto SPRITES_BUFFER_NAME         = "StormKit:2d_pipeline:render_sprites:sprites_buffer";
        constexpr auto SPRITES_STAGING_BUFFER_NAME = "StormKit:2d_pipeline:update_sprites_buffer:sprites_staging_buffer";

        constexpr auto MAX_SPRITE_COUNT    = 128_usize;
        constexpr auto SPRITES_BUFFER_SIZE = sizeof(SpriteData) * MAX_SPRITE_COUNT;
    } // namespace

    //////////////////////////////////////
    //////////////////////////////////////
    auto SpriteRenderSystem::update(entities::EntityManager&, fsecond, const entities::Entities&) noexcept -> void {
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto SpriteRenderSystem::on_message_received(const Renderer&           renderer,
                                                 entities::EntityManager&  world,
                                                 const entities::Message&  message,
                                                 const entities::Entities& entities) noexcept -> void {
        // auto sprites = m_sprites.write();
        if (message.id == entities::EntityManager::ADDED_ENTITY_MESSAGE_ID) {
            for (auto&& e : message.entities) {
                if (not world.has_component(e, StaticSpriteComponent::type())) continue;

                const auto& sprite_component = world
                                                 .template get_component<StaticSpriteComponent>(e, StaticSpriteComponent::type());

                dlog("Add sprite from entity: {}.", e);
                m_sprites.write().emplace_back(Sprite {
                  .e       = e,
                  .texture = TryAssert(gpu::ImageView::create(renderer.device(),
                                                              renderer.resources().get_image(sprite_component.texture_id)),
                                       std::format("Failed to create image view for entity: {}!", e)),
                  .sampler = TryAssert(gpu::Sampler::create(renderer.device(), {}),
                                       std::format("Failed to create sampler for entity: {}!", e)) });
            }

        } else if (message.id == entities::EntityManager::REMOVED_ENTITY_MESSAGE_ID) {
            auto&& [remove_begin, remove_end] = stdr::remove_if(m_sprites.write(), [&message](const auto& sprite) noexcept {
                auto removed = stdr::any_of(message.entities, monadic::is_equal(sprite.e));
                if (removed) dlog("Remove sprite from entity: {}.", sprite.e);

                return removed;
            });

            if (remove_begin != stdr::cend(m_sprites.read())) { m_sprites.write().erase(remove_begin, remove_end); }
        }
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto SpriteRenderSystem::insert_tasks(const Application&        application,
                                          FrameBuilder&             graph,
                                          FrameBuilder::ResourceID  backbuffer_id,
                                          FrameBuilder::ResourceID  camera_buffer_id,
                                          const gpu::DescriptorSet& camera_descriptor_set,
                                          u32                       camera_current_offset) noexcept -> void {
        const auto sprites_buffer_id = graph.retain_buffer(SPRITES_BUFFER_NAME, *m_sprite_data.buffer);

        if (m_sprites.dirty()) update_task(application, graph, sprites_buffer_id);
        render_static_sprite_task(graph,
                                  backbuffer_id,
                                  sprites_buffer_id,
                                  camera_buffer_id,
                                  camera_descriptor_set,
                                  camera_current_offset);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto SpriteRenderSystem::do_init(const Renderer&                 renderer,
                                     const gpu::RasterPipelineState& initial_state,
                                     const gpu::DescriptorSetLayout& camera_descriptor_layout) noexcept -> gpu::Expected<void> {
        const auto& device = renderer.device();

        m_static_sprite_data
          .vertex_shader = Try(gpu::Shader::load_from_bytes(device, QUAD_SPRITE_SHADER, gpu::ShaderStageFlag::VERTEX));
        m_static_sprite_data
          .fragment_shader = Try(gpu::Shader::load_from_bytes(device, QUAD_SPRITE_SHADER, gpu::ShaderStageFlag::FRAGMENT));

        auto static_sprite_pipeline_state = auto(initial_state);
        static_sprite_pipeline_state
          .shader_state = to_refs(m_static_sprite_data.vertex_shader, m_static_sprite_data.fragment_shader);

        m_static_sprite_data
          .descriptor_layout = Try(gpu::DescriptorSetLayout::
                                     create(device,
                                            into_dyn_array<gpu::DescriptorSetLayoutBinding>(SpriteData::layout_binding())));

        m_static_sprite_data
          .pipeline_layout = Try(gpu::PipelineLayout::create(device,
                                                             { .descriptor_set_layouts = to_refs(camera_descriptor_layout,
                                                                                                 m_static_sprite_data
                                                                                                   .descriptor_layout) }));

        const auto rendering_info = gpu::RasterPipelineRenderingInfo {
            .color_attachment_formats = { gpu::PixelFormat::RGBA8_UNORM }
        };

        m_static_sprite_data
          .pipeline = Try(gpu::Pipeline::create(device,
                                                static_sprite_pipeline_state,
                                                m_static_sprite_data.pipeline_layout,
                                                rendering_info));

        const auto pool_sizes         = to_array<gpu::DescriptorPool::Size>({
          {
           .type             = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
           .descriptor_count = 1,
           },
        });
        m_sprite_data.descriptor_pool = Try(gpu::DescriptorPool::create(device, pool_sizes, 1));

        m_static_sprite_data
          .descriptor_set = Try(m_sprite_data.descriptor_pool->create_descriptor_set(m_static_sprite_data.descriptor_layout));

        m_sprite_data
          .buffer       = Try(gpu::Buffer::create(device,
                                                  {
                                                    .usages   = gpu::BufferUsageFlag::UNIFORM | gpu::BufferUsageFlag::TRANSFER_DST,
                                                    .size     = SPRITES_BUFFER_SIZE * renderer.buffering_count(),
                                                    .property = gpu::MemoryPropertyFlag::DEVICE_LOCAL,
                                                  }));
        const auto sets = into_dyn_array<gpu::Descriptor>(gpu::BufferDescriptor {
          .type    = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
          .binding = 0,
          .buffer  = as_ref(m_sprite_data.buffer),
          .range   = SPRITES_BUFFER_SIZE,
          .offset  = 0,
        });

        m_static_sprite_data.descriptor_set->update(sets);

        Return {};
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto SpriteRenderSystem::update_task(const Application&       application,
                                         FrameBuilder&            graph,
                                         FrameBuilder::ResourceID sprites_buffer_id) noexcept -> void {
        dlog("sprites dirty!");
        m_sprites.mark_not_dirty();

        const auto& renderer = application.renderer();

        struct UpdateStaticSpriteTaskData {
            FrameBuilder::ResourceID sprites_staging_buffer_id = {};
            FrameBuilder::ResourceID sprites_buffer_id         = {};
        };

        graph.add_transfer_task<UpdateStaticSpriteTaskData>(
          UPDATE_SPRITES_TASK_NAME,
          [&](auto& builder, auto& data) noexcept {
              data.sprites_staging_buffer_id = builder.create_buffer(SPRITES_STAGING_BUFFER_NAME,
                                                                     {
                                                                       .usages = gpu::BufferUsageFlag::TRANSFER_SRC,
                                                                       .size   = SPRITES_BUFFER_SIZE,
                                                                     });
              data.sprites_buffer_id         = sprites_buffer_id;

              m_sprite_data.current_offset = (renderer.current_frame() * SPRITES_BUFFER_SIZE) % renderer.buffering_count();

              builder.write_buffer(data.sprites_buffer_id);
              builder.write_buffer(data.sprites_staging_buffer_id);
          },
          [this,
           &application,
           &renderer,
           offset = m_sprite_data.current_offset](auto& frame_resources, auto& cmb, const auto& data) noexcept {
              auto&       sprites_staging_buffer = frame_resources.get_buffer(data.sprites_staging_buffer_id);
              const auto& sprites_buffer         = frame_resources.get_buffer(data.sprites_buffer_id);

              const auto& read        = m_sprites.read();
              const auto  sprite_data = read
                                        | stdv::transform([](const auto& pair) static noexcept { return pair.e; })
                                        | stdv::transform([this, &application](auto e) noexcept {
                                             auto        world    = application.world().read();
                                             const auto& position = world->get_component<pipeline_2d::TransformComponent>(e)
                                                                      .position;
                                             const auto& sprite   = world->get_component<pipeline_2d::StaticSpriteComponent>(e);
                                             world.lock.unlock();

                                             const auto width  = sprite.texture_bounds.right - sprite.texture_bounds.left;
                                             const auto height = sprite.texture_bounds.bottom - sprite.texture_bounds.top;

                                             auto transform = math::fmat4::identity();
                                             transform      = math::scale(transform, { width, height, 1.f });
                                             transform = math::translate(transform, math::fvec3 { position.x, position.y, 0.f });

                                             return math::transpose(transform);
                                          })
                                        | stdr::to<std::vector>();
              sprites_staging_buffer.upload(as_bytes(sprite_data));

              cmb.copy_buffer(sprites_staging_buffer, sprites_buffer, SPRITES_BUFFER_SIZE, offset);
          });
    }

    //////////////////////////////////////
    //////////////////////////////////////
    auto SpriteRenderSystem::render_static_sprite_task(FrameBuilder&             graph,
                                                       FrameBuilder::ResourceID  backbuffer_id,
                                                       FrameBuilder::ResourceID  camera_buffer_id,
                                                       FrameBuilder::ResourceID  sprites_buffer_id,
                                                       const gpu::DescriptorSet& camera_descriptor_set,
                                                       u32                       camera_current_offset) noexcept -> void {
        struct RenderSpriteTaskData {
            FrameBuilder::ResourceID camera_buffer_id  = {};
            FrameBuilder::ResourceID sprites_buffer_id = {};
            FrameBuilder::ResourceID backbuffer_id     = {};
        };

        const auto& [_, render_sprite_data] = graph.add_raster_task<RenderSpriteTaskData>(
          RENDER_SPRITES_TASK_NAME,
          [&](auto& builder, auto& data) noexcept {
              data.camera_buffer_id  = camera_buffer_id;
              data.sprites_buffer_id = sprites_buffer_id;
              data.backbuffer_id     = backbuffer_id;

              builder.read_buffer(data.camera_buffer_id);
              builder.read_buffer(data.sprites_buffer_id);
              builder.write_attachment(data.backbuffer_id, gpu::ClearColor { .color = colors::BLACK<f32> });
          },
          [&camera_descriptor_set,
           camera_current_offset,
           this](const auto& frame_resources, auto& cmb, const auto& data) noexcept {
              cmb.bind_pipeline(m_static_sprite_data.pipeline)
                .bind_descriptor_sets(m_static_sprite_data.pipeline,
                                      m_static_sprite_data.pipeline_layout,
                                      as_refs(camera_descriptor_set, m_static_sprite_data.descriptor_set),
                                      to_array<u32>({ camera_current_offset, m_sprite_data.current_offset }));

              const auto& access = m_sprites.read();
              for (const auto& _ : access) cmb.draw(4);
          },
          FrameBuilder::ROOT);

        graph.set_backbuffer(render_sprite_data->backbuffer_id);
    }
} // namespace stormkit::engine::pipeline_2d
