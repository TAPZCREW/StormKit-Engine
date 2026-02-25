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
        m_next_resource_id <<= 1;
        ensures(not m_next_resource_id.none(), "resource id overflow");

        return node.first;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::add_task(std::string&&                  name,
                                Task::Type                     type,
                                FrameBuilder::SetupClosure     setup,
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
        m_next_task_id <<= 1;
        ensures(not m_next_task_id.none(), "task id overflow");

        auto builder = FrameTaskBuilder { node.second, m_next_resource_id, m_next_task_id, m_resources, m_tasks };
        std::invoke(setup, builder);

        return node.first;
    }
#ifdef DEBUG_DAG
    namespace {
        constexpr auto colorize_overloaded = Overloaded {
            [](const FrameBuilder::RetainedBuffer&) static noexcept { return "cyan"; },
            [](const FrameBuilder::RetainedImage&) static noexcept { return "orange"; },
            [](const FrameBuilder::CreateBuffer&) static noexcept { return "green"; },
            [](const FrameBuilder::CreateImage&) static noexcept { return "maroon"; },
            [](const FrameBuilder::BufferAccess&) static noexcept { return "pink"; },
            [](const FrameBuilder::ImageAccess&) static noexcept { return "red"; },
            [](const FrameBuilder::AttachmentAccess&) static noexcept { return "blue"; },
            [](const FrameBuilder::Task&) static noexcept { return "white"; },
        };

        constexpr auto colorize_visitor = [](const auto& value) static noexcept {
            return std::visit(colorize_overloaded, value);
        };

        constexpr auto colorize_visitor_reversed = [](const auto& value) static noexcept {
            return std::visit(colorize_overloaded, *value);
        };

        constexpr auto format_overloaded = Overloaded {
            [](const FrameBuilder::BufferAccess& access) static noexcept {
                return std::format("buffer access id: {}", access.id);
            },
            [](const FrameBuilder::ImageAccess& access) static noexcept { return std::format("image access id: {}", access.id); },
            [](const FrameBuilder::AttachmentAccess& access) static noexcept {
                return std::format("attachment access id: {}", access.id);
            },
            [](const auto& description) static noexcept { return std::format("{}", description.name); },
        };

        constexpr auto format_visitor = [](const auto& value) static noexcept { return std::visit(format_overloaded, value); };

        constexpr auto format_visitor_reversed = [](const auto& value) static noexcept {
            return std::visit(format_overloaded, *value);
        };

        auto print_dag(auto& dag, stdfs::path path = "./frame.dot") noexcept {
            TryAssert(io::write_text(std::move(path), dag.dump({ .colorize = colorize_visitor, .format_value = format_visitor })),
                      "Failed to write frame.dot");
        }

        auto print_dag_reversed(auto& dag) noexcept {
            TryAssert(io::write_text("./frame_reversed.dot",
                                     dag
                                       .dump({ .colorize = colorize_visitor_reversed, .format_value = format_visitor_reversed })),
                      "Failed to write frame_reversed.dot");
        }
    } // namespace
#endif

    //    /////////////////////////////////////
    //    /////////////////////////////////////
    //    auto FrameBuilder::bake() noexcept -> void {
    //        EXPECTS(m_backbuffer != INVALID_ID);
    // #ifdef DEBUG_DAG
    //        print_dag(m_dag);
    // #endif

    //    const auto reversed = m_dag.reverse_view();
    // #ifdef DEBUG_DAG
    //    print_dag_reversed(reversed);
    // #endif

    //    auto queue = std::queue<GraphID> {};
    //    queue.push(m_backbuffer);
    //    // for (auto&& [id, _] : cull_imunes) queue.push(id);

    //    auto visited = std::vector<GraphID> {};
    //    visited.reserve(stdr::size(m_dag.vertices()));

    //    while (not queue.empty()) {
    //        const auto id = queue.front();
    //        queue.pop();

    //    if (stdr::contains(visited, id)) {
    //        std::println("already visited {}", id);
    //        continue;
    //    }

    //    std::println("visit {}", id);
    //    visited.emplace_back(id);

    //    const auto childs = reversed.adjacent_edges(id);
    //    for (auto [from, to] : childs) {
    //        std::println("queue {}", to);
    //        queue.push(to);
    //    }
    // }

    //    for (auto&& [id, description] : reversed.vertices()) {
    //        const auto cull_imune = std::visit([](const auto& value) static noexcept { return value.cull_imune; },
    //        *description); if (not stdr::contains(visited, id) and not cull_imune) {
    //            std::println("remove {}", id);
    //            m_dag.remove_vertex(id);
    //        }
    //    }

    //    m_baked_graph = TryAssert(m_dag.topological_sort(), "Cycle detected!");
    //    // m_reversed_baked_graph = TryAssert(m_reversed_dag.reverse_view().topological_sort(), "Cycle detected!");
    // #ifdef DEBUG_DAG
    //    print_dag(m_dag, "./frame_culled.dot");
    // #endif
    // }

    //    struct Pass {
    //        GraphID id;

    //    std::vector<GraphID> attachments;
    // };

    //    struct Attachments {};

    //    /////////////////////////////////////
    //    /////////////////////////////////////
    //    auto FrameBuilder::make_frame(const gpu::Device&      device,
    //                                  const gpu::Queue&       queue,
    //                                  const gpu::CommandPool& command_pool,
    //                                  FramePool&              frame_pool,
    //                                  const math::rect<i32>&  render_area) const noexcept -> gpu::Expected<Frame> {
    //        EXPECTS(m_backbuffer != INVALID_ID);
    //        EXPECTS(baked());

    //    auto frame        = Frame {};
    //    frame.m_cmb       = Try(command_pool.create_command_buffer());
    //    frame.m_fence     = Try(gpu::Fence::create(device));
    //    frame.m_semaphore = Try(gpu::Semaphore::create(device));

    //    auto& resources                                                          = frame.m_resources;
    //    auto& [images, views, buffers, image_mapper, view_mapper, buffer_mapper] = resources;

    //    auto transition_cmb   = Try(command_pool.create_command_buffer());
    //    auto transition_fence = Try(gpu::Fence::create(device));

    //    for (const auto id : *m_baked_graph) {
    //        const auto& node = m_dag.get_vertex_value(id);

    //    if (is<CreateImage>(node)) {
    //        const auto  create_image = as<CreateImage>(node);
    //        const auto& image = images.emplace_back(Try(frame_pool.create_or_reuse_image(device, create_image.create_info)));
    //        image_mapper.emplace_back(id, core::hash(create_image.name), core::hash(create_image.create_info), as_ref(image));
    //    } else if (is<RetainedImage>(node)) {
    //        const auto retained_image = as<CreateImage>(node);
    //        image_mapper.emplace_back(id, core::hash(retained_image.name), 0u, as_ref(retained_image.image));
    //    } else if (is<CreateBuffer>(node)) {
    //        const auto  create_buffer = as<CreateBuffer>(node);
    //        const auto& buffer = buffers
    //                               .emplace_back(Try(frame_pool.create_or_reuse_buffer(device, create_buffer.create_info)));
    //        buffer_mapper
    //          .emplace_back(id, core::hash(create_buffer.name), core::hash(create_buffer.create_info), as_ref(buffer));
    //    } else if (is<RetainedBuffer>(node)) {
    //        const auto retained_buffer = as<CreateBuffer>(node);
    //        buffer_mapper.emplace_back(id, core::hash(retained_buffer.name), 0, as_ref(retained_buffer.buffer));
    //    }
    // }

    //    Try(transition_cmb.begin(true));
    //    // Try(std::visit(
    //    //   Overloaded {
    //    //     [&buffers, &buffer_mapper, &frame_pool, id, &device](const FrameBuffer& description) mutable noexcept
    //    //       -> gpu::Expected<void> {
    //    //         const auto& buffer = buffers
    //    //                                .emplace_back(Try(frame_pool.create_or_reuse_buffer(device,
    //    //                                description.create_info)));
    //    //         buffer_mapper.emplace_back(id, hash(description.name), core::hash(description.create_info),
    //    //         as_ref(buffer)); Return {};
    //    //     },
    //    //     [&buffer_mapper, id](const RetainedBuffer& description) mutable noexcept -> gpu::Expected<void> {
    //    //         buffer_mapper.emplace_back(id, hash(description.name), 0_u32, description.buffer);

    //    //    return {};
    //    // },
    //    // [&images,
    //    // &image_mapper,
    //    // &frame_pool,
    //    // id,
    //    // &device,
    //    // &transition_cmb](const FrameImage& description) mutable noexcept -> gpu::Expected<void> {
    //    //    const auto& image = images
    //    //                          .emplace_back(Try(frame_pool.create_or_reuse_image(device, description.create_info)));
    //    //    image_mapper.emplace_back(id, hash(description.name), core::hash(description.create_info), as_ref(image));

    //    //    Return {};
    //    // },
    //    // [&image_mapper, id](const RetainedImage& description) mutable noexcept -> gpu::Expected<void> {
    //    //    image_mapper.emplace_back(id, hash(description.name), 0_u32, description.image);
    //    //    return {};
    //    // },
    //    // [&images, &image_mapper, id, &device, &transition_cmb](const AttachmentDescription&) mutable noexcept
    //    //  -> gpu::Expected<void> {
    //    //    const auto& view = views.emplace_back(Try(gpu::ImageView::create(device, description.create_info)));
    //    //    views_mapper.emplace_back(id, hash(description.name), 0_u32, as_ref(view));

    //    //    transition_cmb.begin_debug_region(std::format("Transition:{}", description.name))
    //    //      .transition_image_layout(image, gpu::ImageLayout::UNDEFINED, gpu::ImageLayout::ATTACHMENT_OPTIMAL)
    //    //      .end_debug_region();
    //    // },
    //    // [](const auto&) static noexcept -> gpu::Expected<void> { return {}; },
    //    // },
    //    // node));
    // }

    //    Try(transition_cmb.end());
    //    TryDiscard(transition_cmb.submit(queue, {}, {}, {}, as_ref(transition_fence)));

    //    // prepare attachments
    //    // auto passes = std::vector<Pass> {};
    //    // for (const auto id : *m_baked_graph) {
    //    //     const auto& [_, description_variant] = m_dag.get_vertex_value(id);
    //    //     if (not is<FrameBuilder::TaskDescription>(description_variant)) continue;

    //    //    const auto& task = as<FrameBuilder::TaskDescription>(description_variant);
    //    //    if (not task.type == TaskDescriptionType::RASTER) continue;

    //    //    // write
    //    //    for (const auto [_, to] : m_dag.adjacent_edges(id)) {
    //    //        const auto& [_, attachment_variant] = m_dag.get_vertex_value(to);
    //    //        if (not is<ImageDescription>(attachment_variant) or not is<RetainedImageDescription>(attachment_variant))
    //    //            continue;
    //    //    }

    //    //    // read
    //    //    for (const auto [_, to] : m_dag.reversed_view().adjacent_edges(id)) {
    //    //        const auto& [_, attachment_variant] = m_dag.get_vertex_value(to);
    //    //        if (not is<ImageDescription>(attachment_variant) or not is<RetainedImageDescription>(attachment_variant))
    //    //            continue;
    //    //    }
    //    // }

    //    // struct RenderingInfo {
    //    //     struct Attachment {
    //    //         struct Resolve {
    //    //             Ref<const gpu::ImageView> image_view;
    //    //             ResolveModeFlag           mode;
    //    //             gpu::ImageLayout          layout = ImageLayout::ATTACHMENT_OPTIMAL;
    //    //         };

    //    //    Ref<const gpu::ImageView> image_view;
    //    //    gpu::ImageLayout          layout = ImageLayout::ATTACHMENT_OPTIMAL;

    //    //    std::optional<Resolve> resolve = std::nullopt;

    //    //    AttachmentLoadOperation  load_op  = AttachmentLoadOperation::CLEAR;
    //    //    AttachmentStoreOperation store_op = AttachmentStoreOperation::STORE;

    //    //    std::optional<ClearValue> clear_value = std::nullopt;
    //    // };

    //    //    math::irect render_area;
    //    //    u32         layer_count = 1u;
    //    //    u32         view_mask   = 0u;

    //    //    std::vector<Attachment>   color_attachments;
    //    //    std::optional<Attachment> depth_attachment   = std::nullopt;
    //    //    std::optional<Attachment> stencil_attachment = std::nullopt;
    //    // };

    //    auto& cmb = *frame.m_cmb;
    //    Try(cmb.begin());
    //    for (const auto id : *m_baked_graph) {
    //        const auto& node = m_dag.get_vertex_value(id);

    //    // Try(std::visit(Overloaded {
    //    //                  [](const ImageDescription& description) noexcept {

    //    //    },
    //    //    [](const RetainedImageDescription& description) noexcept {

    //    //    },
    //    //    [](const FrameBuilder::TaskDescription& task) mutable noexcept {
    //    //        cmb.begin_debug_region(std::format("Task:{}", task.name));
    //    //        switch (task.type) {
    //    //            case TaskDescription::Type::RASTER: {
    //    //                auto& [color_attachments,
    //    //                       depth_attachment,
    //    //                       stencil_attachment] = *stdr::find_if(read_attachments,
    //    //                                                            [id](const auto& pair) noexcept {
    //    //                                                                return pair.first == id;
    //    //                                                            });
    //    //                auto raster_task           = RasterTask {
    //    //                    .rendering_info = gpu::
    //    //                      RenderingInfo { .render_area        = render_area,
    //    //                                     .color_attachments  = std::move(color_attachments),
    //    //                                     .depth_attachment   = std::move(depth_attachment),
    //    //                                     .stencil_attachment = std::move(stencil_attachment) },
    //    //                    .cmb = Try(command_pool.create_command_buffer(gpu::CommandBufferLevel::SECONDARY))
    //    //                };
    //    //                cmb.begin_rendering(raster_task.rendering_info);

    //    //    task.execute(resources, cmb);

    //    //    cmb.end_rendering();
    //    // } break;
    //    // case TaskDescription::Type::COMPUTE: {
    //    // } break;
    //    // case TaskDescription::Type::TRANSFER: {
    //    //    auto raster_task = TransferTask {
    //    //        .cmb = Try(command_pool.create_command_buffer(gpu::CommandBufferLevel::SECONDARY))
    //    //    };
    //    //    cmb.begin_rendering(raster_task.rendering_info);

    //    //    task.execute(resources, cmb);

    //    //    cmb.end_rendering();
    //    // } break;
    //    // case TaskDescription::Type::RAYTRACING: {
    //    // } break;
    //    // }
    //    // cmb.end_debug_region();

    //    //    attachments.clear();
    //    // } },
    //    // node));
    // }

    //    Try(cmb.end());

    //    TryDiscard(transition_fence.wait());

    //    Return frame;
    // }
} // namespace stormkit::engine
