module;

#include <stormkit/core/contract_macro.hpp>

module stormkit.engine;

import std;

import stormkit;

import :renderer;

using namespace std::chrono_literals;

namespace stdr = std::ranges;

namespace stormkit::engine {
    /////////////////////////////////////
    /////////////////////////////////////
    auto RenderSurface::do_init(const Renderer& renderer, const wsi::Window& window) noexcept -> gpu::Expected<void> {
        const auto& instance     = renderer.instance();
        const auto& device       = renderer.device();
        const auto& raster_queue = renderer.raster_queue();
        const auto& command_pool = renderer.main_command_pool();

        using core::monadic::emplace_to;

        return gpu::Surface::create_from_window(instance, window)
          .transform(core::monadic::set(m_surface))
          .and_then(bind_front(gpu::SwapChain::create, as_ref(device), as_ref(m_surface), as_ref(window.extent()), std::nullopt))
          .transform(core::monadic::set(m_swapchain))
          .and_then([&, this] noexcept -> gpu::Expected<void> {
              for (auto _ : range(stdr::size(m_swapchain->images()))) {
                  auto res = gpu::Semaphore::create(device)
                               .transform(emplace_to(m_image_availables))
                               .and_then(bind_front(gpu::Semaphore::create, as_ref(device)))
                               .transform(emplace_to(m_render_finisheds))
                               .and_then(bind_front(gpu::Fence::create_signaled, as_ref(device)))
                               .transform(emplace_to(m_in_flight_fences));
                  if (not res) return std::unexpected { res.error() };
              }

              return {};
          })
          .and_then(bind_front(&gpu::CommandPool::create_command_buffers,
                               &command_pool,
                               stdr::size(m_swapchain->images()),
                               gpu::CommandBufferLevel::PRIMARY))
          .and_then([&, this](auto&& transition_command_buffers) noexcept -> gpu::Expected<void> {
              for (auto i : range(stdr::size(transition_command_buffers))) {
                  auto&& image                     = m_swapchain->images()[i];
                  auto&& transition_command_buffer = transition_command_buffers[i];

                  transition_command_buffer.begin(true);
                  transition_command_buffer
                    .transition_image_layout(image, gpu::ImageLayout::UNDEFINED, gpu::ImageLayout::PRESENT_SRC);
                  transition_command_buffer.end();
              }

              auto res = gpu::Fence::create(device);
              if (not res) return std::unexpected { res.error() };
              auto& fence = *res;
              auto  cmbs  = to_refs(transition_command_buffers);
              raster_queue.submit({ .command_buffers = std::move(cmbs) }, as_opt_ref(fence));

              return fence.wait().transform(core::monadic::discard());
          });
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto RenderSurface::begin_frame(const gpu::Device& device) -> gpu::Expected<Frame> {
        EXPECTS(m_surface.initialized());
        EXPECTS(m_swapchain.initialized());

        const auto& image_available = m_image_availables[m_current_frame];
        const auto& render_finished = m_render_finisheds[m_current_frame];
        auto&       in_flight       = m_in_flight_fences[m_current_frame];

        return in_flight.wait()
          .transform([&in_flight](auto&& _) noexcept { in_flight.reset(); })
          .and_then(bind_front(&gpu::SwapChain::acquire_next_image, &(m_swapchain.get()), 100ms, as_ref(image_available)))
          .transform([&, this](auto&& _result) noexcept {
              auto&& [result, image_index] = _result; // TODO handle result
              return Frame { .current_frame   = as<u32>(m_current_frame),
                             .image_index     = image_index,
                             .image_available = as_ref(image_available),
                             .render_finished = as_ref(render_finished),
                             .in_flight       = as_ref(in_flight) };
          });
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto RenderSurface::present_frame(const gpu::Queue& queue, const Frame& frame) -> gpu::Expected<void> {
        const auto image_indices   = std::array { frame.image_index };
        const auto wait_semaphores = as_refs<std::array>(frame.render_finished);
        const auto swapchains      = as_refs<std::array>(m_swapchain);

        return queue.present(swapchains, wait_semaphores, image_indices).transform([this](auto&& _) noexcept {
            if (++m_current_frame >= buffering_count()) m_current_frame = 0;
        });
    }
} // namespace stormkit::engine
