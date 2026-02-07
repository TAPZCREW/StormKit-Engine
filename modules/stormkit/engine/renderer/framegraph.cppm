module;

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

export module stormkit.engine:renderer.framegraph;

import std;

import stormkit.core;
import stormkit.gpu;

import :renderer.render_surface;

export namespace stormkit::engine {
    using GraphID                    = dag::VertexID;
    inline constexpr auto INVALID_ID = std::numeric_limits<GraphID>::max();

    struct BufferCreateDescription {
        std::string name;

        usize size;

        bool cull_imune = false;

        auto operator==(const BufferCreateDescription&) const noexcept -> bool;
    };

    struct ImageCreateDescription {
        std::string name;

        math::uextent3      extent;
        gpu::ImageType      type;
        gpu::PixelFormat    format;
        gpu::ImageUsageFlag usages = gpu::ImageUsageFlag::COLOR_ATTACHMENT;

        u32 layers = 1u;

        bool cull_imune = false;
        auto operator==(const ImageCreateDescription&) const noexcept -> bool;
    };

    struct ImageReadDescription {
        GraphID            image;
        gpu::ImageViewType type;
        bool               cull_imune = false;
        auto               operator==(const ImageReadDescription&) const noexcept -> bool;
    };

    struct ImageWriteDescription {
        GraphID            image;
        gpu::ImageViewType type;

        std::optional<gpu::ClearValue> clear_value;
        bool                           cull_imune = false;
        auto                           operator==(const ImageWriteDescription&) const noexcept -> bool;
    };

    struct RasterTask {
        gpu::RenderingInfo rendering_info;
        gpu::CommandBuffer cmb;
    };

    struct ComputeTask {
        gpu::CommandBuffer cmb;
    };

    using Task = std::variant<RasterTask, ComputeTask>;

    class Frame {
      public:
        struct FrameResources {
            template<typename T>
            struct Map {
                GraphID      id;
                hash32       hash;
                Ref<const T> value;
            };

            std::vector<gpu::Image>     images;
            std::vector<gpu::ImageView> views;
            std::vector<gpu::Buffer>    buffers;

            std::vector<Map<gpu::Image>>     image_mapper;
            std::vector<Map<gpu::ImageView>> view_mapper;
            std::vector<Map<gpu::Buffer>>    buffer_mapper;
        };

        ~Frame() noexcept;

        Frame(Frame&&) noexcept;
        Frame(const Frame&) noexcept = delete;

        auto operator=(Frame&&) noexcept -> Frame&;
        auto operator=(const Frame&) noexcept -> Frame& = delete;

        auto execute(const gpu::Queue& queue) noexcept -> gpu::Expected<Ref<const gpu::Semaphore>>;

        [[nodiscard]]
        auto backbuffer() const noexcept -> const gpu::Image&;
        [[nodiscard]]
        auto fence(this auto& self) noexcept -> decltype(auto);

      private:
        constexpr Frame() noexcept;

        hash32            m_hash;
        FrameResources    m_resources;
        std::vector<Task> m_tasks = {};

        DeferInit<gpu::CommandBuffer> m_cmb;
        DeferInit<gpu::Fence>         m_fence;
        DeferInit<gpu::Semaphore>     m_semaphore;

        OptionalRef<const gpu::Image> m_backbuffer;

        friend class FrameBuilder;
        friend class FramePool;
    };

    class FramePool {
      public:
        FramePool() noexcept;
        ~FramePool() noexcept;

        FramePool(FramePool&&) noexcept;
        FramePool(const FramePool&) noexcept = delete;

        auto operator=(FramePool&&) noexcept -> FramePool&;
        auto operator=(const FramePool&) noexcept -> FramePool& = delete;

        auto recycle_frame(Frame&& frame) noexcept -> void;

        auto find_reusable_frame(hash32 hash) noexcept -> std::optional<Frame>;

        auto create_or_reuse_image(const gpu::Device& device, const ImageCreateDescription& description) noexcept
          -> gpu::Expected<gpu::Image>;

        auto create_or_reuse_image_view(const gpu::Device&          device,
                                        const gpu::Image&           image,
                                        const ImageReadDescription& description) noexcept -> gpu::Expected<gpu::ImageView>;
        auto create_or_reuse_image_view(const gpu::Device&           device,
                                        const gpu::Image&            image,
                                        const ImageWriteDescription& description) noexcept -> gpu::Expected<gpu::ImageView>;

        auto create_or_reuse_buffer(const gpu::Device& device, const BufferCreateDescription& description) noexcept
          -> gpu::Expected<gpu::Buffer>;

      private:
        auto update_pool() noexcept -> void;

        std::vector<Frame> m_staggering_frames;

        template<typename U>
        struct Recycled {
            hash32 hash;
            U      value;
        };

        std::vector<Recycled<gpu::Image>>  m_reusable_images;
        std::vector<Recycled<gpu::Buffer>> m_reusable_buffers;
    };

    class FrameBuilder {
      public:
        class FrameTaskBuilder;

        using SetupClosure   = FunctionRef<void(FrameTaskBuilder&)>;
        using ExecuteClosure = std::function<void(gpu::CommandBuffer&)>;

        struct TaskDescription {
            enum class Type {
                RASTER,
                COMPUTE,
                TRANSFER,
                RAYTRACING,
            } type;

            std::string name;

            ExecuteClosure execute;

            bool cull_imune = false;

            auto operator==(const TaskDescription&) const noexcept -> bool;
        };

        using GraphEntry = std::
          variant<BufferCreateDescription, ImageCreateDescription, ImageReadDescription, ImageWriteDescription, TaskDescription>;

        using DAG = core::DAG<GraphEntry>;

        class FrameTaskBuilder {
          public:
            auto create_buffer(BufferCreateDescription&& description) noexcept -> GraphID;
            auto create_image(ImageCreateDescription&& description) noexcept -> GraphID;

            auto read_buffer(GraphID) noexcept -> void;
            auto read_image(GraphID, gpu::ImageViewType type) noexcept -> void;

            auto write_buffer(GraphID) noexcept -> void;
            auto write_image(GraphID, gpu::ImageViewType type, std::optional<gpu::ClearValue> clear_value = std::nullopt) noexcept
              -> void;

          private:
            FrameTaskBuilder(GraphID, DAG&) noexcept;

            GraphID m_description_id;
            DAG&    m_dag;

            friend class FrameBuilder;
        };

        FrameBuilder() noexcept;
        ~FrameBuilder() noexcept;

        FrameBuilder(FrameBuilder&&) noexcept;
        FrameBuilder(const FrameBuilder&) noexcept = delete;

        auto operator=(FrameBuilder&&) noexcept -> FrameBuilder&;
        auto operator=(const FrameBuilder&) noexcept -> FrameBuilder& = delete;

        auto add_raster_task(std::string name, SetupClosure setup, ExecuteClosure execute, bool imune_to_cull = false) noexcept
          -> GraphID;
        // auto add_transfer_task(std::string name, SetupClosure setup, ExecuteClosure execute, bool imune_to_cull = false)
        // noexcept -> GraphID;
        // auto add_compute_task(std::string name, SetupClosure setup, ExecuteClosure execute, bool
        // imune_to_cull = false) noexcept -> GraphID;
        //
        auto set_backbuffer(GraphID id) noexcept -> void;

        auto bake() noexcept -> void;

        [[nodiscard]]
        auto baked() const noexcept -> bool;

        [[nodiscard]]
        auto make_frame(const gpu::Device&      device,
                        const gpu::Queue&       queue,
                        const gpu::CommandPool& command_pool,
                        FramePool&              frame_pool,
                        const math::rect<i32>&  render_area) const noexcept -> gpu::Expected<Frame>;

        [[nodiscard]]
        auto hash() const noexcept -> hash32;

      private:
        [[nodiscard]]
        auto add_task(std::string&&, TaskDescription::Type, SetupClosure, ExecuteClosure&&, bool) noexcept -> GraphID;

        DAG                                 m_dag;
        std::optional<std::vector<GraphID>> m_baked_graph;
        hash32                              m_hash = 0_u32;

        GraphID m_backbuffer = INVALID_ID;
    };

    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const BufferCreateDescription& description) noexcept -> Ret;
    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const ImageCreateDescription& description) noexcept -> Ret;
    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const FrameBuilder::TaskDescription& description) noexcept -> Ret;
    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const FrameBuilder::GraphEntry& description) noexcept -> Ret;
} // namespace stormkit::engine

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace stdr = std::ranges;
namespace stdv = std::views;

namespace stormkit::engine {
    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    constexpr Frame::Frame() noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Frame::~Frame() noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Frame::Frame(Frame&&) noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Frame::operator=(Frame&&) noexcept -> Frame& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto Frame::execute(const gpu::Queue& queue) noexcept -> gpu::Expected<Ref<const gpu::Semaphore>> {
        Try(m_cmb->submit(queue, {}, {}, as_refs<std::array>(m_semaphore), as_ref(m_fence)));
        Return as_ref(m_semaphore);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Frame::backbuffer() const noexcept -> const gpu::Image& {
        EXPECTS(m_backbuffer != nullptr);
        return *m_backbuffer;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Frame::fence(this auto& self) noexcept -> decltype(auto) {
        EXPECTS(self.m_fence.initialized());
        return *self.m_fence;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FramePool::FramePool() noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FramePool::~FramePool() noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FramePool::FramePool(FramePool&&) noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FramePool::operator=(FramePool&&) noexcept -> FramePool& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FramePool::recycle_frame(Frame&& frame) noexcept -> void {
        std::println("recycle frame!");

        m_staggering_frames.emplace_back(std::move(frame));

        update_pool();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FramePool::create_or_reuse_image(const gpu::Device& device, const ImageCreateDescription& description) noexcept
      -> gpu::Expected<gpu::Image> {
        update_pool();
        const auto des_hash = hash(description);

        if (auto it = stdr::find_if(m_reusable_images, [des_hash](auto& image) { return image.hash == des_hash; });
            it != stdr::cend(m_reusable_images)) {
            auto image = std::move(it->value);
            m_reusable_images.erase(it);
            std::println("REUSE IMAGE FOR {}", description.name);
            return image;
        }

        Return Try(gpu::Image::create(device,
                                      { .extent = description.extent,
                                        .format = description.format,
                                        .layers = description.layers,
                                        .type   = description.type,
                                        .usages = description.usages }));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FramePool::create_or_reuse_image_view(const gpu::Device&          device,
                                                      const gpu::Image&           image,
                                                      const ImageReadDescription& description) noexcept
      -> gpu::Expected<gpu::ImageView> {
        update_pool();
        const auto aspect_flag = [&image] noexcept {
            if (is_depth_stencil_format(image.format())) return gpu::ImageAspectFlag::DEPTH | gpu::ImageAspectFlag::STENCIL;
            else if (gpu::is_depth_format(image.format()))
                return gpu::ImageAspectFlag::DEPTH;

            return gpu::ImageAspectFlag::COLOR;
        }();

        Return Try(gpu::ImageView::create(device, image, description.type, { .aspect_mask = aspect_flag }));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FramePool::create_or_reuse_image_view(const gpu::Device&           device,
                                                      const gpu::Image&            image,
                                                      const ImageWriteDescription& description) noexcept
      -> gpu::Expected<gpu::ImageView> {
        update_pool();
        const auto aspect_flag = [&image] noexcept {
            if (is_depth_stencil_format(image.format())) return gpu::ImageAspectFlag::DEPTH | gpu::ImageAspectFlag::STENCIL;
            else if (gpu::is_depth_format(image.format()))
                return gpu::ImageAspectFlag::DEPTH;

            return gpu::ImageAspectFlag::COLOR;
        }();

        Return Try(gpu::ImageView::create(device, image, description.type, { .aspect_mask = aspect_flag }));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FramePool::create_or_reuse_buffer(const gpu::Device& device, const BufferCreateDescription& description) noexcept
      -> gpu::Expected<gpu::Buffer> {
        update_pool();
        const auto des_hash = hash(description);

        if (auto it = stdr::find_if(m_reusable_buffers, [des_hash](auto& buffer) { return buffer.hash == des_hash; });
            it != stdr::cend(m_reusable_buffers)) {
            auto buffer = std::move(it->value);
            m_reusable_buffers.erase(it);
            return buffer;
        }

        Return Try(gpu::Buffer::create(device,
                                       {
                                         .size = description.size,
                                       }));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FramePool::update_pool() noexcept -> void {
        auto&& finished_frames = stdr::remove_if(m_staggering_frames, [](const auto& frame) static noexcept {
            return frame.fence().status() == gpu::Fence::Status::SIGNALED;
        });

        for (auto& frame : finished_frames) {
            auto& image_mapper  = frame.m_resources.image_mapper;
            auto& buffer_mapper = frame.m_resources.buffer_mapper;
            auto& images        = frame.m_resources.images;
            auto& buffers       = frame.m_resources.buffers;

            stdr::move(images | stdv::transform([&image_mapper](auto&& image) noexcept {
                           const auto it = stdr::find_if(image_mapper, [&image](auto&& map) noexcept {
                               return map.value.get() == &image;
                           });
                           EXPECTS(it != stdr::cend(image_mapper));
                           return Recycled<gpu::Image> {
                               .hash  = it->hash,
                               .value = std::move(image),
                           };
                       }),
                       std::back_inserter(m_reusable_images));
            stdr::move(buffers | stdv::transform([&buffer_mapper](auto&& buffer) noexcept {
                           const auto it = stdr::find_if(buffer_mapper, [&buffer](auto&& map) noexcept {
                               return map.value.get() == &buffer;
                           });
                           EXPECTS(it != stdr::cend(buffer_mapper));
                           return Recycled<gpu::Buffer> {
                               .hash  = it->hash,
                               .value = std::move(buffer),
                           };
                       }),
                       std::back_inserter(m_reusable_buffers));
        }

        auto&& [begin, end] = finished_frames;
        m_staggering_frames.erase(begin, end);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameBuilder::FrameTaskBuilder::FrameTaskBuilder(GraphID id, DAG& dag) noexcept
        : m_description_id { id }, m_dag { dag } {
        EXPECTS(m_dag.has_vertex(id));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::create_buffer(BufferCreateDescription&& description) noexcept -> GraphID {
        const auto id = m_dag.add_vertex(std::move(description));

        m_dag.add_edge(m_description_id, id);

        return id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::create_image(ImageCreateDescription&& description) noexcept -> GraphID {
        const auto id = m_dag.add_vertex(std::move(description));

        m_dag.add_edge(m_description_id, id);

        return id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_image(GraphID image_id, gpu::ImageViewType type) noexcept -> void {
        EXPECTS(m_dag.has_vertex(image_id));

        const auto id = m_dag.add_vertex(ImageReadDescription { .image = image_id, .type = type });

        m_dag.add_edge(image_id, id);
        m_dag.add_edge(id, m_description_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_image(GraphID                        image_id,
                                                            gpu::ImageViewType             type,
                                                            std::optional<gpu::ClearValue> clear_value) noexcept -> void {
        EXPECTS(m_dag.has_vertex(image_id));

        const auto& image = as<ImageCreateDescription>(m_dag.get_vertex_value(image_id).value);

        const auto id = m_dag.add_vertex(ImageWriteDescription {
          .image       = image_id,
          .type        = type,
          .clear_value = std::move(clear_value),
          .cull_imune  = image.cull_imune });

        m_dag.add_edge(image_id, id);
        m_dag.add_edge(m_description_id, id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_buffer(GraphID id) noexcept -> void {
        EXPECTS(m_dag.has_vertex(id));

        m_dag.add_edge(id, m_description_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_buffer(GraphID id) noexcept -> void {
        EXPECTS(m_dag.has_vertex(id));

        m_dag.add_edge(m_description_id, id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameBuilder::FrameBuilder() noexcept
        : m_dag { dag::DIRECTED } {};

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameBuilder::~FrameBuilder() noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameBuilder::FrameBuilder(FrameBuilder&&) noexcept = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::operator=(FrameBuilder&&) noexcept -> FrameBuilder& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_raster_task(std::string                  name,
                                              FrameBuilder::SetupClosure   setup,
                                              FrameBuilder::ExecuteClosure execute,
                                              bool                         imune_to_cull) noexcept -> GraphID {
        return add_task(std::move(name), TaskDescription::Type::RASTER, std::move(setup), std::move(execute), imune_to_cull);
    }

    // /////////////////////////////////////
    // /////////////////////////////////////
    // STORMKIT_FORCE_INLINE
    // inline auto FrameBuilder::add_transfer_task(std::string                     name,
    //                                                FrameBuilder::SetupClosure   setup,
    //                                                FrameBuilder::ExecuteClosure execute,
    //                                                bool                            imune_to_cull) noexcept -> GraphID {
    //     return add_task(std::move(name), TaskDescription::Type::TRANSFER, std::move(setup), std::move(execute), imune_to_cull);
    // }

    // /////////////////////////////////////
    // /////////////////////////////////////
    // STORMKIT_FORCE_INLINE
    // inline auto FrameBuilder::add_compute_task(std::string                     name,
    //                                               FrameBuilder::SetupClosure   setup,
    //                                               FrameBuilder::ExecuteClosure execute,
    //                                               bool                            imune_to_cull) noexcept -> GraphID {
    //     return add_task(std::move(name), TaskDescription::Type::COMPUTE, std::move(setup), std::move(execute), imune_to_cull);
    // }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::set_backbuffer(GraphID id) noexcept -> void {
        EXPECTS(id != INVALID_ID);
        m_backbuffer = id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::baked() const noexcept -> bool {
        return m_baked_graph.has_value();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::hash() const noexcept -> hash32 {
        EXPECTS(baked());

        return m_hash;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::add_task(std::string&&                  name,
                                       TaskDescription::Type          type,
                                       FrameBuilder::SetupClosure     setup,
                                       FrameBuilder::ExecuteClosure&& execute,
                                       bool                           imune_to_cull) noexcept -> GraphID {
        const auto id = m_dag.add_vertex(TaskDescription {
          .type       = type,
          .name       = std::move(name),
          .execute    = std::move(execute),
          .cull_imune = imune_to_cull });

        auto builder = FrameTaskBuilder { id, m_dag };
        setup(builder);

        return id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const BufferCreateDescription& description) noexcept -> Ret {
        auto hash = Ret { 0 };

        hash_combine(hash, description.name, description.size, description.cull_imune);

        return hash;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const ImageCreateDescription& description) noexcept -> Ret {
        auto hash = Ret { 0 };

        hash_combine(hash,
                     description.name,
                     description.extent,
                     description.type,
                     description.format,
                     description.layers,
                     description.cull_imune);

        return hash;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const FrameBuilder::TaskDescription& description) noexcept -> Ret {
        auto hash = Ret { 0 };

        hash_combine(hash, description.name, description.type, description.cull_imune);

        return hash;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<meta::HashType Ret = hash32>
    constexpr auto hasher(const FrameBuilder::GraphEntry& description) noexcept -> Ret {
        return std::visit(Overloaded {
                            [](const BufferCreateDescription& value) static noexcept { return hash(value); },
                            [](const ImageCreateDescription& value) static noexcept { return hash(value); },
                            [](const ImageReadDescription&) static noexcept { return 0_u32; },
                            [](const ImageWriteDescription&) static noexcept { return 0_u32; },
                            [](const FrameBuilder::TaskDescription& value) static noexcept { return hash(value); },
                          },
                          description);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto BufferCreateDescription::operator==(const BufferCreateDescription& other) const noexcept -> bool {
        if (name != other.name) return false;
        else if (size != other.size)
            return false;
        else if (cull_imune != other.cull_imune)
            return false;

        return true;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto ImageCreateDescription::operator==(const ImageCreateDescription& other) const noexcept -> bool {
        if (name != other.name) return false;
        else if (extent != other.extent)
            return false;
        else if (type != other.type)
            return false;
        else if (format != other.format)
            return false;
        else if (usages != other.usages)
            return false;
        else if (layers != other.layers)
            return false;
        else if (cull_imune != other.cull_imune)
            return false;
        return true;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto ImageReadDescription::operator==(const ImageReadDescription& other) const noexcept -> bool {
        if (image != other.image) return false;
        else if (type != other.type)
            return false;
        else if (cull_imune != other.cull_imune)
            return false;
        return true;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto ImageWriteDescription::operator==(const ImageWriteDescription& other) const noexcept -> bool {
        if (image != other.image) return false;
        else if (type != other.type)
            return false;
        else if (clear_value != other.clear_value)
            return false;
        else if (cull_imune != other.cull_imune)
            return false;
        return true;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::TaskDescription::operator==(const FrameBuilder::TaskDescription& other) const noexcept -> bool {
        if (name != other.name) return false;
        // else if (execute != other.execute)
        //     return false;
        else if (cull_imune != other.cull_imune)
            return false;
        return true;
    }
} // namespace stormkit::engine
