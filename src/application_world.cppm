module;

#include <stormkit/lua/lua.hpp>

module stormkit.engine:application.world;

import std;
import stormkit;

namespace stdr = std::ranges;
namespace stdv = std::views;

export namespace stormkit::engine {
    struct World {
        World(Locked<entities::EntityManager>& world) noexcept : m_world { world } {}

        auto make_entity() noexcept -> decltype(auto) {
            auto world = m_world.write();
            return world->make_entity();
        }

        auto destroy_entity(entities::Entity e) noexcept -> decltype(auto) {
            auto world = m_world.write();
            world->destroy_entity(e);
        }

        auto destroy_all_entities() noexcept -> decltype(auto) {
            auto world = m_world.write();
            world->destroy_all_entities();
        }

        auto has_entity(entities::Entity e) noexcept -> decltype(auto) {
            auto world = m_world.read();
            return world->has_entity(e);
        }

        auto add_component(entities::Entity e, sol::table component) noexcept -> decltype(auto) {
            const auto type_closure = component.get<std::optional<sol::protected_function>>("type");
            ensures(type_closure.has_value(), "Missing type() function on lua component");

            const auto type   = sol::protected_function { *type_closure };
            const auto result = lua::luacall(type, component);
            const auto value  = sol::object { result };

            ensures(value.is<std::string>(), "Component type() must return a string");
            const auto name  = value.as<std::string>();
            const auto _type = hash(name);

            auto it = stdr::find_if(m_components_converter, [&name](const auto& converter) noexcept {
                return converter.name == name;
            });
            if (it != stdr::cend(m_components_converter)) {
                const auto& converter = *it;
                auto        world     = m_world.write();

                converter.add(*world, e, component);
            } else {
                auto world = m_world.write();
                world->add_component<entities::lua::LuaComponent>(e,
                                                                  entities::lua::LuaComponent { .data  = std::move(component),
                                                                                                ._type = _type });
            }
        }

        auto get_component(sol::this_state state, entities::Entity e, std::string_view name) noexcept -> sol::reference {
            auto it = stdr::find_if(m_components_converter, [&name](const auto& converter) noexcept {
                return converter.name == name;
            });
            if (it != stdr::cend(m_components_converter)) {
                const auto& converter = *it;
                auto        world     = m_world.write();

                return converter.get(sol::state_view { state }, *world, e);
            }

            auto world = m_world.write();
            return world->get_component<entities::lua::LuaComponent>(e, name).data;
        }

        auto get_component(sol::this_state state, entities::Entity e, std::string_view name) const noexcept -> sol::reference {
            auto it = stdr::find_if(m_components_converter, [&name](const auto& converter) noexcept {
                return converter.name == name;
            });
            if (it != stdr::cend(m_components_converter)) {
                const auto& converter = *it;
                auto        world     = m_world.read();

                return converter.get_const(sol::state_view { state }, *world, e);
            }

            auto world = m_world.read();
            return world->get_component<entities::lua::LuaComponent>(e, name).data;
        }

        auto has_component(entities::Entity entity, std::string_view name) noexcept -> decltype(auto) {
            auto world = m_world.read();
            return world->has_component(entity, name);
        }

        auto entities() noexcept -> decltype(auto) {
            auto world = m_world.read();
            return world->entities();
        }

        auto entity_count() noexcept -> decltype(auto) {
            auto world = m_world.read();
            return world->entity_count();
        }

        auto components_types_of(entities::Entity e) noexcept -> decltype(auto) {
            auto world = m_world.read();
            return world->components_types_of(e);
        }

        auto add_system(std::string name, std::vector<std::string> types, sol::table opt) noexcept -> void;

        auto has_system(std::string_view name) noexcept -> decltype(auto) {
            auto world = m_world.read();
            return world->has_system(name);
        }

        auto remove_system(std::string_view name) noexcept -> decltype(auto) {
            auto world = m_world.write();
            world->remove_system(name);
        }

        Locked<entities::EntityManager>& m_world;

        struct ComponentConverter {
            std::string                                                                                      name;
            std::function<void(entities::EntityManager&, entities::Entity, sol::object)>                     add;
            std::function<sol::reference(sol::state_view, entities::EntityManager&, entities::Entity)>       get;
            std::function<sol::reference(sol::state_view, const entities::EntityManager&, entities::Entity)> get_const;
        };

        using ComponentConverters = std::vector<ComponentConverter>;

        ComponentConverters m_components_converter;
    };

    auto bind_world(sol::table& entities) noexcept -> void {
        auto world = [&entities]() { return entities.new_usertype<World>("world", sol::no_constructor); }();

        world["make_entity"]          = &World::make_entity;
        world["destroy_entity"]       = &World::destroy_entity;
        world["destroy_all_entities"] = &World::destroy_all_entities;
        world["has_entity"]           = &World::has_entity;
        world["add_component"]        = &World::add_component;
        world.set_function("get_component",
                           sol::overload(sol::resolve<sol::reference(sol::this_state,
                                                                     entities::Entity,
                                                                     std::string_view)>(&World::get_component),
                                         sol::resolve<sol::reference(sol::this_state, entities::Entity, std::string_view)
                                                        const>(&World::get_component)));
        world["has_component"]       = &World::has_component;
        world["entities"]            = &World::entities;
        world["entity_count"]        = &World::entity_count;
        world["components_types_of"] = &World::components_types_of;
        world["add_system"]          = &World::add_system;
        world["has_system"]          = &World::has_system;
        world["remove_system"]       = &World::remove_system;
    }

    template<entities::meta::IsComponentType T>
    auto bind_component_to_world(World& world, std::string_view name) noexcept {
        world.m_components_converter.emplace_back(World::ComponentConverter {
          .name = std::string { name },
          .add  = [](entities::EntityManager& world,
                     entities::Entity         e,
                     sol::object              component) static noexcept { world.add_component<T>(e, auto(component.as<T>())); },
          .get =
            [](sol::state_view state, entities::EntityManager& world, entities::Entity e) noexcept {
                auto& component = world.get_component<T>(e);
                return sol::make_reference(state, std::ref(component));
            },
          .get_const =
            [](sol::state_view state, const entities::EntityManager& world, entities::Entity e) noexcept {
                const auto& component = world.get_component<T>(e);
                return sol::make_reference(state, std::cref(component));
            },
        });
    }
} // namespace stormkit::engine
