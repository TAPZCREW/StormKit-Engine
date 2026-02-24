module;

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:renderer.framegraph;

import std;

import stormkit;

import :renderer.render_surface;

export namespace stormkit::engine {
    using GraphID                    = dag::VertexID;
    inline constexpr auto INVALID_ID = std::numeric_limits<GraphID>::max();

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
                hash32       name;
                hash32       hash;
                Ref<const T> value;
            };

            std::vector<gpu::Image>     images;
            std::vector<gpu::ImageView> views;
            std::vector<gpu::Buffer>    buffers;

            std::vector<Map<gpu::Image>>     image_mapper;
            std::vector<Map<gpu::ImageView>> view_mapper;
            std::vector<Map<gpu::Buffer>>    buffer_mapper;

            [[nodiscard]]
            auto get_buffer(std::string_view name) noexcept -> const gpu::Buffer&;

            [[nodiscard]]
            auto get_image(std::string_view name) noexcept -> const gpu::Image&;

            [[nodiscard]]
            auto get_image_view(std::string_view name) noexcept -> const gpu::ImageView&;
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

    // struct Attachment {
    //     struct Resolve {
    //         Ref<const gpu::ImageView> image_view;
    //         ResolveModeFlag           mode;
    //         gpu::ImageLayout          layout = ImageLayout::ATTACHMENT_OPTIMAL;
    //     };

    //    Ref<const gpu::ImageView> image_view;
    //    gpu::ImageLayout          layout = ImageLayout::ATTACHMENT_OPTIMAL;

    //    std::optional<Resolve> resolve = std::nullopt;

    //    AttachmentLoadOperation  load_op  = AttachmentLoadOperation::CLEAR;
    //    AttachmentStoreOperation store_op = AttachmentStoreOperation::STORE;

    //    std::optional<ClearValue> clear_value = std::nullopt;
    // };

    // math::irect render_area;
    // u32         layer_count = 1u;
    // u32         view_mask   = 0u;

    // std::vector<Attachment>   color_attachments;
    // std::optional<Attachment> depth_attachment   = std::nullopt;
    // std::optional<Attachment> stencil_attachment = std::nullopt;

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

        auto create_or_reuse_image(const gpu::Device& device, const gpu::Image::CreateInfo& create_info) noexcept
          -> gpu::Expected<gpu::Image>;
        auto create_or_reuse_buffer(const gpu::Device& device, const gpu::Buffer::CreateInfo& create_info) noexcept
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

    class STORMKIT_ENGINE_API FrameBuilder {
      public:
        class FrameTaskBuilder;

        using SetupClosure   = FunctionRef<void(FrameTaskBuilder&)>;
        using ExecuteClosure = std::function<void(Frame::FrameResources&, gpu::CommandBuffer&)>;

        struct Task {
            enum class Type {
                RASTER,
                COMPUTE,
                TRANSFER,
                RAYTRACING,
            } type;

            std::string name;

            ExecuteClosure execute;

            bool cull_imune = false;
        };

        struct CreateImage {
            std::string name;

            gpu::Image::CreateInfo create_info;
        };

        struct CreateBuffer {
            std::string name;

            gpu::Buffer::CreateInfo create_info;
        };

        struct RetainedImage {
            std::string name;

            Ref<const gpu::Image> image;
        };

        struct RetainedBuffer {
            std::string name;

            Ref<const gpu::Buffer> buffer;
        };

        struct BufferAccess {
            GraphID id;

            ioffset              offset = 0;
            std::optional<usize> size;
        };

        struct ImageAccess {
            GraphID id;
        };

        struct AttachmentAccess {
            GraphID id;

            std::optional<gpu::ClearValue> clear_value;
        };

        using Node = std::
          variant<CreateBuffer, CreateImage, RetainedBuffer, RetainedImage, BufferAccess, ImageAccess, AttachmentAccess, Task>;
        using DAG = core::DAG<Node>;

        class FrameTaskBuilder {
          public:
            auto create_buffer(std::string name, gpu::Buffer::CreateInfo create_info, bool cull_imune = false) noexcept
              -> GraphID;

            auto read_buffer(GraphID) noexcept -> void;
            auto write_buffer(GraphID) noexcept -> void;

            auto create_image(std::string name, gpu::Image::CreateInfo create_info, bool cull_imune = false) noexcept -> GraphID;

            auto read_image(GraphID) noexcept -> void;
            auto write_image(GraphID) noexcept -> void;

            auto read_attachment(GraphID) noexcept -> void;
            auto write_attachment(GraphID, std::optional<gpu::ClearValue> clear_value = std::nullopt) noexcept -> void;

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
        auto add_transfer_task(std::string name, SetupClosure setup, ExecuteClosure execute, bool imune_to_cull = false) noexcept
          -> GraphID;
        auto add_compute_task(std::string name, SetupClosure setup, ExecuteClosure execute, bool imune_to_cull = false) noexcept
          -> GraphID;
        auto add_raytracing_task(std::string    name,
                                 SetupClosure   setup,
                                 ExecuteClosure execute,
                                 bool           imune_to_cull = false) noexcept -> GraphID;

        auto retain_buffer(std::string name, const gpu::Buffer& buffer) noexcept -> GraphID;
        auto retain_image(std::string name, const gpu::Image& image) noexcept -> GraphID;

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
        auto add_task(std::string&&, Task::Type, SetupClosure, ExecuteClosure&&, bool) noexcept -> GraphID;

        DAG                                 m_dag;
        std::optional<std::vector<GraphID>> m_baked_graph;
        std::optional<std::vector<GraphID>> m_reversed_baked_graph;
        hash32                              m_hash = 0_u32;

        GraphID m_backbuffer = INVALID_ID;
    };

    auto operator==(const FrameBuilder::Node& first, const FrameBuilder::Node& second) noexcept -> bool = delete;
} // namespace stormkit::engine

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace stdr = std::ranges;
namespace stdv = std::views;

namespace stormkit::engine {
    /////////////////////////////////////
    /////////////////////////////////////
    inline auto Frame::FrameResources::get_buffer(std::string_view name) noexcept -> const gpu::Buffer& {
        EXPECTS(not stdr::empty(name));

        auto it = stdr::find_if(buffer_mapper, [name = hash(name)](const auto& mapping) noexcept {
            return mapping.name == name;
        });
        ENSURES(it != stdr::cend(buffer_mapper));

        return *it->value;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto Frame::FrameResources::get_image(std::string_view name) noexcept -> const gpu::Image& {
        EXPECTS(not stdr::empty(name));

        auto it = stdr::find_if(image_mapper, [name = hash(name)](const auto& mapping) noexcept { return mapping.name == name; });
        ENSURES(it != stdr::cend(image_mapper));

        return *it->value;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto Frame::FrameResources::get_image_view(std::string_view name) noexcept -> const gpu::ImageView& {
        EXPECTS(not stdr::empty(name));

        auto it = stdr::find_if(view_mapper, [name = hash(name)](const auto& mapping) noexcept { return mapping.name == name; });
        ENSURES(it != stdr::cend(view_mapper));

        return *it->value;
    }

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
    inline auto FramePool::create_or_reuse_image(const gpu::Device& device, const gpu::Image::CreateInfo& create_info) noexcept
      -> gpu::Expected<gpu::Image> {
        update_pool();
        const auto des_hash = hash(create_info);

        if (auto it = stdr::find_if(m_reusable_images, [des_hash](auto& image) { return image.hash == des_hash; });
            it != stdr::cend(m_reusable_images)) {
            auto image = std::move(it->value);
            m_reusable_images.erase(it);
            return image;
        }

        Return Try(gpu::Image::create(device, create_info));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FramePool::create_or_reuse_buffer(const gpu::Device& device, const gpu::Buffer::CreateInfo& create_info) noexcept
      -> gpu::Expected<gpu::Buffer> {
        update_pool();
        const auto des_hash = hash(create_info);

        if (auto it = stdr::find_if(m_reusable_buffers, [des_hash](auto& buffer) { return buffer.hash == des_hash; });
            it != stdr::cend(m_reusable_buffers)) {
            auto buffer = std::move(it->value);
            m_reusable_buffers.erase(it);
            return buffer;
        }

        Return Try(gpu::Buffer::create(device, create_info));
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
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::FrameTaskBuilder::create_buffer(std::string             name,
                                                              gpu::Buffer::CreateInfo create_info,
                                                              bool                    cull_imune) noexcept -> GraphID {
        return m_dag.add_vertex(CreateBuffer { .name = std::move(name), .create_info = std::move(create_info) });
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_buffer(GraphID buffer_id) noexcept -> void {
        EXPECTS(m_dag.has_vertex(buffer_id));

        auto& [_, node] = m_dag.get_vertex_value(buffer_id);
        ENSURES(is<CreateBuffer>(node) or is<RetainedBuffer>(node));

        const auto buffer_access_id = m_dag.add_vertex(BufferAccess { .id = buffer_id });

        m_dag.add_edge(buffer_id, buffer_access_id);
        m_dag.add_edge(buffer_access_id, m_description_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_buffer(GraphID buffer_id) noexcept -> void {
        EXPECTS(m_dag.has_vertex(buffer_id));

        auto& [_, node] = m_dag.get_vertex_value(buffer_id);
        ENSURES(is<CreateBuffer>(node) or is<RetainedBuffer>(node));

        const auto buffer_access_id = m_dag.add_vertex(BufferAccess { .id = buffer_id });

        m_dag.add_edge(buffer_id, buffer_access_id);
        m_dag.add_edge(m_description_id, buffer_access_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::FrameTaskBuilder::create_image(std::string            name,
                                                             gpu::Image::CreateInfo create_info,
                                                             bool                   cull_imune) noexcept -> GraphID {
        return m_dag.add_vertex(CreateImage { .name = std::move(name), .create_info = std::move(create_info) });
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_image(GraphID image_id) noexcept -> void {
        EXPECTS(m_dag.has_vertex(image_id));

        auto& [_, node] = m_dag.get_vertex_value(image_id);
        ENSURES(is<CreateImage>(node) or is<RetainedImage>(node));

        const auto image_access_id = m_dag.add_vertex(ImageAccess { .id = image_id });

        m_dag.add_edge(image_id, image_access_id);
        m_dag.add_edge(image_access_id, m_description_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_image(GraphID image_id) noexcept -> void {
        EXPECTS(m_dag.has_vertex(image_id));

        auto& [_, node] = m_dag.get_vertex_value(image_id);
        ENSURES(is<CreateImage>(node) or is<RetainedImage>(node));

        const auto image_access_id = m_dag.add_vertex(ImageAccess { .id = image_id });

        m_dag.add_edge(image_id, image_access_id);
        m_dag.add_edge(m_description_id, image_access_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_attachment(GraphID image_id) noexcept -> void {
        EXPECTS(m_dag.has_vertex(image_id));

        auto& [_, node] = m_dag.get_vertex_value(image_id);
        ENSURES(is<CreateImage>(node) or is<RetainedImage>(node));

        const auto attachment_id = m_dag.add_vertex(AttachmentAccess { .id = image_id });

        m_dag.add_edge(image_id, attachment_id);
        m_dag.add_edge(attachment_id, m_description_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_attachment(GraphID                        image_id,
                                                                 std::optional<gpu::ClearValue> clear_value) noexcept -> void {
        EXPECTS(m_dag.has_vertex(image_id));

        auto& [_, node] = m_dag.get_vertex_value(image_id);
        ENSURES(is<CreateImage>(node) or is<RetainedImage>(node));

        const auto attachment_id = m_dag.add_vertex(AttachmentAccess { .id = image_id });

        m_dag.add_edge(image_id, attachment_id);
        m_dag.add_edge(m_description_id, attachment_id);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameBuilder::FrameBuilder() noexcept
        : m_dag {} {
    }

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
        return add_task(std::move(name), Task::Type::RASTER, std::move(setup), std::move(execute), imune_to_cull);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_transfer_task(std::string                  name,
                                                FrameBuilder::SetupClosure   setup,
                                                FrameBuilder::ExecuteClosure execute,
                                                bool                         imune_to_cull) noexcept -> GraphID {
        return add_task(std::move(name), Task::Type::TRANSFER, std::move(setup), std::move(execute), imune_to_cull);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_compute_task(std::string                  name,
                                               FrameBuilder::SetupClosure   setup,
                                               FrameBuilder::ExecuteClosure execute,
                                               bool                         imune_to_cull) noexcept -> GraphID {
        return add_task(std::move(name), Task::Type::COMPUTE, std::move(setup), std::move(execute), imune_to_cull);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_raytracing_task(std::string                  name,
                                                  FrameBuilder::SetupClosure   setup,
                                                  FrameBuilder::ExecuteClosure execute,
                                                  bool                         imune_to_cull) noexcept -> GraphID {
        return add_task(std::move(name), Task::Type::RAYTRACING, std::move(setup), std::move(execute), imune_to_cull);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::retain_image(std::string name, const gpu::Image& image) noexcept -> GraphID {
        return m_dag.add_vertex(RetainedImage { .name = std::move(name), .image = as_ref(image) });
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::retain_buffer(std::string name, const gpu::Buffer& buffer) noexcept -> GraphID {
        return m_dag.add_vertex(RetainedBuffer { .name = std::move(name), .buffer = as_ref(buffer) });
    }

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
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::hash() const noexcept -> hash32 {
        EXPECTS(baked());

        return m_hash;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_task(std::string&&                  name,
                                       Task::Type                     type,
                                       FrameBuilder::SetupClosure     setup,
                                       FrameBuilder::ExecuteClosure&& execute,
                                       bool                           imune_to_cull) noexcept -> GraphID {
        const auto id = m_dag.add_vertex(Task {
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
    // STORMKIT_FORCE_INLINE
    // inline auto operator==(const FrameBuilder::Task& first, const FrameBuilder::Task& second) noexcept -> bool {
    //     const auto& first_name  = std::visit([](const auto& description) static noexcept { return description.name; }, first);
    //     const auto& second_name = std::visit([](const auto& description) static noexcept { return description.name; }, second);

    //    return first_name == second_name;
    // }
} // namespace stormkit::engine
