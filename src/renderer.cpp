module;

#include <stormkit/core/try_expected.hpp>
#include <stormkit/log/log_macro.hpp>

module stormkit.engine;

import std;

import stormkit.core;
import stormkit.log;
import stormkit.gpu;

import :renderer;
import :renderer.framegraph;

using namespace std::literals;

namespace stdr = std::ranges;
namespace cm   = stormkit::monadic;

namespace stormkit::engine {
    LOGGER("Renderer")

    namespace {
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

        m_raster_queue      = gpu::Queue::create(*m_device, m_device->raster_queue_entry());
        m_main_command_pool = Try(gpu::CommandPool::create(*m_device));
        ilog("GPU main command pool successfully initialized. ✓");

        Try(do_init_render_surface(std::move(window)));
        ilog("GPU windowed render surface successfully initialized. ✓");

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
    auto Renderer::thread_loop(std::mutex& framegraph_mutex, std::atomic_bool& rebuild_graph, std::stop_token token) noexcept
      -> void {
        set_current_thread_name("StormKit:RenderThread");
        // auto logger_singleton = log::Logger::create_logger_instance<log::ConsoleLogger>();

        m_command_buffers = TryAssert(m_main_command_pool->create_command_buffers(m_surface->buffering_count()),
                                      "Failed to create main command buffers");

        for (;;) {
            if (token.stop_requested()) break;

            auto frame = TryAssert(m_surface->begin_frame(*m_device), "Failed to start frame!");
            TryAssert(do_render(framegraph_mutex, rebuild_graph, frame), "Failed to render frame!");
            TryAssert(m_surface->present_frame(m_raster_queue, frame), "Failed to present frame!");
        }

        m_device->wait_idle();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto Renderer::do_render(std::mutex& framegraph_mutex, std::atomic_bool& rebuild_graph, RenderSurface::Frame& frame) noexcept
      -> gpu::Expected<void> {
        if (rebuild_graph) {
            auto _ = std::unique_lock { framegraph_mutex };

            for (auto&& frame : m_frames) m_frame_pool.recycle_frame(std::move(*frame));

            m_frames.clear();
            m_frames.resize(m_surface->buffering_count());

            rebuild_graph = false;
        }

        auto& current_frame = m_frames[frame.current_frame];

        if (not current_frame.initialized())
            current_frame = Try(m_frame_builder
                                  .make_frame(*m_device,
                                              m_raster_queue,
                                              m_main_command_pool,
                                              m_frame_pool,
                                              { .x      = 0,
                                                .y      = 0,
                                                .width  = m_extent.to<i32>().width,
                                                .height = m_extent.to<i32>().height }));

        auto&& present_image = m_surface->images()[frame.image_index];
        auto&& blit_cmb      = m_command_buffers[frame.current_frame];

        Try(blit_cmb.reset());
        Try(blit_cmb.begin(true));

        if (current_frame->fence().status() == gpu::Fence::Status::SIGNALED) {
            TryAssert(current_frame->fence().wait(), "");
            TryDiscard(current_frame->fence().reset());
        }
        const auto& semaphore = Try(current_frame->execute(m_raster_queue));

        auto& backbuffer = current_frame->backbuffer();
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

        auto wait       = as_refs<std::array>(semaphore, frame.submission_resources->image_available);
        auto stage_mask = std::array { gpu::PipelineStageFlag::COLOR_ATTACHMENT_OUTPUT, gpu::PipelineStageFlag::TRANSFER };
        auto signal     = as_refs<std::array>(frame.submission_resources->render_finished);

        TryAssert(blit_cmb.submit(m_raster_queue, wait, stage_mask, signal, as_ref(frame.submission_resources->in_flight)), "");

        Return {};
    }
} // namespace stormkit::engine
