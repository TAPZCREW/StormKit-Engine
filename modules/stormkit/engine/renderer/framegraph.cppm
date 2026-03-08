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

        template<typename TaskData>
        using SetupClosure = FunctionRef<void(FrameTaskBuilder&, TaskData&)>;
        template<typename TaskData>
        using ExecuteClosure = std::function<void(FrameResourcesAccessor&, gpu::CommandBuffer&, const TaskData&)>;

        using RawExecuteClosure = std::function<void(FrameResourcesAccessor&, gpu::CommandBuffer&, std::span<const std::byte>)>;

        using ResourceID = std::bitset<32>;
        using TaskID     = std::bitset<32>;
        using CombinedID = std::bitset<64>;

        struct Resource {
            using Data = std::
              variant<std::monostate, gpu::Image::CreateInfo, gpu::Buffer::CreateInfo, Ref<gpu::Image>, Ref<gpu::Buffer>>;

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

            RawExecuteClosure execute;

            std::vector<std::byte> data = {};

            std::vector<std::pair<ResourceID, gpu::ClearValue>> clear_values;

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
            FrameTaskBuilder(Task&, ResourceID&, Resources&, Tasks&) noexcept;

            Task& m_task;

            ResourceID& m_next_resource_id;

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

        template<typename TaskData>
        auto add_raster_task(std::string              name,
                             SetupClosure<TaskData>   setup,
                             ExecuteClosure<TaskData> execute,
                             std::optional<Root>      root = std::nullopt) noexcept -> std::pair<TaskID, Ref<const TaskData>>;
        template<typename TaskData>
        auto add_transfer_task(std::string              name,
                               SetupClosure<TaskData>   setup,
                               ExecuteClosure<TaskData> execute,
                               std::optional<Root>      root = std::nullopt) noexcept -> std::pair<TaskID, Ref<const TaskData>>;
        template<typename TaskData>
        auto add_compute_task(std::string              name,
                              SetupClosure<TaskData>   setup,
                              ExecuteClosure<TaskData> execute,
                              std::optional<Root>      root = std::nullopt) noexcept -> std::pair<TaskID, Ref<const TaskData>>;
        template<typename TaskData>
        auto add_raytracing_task(std::string              name,
                                 SetupClosure<TaskData>   setup,
                                 ExecuteClosure<TaskData> execute,
                                 std::optional<Root>      root = std::nullopt) noexcept -> std::pair<TaskID, Ref<const TaskData>>;

        [[nodiscard]]
        auto retain_buffer(std::string name, gpu::Buffer& buffer) noexcept -> ResourceID;
        [[nodiscard]]
        auto retain_image(std::string name, gpu::Image& image) noexcept -> ResourceID;

        [[nodiscard]]
        auto has_backbuffer() const noexcept -> bool;
        [[nodiscard]]
        auto backbuffer() const noexcept -> ResourceID;
        auto set_backbuffer(ResourceID id) noexcept -> void;

        [[nodiscard]]
        auto dump() const noexcept -> std::string;

        [[nodiscard]]
        auto tasks() const noexcept -> const Tasks&;
        [[nodiscard]]
        auto resources() const noexcept -> const Resources&;

      private:
        template<typename TaskData>
        [[nodiscard]]
        auto add_task(std::string&&,
                      Task::Type,
                      SetupClosure<TaskData>&&,
                      ExecuteClosure<TaskData>&&,
                      std::optional<Root>) noexcept -> std::pair<TaskID, Ref<const TaskData>>;
        [[nodiscard]]
        auto do_add_task(std::string&&, Task::Type, RawExecuteClosure&&, std::optional<Root>) noexcept -> Task&;

        ResourceID m_next_resource_id = { 1 };
        TaskID     m_next_task_id     = { 1 };

        Resources m_resources = {};
        Tasks     m_tasks     = {};

        std::optional<ResourceID> m_backbuffer_id = std::nullopt;
    };

    class FrameResourcesAccessor {
      public:
        using Images     = std::vector<std::pair<engine::FrameBuilder::ResourceID, Ref<gpu::Image>>>;
        using ImageViews = std::span<std::pair<engine::FrameBuilder::CombinedID, gpu::ImageView>>;
        using Buffers    = std::vector<std::pair<engine::FrameBuilder::ResourceID, Ref<gpu::Buffer>>>;

        FrameResourcesAccessor(const Images& images, ImageViews&& image_views, const Buffers& buffers) noexcept;
        ~FrameResourcesAccessor() noexcept;

        FrameResourcesAccessor(const FrameResourcesAccessor&)                    = delete;
        auto operator=(const FrameResourcesAccessor&) -> FrameResourcesAccessor& = delete;

        FrameResourcesAccessor(FrameResourcesAccessor&&) noexcept                    = delete;
        auto operator=(FrameResourcesAccessor&&) noexcept -> FrameResourcesAccessor& = delete;

        template<typename Self>
        auto get_image(this Self& self, const FrameBuilder::ResourceID& id) noexcept -> meta::ForwardConst<Self, gpu::Image>&;
        template<typename Self>
        auto get_image_view(this Self& self, const FrameBuilder::CombinedID& id) noexcept
          -> meta::ForwardConst<Self, gpu::ImageView>&;
        template<typename Self>
        auto get_buffer(this Self& self, const FrameBuilder::ResourceID& id) noexcept -> meta::ForwardConst<Self, gpu::Buffer>&;

        // auto get_image(const FrameBuilder::ResourceID& id) noexcept -> gpu::Image&;
        // auto get_image_view(const FrameBuilder::CombinedID& id) noexcept -> gpu::ImageView&;
        // auto get_buffer(const FrameBuilder::ResourceID& id) noexcept -> gpu::Buffer&;
        // auto get_image(const FrameBuilder::ResourceID& id) const noexcept -> const gpu::Image&;
        // auto get_image_view(const FrameBuilder::CombinedID& id) const noexcept -> const gpu::ImageView&;
        // auto get_buffer(const FrameBuilder::ResourceID& id) const noexcept -> const gpu::Buffer&;

      private:
        const Images&    m_images;
        const Buffers&   m_buffers;
        const ImageViews m_image_views;
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
                                                            Resources&  resources,
                                                            Tasks&      tasks) noexcept
        : m_task { task }, m_next_resource_id { next_resource_id }, m_resources { resources }, m_tasks { tasks } {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_buffer(ResourceID buffer_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [buffer_id](const auto& node) noexcept { return node.first == buffer_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Buffer::CreateInfo>(node.data) or is<Ref<gpu::Buffer>>(node.data));

        m_task.reads |= buffer_id;
        node.read_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_buffer(ResourceID buffer_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [buffer_id](const auto& node) noexcept { return node.first == buffer_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Buffer::CreateInfo>(node.data) or is<Ref<gpu::Buffer>>(node.data));

        m_task.writes |= buffer_id;
        node.wrote_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_image(ResourceID image_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [image_id](const auto& node) noexcept { return node.first == image_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<gpu::Image>>(node.data));

        m_task.reads |= image_id;
        node.read_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::write_image(ResourceID image_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [image_id](const auto& node) noexcept { return node.first == image_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<gpu::Image>>(node.data));

        m_task.writes |= image_id;
        node.wrote_by |= m_task.id;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    inline auto FrameBuilder::FrameTaskBuilder::read_attachment(ResourceID image_id) noexcept -> void {
        auto it = stdr::find_if(m_resources, [image_id](const auto& node) noexcept { return node.first == image_id; });
        EXPECTS(it != stdr::cend(m_resources));

        auto& [_, node] = *it;

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<gpu::Image>>(node.data));

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

        EXPECTS(is<gpu::Image::CreateInfo>(node.data) or is<Ref<gpu::Image>>(node.data));

        m_task.attachments |= image_id;
        m_task.writes |= image_id;
        node.wrote_by |= m_task.id;
        node.attached_in |= m_task.id;

        m_task.clear_values.emplace_back(image_id, std::move(*clear_value));
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
    template<typename TaskData>
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_raster_task(std::string              name,
                                              SetupClosure<TaskData>   setup,
                                              ExecuteClosure<TaskData> execute,
                                              std::optional<Root>      root) noexcept -> std::pair<TaskID, Ref<const TaskData>> {
        return add_task<TaskData>(std::move(name), Task::Type::RASTER, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<typename TaskData>
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_transfer_task(std::string              name,
                                                SetupClosure<TaskData>   setup,
                                                ExecuteClosure<TaskData> execute,
                                                std::optional<Root> root) noexcept -> std::pair<TaskID, Ref<const TaskData>> {
        return add_task<TaskData>(std::move(name), Task::Type::TRANSFER, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<typename TaskData>
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_compute_task(std::string              name,
                                               SetupClosure<TaskData>   setup,
                                               ExecuteClosure<TaskData> execute,
                                               std::optional<Root>      root) noexcept -> std::pair<TaskID, Ref<const TaskData>> {
        return add_task<TaskData>(std::move(name), Task::Type::COMPUTE, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<typename TaskData>
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_raytracing_task(std::string              name,
                                                  SetupClosure<TaskData>   setup,
                                                  ExecuteClosure<TaskData> execute,
                                                  std::optional<Root> root) noexcept -> std::pair<TaskID, Ref<const TaskData>> {
        return add_task<TaskData>(std::move(name), Task::Type::RAYTRACING, std::move(setup), std::move(execute), std::move(root));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::has_backbuffer() const noexcept -> bool {
        return m_backbuffer_id != std::nullopt;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::backbuffer() const noexcept -> ResourceID {
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
    template<typename TaskData>
    STORMKIT_FORCE_INLINE
    inline auto FrameBuilder::add_task(std::string&&              name,
                                       Task::Type                 type,
                                       SetupClosure<TaskData>&&   setup,
                                       ExecuteClosure<TaskData>&& execute,
                                       std::optional<Root>        root) noexcept -> std::pair<TaskID, Ref<const TaskData>> {
        auto& task = do_add_task(
          std::move(name),
          type,
          [execute = std::move(execute)](auto& accessor, auto& cmb, auto bytes) noexcept {
              execute(accessor, cmb, *std::bit_cast<const TaskData*>(stdr::data(bytes)));
          },
          std::move(root));

        task.data.resize(sizeof(TaskData));
        auto& task_data = *(new (stdr::data(task.data)) TaskData {});

        auto builder = FrameTaskBuilder { task, m_next_resource_id, m_resources, m_tasks };
        std::invoke(setup, builder, task_data);

        return std::make_pair(task.id, as_ref(task_data));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline FrameResourcesAccessor::FrameResourcesAccessor(const Images&  images,
                                                          ImageViews&&   image_views,
                                                          const Buffers& buffers) noexcept
        : m_images { images }, m_buffers { buffers }, m_image_views { std::move(image_views) } {
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
    template<typename Self>
    inline auto FrameResourcesAccessor::get_image(this Self& self, const FrameBuilder::ResourceID& id) noexcept
      -> meta::ForwardConst<Self, gpu::Image>& {
        const auto it = stdr::find_if(self.m_images, [&id](const auto& pair) noexcept { return pair.first == id; });
        ENSURES(it != stdr::cend(self.m_images));

        return std::forward_like<Self&>(*it->second);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<typename Self>
    inline auto FrameResourcesAccessor::get_image_view(this Self& self, const FrameBuilder::CombinedID& id) noexcept
      -> meta::ForwardConst<Self, gpu::ImageView>& {
        const auto it = stdr::find_if(self.m_image_views, [&id](const auto& pair) noexcept { return pair.first == id; });
        ENSURES(it != stdr::cend(self.m_image_views));

        return std::forward_like<Self&>(it->second);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    template<typename Self>
    inline auto FrameResourcesAccessor::get_buffer(this Self& self, const FrameBuilder::ResourceID& id) noexcept
      -> meta::ForwardConst<Self, gpu::Buffer>& {
        const auto it = stdr::find_if(self.m_buffers, [&id](const auto& pair) noexcept { return pair.first == id; });
        ENSURES(it != stdr::cend(self.m_buffers));

        return std::forward_like<Self&>(*it->second);
    }

    // /////////////////////////////////////
    // /////////////////////////////////////
    // inline auto FrameResourcesAccessor::get_image(const FrameBuilder::ResourceID& id) noexcept -> gpu::Image& {
    //     const auto it = stdr::find_if(m_images, [&id](const auto& pair) noexcept { return pair.first == id; });
    //     ENSURES(it != stdr::cend(m_images));

    //    return *it->second;
    // }

    // /////////////////////////////////////
    // /////////////////////////////////////
    // inline auto FrameResourcesAccessor::get_image_view(const FrameBuilder::CombinedID& id) noexcept -> gpu::ImageView& {
    //     const auto it = stdr::find_if(m_image_views, [&id](const auto& pair) noexcept { return pair.first == id; });
    //     ENSURES(it != stdr::cend(m_image_views));

    //    return it->second;
    // }

    // /////////////////////////////////////
    // /////////////////////////////////////
    // inline auto FrameResourcesAccessor::get_buffer(const FrameBuilder::ResourceID& id) noexcept -> gpu::Buffer& {
    //     const auto it = stdr::find_if(m_buffers, [&id](const auto& pair) noexcept { return pair.first == id; });
    //     ENSURES(it != stdr::cend(m_buffers));

    //    return *it->second;
    // }

} // namespace stormkit::engine
