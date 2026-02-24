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
    class STORMKIT_ENGINE_API BidimPipeline {
        struct PrivateFuncTag {};

      public:
        struct SpriteVertex {
            math::fvec2 position;
            math::fvec2 uv;
        };

        struct Sprite {
            std::array<SpriteVertex, 4> vertices = {
                SpriteVertex { { 0.f, 0.f }, { 0.f, 0.f } },
                SpriteVertex { { 0.f, 1.f }, { 0.f, 1.f } },
                SpriteVertex { { 1.f, 0.f }, { 1.f, 0.f } },
                SpriteVertex { { 1.f, 1.f }, { 1.f, 1.f } },
            };
            OptionalRef<const gpu::ImageView> texture;
        };

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

        auto add_sprite(Sprite sprite) noexcept -> u32;
        auto remove_sprite(u32 id) noexcept -> void;

        auto update_framegraph(const Renderer& renderer, FrameBuilder& graph) noexcept -> void;

      private:
        auto do_init(const Renderer&) noexcept -> gpu::Expected<void>;

        struct RenderData {
            DeferInit<gpu::Shader> vertex_shader;
            DeferInit<gpu::Shader> fragment_shader;

            DeferInit<gpu::Buffer> vertex_buffer;

            DeferInit<gpu::PipelineLayout> pipeline_layout;
            gpu::RasterPipelineState       pipeline_state;
            DeferInit<gpu::Pipeline>       pipeline;
        };

        // Heap<RenderData> m_render_data;
        RenderData m_render_data;

        math::fextent2 m_viewport;
        math::fmat4    m_projection_matrix;

        u32                                 m_next_sprite_id = 0;
        std::vector<std::pair<u32, Sprite>> m_sprites;

        bool m_dirty = true;
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
    inline BidimPipeline::BidimPipeline(BidimPipeline&&) noexcept = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto BidimPipeline::operator=(BidimPipeline&&) noexcept -> BidimPipeline& = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto BidimPipeline::create(Application& application, const math::fextent2& viewport) noexcept
      -> gpu::Expected<BidimPipeline> {
        auto sprite_renderer = BidimPipeline { application, viewport, PrivateFuncTag {} };
        Try(sprite_renderer.do_init(application.renderer()));
        Return sprite_renderer;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto BidimPipeline::allocate(Application& application, const math::fextent2& viewport) noexcept
      -> gpu::Expected<Heap<BidimPipeline>> {
        auto sprite_renderer = core::allocate_unsafe<BidimPipeline>(application, viewport, PrivateFuncTag {});
        Try(sprite_renderer->do_init(application.renderer()));
        Return sprite_renderer;
    }
} // namespace stormkit::engine
