// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>
#include <stormkit/log/log_macro.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:renderer;

import std;

import stormkit.core;
import stormkit.log;
import stormkit.gpu;

export import :renderer.framegraph;
export import :renderer.render_surface;

export namespace stormkit::engine {
    class STORMKIT_ENGINE_API Renderer final {
        struct PrivateFuncTag {};

      public:
        using BuildFrameClosure = FunctionRef<void(FrameBuilder&)>;

        Renderer(ThreadPool& thread_pool, PrivateFuncTag) noexcept;
        ~Renderer();

        Renderer(const Renderer&)                    = delete;
        auto operator=(const Renderer&) -> Renderer& = delete;

        Renderer(Renderer&&) noexcept;
        auto operator=(Renderer&&) noexcept -> Renderer&;

        [[nodiscard]]
        static auto create(std::string_view               application_name,
                           ThreadPool&                    thread_pool,
                           OptionalRef<const wsi::Window> window) noexcept -> gpu::Expected<Renderer>;
        [[nodiscard]]
        static auto allocate(std::string_view               application_name,
                             ThreadPool&                    thread_pool,
                             OptionalRef<const wsi::Window> window) noexcept -> gpu::Expected<Heap<Renderer>>;

        auto start_rendering(std::mutex& framegraph_mutex, std::atomic_bool& rebuild_graph) noexcept -> void;

        auto instance() const noexcept -> const gpu::Instance&;
        auto device() const noexcept -> const gpu::Device&;
        auto surface() const noexcept -> const RenderSurface&;
        auto raster_queue() const noexcept -> const gpu::Queue&;
        auto main_command_pool() const noexcept -> const gpu::CommandPool&;

        auto render_frame(std::mutex& framegraph_mutex, std::atomic_bool& rebuild_graph, BuildFrameClosure build_frame) noexcept
          -> void;

      private:
        auto do_init(std::string_view, OptionalRef<const wsi::Window>) noexcept -> gpu::Expected<void>;
        auto do_init_instance(std::string_view) noexcept -> gpu::Expected<void>;
        auto do_init_device() noexcept -> gpu::Expected<void>;
        auto do_init_render_surface(OptionalRef<const wsi::Window>) noexcept -> gpu::Expected<void>;

        auto thread_loop(std::mutex&, std::atomic_bool&, std::stop_token) noexcept -> void;
        auto do_render(std::mutex&, std::atomic_bool&, RenderSurface::Frame&) noexcept -> gpu::Expected<void>;

        bool                        m_validation_layers_enabled = false;
        u32                         m_current_frame             = 0;
        math::uextent2              m_extent;
        DeferInit<gpu::Instance>    m_instance;
        Heap<gpu::Device>           m_device;
        DeferInit<RenderSurface>    m_surface;
        DeferInit<gpu::Queue>       m_raster_queue;
        DeferInit<gpu::CommandPool> m_main_command_pool;

        Ref<ThreadPool> m_thread_pool;

        std::vector<gpu::CommandBuffer> m_command_buffers;

        std::jthread m_render_thread;

        FramePool    m_frame_pool;
        FrameBuilder m_frame_builder;

        std::vector<DeferInit<Frame>> m_frames;
    };

} // namespace stormkit::engine

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace stormkit::engine {
    LOGGER("Renderer")

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    Renderer::Renderer(ThreadPool& thread_pool, PrivateFuncTag) noexcept
        : m_thread_pool { as_ref_mut(thread_pool) } {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Renderer::~Renderer() = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Renderer::Renderer(Renderer&&) noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::operator=(Renderer&&) noexcept -> Renderer& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::create(std::string_view               application_name,
                                 ThreadPool&                    thread_pool,
                                 OptionalRef<const wsi::Window> window) noexcept -> gpu::Expected<Renderer> {
        auto renderer = Renderer { thread_pool, PrivateFuncTag {} };
        Try(renderer.do_init(application_name, std::move(window)));
        Return renderer;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::allocate(std::string_view               application_name,
                                   ThreadPool&                    thread_pool,
                                   OptionalRef<const wsi::Window> window) noexcept -> gpu::Expected<Heap<Renderer>> {
        auto renderer = core::allocate_unsafe<Renderer>(thread_pool, PrivateFuncTag {});
        Try(renderer->do_init(application_name, std::move(window)));
        Return renderer;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::start_rendering(std::mutex& framegraph_mutex, std::atomic_bool& rebuild_graph) noexcept -> void {
        m_render_thread = std::jthread {
            bind_front(&Renderer::thread_loop, this, std::ref(framegraph_mutex), std::ref(rebuild_graph))
        };
        set_thread_name(m_render_thread, "StormKit:RenderThread");
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::render_frame(std::mutex&       framegraph_mutex,
                                       std::atomic_bool& rebuild_graph,
                                       BuildFrameClosure build_frame) noexcept -> void {
        auto _ = std::unique_lock { framegraph_mutex };

        auto builder = FrameBuilder {};
        std::invoke(build_frame, builder);
        builder.bake();
        if (not m_frame_builder.baked() or m_frame_builder.hash() != builder.hash()) {
            ilog("FrameGraph has been updated");
            m_frame_builder = std::move(builder);
            rebuild_graph   = true;
        }
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::instance() const noexcept -> const gpu::Instance& {
        EXPECTS(m_instance.initialized());
        return m_instance.get();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::device() const noexcept -> const gpu::Device& {
        EXPECTS(m_device != nullptr);
        return *m_device;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::surface() const noexcept -> const RenderSurface& {
        EXPECTS(m_surface.initialized());
        return m_surface.get();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::raster_queue() const noexcept -> const gpu::Queue& {
        EXPECTS(m_raster_queue.initialized());
        return m_raster_queue.get();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::main_command_pool() const noexcept -> const gpu::CommandPool& {
        EXPECTS(m_main_command_pool.initialized());
        return m_main_command_pool.get();
    }
} // namespace stormkit::engine
