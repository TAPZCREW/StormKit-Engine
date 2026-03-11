module;

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;
import :ecs;
import :pipeline_2d;
import :application.world;
import :renderer;

namespace sm = stormkit::monadic;

namespace stormkit::engine {
    auto make_sprite(World& _world, TextureID texture_id, const math::fbounding_rect& sprite_rect) {
        auto world = _world.m_world.write();

        auto e = world->make_entity();

        world->add_component(e, pipeline_2d::TransformComponent {});
        world->add_component(e, pipeline_2d::StaticSpriteComponent { texture_id, sprite_rect });

        return e;
    }

    auto bind_pipeline_2d_components(sol::state& global_state, sol::table& engine) noexcept -> void {
        engine.new_usertype<pipeline_2d::TransformComponent>("transform_component",
                                                             sol::constructors<pipeline_2d::TransformComponent(),
                                                                               pipeline_2d::TransformComponent(math::fvec2)> {},
                                                             "position",
                                                             &pipeline_2d::TransformComponent::position,
                                                             "type",
                                                             &pipeline_2d::TransformComponent::component_name);
        engine.new_usertype<
          pipeline_2d::StaticSpriteComponent>("texture_component",
                                              sol::constructors<pipeline_2d::StaticSpriteComponent(),
                                                                pipeline_2d::StaticSpriteComponent(TextureID,
                                                                                                   math::fbounding_rect)> {},
                                              "texture",
                                              &pipeline_2d::StaticSpriteComponent::texture_id,
                                              "type",
                                              &pipeline_2d::StaticSpriteComponent::component_name);

        auto& world = sol::object { engine["world"] }.as<World>();
        bind_component_to_world<pipeline_2d::TransformComponent>(world, pipeline_2d::TransformComponent::component_name());
        bind_component_to_world<pipeline_2d::StaticSpriteComponent>(world, pipeline_2d::StaticSpriteComponent::component_name());
    }

    auto bind_pipeline_2d(sol::state& global_state) noexcept -> void {
        auto engine = global_state["stormkit"].get_or_create<sol::table>();
        bind_pipeline_2d_components(global_state, engine);

        engine.new_usertype<ResourceStore>(
          "resources_store",
          sol::no_constructor,
          "load_image",
          +[](ResourceStore* store, std::string_view path) static noexcept { return store->load_image(stdfs::path { path }); });
        auto pipeline_2d      = engine["2d"].get_or_create<sol::table>();
        engine["make_sprite"] = &make_sprite;
    }

} // namespace stormkit::engine
