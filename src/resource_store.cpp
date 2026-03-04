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

        ensures(not m_textures.contains(id), std::format("Image {} already loaded!", path.string()));

        auto image = image::Image {};
        TryAssert(image.load_from_file(path), std::format("Failed to load image {}, reason: ", path.string()));

        m_textures
          .emplace(id,
                   TryAssert(gpu::Image::create(m_device,
                                                {
                                                  .extent = image.extent(),
                                                  .format = gpu::from_image(image.format()),
                                                  .usages = gpu::ImageUsageFlag::SAMPLED | gpu::ImageUsageFlag::TRANSFER_DST,
                                                }),
                             std::format("Failed to allocate gpu resources for image {}", path.string())));

        return id;
    }
} // namespace stormkit::engine
