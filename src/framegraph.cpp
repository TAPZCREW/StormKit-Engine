module;

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

// #define DEBUG_DAG

module stormkit.engine;

import std;

import stormkit;

import :renderer.framegraph;

namespace stormkit::engine {
    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::FrameTaskBuilder::create_buffer(std::string name, gpu::Buffer::CreateInfo create_info) noexcept
      -> ResourceID {
        const auto name_hash = hash(name);
        expects(not stdr::any_of(m_resources,
                                 [name_hash](const auto& node) noexcept { return node.second.name_hash == name_hash; }),
                std::format("resource {} already present in graph", name));

        const auto& node = m_resources.emplace_back(m_next_resource_id,
                                                    FrameBuilder::Resource {
                                                      .name      = std::move(name),
                                                      .name_hash = name_hash,
                                                      .id        = m_next_resource_id,
                                                      .data      = std::move(create_info),
                                                    });
        if (m_next_resource_id.none()) m_next_resource_id |= 0b1;
        else
            m_next_resource_id <<= 1;
        ensures(not m_next_resource_id.none(), "resource id overflow");

        return node.first;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::FrameTaskBuilder::create_image(std::string name, gpu::Image::CreateInfo create_info) noexcept
      -> ResourceID {
        const auto name_hash = hash(name);
        expects(not stdr::any_of(m_resources,
                                 [name_hash](const auto& node) noexcept { return node.second.name_hash == name_hash; }),
                std::format("resource {} already present in graph", name));

        const auto& node = m_resources.emplace_back(m_next_resource_id,
                                                    FrameBuilder::Resource {
                                                      .name      = std::move(name),
                                                      .name_hash = name_hash,
                                                      .id        = m_next_resource_id,
                                                      .data      = std::move(create_info),
                                                    });
        if (m_next_resource_id.none()) m_next_resource_id |= 0b1;
        else
            m_next_resource_id <<= 1;
        ensures(not m_next_resource_id.none(), "resource id overflow");

        return node.first;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::retain_image(std::string name, const gpu::Image& image) noexcept -> ResourceID {
        const auto name_hash = hash(name);
        expects(not stdr::any_of(m_resources,
                                 [name_hash](const auto& node) noexcept { return node.second.name_hash == name_hash; }),
                std::format("resource {} already present in graph", name));

        const auto& node = m_resources.emplace_back(m_next_resource_id,
                                                    FrameBuilder::Resource {
                                                      .name      = std::move(name),
                                                      .name_hash = name_hash,
                                                      .id        = m_next_resource_id,
                                                      .data      = as_ref(image),
                                                    });
        if (m_next_resource_id.none()) m_next_resource_id |= 0b1;
        else
            m_next_resource_id <<= 1;
        ensures(not m_next_resource_id.none(), "resource id overflow");

        return node.first;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::retain_buffer(std::string name, const gpu::Buffer& buffer) noexcept -> ResourceID {
        const auto name_hash = hash(name);
        expects(not stdr::any_of(m_resources,
                                 [name_hash](const auto& node) noexcept { return node.second.name_hash == name_hash; }),
                std::format("resource {} already present in graph", name));

        const auto& node = m_resources.emplace_back(m_next_resource_id,
                                                    FrameBuilder::Resource {
                                                      .name      = std::move(name),
                                                      .name_hash = name_hash,
                                                      .id        = m_next_resource_id,
                                                      .data      = as_ref(buffer),
                                                    });
        if (m_next_resource_id.none()) m_next_resource_id |= 0b1;
        else
            m_next_resource_id <<= 1;
        ensures(not m_next_resource_id.none(), "resource id overflow {}");

        return node.first;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::add_task(std::string&&                  name,
                                Task::Type                     type,
                                FrameBuilder::SetupClosure&&   setup,
                                FrameBuilder::ExecuteClosure&& execute,
                                std::optional<Root>            root) noexcept -> TaskID {
        const auto name_hash = hash(name);
        expects(not stdr::any_of(m_tasks, [name_hash](const auto& node) noexcept { return node.second.name_hash == name_hash; }),
                std::format("task {} already present in graph", name));

        auto& node = m_tasks.emplace_back(m_next_task_id,
                                          FrameBuilder::Task {
                                            .name      = std::move(name),
                                            .name_hash = name_hash,
                                            .id        = m_next_task_id,
                                            .type      = type,
                                            .execute   = std::move(execute),
                                            .root      = root != std::nullopt,
                                          });
        if (m_next_task_id.none()) m_next_task_id |= 0b1;
        else
            m_next_task_id <<= 1;
        ensures(not m_next_task_id.none(), "task id overflow");

        auto builder = FrameTaskBuilder { node.second, m_next_resource_id, m_next_task_id, m_resources, m_tasks };
        std::invoke(setup, builder);

        return node.first;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::dump() const noexcept -> std::string {
        struct Vertex {
            std::string format_value;
            std::string color;
        };

        auto dag = DAG<Vertex> { stdr::size(m_resources) + stdr::size(m_tasks) };

        constexpr auto get_task_color = [](Task::Type type) static noexcept {
            switch (type) {
                case Task::Type::RASTER: return "white";
                case Task::Type::COMPUTE: return "orange";
                case Task::Type::TRANSFER: return "cyan";
                case Task::Type::RAYTRACING: return "yellow";
                default: break;
            }

            std::unreachable();
        };

        constexpr auto get_resource_color = [](const Resource& resource) static noexcept {
            EXPECTS(not is<std::monostate>(resource.data));

            if (is<gpu::Image::CreateInfo>(resource.data)) return "green";
            else if (is<gpu::Image::CreateInfo>(resource.data))
                return "red";
            else if (is<Ref<const gpu::Image>>(resource.data))
                return "olive";
            else if (is<Ref<const gpu::Buffer>>(resource.data))
                return "pink";

            std::unreachable();
        };

        auto map = std::vector<std::pair<TaskID, dag::VertexID>> {};
        map.reserve(stdr::size(m_tasks));
        for (const auto& [_, task] : m_tasks) {
            auto vertex_id = dag.add_vertex({ .format_value = std::format("{} root: {} id: 0b{}",
                                                                          task.name,
                                                                          task.root,
                                                                          task.id.to_string()),
                                              .color        = get_task_color(task.type) });
            map.emplace_back(task.id, vertex_id);
        }

        for (const auto& [_, resource] : m_resources) {
            auto vertex_id = dag.add_vertex({ .format_value = std::format("{} id: 0b{}", resource.name, resource.id.to_string()),
                                              .color        = get_resource_color(resource) });

            for (const auto& [task_id, task_vertex_id] : map) {
                if ((resource.wrote_by & task_id) == task_id) dag.add_edge(task_vertex_id, vertex_id);
                else if ((resource.read_by & task_id) == task_id)
                    dag.add_edge(vertex_id, task_vertex_id);
            }
        }

        return dag.dump({ .colorize     = [](const auto& value) static noexcept { return value.color; },
                          .format_value = [](const auto& value) static noexcept { return value.format_value; } });
    }
} // namespace stormkit::engine
