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

namespace stdfs = std::filesystem;

export namespace stormkit::engine {
    using TextureID = hash32;

    inline constexpr auto INVALID_TEXTURE_ID = std::numeric_limits<TextureID>::max();

    struct FrameResources {
        using Images     = std::vector<std::pair<engine::FrameBuilder::ResourceID, gpu::Image>>;
        using ImagesMap  = std::vector<std::pair<engine::FrameBuilder::ResourceID, Ref<const gpu::Image>>>;
        using ImageViews = std::vector<std::pair<engine::FrameBuilder::CombinedID, gpu::ImageView>>;
        using Buffers    = std::vector<std::pair<engine::FrameBuilder::ResourceID, gpu::Buffer>>;
        using BuffersMap = std::vector<std::pair<engine::FrameBuilder::ResourceID, Ref<const gpu::Buffer>>>;

        gpu::Semaphore semaphore;
        gpu::Fence     fence;

        Images  created_images  = {};
        Buffers created_buffers = {};

        ImagesMap  images      = {};
        BuffersMap buffers     = {};
        ImageViews image_views = {};

        OptionalRef<const gpu::Image> backbuffer = std::nullopt;

        struct Pass {
            gpu::CommandBuffer cmb;
        };

        gpu::CommandBuffer main_cmb;
        std::vector<Pass>  passes = {};
    };

    class FrameResourceCache {
      public:
        explicit FrameResourceCache(const gpu::Device& device) noexcept;
        ~FrameResourceCache() noexcept;

        FrameResourceCache(const FrameResourceCache&)                    = delete;
        auto operator=(const FrameResourceCache&) -> FrameResourceCache& = delete;

        FrameResourceCache(FrameResourceCache&&) noexcept;
        auto operator=(FrameResourceCache&&) noexcept -> FrameResourceCache&;

        auto cache_old_resources(FrameResources&& resources) noexcept -> void;

        auto get_or_create_image(const gpu::Image::CreateInfo& create_info) noexcept -> gpu::Image;
        auto get_or_create_buffer(const gpu::Buffer::CreateInfo& create_info) noexcept -> gpu::Buffer;

      private:
        Ref<const gpu::Device>                      m_device;
        std::vector<std::pair<hash32, gpu::Image>>  m_images;
        std::vector<std::pair<hash32, gpu::Buffer>> m_buffers;
    };

    class ResourceStore {
      public:
        explicit ResourceStore(const gpu::Device& device) noexcept;
        ~ResourceStore() noexcept;

        ResourceStore(const ResourceStore&)                    = delete;
        auto operator=(const ResourceStore&) -> ResourceStore& = delete;

        ResourceStore(ResourceStore&&) noexcept;
        auto operator=(ResourceStore&&) noexcept -> ResourceStore&;

        auto load_image(const stdfs::path& path) -> TextureID;

      private:
        Ref<const gpu::Device>         m_device;
        HashMap<TextureID, gpu::Image> m_textures;
    };

    class STORMKIT_ENGINE_API Renderer final {
        struct PrivateFuncTag {};

      public:
        using BuildFrameClosure = FunctionRef<void(FrameBuilder&)>;

        Renderer(ThreadPool& thread_pool, PrivateFuncTag) noexcept;
        ~Renderer() noexcept;

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

        auto start_rendering(std::mutex& frame_builder_mutex, std::atomic_bool& window_is_open) noexcept -> void;

        auto instance() const noexcept -> const gpu::Instance&;
        auto device() const noexcept -> const gpu::Device&;
        auto surface() const noexcept -> const RenderSurface&;
        auto raster_queue() const noexcept -> const gpu::Queue&;
        auto main_command_pool() const noexcept -> const gpu::CommandPool&;
        template<typename Self>
        auto resources(this Self& self) noexcept -> meta::ForwardConst<Self, ResourceStore>&;

        auto dump_framegraph() const noexcept -> void;

        auto build_frame(std::mutex& frame_builder_mutex, BuildFrameClosure build_frame) noexcept -> void;

      private:
        auto do_init(std::string_view, OptionalRef<const wsi::Window>) noexcept -> gpu::Expected<void>;
        auto do_init_instance(std::string_view) noexcept -> gpu::Expected<void>;
        auto do_init_device() noexcept -> gpu::Expected<void>;
        auto do_init_render_surface(OptionalRef<const wsi::Window>) noexcept -> gpu::Expected<void>;

        auto thread_loop(std::mutex&, std::atomic_bool&, std::stop_token) noexcept -> void;
        auto do_render(std::mutex&, RenderSurface::Frame&) noexcept -> gpu::Expected<void>;

        auto realize_frame(RenderSurface::Frame&) noexcept -> FrameResources;

        bool                          m_validation_layers_enabled = false;
        u32                           m_current_frame             = 0;
        math::uextent2                m_extent;
        DeferInit<gpu::Instance>      m_instance;
        Heap<gpu::Device>             m_device;
        DeferInit<RenderSurface>      m_surface;
        DeferInit<gpu::Queue>         m_raster_queue;
        DeferInit<gpu::CommandPool>   m_main_command_pool;
        DeferInit<ResourceStore>      m_resource_store;
        DeferInit<FrameResourceCache> m_frame_resource_cache;

        Ref<ThreadPool> m_thread_pool;

        std::vector<gpu::CommandBuffer> m_command_buffers;

        std::jthread m_render_thread;

        FrameBuilder m_frame;

        std::vector<DeferInit<FrameResources>> m_frame_resources;

        mutable std::atomic_bool m_dump_next_graph = false;
    };

} // namespace stormkit::engine

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace stormkit::engine {
    LOGGER("renderer")

    /////////////////////////////////////
    /////////////////////////////////////
    inline ResourceStore::ResourceStore(const gpu::Device& device) noexcept : m_device { as_ref(device) } {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline ResourceStore::~ResourceStore() noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    inline ResourceStore::ResourceStore(ResourceStore&&) noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto ResourceStore::operator=(ResourceStore&&) noexcept -> ResourceStore& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    inline FrameResourceCache::FrameResourceCache(const gpu::Device& device) noexcept : m_device { as_ref(device) } {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline FrameResourceCache::~FrameResourceCache() noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    inline FrameResourceCache::FrameResourceCache(FrameResourceCache&&) noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameResourceCache::operator=(FrameResourceCache&&) noexcept -> FrameResourceCache& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    Renderer::Renderer(ThreadPool& thread_pool, PrivateFuncTag) noexcept
        : m_thread_pool { as_ref_mut(thread_pool) } {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Renderer::~Renderer() noexcept {
        m_device->wait_idle();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Renderer::Renderer(Renderer&& other) noexcept
        : m_validation_layers_enabled { std::exchange(other.m_validation_layers_enabled, false) },
          m_current_frame { std::exchange(other.m_current_frame, 0) },
          m_extent { std::exchange(other.m_extent, {}) },
          m_instance { std::exchange(other.m_instance, {}) },
          m_device { std::exchange(other.m_device, {}) },
          m_surface { std::exchange(other.m_surface, {}) },
          m_raster_queue { std::exchange(other.m_raster_queue, {}) },
          m_main_command_pool { std::exchange(other.m_main_command_pool, {}) },
          m_resource_store { std::exchange(other.m_resource_store, {}) },
          m_thread_pool { other.m_thread_pool },
          m_command_buffers { std::exchange(other.m_command_buffers, {}) },
          m_render_thread { std::exchange(other.m_render_thread, {}) },
          m_frame { std::exchange(other.m_frame, {}) },
          m_dump_next_graph { other.m_dump_next_graph.load() } {
        other.m_dump_next_graph.store(false);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::operator=(Renderer&& other) noexcept -> Renderer& {
        if (&other == this) [[unlikely]]
            return *this;

        m_validation_layers_enabled = std::exchange(other.m_validation_layers_enabled, false);
        m_current_frame             = std::exchange(other.m_current_frame, 0);
        m_extent                    = std::exchange(other.m_extent, {});
        m_instance                  = std::exchange(other.m_instance, {});
        m_device                    = std::exchange(other.m_device, {});
        m_surface                   = std::exchange(other.m_surface, {});
        m_raster_queue              = std::exchange(other.m_raster_queue, {});
        m_main_command_pool         = std::exchange(other.m_main_command_pool, {});
        m_resource_store            = std::exchange(other.m_resource_store, {});
        m_thread_pool               = as_ref_mut(other.m_thread_pool);
        m_command_buffers           = std::exchange(other.m_command_buffers, {});
        m_render_thread             = std::exchange(other.m_render_thread, {});
        m_frame                     = std::exchange(other.m_frame, {});
        m_dump_next_graph           = other.m_dump_next_graph.load();
        other.m_dump_next_graph.store(false);

        return *this;
    }

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
    inline auto Renderer::start_rendering(std::mutex& frame_builder_mutex, std::atomic_bool& window_is_open) noexcept -> void {
        m_render_thread = std::jthread {
            bind_front(&Renderer::thread_loop, this, std::ref(frame_builder_mutex), std::ref(window_is_open))
        };
        set_thread_name(m_render_thread, "StormKit:RenderThread");
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

    /////////////////////////////////////
    /////////////////////////////////////
    template<typename Self>
    STORMKIT_FORCE_INLINE
    inline auto Renderer::resources(this Self& self) noexcept -> meta::ForwardConst<Self, ResourceStore>& {
        EXPECTS(self.m_main_command_pool.initialized());
        return std::forward_like<Self&>(self.m_resource_store.get());
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::dump_framegraph() const noexcept -> void {
        m_dump_next_graph = true;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Renderer::build_frame(std::mutex& frame_builder_mutex, BuildFrameClosure build_frame) noexcept -> void {
        auto frame = FrameBuilder {};
        std::invoke(build_frame, frame);

        auto _  = std::unique_lock { frame_builder_mutex };
        m_frame = std::move(frame);
    }
} // namespace stormkit::engine
