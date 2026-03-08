// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:sprite_renderer;

import std;

import stormkit;

import :core;
import :renderer;

namespace stdr = std::ranges;

export namespace stormkit::engine {
    struct SpriteVertex {
        math::fvec2 position;
        math::fvec2 uv;
    };

    class STORMKIT_ENGINE_API BidimPipeline {
        struct PrivateFuncTag {};

      public:
        BidimPipeline(Application& application, const math::fextent2& viewport, PrivateFuncTag) noexcept;
        ~BidimPipeline();

        BidimPipeline(const BidimPipeline&)                    = delete;
        auto operator=(const BidimPipeline&) -> BidimPipeline& = delete;

        BidimPipeline(BidimPipeline&&) noexcept;
        auto operator=(BidimPipeline&&) noexcept -> BidimPipeline&;

        [[nodiscard]]
        static auto create(Application& application, const math::fextent2& viewport) noexcept -> gpu::Expected<BidimPipeline>;
        [[nodiscard]]
        static auto allocate(Application& application, const math::fextent2& viewport) noexcept
          -> gpu::Expected<Heap<BidimPipeline>>;

        auto init_ecs(Application& application) -> void;

        auto update_framegraph(const Renderer& renderer, FrameBuilder& graph) noexcept -> void;

      private:
        struct Sprite {
            gpu::ImageView texture;
            gpu::Sampler   sampler;
        };

        auto do_init(Application&) noexcept -> gpu::Expected<void>;
        auto do_init_scene_data(Application&) noexcept -> gpu::Expected<void>;
        auto do_init_buffered_scene_data(Application&) noexcept -> gpu::Expected<void>;

        auto insert_update_camera_task(const Renderer&, FrameBuilder&, FrameBuilder::ResourceID) noexcept -> void;
        auto insert_update_sprites_task(const Renderer&, FrameBuilder&) noexcept -> void;
        auto insert_render_sprites_task(const Renderer&,
                                        FrameBuilder&,
                                        FrameBuilder::ResourceID,
                                        FrameBuilder::ResourceID) noexcept -> void;

        auto on_message_received(const Renderer&,
                                 const entities::EntityManager&,
                                 const entities::Message&,
                                 const entities::Entities&) noexcept -> void;

        struct SceneData {
            DeferInit<gpu::Shader> vertex_shader;
            DeferInit<gpu::Shader> fragment_shader;

            DeferInit<gpu::Buffer> vertex_buffer;

            DeferInit<gpu::PipelineLayout> pipeline_layout;
            gpu::RasterPipelineState       pipeline_state;
            DeferInit<gpu::Pipeline>       pipeline;

            DeferInit<gpu::DescriptorPool> descriptor_pool;
        } m_scene_data;

        struct BufferedSceneData {
            DeferInit<gpu::DescriptorSetLayout> camera_descriptor_layout;
            DeferInit<gpu::DescriptorSet>       camera_descriptor_set;
            DeferInit<gpu::Buffer>              camera_buffer;
            u32                                 camera_current_offset = 0;

            DeferInit<gpu::DescriptorSetLayout> sprite_descriptor_layout;
            DeferInit<gpu::DescriptorSet>       sprite_descriptor_set;
            DeferInit<gpu::Buffer>              sprite_buffer;
            u32                                 sprite_current_offset = 0;
        } m_buffered_scene_data;

        struct Camera {
            math::fmat4 projection = math::fmat4::identity();
            math::fmat4 view       = math::fmat4::identity();

            static constexpr auto layout_binding() -> gpu::DescriptorSetLayoutBinding {
                return { .binding          = 0,
                         .type             = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
                         .stages           = gpu::ShaderStageFlag::VERTEX,
                         .descriptor_count = 1 };
            }
        } m_camera;

        math::fextent2 m_viewport;

        Locked<std::vector<std::pair<entities::Entity, Sprite>>> m_sprites;

        struct Dirty {
            inline Dirty() noexcept  = default;
            inline ~Dirty() noexcept = default;

            inline Dirty(Dirty&& other) noexcept : camera { other.camera.load() }, sprites { other.sprites.load() } {}

            inline auto operator=(Dirty&& other) noexcept -> Dirty& {
                if (this == &other) [[unlikely]]
                    return *this;

                camera.store(other.camera.load());
                sprites.store(other.sprites.load());

                return *this;
            }

            std::atomic_bool camera  = true;
            std::atomic_bool sprites = true;
        } m_dirty;
    };
} // namespace stormkit::engine

/////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
/////////////////////////////////////////////////////////////////////

namespace stormkit::engine {

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline BidimPipeline::~BidimPipeline() = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline BidimPipeline::BidimPipeline(BidimPipeline&& other) noexcept = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto BidimPipeline::operator=(BidimPipeline&& other) noexcept -> BidimPipeline& = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto BidimPipeline::create(Application& application, const math::fextent2& viewport) noexcept
      -> gpu::Expected<BidimPipeline> {
        auto sprite_renderer = BidimPipeline { application, viewport, PrivateFuncTag {} };
        Try(sprite_renderer.do_init(application));
        Return sprite_renderer;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto BidimPipeline::allocate(Application& application, const math::fextent2& viewport) noexcept
      -> gpu::Expected<Heap<BidimPipeline>> {
        auto sprite_renderer = core::allocate_unsafe<BidimPipeline>(application, viewport, PrivateFuncTag {});
        Try(sprite_renderer->do_init(application));
        Return sprite_renderer;
    }
} // namespace stormkit::engine
