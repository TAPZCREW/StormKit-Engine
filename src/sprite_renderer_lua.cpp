module;

#include <stormkit/lua/lua.hpp>

module stormkit.engine:sprite_renderer_lua;

import std;

import stormkit;

import :core;
import :ecs;
import :sprite_renderer;
import :renderer;

namespace sm = stormkit::monadic;

namespace stormkit::engine {
    auto make_sprite(entities::EntityManager& manager, TextureID texture_id, const math::fbounding_rect& sprite_rect) {
        auto e = manager.make_entity();

        manager.add_component(e, bidim::PositionComponent {});
        manager.add_component(e, bidim::StaticSpriteComponent { texture_id, sprite_rect });

        return e;
    }

    auto bind_bidim_components(sol::table& engine) noexcept -> void {
        engine.new_usertype<
          bidim::PositionComponent>("position_component",
                                    sol::constructors<bidim::PositionComponent(), bidim::PositionComponent(math::fvec2)> {},
                                    "position",
                                    &bidim::PositionComponent::position,
                                    "type",
                                    &bidim::PositionComponent::component_name);
        engine.new_usertype<
          bidim::StaticSpriteComponent>("texture_component",
                                        sol::constructors<bidim::StaticSpriteComponent(),
                                                          bidim::StaticSpriteComponent(TextureID, math::fbounding_rect)> {},
                                        "texture",
                                        &bidim::StaticSpriteComponent::texture_id,
                                        "type",
                                        &bidim::StaticSpriteComponent::component_name);
    }

    auto bind_bidim(sol::state& global_state) noexcept -> void {
        auto engine = global_state["stormkit"].get_or_create<sol::table>();
        engine.new_usertype<ResourceStore>(
          "resources_store",
          sol::no_constructor,
          "load_image",
          +[](ResourceStore* store, std::string_view path) static noexcept { return store->load_image(stdfs::path { path }); });
        auto bidim            = engine["2d"].get_or_create<sol::table>();
        engine["make_sprite"] = &make_sprite;

        bind_bidim_components(engine);
    }

} // namespace stormkit::engine
