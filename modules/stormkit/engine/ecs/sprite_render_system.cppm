// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:ecs.sprite_render_system;

import std;

import stormkit;

import :core;
import :sprite_renderer;

export namespace stormkit::engine::bidim {
    struct PositionComponent {
        math::fvec2 position = { 0.f, 0.f };

        static constexpr auto component_name() noexcept -> std::string_view { return "PositionComponent"; }

        static constexpr auto type() noexcept -> entities::ComponentType { return hash(component_name()); }
    };

    struct StaticSpriteComponent {
        TextureID texture_id = INVALID_TEXTURE_ID;

        math::fbounding_rect texture_bounds = {};

        static constexpr auto component_name() noexcept -> std::string_view { return "StaticSpriteComponent"; }

        static constexpr auto type() noexcept -> entities::ComponentType { return hash(component_name()); }
    };
} // namespace stormkit::engine::bidim

/////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
/////////////////////////////////////////////////////////////////////

namespace stormkit::engine::bidim {
} // namespace stormkit::engine::bidim
