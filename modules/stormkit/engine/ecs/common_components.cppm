// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>

#include <stormkit/engine/api.hpp>

#include <stormkit/lua/lua.hpp>

export module stormkit.engine:ecs.common_components;

import std;

import stormkit;

export namespace stormkit::engine {
    struct DebugNameComponent {
        std::string name;

        constexpr auto component_name() const noexcept -> std::string_view { return "DebugNameComponent"; }

        constexpr auto type() const noexcept -> entities::ComponentType { return hash(component_name()); }
    };

    struct TransformComponent {
        math::fmat4 model;

        constexpr auto component_name() const noexcept -> std::string_view { return "TransformComponent"; }

        constexpr auto type() const noexcept -> entities::ComponentType { return hash(component_name()); }
    };

    STORMKIT_ENGINE_API auto bind_common_components(sol::table& engine) noexcept -> void;
} // namespace stormkit::engine
