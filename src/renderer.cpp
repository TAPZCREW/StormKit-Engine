module;

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
    LOGGER("stormkit.Renderer")

    namespace {
        constexpr auto RAYTRACING_EXTENSIONS = std::array {
            "VK_KHR_ray_tracing_pipeline"sv,     "VK_KHR_acceleration_structure"sv, "VK_KHR_buffer_device_address"sv,
            "VK_KHR_deferred_host_operations"sv, "VK_EXT_descriptor_indexing"sv,    "VK_KHR_spirv_1_4"sv,
            "VK_KHR_shader_float_controls"sv
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

        /////////////////////////////////////
        /////////////////////////////////////
        // auto
        //     chooseSwapExtent(const math::Extent2<u32>&      extent,
        //                      const gpu::SurfaceCapabilities& capabilities) noexcept ->
        //                      gpu::Extent2<u32> {
        //     constexpr static auto int_max = std::numeric_limits<u32>::max();
        //
        //     if (capabilities.currentExtent.width != int_max &&
        //         capabilities.currentExtent.height != int_max)
        //         return capabilities.currentExtent;
        //
        //     auto actual_extent   = extent;
        //     actual_extent.width  = math::clamp(actual_extent.width,
        //                                             capabilities.minImageExtent.width,
        //                                             capabilities.maxImageExtent.width);
        //     actual_extent.height = math::clamp(actual_extent.height,
        //                                              capabilities.minImageExtent.height,
        //                                              capabilities.maxImageExtent.height);
        //
        //     return actual_extent;
        // }
        //
        // /////////////////////////////////////
        // /////////////////////////////////////
        // auto chooseImageCount(const gpu::SurfaceCapabilities& capabilities) noexcept
        //     -> u32 {
        //     auto image_count = capabilities.minImageCount + 1;
        //     return math::clamp(image_count,
        //                              capabilities.minImageCount,
        //                              capabilities.maxImageCount);
        // }
    } // namespace

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init(std::string_view application_name, OptionalRef<const wsi::Window> window) noexcept
      -> gpu::Expected<void> {
        ilog("Initializing Renderer");
        return do_init_instance(application_name)
          .and_then(bind_front(&Renderer::do_init_device, this))
          .transform(bind_front(gpu::Queue::create, as_ref(m_device), m_device->raster_queue_entry()))
          .transform(cm::set(m_raster_queue))
          .and_then(bind_front(gpu::CommandPool::create, as_ref(m_device)))
          .transform(cm::set(m_main_command_pool))
          .and_then(bind_front(&Renderer::do_init_render_surface, this, std::move(window)));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init_instance(std::string_view application_name) noexcept -> gpu::Expected<void> {
        return gpu::Instance::create(std::string { application_name }).transform(cm::set(m_instance));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init_device() noexcept -> gpu::Expected<void> {
        const auto& physical_devices = m_instance->physical_devices();
        auto        physical_device  = pick_physical_device(physical_devices);

        ilog("Using physical device {}", *physical_device);

        return gpu::Device::create(*physical_device, *m_instance).transform(cm::set(m_device));
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_init_render_surface(OptionalRef<const wsi::Window> window) noexcept -> gpu::Expected<void> {
        if (window) return RenderSurface::create(*this, *window).transform(cm::set(m_surface));

        ensures(not window, "Offscreen rendering not yet implemented");

        return {};
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::thread_loop(std::mutex& framegraph_mutex, std::atomic_bool& rebuild_graph, std::stop_token token) noexcept
      -> void {
        set_current_thread_name("StormKit:RenderThread");

        m_command_buffers = *m_main_command_pool->create_command_buffers(m_surface->buffering_count());

        m_framegraphs.resize(m_surface->buffering_count());

        for (;;) {
            if (token.stop_requested()) return;

            m_surface->begin_frame(m_device)
              .and_then(bind_front(&Renderer::do_render, this, std::ref(framegraph_mutex), std::ref(rebuild_graph)))
              .and_then(bind_front(&RenderSurface::present_frame, &m_surface.get(), as_ref(m_raster_queue)))
              .transform_error(cm::assert("Failed to render frame"));
        }

        m_device->wait_idle();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_render(std::mutex& framegraph_mutex, std::atomic_bool& rebuild_graph, RenderSurface::Frame&& frame) noexcept
      -> gpu::Expected<RenderSurface::Frame> {
        if (rebuild_graph) {
            auto _ = std::unique_lock { framegraph_mutex };
            m_graph_builder.bake();

            for (auto&& framegraph : m_framegraphs) framegraph = m_graph_builder.create_framegraph(m_device, m_main_command_pool);

            rebuild_graph = false;
        }

        auto&& framegraph = m_framegraphs[frame.current_frame];

        auto&& present_image = m_surface->images()[frame.current_frame];
        auto&& blit_cmb      = m_command_buffers[frame.current_frame];

        blit_cmb.reset();
        blit_cmb.begin(true);
        auto&& result = framegraph->execute(m_raster_queue);
        if (not result) return std::unexpected { result.error() };

        auto&& semaphore  = *result;
        auto&& backbuffer = framegraph->backbuffer();
        blit_cmb.transition_image_layout(backbuffer,
                                         gpu::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
                                         gpu::ImageLayout::TRANSFER_SRC_OPTIMAL);
        blit_cmb.transition_image_layout(present_image, gpu::ImageLayout::PRESENT_SRC, gpu::ImageLayout::TRANSFER_DST_OPTIMAL);
        blit_cmb
          .blit_image(backbuffer,
                      present_image,
                      gpu::ImageLayout::TRANSFER_SRC_OPTIMAL,
                      gpu::ImageLayout::TRANSFER_DST_OPTIMAL,
                      std::array {
                        gpu::BlitRegion { .src        = {},
                                         .dst        = {},
                                         .src_offset = { math::vec3i { 0, 0, 0 }, backbuffer.extent().to<i32>(), },
                                         .dst_offset = { math::vec3i { 0, 0, 0 }, present_image.extent().to<i32>(), } }
        },
                      gpu::Filter::LINEAR);
        blit_cmb.transition_image_layout(backbuffer,
                                         gpu::ImageLayout::TRANSFER_SRC_OPTIMAL,
                                         gpu::ImageLayout::COLOR_ATTACHMENT_OPTIMAL);
        blit_cmb.transition_image_layout(present_image, gpu::ImageLayout::TRANSFER_DST_OPTIMAL, gpu::ImageLayout::PRESENT_SRC);
        blit_cmb.end();

        auto wait       = as_refs<std::array>(semaphore, frame.image_available);
        auto stage_mask = std::array { gpu::PipelineStageFlag::COLOR_ATTACHMENT_OUTPUT, gpu::PipelineStageFlag::TRANSFER };
        auto signal     = as_refs<std::array>(frame.render_finished);

        blit_cmb.submit(m_raster_queue, wait, stage_mask, signal, frame.in_flight);
        return frame;
    }
} // namespace stormkit::engine
