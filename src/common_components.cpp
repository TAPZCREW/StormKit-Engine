// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit;

import :application.world;

namespace stormkit::engine {
    auto bind_common_components(sol::table& engine) noexcept -> void {
        engine.new_usertype<DebugNameComponent>("debug_name_component",
                                                sol::constructors<DebugNameComponent(std::string)> {},
                                                "name",
                                                &DebugNameComponent::name,
                                                "type",
                                                &DebugNameComponent::component_name);
        // auto& world = sol::object { engine["world"] }.as<World>();
    }
} // namespace stormkit::engine
