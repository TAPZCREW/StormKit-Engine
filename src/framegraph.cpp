module;

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

module stormkit.engine;

import std;

import stormkit.core;
import stormkit.gpu;

import :renderer.framegraph;

namespace stormkit::engine {
    namespace {
        constexpr auto colorize_overloaded = Overloaded {
            [](const BufferCreateDescription& value) static noexcept { return "blue"; },
            [](const ImageCreateDescription& value) static noexcept { return "red"; },
            [](const ImageReadDescription& value) static noexcept { return "pink"; },
            [](const ImageWriteDescription& value) static noexcept { return "orange"; },
            [](const FrameBuilder::TaskDescription& value) static noexcept { return "white"; },
        };

        constexpr auto colorize_visitor = [](const auto& value) static noexcept {
            return std::visit(colorize_overloaded, value);
        };

        constexpr auto make_format_visitor(auto& dag) noexcept {
            return Overloaded {
                [](const BufferCreateDescription& value) static noexcept { return value.name; },
                [](const ImageCreateDescription& value) static noexcept { return value.name; },
                [&dag](const ImageReadDescription& value) noexcept {
                    return std::format("{}:read", as<ImageCreateDescription>(dag.get_vertex_value(value.image).value).name);
                },
                [&dag](const ImageWriteDescription& value) noexcept {
                    return std::format("{}:write", as<ImageCreateDescription>(dag.get_vertex_value(value.image).value).name);
                },
                [](const FrameBuilder::TaskDescription& value) static noexcept { return value.name; },
            };
        }

        auto print_dag(auto& dag) noexcept {
            std::println("{}", dag.dump({ .colorize = colorize_visitor, .format_value = [&dag](const auto& value) noexcept {
                                             return std::visit(make_format_visitor(dag), value);
                                         } }));
        }
    } // namespace

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::bake() noexcept -> void {
        EXPECTS(m_backbuffer != INVALID_ID);
        // print_dag(m_dag);
        // cull unused nodes
        const auto reversed    = m_dag.reverse();
        auto       cull_imunes = reversed.vertices() | stdv::filter([](const auto& variant) static noexcept {
                               return std::visit([](const auto& value) static noexcept { return value.cull_imune; },
                                                 *variant.value);
                                 });

        auto queue = std::queue<GraphID> {};
        for (auto&& [id, _] : cull_imunes) queue.push(id);

        auto visited = std::vector<GraphID> {};
        visited.reserve(stdr::size(m_dag.vertices()));

        while (not queue.empty()) {
            const auto id = queue.front();
            queue.pop();

            visited.emplace_back(id);

            const auto childs = reversed.adjacent_edges(id);
            for (auto [from, to] : childs) queue.push(to);
        }

        for (auto&& [id, description] : reversed.vertices())
            if (not stdr::contains(visited, id)) m_dag.remove_vertex(id);

        m_baked_graph = TryAssert(m_dag.topological_sort(), "Cycle detected!");
        // print_dag(m_dag);

        m_hash = 0u;
        for (auto&& id : *m_baked_graph) hash_combine(m_hash, m_dag.get_vertex_value(id).value);
    } // namespace

    template<typename T>
    struct DAGResource {
        GraphID      id;
        Ref<const T> description;
    };

    struct Pass {
        GraphID                                  id;
        Ref<const FrameBuilder::TaskDescription> description;

        std::vector<DAGResource<ImageCreateDescription>> image_creates = {};
        std::vector<DAGResource<ImageReadDescription>>   image_reads   = {};
        std::vector<DAGResource<ImageWriteDescription>>  image_writes  = {};

        std::vector<DAGResource<BufferCreateDescription>> buffer_creates = {};
        // std::vector<DAGResource<BufferReadDescription>>        buffer_read   = {};
        // std::vector<DAGResource<BufferWriteDescription>>        buffer_write  = {};
    };

    struct FrameDescription {
        std::vector<Pass> passes;
    };

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameBuilder::make_frame(const gpu::Device&      device,
                                  const gpu::Queue&       queue,
                                  const gpu::CommandPool& command_pool,
                                  FramePool&              frame_pool,
                                  const math::rect<i32>&  render_area) const noexcept -> gpu::Expected<Frame> {
        EXPECTS(m_backbuffer != INVALID_ID);
        EXPECTS(baked());

        auto frame_description = FrameDescription {};
        for (auto&& id : *m_baked_graph) {
            const auto& [_, description_variant] = m_dag.get_vertex_value(id);

            if (std::holds_alternative<TaskDescription>(description_variant)) {
                const auto& task_description = std::get<TaskDescription>(description_variant);

                auto& pass = frame_description.passes.emplace_back(Pass { .id = id, .description = as_ref(task_description) });
                for (auto&& [_, to] : m_dag.adjacent_edges(id)) {
                    const auto& [_, res_description_variant] = m_dag.get_vertex_value(to);
                    std::visit(Overloaded {
                                 [&pass, to](const BufferCreateDescription& buffer_description) mutable noexcept {
                                     pass.buffer_creates.emplace_back(to, as_ref(buffer_description));
                                 },
                                 // [&pass, to](const BufferReadDescription& buffer_description) mutable noexcept {
                                 //     pass.buffer_reads.emplace_back(to, as_ref(buffer_description));
                                 // },
                                 // [&pass, to](const BufferWriteDescription& buffer_description) mutable noexcept {
                                 //     pass.buffer_writes.emplace_back(to, as_ref(buffer_description));
                                 // },
                                 [&pass, to](const ImageCreateDescription& image_description) mutable noexcept {
                                     pass.image_creates.emplace_back(to, as_ref(image_description));
                                 },
                                 [&pass, to](const ImageReadDescription& image_description) mutable noexcept {
                                     pass.image_reads.emplace_back(to, as_ref(image_description));
                                 },
                                 [&pass, to](const ImageWriteDescription& image_description) mutable noexcept {
                                     pass.image_writes.emplace_back(to, as_ref(image_description));
                                 },
                                 [](const auto&) static noexcept {},
                               },
                               res_description_variant);
                }
            }
        }

        auto frame        = Frame {};
        frame.m_cmb       = Try(command_pool.create_command_buffer());
        frame.m_fence     = Try(gpu::Fence::create(device));
        frame.m_semaphore = Try(gpu::Semaphore::create(device));
        auto& resources   = frame.m_resources;
        auto& cmb         = *frame.m_cmb;
        auto& fence       = *frame.m_fence;
        auto& semaphore   = *frame.m_semaphore;

        auto transition_cmb   = Try(command_pool.create_command_buffer());
        auto transition_fence = Try(gpu::Fence::create(device));

        Try(cmb.begin());
        Try(transition_cmb.begin(true));
        for (const auto& pass : frame_description.passes) {
            const auto& task = pass.description;
            cmb.begin_debug_region(std::format("Task:{}", task->name));

            // create written resources first
            // reserved so pointers are stables
            auto& images       = resources.images;
            auto& image_mapper = resources.image_mapper;
            images.reserve(stdr::size(pass.image_creates));
            image_mapper.reserve(stdr::size(pass.image_creates));
            for (const auto& [id, image_description] : pass.image_creates) {
                const auto is_depth = gpu::is_depth_format(image_description->format)
                                      or gpu::is_depth_stencil_format(image_description->format);

                const auto usage = [&] noexcept {
                    auto out = (is_depth) ? gpu::ImageUsageFlag::DEPTH_STENCIL_ATTACHMENT : gpu::ImageUsageFlag::COLOR_ATTACHMENT;

                    if (id == m_backbuffer) out |= gpu::ImageUsageFlag::TRANSFER_SRC;

                    return out;
                }();

                auto& frame_image = images.emplace_back(Try(frame_pool.create_or_reuse_image(device, image_description)));
                image_mapper.emplace_back(id, hasher<hash32>(*image_description), as_ref(frame_image));
                transition_cmb.begin_debug_region(std::format("Transition:{}", image_description->name))
                  .transition_image_layout(frame_image, gpu::ImageLayout::UNDEFINED, gpu::ImageLayout::ATTACHMENT_OPTIMAL)
                  .end_debug_region();

                if (not frame.m_backbuffer and id == m_backbuffer) frame.m_backbuffer = as_opt_ref(frame_image);
            }

            auto color_attachments = std::vector<gpu::RenderingInfo::Attachment> {};
            color_attachments.reserve(stdr::size(resources.images));
            auto depth_attachment   = std::optional<gpu::RenderingInfo::Attachment> {};
            auto stencil_attachment = std::optional<gpu::RenderingInfo::Attachment> {};
            auto i                  = 0u;

            // reserved so pointers are stables
            auto& views = resources.views;
            views.reserve(stdr::size(pass.image_writes) + stdr::size(pass.image_reads));
            for (const auto& [id, view_description] : pass.image_writes) {
                const auto& it = stdr::find_if(image_mapper, [id = view_description->image](const auto& image) noexcept {
                    return image.id == id;
                });
                ENSURES(it != stdr::cend(image_mapper));
                const auto& image = *it->value;

                auto& view = views.emplace_back(Try(frame_pool.create_or_reuse_image_view(device, image, view_description)));

                auto attachment = gpu::RenderingInfo::Attachment { .image_view  = as_ref(view),
                                                                   .clear_value = view_description->clear_value };

                if (gpu::is_depth_format(image.format())) depth_attachment = std::move(attachment);
                else
                    color_attachments.emplace_back(std::move(attachment));
                ++i;
            }

            auto& buffers       = resources.buffers;
            auto& buffer_mapper = resources.buffer_mapper;
            buffers.reserve(stdr::size(pass.buffer_creates));
            buffer_mapper.reserve(stdr::size(pass.buffer_creates));

            switch (task->type) {
                case TaskDescription::Type::RASTER: {
                    auto raster_task = RasterTask {
                        .rendering_info = gpu::RenderingInfo { .render_area        = render_area,
                                                              .color_attachments  = std::move(color_attachments),
                                                              .depth_attachment   = std::move(depth_attachment),
                                                              .stencil_attachment = std::move(stencil_attachment) },
                        .cmb            = Try(command_pool.create_command_buffer(gpu::CommandBufferLevel::SECONDARY))
                    };
                    cmb.begin_rendering(raster_task.rendering_info);

                    task->execute(cmb);

                    cmb.end_rendering();
                } break;
                case TaskDescription::Type::COMPUTE: {
                } break;
                case TaskDescription::Type::TRANSFER: {
                } break;
                case TaskDescription::Type::RAYTRACING: {
                } break;
            }
            cmb.end_debug_region();
        }

        Try(cmb.end());
        Try(transition_cmb.end());

        TryDiscard(transition_cmb.submit(queue, {}, {}, {}, as_ref(transition_fence)));
        TryDiscard(transition_fence.wait());

        return frame;
    }
} // namespace stormkit::engine
