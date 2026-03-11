// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/try_expected.hpp>

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;

namespace stdfs = std::filesystem;

namespace stormkit::engine {

    /////////////////////////////////////
    /////////////////////////////////////
    auto ResourceStore::load_image(const stdfs::path& path) -> TextureID {
        const auto id = hash(path.string());

        if (m_textures.contains(id)) return id;

        auto image = image::Image {};
        TryAssert(image.load_from_file(path), std::format("Failed to load image {}, reason: !", path.string()));

        const auto& device       = m_renderer->device();
        const auto& command_pool = m_renderer->main_command_pool();
        const auto& raster_queue = m_renderer->raster_queue();

        std::println("AAAAAAAAAAAAAAAAAAAAAAAAAA {}", std::bit_cast<uptr>(device.device_table().vkCreateImage));
        const auto& [it, _] = m_textures
                                .emplace(id,
                                         TryAssert(gpu::Image::create(device,
                                                                      {
                                                                        .extent = image.extent(),
                                                                        .format = gpu::from_image(image.format()),
                                                                        .usages = gpu::ImageUsageFlag::SAMPLED
                                                                                  | gpu::ImageUsageFlag::TRANSFER_DST,
                                                                      }),
                                                   std::format("Failed to allocate gpu resources for image {}!", path.string())));
        const auto& [_, texture] = *it;

        const auto end_str   = std::format("for texture {}", path.string());
        const auto error_str = std::format("upload and layout transition command buffer {}", end_str);

        auto staging_buffer = TryAssert(gpu::Buffer::create(device,
                                                            { .usages = gpu::BufferUsageFlag::TRANSFER_SRC,
                                                              .size   = image.size() }),
                                        std::format("Failed to allocate gpu texture staging buffer {}!", end_str));
        TryAssert(staging_buffer.upload(image.data()),
                  std::format("Failed to upload texture data to staging buffer {}!", end_str));

        const auto copy = {
            gpu::BufferImageCopy {
                                  .buffer_offset       = 0,
                                  .buffer_row_length   = 0,
                                  .buffer_image_height = 0,
                                  .subresource_layers  = {},
                                  .offset              = {},
                                  .extent              = texture.extent() }
        };

        auto cmb = TryAssert(command_pool.create_command_buffer(), std::format("Failed to create {}!", error_str));
        TryAssert(cmb.begin(true), std::format("Failed to begin recording of {}!", error_str));
        cmb.transition_image_layout(texture, gpu::ImageLayout::UNDEFINED, gpu::ImageLayout::TRANSFER_DST_OPTIMAL);
        cmb.copy_buffer_to_image(staging_buffer, texture, as_view(copy));
        cmb.transition_image_layout(texture, gpu::ImageLayout::TRANSFER_DST_OPTIMAL, gpu::ImageLayout::SHADER_READ_ONLY_OPTIMAL);
        TryAssert(cmb.end(), std::format("Failed to end recording of {}!", error_str));

        auto fence = TryAssert(gpu::Fence::create(device), std::format("Failed to create fence for ", error_str));
        TryAssert(cmb.submit(raster_queue, {}, {}, {}, as_ref(fence)), std::format("Failed to submit {}!", error_str));
        TryAssert(fence.wait(), std::format("Failed to wait for {}!", error_str));

        return id;
    }
} // namespace stormkit::engine
