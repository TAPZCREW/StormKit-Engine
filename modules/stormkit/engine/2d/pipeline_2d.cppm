// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:pipeline_2d;

import std;

import stormkit;

import :core;
import :renderer;
import :dirty;
import :ecs.sprite_render_system;

namespace stdr = std::ranges;

export namespace stormkit::engine {
    struct Camera {
        math::fmat4 projection = math::fmat4::identity();
        math::fmat4 view       = math::fmat4::identity();

        static constexpr auto layout_binding() -> gpu::DescriptorSetLayoutBinding {
            return { .binding          = 0,
                     .type             = gpu::DescriptorType::UNIFORM_BUFFER_DYNAMIC,
                     .stages           = gpu::ShaderStageFlag::VERTEX,
                     .descriptor_count = 1 };
        }
    };

    class STORMKIT_ENGINE_API Pipeline2D {
        struct PrivateTag {};

      public:
        Pipeline2D(Application& application, const math::fextent2& viewport, PrivateTag) noexcept;
        ~Pipeline2D() noexcept;

        Pipeline2D(const Pipeline2D&)                    = delete;
        auto operator=(const Pipeline2D&) -> Pipeline2D& = delete;

        Pipeline2D(Pipeline2D&&) noexcept;
        auto operator=(Pipeline2D&&) noexcept -> Pipeline2D&;

        [[nodiscard]]
        static auto create(Application& application, const math::fextent2& viewport) noexcept -> gpu::Expected<Pipeline2D>;
        [[nodiscard]]
        static auto allocate(Application& application, const math::fextent2& viewport) noexcept
          -> gpu::Expected<Heap<Pipeline2D>>;

        auto init_ecs(Application& application) -> void;

        auto update_framegraph(const Application& application, FrameBuilder& graph) noexcept -> void;

      private:
        auto do_init(Application&) noexcept -> gpu::Expected<void>;

        auto update_task(const Renderer&, FrameBuilder&, FrameBuilder::ResourceID) noexcept -> void;

        Ref<const Locked<entities::EntityManager>> m_world;

        struct {
            gpu::RasterPipelineState pipeline_state;

            DeferInit<gpu::DescriptorPool> descriptor_pool;

            DeferInit<gpu::DescriptorSetLayout> camera_descriptor_layout;
            DeferInit<gpu::DescriptorSet>       camera_descriptor_set;
            DeferInit<gpu::Buffer>              camera_buffer;
            u32                                 camera_current_offset = 0;
        } m_scene_data;

        struct _ViewData {
            Camera         camera;
            math::fextent2 viewport;
        };

        using ViewData = Dirtyable<_ViewData>;

        ViewData m_view;

        Locked<DeferInit<pipeline_2d::SpriteRenderSystem>> m_sprite_render_system;
    };

    inline constexpr auto PIPELINE_2D_LOGGER = log::Module { "2d pipeline" };
} // namespace stormkit::engine

/////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
/////////////////////////////////////////////////////////////////////

namespace stormkit::engine {
    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Pipeline2D::~Pipeline2D() noexcept = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Pipeline2D::Pipeline2D(Pipeline2D&&) noexcept = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Pipeline2D::operator=(Pipeline2D&&) noexcept -> Pipeline2D& = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Pipeline2D::create(Application& application, const math::fextent2& viewport) noexcept
      -> gpu::Expected<Pipeline2D> {
        auto sprite_renderer = Pipeline2D { application, viewport, PrivateTag {} };
        Try(sprite_renderer.do_init(application));
        Return sprite_renderer;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Pipeline2D::allocate(Application& application, const math::fextent2& viewport) noexcept
      -> gpu::Expected<Heap<Pipeline2D>> {
        auto sprite_renderer = core::allocate_unsafe<Pipeline2D>(application, viewport, PrivateTag {});
        Try(sprite_renderer->do_init(application));
        Return sprite_renderer;
    }
} // namespace stormkit::engine
