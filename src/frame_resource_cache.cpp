module;

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

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameResourceCache::cache_old_resources(FrameResources&& resources) noexcept -> void {
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameResourceCache::get_or_create_image(const gpu::Image::CreateInfo& create_info) noexcept -> gpu::Image {
        return TryAssert(gpu::Image::create(m_device, create_info), "Failed to allocate frame image!");
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameResourceCache::get_or_create_buffer(const gpu::Buffer::CreateInfo& create_info) noexcept -> gpu::Buffer {
        return TryAssert(gpu::Buffer::create(m_device, create_info), "Failed to allocate frame buffer!");
    }
} // namespace stormkit::engine
