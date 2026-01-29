module;

#include <stormkit/core/contract_macro.hpp>

#include <stormkit/core/try_expected.hpp>

module stormkit.engine;

import std;

import stormkit.core;
import stormkit.wsi;
import stormkit.gpu;

import :renderer;

using namespace std::chrono_literals;

namespace stdr = std::ranges;
namespace cm   = stormkit::core::monadic;

namespace stormkit::engine {
    /////////////////////////////////////
    /////////////////////////////////////
    auto RenderSurface::do_init(const Renderer& renderer, const wsi::Window& window) noexcept -> gpu::Expected<void> {
        const auto& instance     = renderer.instance();
        const auto& device       = renderer.device();
        const auto& raster_queue = renderer.raster_queue();
        const auto& command_pool = renderer.main_command_pool();

        m_surface   = Try(gpu::Surface::create_from_window(instance, window));
        m_swapchain = Try(gpu::SwapChain::create(device, m_surface, window.extent(), std::nullopt));

        const auto image_count = stdr::size(m_swapchain->images());
        m_buffering_count      = image_count >= 4 ? 3 : image_count;

        for (auto _ : range(m_buffering_count)) {
            m_submission_resources.push_back({
              .in_flight       = Try(gpu::Fence::create_signaled(device)),
              .image_available = Try(gpu::Semaphore::create(device)),
              .render_finished = Try(gpu::Semaphore::create(device)),
            });
        }

        auto transition_command_buffers = Try(command_pool.create_command_buffers(image_count, gpu::CommandBufferLevel::PRIMARY));

        for (auto i : range(image_count)) {
            auto&& image                     = m_swapchain->images()[i];
            auto&& transition_command_buffer = transition_command_buffers[i];

            transition_command_buffer.begin(true);
            transition_command_buffer.transition_image_layout(image, gpu::ImageLayout::UNDEFINED, gpu::ImageLayout::PRESENT_SRC);
            transition_command_buffer.end();
        }

        auto fence = Try(gpu::Fence::create(device));
        auto cmbs  = to_refs(transition_command_buffers);
        Try(raster_queue.submit({ .command_buffers = std::move(cmbs) }, as_opt_ref(fence)));

        Return fence.wait().transform(cm::discard());
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto RenderSurface::begin_frame(const gpu::Device& device) -> gpu::Expected<Frame> {
        auto& submission_resources = m_submission_resources[m_current_frame];

        const auto& image_available = submission_resources.image_available;
        const auto& render_finished = submission_resources.render_finished;
        auto&       in_flight       = submission_resources.in_flight;

        Try(in_flight.wait());
        in_flight.reset();

        auto&& [_, image_index] = Try(m_swapchain->acquire_next_image(100ms, image_available));
        Return Frame { .current_frame        = as<u32>(m_current_frame),
                       .image_index          = image_index,
                       .submission_resources = as_ref(submission_resources) };
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto RenderSurface::present_frame(const gpu::Queue& queue, const Frame& frame) -> gpu::Expected<void> {
        const auto& render_finished = frame.submission_resources->render_finished;

        const auto image_indices   = std::array { frame.image_index };
        const auto wait_semaphores = as_refs<std::array>(render_finished);
        const auto swapchains      = as_refs<std::array>(m_swapchain);

        Try(queue.present(swapchains, wait_semaphores, image_indices));
        if (++m_current_frame >= m_buffering_count) m_current_frame = 0;

        Return {};
    }
} // namespace stormkit::engine
