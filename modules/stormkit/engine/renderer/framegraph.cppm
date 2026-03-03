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
    class FrameResourcesAccessor;

    class STORMKIT_ENGINE_API FrameBuilder {
      public:
        static constexpr struct Root {
        } ROOT;

        class FrameTaskBuilder;

        using SetupClosure   = FunctionRef<void(FrameTaskBuilder&)>;
        using ExecuteClosure = std::function<void(const FrameResourcesAccessor&, gpu::CommandBuffer&)>;

        using ResourceID = std::bitset<32>;
        using TaskID     = std::bitset<32>;
        using CombinedID = std::bitset<64>;

        struct Resource {
            using Data = std::variant<std::monostate,
                                      gpu::Image::CreateInfo,
                                      gpu::Buffer::CreateInfo,
                                      Ref<const gpu::Image>,
                                      Ref<const gpu::Buffer>>;

            std::string name;
            hash32      name_hash;
            ResourceID  id;

            Data data = std::monostate {};

            TaskID attached_in = {};
            TaskID read_by     = {};
            TaskID wrote_by    = {};
        };

        struct Task {
            std::string name;
            hash32      name_hash;
            TaskID      id;

            enum class Type {
                RASTER,
                COMPUTE,
                TRANSFER,
                RAYTRACING,
            } type;

            ResourceID attachments = {};
            ResourceID reads       = {};
            ResourceID writes      = {};

            ExecuteClosure execute;

            bool root = false;
        };

        using ResourceNode = std::pair<ResourceID, Resource>;
        using Resources    = std::vector<ResourceNode>;
        using TaskNode     = std::pair<TaskID, Task>;
        using Tasks        = std::vector<TaskNode>;

        class FrameTaskBuilder {
          public:
            auto create_buffer(std::string name, gpu::Buffer::CreateInfo create_info) noexcept -> ResourceID;

            auto read_buffer(ResourceID) noexcept -> void;
            auto write_buffer(ResourceID) noexcept -> void;

            auto create_image(std::string name, gpu::Image::CreateInfo create_info) noexcept -> ResourceID;

            auto read_image(ResourceID) noexcept -> void;
            auto write_image(ResourceID) noexcept -> void;

            auto read_attachment(ResourceID) noexcept -> void;
            auto write_attachment(ResourceID, std::optional<gpu::ClearValue> clear_value = std::nullopt) noexcept -> void;

          private:
            FrameTaskBuilder(Task&, ResourceID&, TaskID&, Resources&, Tasks&) noexcept;

            Task& m_task;

            ResourceID& m_next_resource_id;
            TaskID&     m_next_task_id;

            Resources& m_resources;
            Tasks&     m_tasks;

            friend class FrameBuilder;
        };

        FrameBuilder() noexcept;
        ~FrameBuilder() noexcept;

        FrameBuilder(FrameBuilder&&) noexcept;
        FrameBuilder(const FrameBuilder&) noexcept = delete;

        auto operator=(FrameBuilder&&) noexcept -> FrameBuilder&;
        auto operator=(const FrameBuilder&) noexcept -> FrameBuilder& = delete;

        auto add_raster_task(std::string         name,
                             SetupClosure        setup,
                             ExecuteClosure      execute,
                             std::optional<Root> root = std::nullopt) noexcept -> TaskID;
        auto add_transfer_task(std::string         name,
                               SetupClosure        setup,
                               ExecuteClosure      execute,
                               std::optional<Root> root = std::nullopt) noexcept -> TaskID;
        auto add_compute_task(std::string         name,
                              SetupClosure        setup,
                              ExecuteClosure      execute,
                              std::optional<Root> root = std::nullopt) noexcept -> TaskID;
        auto add_raytracing_task(std::string         name,
                                 SetupClosure        setup,
                                 ExecuteClosure      execute,
                                 std::optional<Root> root = std::nullopt) noexcept -> TaskID;

        [[nodiscard]]
        auto retain_buffer(std::string name, const gpu::Buffer& buffer) noexcept -> ResourceID;
        [[nodiscard]]
        auto retain_image(std::string name, const gpu::Image& image) noexcept -> ResourceID;

        [[nodiscard]]
        auto has_backbuffer() noexcept -> bool;
        [[nodiscard]]
        auto backbuffer() noexcept -> ResourceID;
        auto set_backbuffer(ResourceID id) noexcept -> void;

        [[nodiscard]]
        auto dump() const noexcept -> std::string;

        [[nodiscard]]
        auto tasks() const noexcept -> const Tasks&;
        [[nodiscard]]
        auto resources() const noexcept -> const Resources&;

      private:
        [[nodiscard]]
        auto add_task(std::string&&, Task::Type, SetupClosure&&, ExecuteClosure&&, std::optional<Root>) noexcept -> TaskID;

        ResourceID m_next_resource_id = { 1 };
        TaskID     m_next_task_id     = { 1 };

        Resources m_resources = {};
        Tasks     m_tasks     = {};

        std::optional<ResourceID> m_backbuffer_id = std::nullopt;
    };

    class FrameResourcesAccessor {
      public:
        using Images     = std::vector<std::pair<engine::FrameBuilder::ResourceID, Ref<const gpu::Image>>>;
        using ImageViews = std::vector<std::pair<engine::FrameBuilder::CombinedID, gpu::ImageView>>;
        using Buffers    = std::vector<std::pair<engine::FrameBuilder::ResourceID, Ref<const gpu::Buffer>>>;

        FrameResourcesAccessor(const Images& images, const ImageViews& image_views, const Buffers& buffers) noexcept;
        ~FrameResourcesAccessor() noexcept;

        FrameResourcesAccessor(const FrameResourcesAccessor&)                    = delete;
        auto operator=(const FrameResourcesAccessor&) -> FrameResourcesAccessor& = delete;

        FrameResourcesAccessor(FrameResourcesAccessor&&) noexcept                    = delete;
        auto operator=(FrameResourcesAccessor&&) noexcept -> FrameResourcesAccessor& = delete;

        auto get_image(const FrameBuilder::ResourceID& id) const noexcept -> const gpu::Image&;
        auto get_image_view(const FrameBuilder::CombinedID& id) const noexcept -> const gpu::ImageView&;
        auto get_buffer(const FrameBuilder::ResourceID& id) const noexcept -> const gpu::Buffer&;

      private:
        const Images&     m_images;
        const Buffers&    m_buffers;
        const ImageViews& m_image_views;
    };

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
    inline FrameBuilder::FrameTaskBuilder::FrameTaskBuilder(Task&       task,
                                                            ResourceID& next_resource_id,
                                                            TaskID&     next_task_id,
                                                            Resources&  resources,
                                                            Tasks&      tasks) noexcept
        : m_task { task },
          m_next_resource_id { next_resource_id },
          m_next_task_id { next_task_id },
          m_resources { resources },
          m_tasks { tasks } {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_buffer(ResourceID buffer_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [buffer_id](const auto& node) noexcept { return node.first == buffer_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Buffer::CreateInfo>(node.data) or is<Ref<const gpu::Buffer>>(node.data));

        m_task.reads |= buffer_id;
        node.read_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_buffer(ResourceID buffer_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [buffer_id](const auto& node) noexcept { return node.first == buffer_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Buffer::CreateInfo>(node.data) or is<Ref<const gpu::Buffer>>(node.data));

        m_task.writes |= buffer_id;
        node.wrote_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_image(ResourceID image_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [image_id](const auto& node) noexcept { return node.first == image_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<const gpu::Image>>(node.data));

        m_task.reads |= image_id;
        node.read_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_image(ResourceID image_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [image_id](const auto& node) noexcept { return node.first == image_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<const gpu::Image>>(node.data));

        m_task.writes |= image_id;
        node.wrote_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_attachment(ResourceID image_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [image_id](const auto& node) noexcept { return node.first == image_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<const gpu::Image>>(node.data));

        m_task.attachments |= image_id;
        m_task.reads |= image_id;
        node.read_by |= m_task.id;
        node.attached_in |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_attachment(ResourceID                     image_id,
                                                                 std::optional<gpu::ClearValue> clear_value) noexcept -> void {
        auto it = stdr::find_if(m_resources, [image_id](const auto& node) noexcept { return node.first == image_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<const gpu::Image>>(node.data));

        m_task.attachments |= image_id;
        m_task.writes |= image_id;
        node.wrote_by |= m_task.id;
        node.attached_in |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameBuilder::FrameBuilder() noexcept = default;

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
                                              std::optional<Root>          root) noexcept -> TaskID {
        return add_task(std::move(name), Task::Type::RASTER, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_transfer_task(std::string                  name,
                                                FrameBuilder::SetupClosure   setup,
                                                FrameBuilder::ExecuteClosure execute,
                                                std::optional<Root>          root) noexcept -> TaskID {
        return add_task(std::move(name), Task::Type::TRANSFER, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_compute_task(std::string                  name,
                                               FrameBuilder::SetupClosure   setup,
                                               FrameBuilder::ExecuteClosure execute,
                                               std::optional<Root>          root) noexcept -> TaskID {
        return add_task(std::move(name), Task::Type::COMPUTE, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_raytracing_task(std::string                  name,
                                                  FrameBuilder::SetupClosure   setup,
                                                  FrameBuilder::ExecuteClosure execute,
                                                  std::optional<Root>          root) noexcept -> TaskID {
        return add_task(std::move(name), Task::Type::RAYTRACING, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::has_backbuffer() noexcept -> bool {
        return m_backbuffer_id != std::nullopt;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::backbuffer() noexcept -> ResourceID {
        return *m_backbuffer_id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::set_backbuffer(ResourceID id) noexcept -> void {
        m_backbuffer_id = id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::tasks() const noexcept -> const Tasks& {
        return m_tasks;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::resources() const noexcept -> const Resources& {
        return m_resources;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameResourcesAccessor::FrameResourcesAccessor(const Images&     images,
                                                          const ImageViews& image_views,
                                                          const Buffers&    buffers) noexcept
        : m_images { images }, m_image_views { image_views }, m_buffers { buffers } {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameResourcesAccessor::~FrameResourcesAccessor() noexcept = default;

    // /////////////////////////////////////
    // /////////////////////////////////////
    // STORMKIT_FORCE_INLINE
    // inline FrameResourcesAccessor::FrameResourcesAccessor(FrameResourcesAccessor&&) noexcept = default;

    // /////////////////////////////////////
    // /////////////////////////////////////
    // STORMKIT_FORCE_INLINE
    // inline auto FrameResourcesAccessor::operator=(FrameResourcesAccessor&&) noexcept -> FrameResourcesAccessor& = default;

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameResourcesAccessor::get_image(const FrameBuilder::ResourceID& id) const noexcept -> const gpu::Image& {
        const auto it = stdr::find_if(m_images, [&id](const auto& pair) noexcept { return pair.first == id; });
        ENSURES(it != stdr::cend(m_images));

        return *it->second;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameResourcesAccessor::get_image_view(const FrameBuilder::CombinedID& id) const noexcept
      -> const gpu::ImageView& {
        const auto it = stdr::find_if(m_image_views, [&id](const auto& pair) noexcept { return pair.first == id; });
        ENSURES(it != stdr::cend(m_image_views));

        return it->second;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameResourcesAccessor::get_buffer(const FrameBuilder::ResourceID& id) const noexcept -> const gpu::Buffer& {
        const auto it = stdr::find_if(m_buffers, [&id](const auto& pair) noexcept { return pair.first == id; });
        ENSURES(it != stdr::cend(m_buffers));

        return *it->second;
    }

} // namespace stormkit::engine
