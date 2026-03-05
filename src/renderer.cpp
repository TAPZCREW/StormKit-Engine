module;

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/try_expected.hpp>
#include <stormkit/log/log_macro.hpp>

module stormkit.engine;

import std;

import stormkit;

import :renderer;
import :renderer.framegraph;

using namespace std::literals;

namespace stdr = std::ranges;
namespace cm   = stormkit::monadic;

namespace stormkit::engine {
    LOGGER("renderer")

    namespace {
        [[maybe_unused]]
        constexpr auto RAYTRACING_EXTENSIONS = std::array<std::string_view, 0> {
            // "VK_KHR_ray_tracing_pipeline"sv,     "VK_KHR_acceleration_structure"sv, "VK_KHR_buffer_device_address"sv,
            // "VK_KHR_deferred_host_operations"sv, "VK_EXT_descriptor_indexing"sv,    "VK_KHR_spirv_1_4"sv,
            // "VK_KHR_shader_float_controls"sv
        };

        constexpr auto BASE_EXTENSIONS = std::array { "VK_KHR_maintenance3"sv };

        constexpr auto SWAPCHAIN_EXTENSIONS = std::array { "VK_KHR_swapchain"sv };

        /////////////////////////////////////
        /////////////////////////////////////
        auto pick_physical_device(std::span<const gpu::PhysicalDevice> physical_devices) noexcept
          -> OptionalRef<const gpu::PhysicalDevice> {
            auto ranked_devices = std::multimap<u64, Ref<const gpu::PhysicalDevice>> {};

            for (const auto& physical_device : physical_devices) {
                if (not physical_device.check_extension_support(BASE_EXTENSIONS)) {
                    dlog("Base required extensions not supported for GPU {}", physical_device);
                    continue;
                }
                if (not physical_device.check_extension_support(SWAPCHAIN_EXTENSIONS)) {
                    dlog("Swapchain required extensions not supported for GPU {}", physical_device);
                    continue;
                }

                const auto& info = physical_device.info();

                dlog("Scoring for {}\n"
                     "    device id:      {:#06x}\n"
                     "    vendor name:    {}\n"
                     "    vendor id:      {}\n"
                     "    api version:    {}.{}.{}\n"
                     "    driver version: {}.{}.{}\n"
                     "    type:           {}",
                     info.device_name,
                     info.device_id,
                     info.vendor_name,
                     info.vendor_id,
                     info.api_major_version,
                     info.api_minor_version,
                     info.api_patch_version,
                     info.driver_major_version,
                     info.driver_minor_version,
                     info.driver_patch_version,
                     info.type);

                const auto score = score_physical_device(physical_device);

                dlog("Score is {}", score);

                ranked_devices.emplace(score, as_ref(physical_device));
            }

            if (stdr::empty(ranked_devices)) return std::nullopt;

            return ranked_devices.rbegin()->second;
        }
    } // namespace

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init(std::string_view application_name, OptionalRef<const wsi::Window> window) noexcept
      -> gpu::Expected<void> {
        m_extent = window->extent();

        ilog("Initializing Renderer...");
        Try(gpu::initialize_backend());
        ilog("Vulkan backend successfully initialized. ✓");
        Try(do_init_instance(application_name));
        ilog("GPU instance successfully initialized. ✓");
        Try(do_init_device());
        ilog("GPU device successfully initialized. ✓");

        m_raster_queue = gpu::Queue::create(*m_device, m_device->raster_queue_entry());
        m_device->set_object_name(*m_raster_queue, "StormKit:raster_queue");
        m_main_command_pool = Try(gpu::CommandPool::create(*m_device));
        ilog("GPU main command pool successfully initialized. ✓");

        Try(do_init_render_surface(std::move(window)));
        ilog("GPU windowed render surface successfully initialized. ✓");

        m_resource_store       = ResourceStore { *m_device };
        m_frame_resource_cache = FrameResourceCache { *m_device };
        m_frame_resources.resize(m_surface->buffering_count());

        ilog("Renderer initialized!");
        Return {};
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init_instance(std::string_view application_name) noexcept -> gpu::Expected<void> {
        m_instance = Try(gpu::Instance::create(std::string { application_name }, true));

        Return {};
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init_device() noexcept -> gpu::Expected<void> {
        const auto& physical_devices = m_instance->physical_devices();
        const auto& physical_device  = pick_physical_device(physical_devices);

        ilog("Using physical device {}", *physical_device);

        m_device = Try(gpu::Device::allocate(physical_device, m_instance));

        m_device->set_object_name(*m_instance, "StormKit:main_instance");
        m_device->set_object_name(*m_device, "StormKit:main_device");

        Return {};
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init_render_surface(OptionalRef<const wsi::Window> window) noexcept -> gpu::Expected<void> {
        if (not window) ensures(not window, "Offscreen rendering not yet implemented");

        m_surface = Try(RenderSurface::create(*this, *window));

        Return {};
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::thread_loop(std::atomic_bool& window_is_open, std::stop_token token) noexcept -> void {
        set_current_thread_name("StormKit:RenderThread");

        m_command_buffers = TryAssert(m_main_command_pool->create_command_buffers(m_surface->buffering_count()),
                                      "Failed to create main command buffers");
        auto i            = 0;
        for (auto& cmb : m_command_buffers) m_device->set_object_name(cmb, std::format("StormKit:blit_cmb_{}", i++));

        for (;;) {
            if (token.stop_requested()) break;
            if (window_is_open) {
                auto frame = TryAssert(m_surface->begin_frame(*m_device), "Failed to start frame!");
                TryAssert(do_render(frame), "Failed to render frame!");
                TryAssert(m_surface->present_frame(m_raster_queue, frame), "Failed to present frame!");
            }
        }

        m_device->wait_idle();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_render(RenderSurface::Frame& frame) noexcept -> gpu::Expected<void> {
        // handle empty graph
        {
            auto frame_builders = m_frame_builders.read();
            if (frame_builders->empty()) {
                auto& blit_cmb = m_command_buffers[frame.current_frame];

                Try(blit_cmb.reset());
                Try(blit_cmb.begin(true));
                Try(blit_cmb.end());

                auto wait       = as_refs<std::array>(frame.submission_resources->image_available);
                auto stage_mask = std::array { gpu::PipelineStageFlag::COLOR_ATTACHMENT_OUTPUT };
                auto signal     = as_refs<std::array>(frame.submission_resources->render_finished);

                TryAssert(blit_cmb
                            .submit(m_raster_queue, wait, stage_mask, signal, as_ref(frame.submission_resources->in_flight)),
                          std::format("Failed to submit frame {} blit command buffer!", frame.current_frame));

                Return {};
            }
        }

        auto frame_builder = FrameBuilder {};
        {
            auto frame_builders = m_frame_builders.write();
            frame_builder       = std::move(frame_builders->front());
            frame_builders->pop();
        }

        auto old                               = std::move(m_frame_resources[frame.current_frame]);
        m_frame_resources[frame.current_frame] = realize_frame(frame_builder);

        if (old.initialized())
            if (not(old->fence.status() == gpu::Fence::Status::SIGNALED))
                TryAssert(old->fence.wait(), std::format("Failed to wait on old frame {} fence!", frame.current_frame));

        auto&       frame_resources = m_frame_resources[frame.current_frame];
        const auto& present_image   = m_surface->images()[frame.image_index];
        auto&       blit_cmb        = m_command_buffers[frame.current_frame];

        auto render_signal = as_refs<std::array>(frame_resources->semaphore);
        TryAssert(frame_resources->main_cmb.submit(m_raster_queue, {}, {}, render_signal, as_ref(frame_resources->fence)),
                  std::format("Failed to submit frame {} main command buffer!", frame.current_frame));

        Try(blit_cmb.reset());
        Try(blit_cmb.begin(true));

        auto& backbuffer = *frame_resources->backbuffer;
        // clang-format off
            blit_cmb
              .transition_image_layout(backbuffer, gpu::ImageLayout::ATTACHMENT_OPTIMAL, gpu::ImageLayout::TRANSFER_SRC_OPTIMAL)
              .transition_image_layout(present_image, gpu::ImageLayout::PRESENT_SRC, gpu::ImageLayout::TRANSFER_DST_OPTIMAL)
              .blit_image(backbuffer,
                          present_image,
                          gpu::ImageLayout::TRANSFER_SRC_OPTIMAL,
                          gpu::ImageLayout::TRANSFER_DST_OPTIMAL,
                          std::array {
                            gpu::BlitRegion { .src        = {},
                                              .dst        = {},
                                              .src_offset = { math::ivec3 { 0, 0, 0 }, backbuffer.extent().to<i32>(), },
                                              .dst_offset = { math::ivec3 { 0, 0, 0 }, present_image.extent().to<i32>(), }, },
                          },
                          gpu::Filter::LINEAR)
              .transition_image_layout(backbuffer, gpu::ImageLayout::TRANSFER_SRC_OPTIMAL, gpu::ImageLayout::ATTACHMENT_OPTIMAL)
              .transition_image_layout(present_image, gpu::ImageLayout::TRANSFER_DST_OPTIMAL, gpu::ImageLayout::PRESENT_SRC);
        // clang-format on

        Try(blit_cmb.end());

        auto wait       = as_refs<std::array>(frame_resources->semaphore, frame.submission_resources->image_available);
        auto stage_mask = std::array { gpu::PipelineStageFlag::COLOR_ATTACHMENT_OUTPUT, gpu::PipelineStageFlag::TRANSFER };
        auto signal     = as_refs<std::array>(frame.submission_resources->render_finished);

        TryAssert(blit_cmb.submit(m_raster_queue, wait, stage_mask, signal, as_ref(frame.submission_resources->in_flight)),
                  std::format("Failed to submit frame {} blit command buffer!", frame.current_frame));

        Return {};
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::realize_frame(const FrameBuilder& frame_builder) noexcept -> FrameResources {
        const auto& tasks     = frame_builder.tasks();
        const auto& resources = frame_builder.resources();

        auto dag = DAG<std::optional<FrameBuilder::TaskID>> { stdr::size(resources) + stdr::size(tasks) };

        auto map = std::vector<std::pair<FrameBuilder::TaskID, dag::VertexID>> {};
        map.reserve(stdr::size(tasks));
        for (const auto& [_id, task] : tasks) {
            auto vertex_id = dag.add_vertex(task.id);
            map.emplace_back(task.id, vertex_id);
        }

        for (const auto& [_id, resource] : resources) {
            auto vertex_id = dag.add_vertex(std::nullopt);

            for (const auto& [task_id, task_vertex_id] : map) {
                if ((resource.wrote_by & task_id) == task_id) dag.add_edge(task_vertex_id, vertex_id);
                else if ((resource.read_by & task_id) == task_id)
                    dag.add_edge(vertex_id, task_vertex_id);
            }
        }

        auto result = dag.topological_sort();
        if (not result) {
            TryAssert(io::write_text("./dag.dot", frame_builder.dump()), "Failed to write dag.dot!");
            ensures(false, std::format("Cycles detected in frame graph {}", result.error()));
        }

        auto ordered_tasks = TryAssert(dag.topological_sort(), "Cycles detected in frame graph!")
                             | stdv::filter([&dag](const auto& id) noexcept { return dag.get_vertex_value(id).has_value(); })
                             | stdv::transform([&dag](const auto& id) noexcept { return *dag.get_vertex_value(id); });

        auto frame_resources = FrameResources {
            .semaphore = TryAssert(gpu::Semaphore::create(*m_device), "Failed to allocate frame semaphore!"),
            .fence     = TryAssert(gpu::Fence::create(*m_device), "Failed to allocate frame fence!"),
            .main_cmb  = TryAssert(m_main_command_pool->create_command_buffer(), "Failed to allocate main frame command buffer!"),
        };

        for (const auto& [_, resource] : resources) {
            std::visit(Overloaded {
                         [&frame_resources, &resource, &map, this](const gpu::Image::CreateInfo& create_info) mutable noexcept {
                             const auto& [id, image] = frame_resources.created_images
                                                         .emplace_back(resource.id,
                                                                       m_frame_resource_cache->get_or_create_image(create_info));
                             if (not resource.attached_in.none()) {
                                 for (const auto& [task_id, _] : map) {
                                     if ((resource.attached_in & task_id) == task_id) {
                                         const auto _task_id     = FrameBuilder::CombinedID { task_id.to_ulong() } << 32;
                                         const auto _resource_id = FrameBuilder::CombinedID { id.to_ulong() };
                                         frame_resources.image_views
                                           .emplace_back(_task_id | _resource_id,
                                                         TryAssert(gpu::ImageView::create(*m_device, image),
                                                                   std::format("Failed to allocate image view for image {}",
                                                                               resource.name)));
                                     }
                                 }
                             }
                         },
                         [&frame_resources, &resource, this](const gpu::Buffer::CreateInfo& create_info) mutable noexcept {
                             frame_resources.created_buffers
                               .emplace_back(resource.id, m_frame_resource_cache->get_or_create_buffer(create_info));
                         },
                         [&frame_resources, &resource, &map, this](const Ref<gpu::Image>& retained_image) noexcept {
                             const auto& [id,
                                          image] = frame_resources.images.emplace_back(resource.id, as_ref_mut(retained_image));
                             if (not resource.attached_in.none()) {
                                 for (const auto& [task_id, _] : map) {
                                     if ((resource.attached_in & task_id) == task_id) {
                                         const auto _task_id     = FrameBuilder::CombinedID { task_id.to_ulong() } << 32;
                                         const auto _resource_id = FrameBuilder::CombinedID { id.to_ulong() };
                                         frame_resources.image_views
                                           .emplace_back(_task_id | _resource_id,
                                                         TryAssert(gpu::ImageView::create(*m_device, image),
                                                                   std::format("Failed to allocate image view for image {}",
                                                                               resource.name)));
                                     }
                                 }
                             }
                         },
                         [&frame_resources, &resource](const Ref<gpu::Buffer>& retained_buffer) noexcept {
                             frame_resources.buffers.emplace_back(resource.id, as_ref_mut(retained_buffer));
                         },
                         [](auto&&) static noexcept {},
                       },
                       resource.data);
        }
        merge(frame_resources.images, frame_resources.created_images | stdv::transform([](auto& pair) static noexcept {
                                          return std::make_pair(pair.first, as_ref_mut(pair.second));
                                      }) | stdv::as_rvalue);

        merge(frame_resources.buffers, frame_resources.created_buffers | stdv::transform([](auto& pair) static noexcept {
                                           return std::make_pair(pair.first, as_ref_mut(pair.second));
                                       }) | stdv::as_rvalue);

        // auto roots = tasks | stdv::filter([](const auto& pair) static noexcept { return pair.second.root; });

        auto& main_cmb = frame_resources.main_cmb;
        TryAssert(main_cmb.begin(true), "Failed to record main frame command buffer!");
        for (const auto& task_id : ordered_tasks) {
            const auto& [_,
                         task] = *stdr::find_if(tasks, [&task_id](const auto& pair) noexcept { return pair.first == task_id; });

            auto& pass = frame_resources.passes.emplace_back(FrameResources::Pass {
              .cmb = TryAssert(m_main_command_pool->create_command_buffer(gpu::CommandBufferLevel::SECONDARY),
                               std::format("Failed to allocate frame pass {} command buffer!", task.name)) });

            auto accessor = FrameResourcesAccessor { frame_resources.images,
                                                     frame_resources.image_views,
                                                     frame_resources.buffers };

            main_cmb.begin_debug_region(std::format("StormKit:frame:{}", task.name));
            switch (task.type) {
                case FrameBuilder::Task::Type::RASTER: {
                    const auto extent         = m_extent.to<i32>();
                    auto       rendering_info = gpu::RenderingInfo {
                        .render_area = { 0, 0, extent.width, extent.height },
                    };
                    auto inheritance_info = gpu::RenderingInheritanceInfo {};

                    main_cmb.begin_debug_region("StormKit:frame:transition_attachments");
                    for (auto& [image_id, image] : frame_resources.images) {
                        if (not((image_id & task.attachments) == image_id)) continue;

                        if (image_id == frame_builder.backbuffer()) frame_resources.backbuffer = as_opt_ref(image);

                        const auto read  = (image_id & task.reads) == image_id;
                        const auto write = (image_id & task.writes) == image_id;

                        const auto load_op = [&] noexcept {
                            if (write) {
                                if (not read) return gpu::AttachmentLoadOperation::CLEAR;
                                return gpu::AttachmentLoadOperation::LOAD;
                            } else if (read)
                                return gpu::AttachmentLoadOperation::LOAD;

                            return gpu::AttachmentLoadOperation::DONT_CARE;
                        }();

                        const auto store_op = [&] noexcept {
                            if (write) return gpu::AttachmentStoreOperation::STORE;

                            return gpu::AttachmentStoreOperation::DONT_CARE;
                        }();

                        const auto&
                          view = stdr::find_if(frame_resources.image_views, [&image_id, &task_id](const auto& pair) noexcept {
                                     const auto _task_id     = FrameBuilder::CombinedID { task_id.to_ulong() } << 32;
                                     const auto _resource_id = FrameBuilder::CombinedID { image_id.to_ulong() };
                                     return pair.first == (_task_id | _resource_id);
                                 })->second;

                        const auto format = image->format();

                        if (gpu::is_stencil_only_format(format)) {
                            rendering_info.stencil_attachment = gpu::RenderingInfo::Attachment {
                                .image_view = as_ref(view),
                                .load_op    = load_op,
                                .store_op   = store_op,
                            };
                            inheritance_info.stencil_attachment = format;
                        } else if (gpu::is_depth_format(format)) {
                            rendering_info.depth_attachment = gpu::RenderingInfo::Attachment {
                                .image_view = as_ref(view),
                                .load_op    = load_op,
                                .store_op   = store_op,
                            };
                            inheritance_info.depth_attachment = format;
                        } else {
                            rendering_info.color_attachments.emplace_back(gpu::RenderingInfo::Attachment {
                              .image_view = as_ref(view),
                              .load_op    = load_op,
                              .store_op   = store_op,
                            });
                            inheritance_info.color_attachments.emplace_back(format);
                        }

                        main_cmb
                          .transition_image_layout(image, gpu::ImageLayout::UNDEFINED, gpu::ImageLayout::ATTACHMENT_OPTIMAL);
                    }
                    main_cmb.end_debug_region();

                    TryAssert(pass.cmb.begin(true, inheritance_info),
                              std::format("Failed to record raster pass {} command buffer!", task.name));
                    task.execute(accessor, pass.cmb, task.data);
                    TryAssert(pass.cmb.end(), std::format("Failed to end pass {} command buffer!", task.name));
                    main_cmb.begin_rendering(rendering_info, true)
                      .execute_sub_command_buffers(into_array(as_ref(pass.cmb)))
                      .end_rendering();
                } break;
                case FrameBuilder::Task::Type::COMPUTE: {
                    TryAssert(pass.cmb.begin(true), std::format("Failed to record compute pass {} command buffer!", task.name));
                    task.execute(accessor, pass.cmb, task.data);
                    TryAssert(pass.cmb.end(), std::format("Failed to end pass {} command buffer!", task.name));
                    main_cmb.execute_sub_command_buffers(into_array(as_ref(pass.cmb)));
                } break;
                case FrameBuilder::Task::Type::TRANSFER: {
                    TryAssert(pass.cmb.begin(true), std::format("Failed to record transfer pass {} command buffer!", task.name));
                    task.execute(accessor, pass.cmb, task.data);
                    TryAssert(pass.cmb.end(), std::format("Failed to end pass {} command buffer!", task.name));
                    main_cmb.execute_sub_command_buffers(into_array(as_ref(pass.cmb)));
                } break;
                case FrameBuilder::Task::Type::RAYTRACING: {
                } break;
                default: std::unreachable();
            }

            main_cmb.end_debug_region();
        }

        TryAssert(main_cmb.end(), "Failed to end main frame command buffer!");

        return frame_resources;
    }
} // namespace stormkit::engine
