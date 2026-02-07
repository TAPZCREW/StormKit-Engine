// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:renderer.render_surface;

import std;

import stormkit.core;
import stormkit.gpu;
import stormkit.wsi;

export namespace stormkit::engine {
    class Renderer;

    class STORMKIT_ENGINE_API RenderSurface {
        struct PrivateFuncTag {};

      public:
        struct SubmissionResources {
            gpu::Fence     in_flight;
            gpu::Semaphore image_available;
            gpu::Semaphore render_finished;
        };

        struct Frame {
            u32 current_frame;
            u32 image_index;

            Ref<const SubmissionResources> submission_resources;
        };

        ~RenderSurface() noexcept;

        RenderSurface(const RenderSurface&)                    = delete;
        auto operator=(const RenderSurface&) -> RenderSurface& = delete;

        RenderSurface(RenderSurface&&) noexcept;
        auto operator=(RenderSurface&&) noexcept -> RenderSurface&;

        static auto create(const Renderer& renderer, const wsi::Window& window) noexcept -> gpu::Expected<RenderSurface>;
        static auto allocate(const Renderer& renderer, const wsi::Window& window) noexcept -> gpu::Expected<Heap<RenderSurface>>;

        [[nodiscard]]
        auto begin_frame(const gpu::Device& device) -> gpu::Expected<Frame>;
        [[nodiscard]]
        auto present_frame(const gpu::Queue& queue, const Frame& frame) -> gpu::Expected<void>;

        [[nodiscard]]
        auto buffering_count() const noexcept -> u32;

        [[nodiscard]]
        auto images() const noexcept -> const std::vector<gpu::Image>&;

        explicit constexpr RenderSurface(PrivateFuncTag) noexcept;

      private:
        auto do_init(const Renderer& renderer, const wsi::Window& window) noexcept -> gpu::Expected<void>;

        DeferInit<gpu::Surface>          m_surface;
        DeferInit<gpu::SwapChain>        m_swapchain;
        std::optional<gpu::SwapChain>    m_old_swapchain;
        u32                              m_buffering_count = 0;
        usize                            m_current_frame   = 0;
        std::vector<SubmissionResources> m_submission_resources;

        bool m_need_recreate;
    };
} // namespace stormkit::engine

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace stormkit::engine {
    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    constexpr RenderSurface::RenderSurface(PrivateFuncTag) noexcept {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline RenderSurface::~RenderSurface() noexcept {
        for (auto&& [fence, _, _] : m_submission_resources) auto _ = fence.wait();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline RenderSurface::RenderSurface(RenderSurface&&) noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto RenderSurface::operator=(RenderSurface&&) noexcept -> RenderSurface& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto RenderSurface::create(const Renderer& renderer, const wsi::Window& window) noexcept

      -> gpu::Expected<RenderSurface> {
        auto render_surface = RenderSurface { PrivateFuncTag {} };
        return render_surface.do_init(renderer, window).transform(core::monadic::consume(render_surface));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto RenderSurface::allocate(const Renderer& renderer, const wsi::Window& window) noexcept
      -> gpu::Expected<Heap<RenderSurface>> {
        auto render_surface = core::allocate_unsafe<RenderSurface>(PrivateFuncTag {});
        return render_surface->do_init(renderer, window).transform(core::monadic::consume(render_surface));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto RenderSurface::buffering_count() const noexcept -> u32 {
        return m_buffering_count;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto RenderSurface::images() const noexcept -> const std::vector<gpu::Image>& {
        return m_swapchain->images();
    }
} // namespace stormkit::engine
