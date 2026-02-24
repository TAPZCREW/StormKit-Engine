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

export namespace stormkit::engine::bidim {
    struct PositionComponent {
        math::fvec2 position = { 0.f, 0.f };

        constexpr auto component_name() const noexcept -> std::string_view { return "PositionComponent"; }

        constexpr auto type() const noexcept -> entities::ComponentType { return hash(component_name()); }
    };

    struct StaticSpriteComponent {
        TextureID texture_id = INVALID_TEXTURE_ID;

        math::fbounding_rect texture_bounds = {};

        constexpr auto component_name() const noexcept -> std::string_view { return "StaticSpriteComponent"; }

        constexpr auto type() const noexcept -> entities::ComponentType { return hash(component_name()); }
    };

    class STORMKIT_ENGINE_API SpriteRenderer {
      public:
        explicit SpriteRenderer(const Renderer& renderer) noexcept;
        ~SpriteRenderer() noexcept;

        SpriteRenderer(const SpriteRenderer&)                    = delete;
        auto operator=(const SpriteRenderer&) -> SpriteRenderer& = delete;

        SpriteRenderer(SpriteRenderer&&) noexcept;
        auto operator=(SpriteRenderer&&) noexcept -> SpriteRenderer&;

      private:
    };

    STORMKIT_ENGINE_API auto init_2d_renderer(Application& application) noexcept -> void;
} // namespace stormkit::engine::bidim

// /////////////////////////////////////////////////////////////////////
// ///                      IMPLEMENTATION                          ///
// /////////////////////////////////////////////////////////////////////

// namespace stormkit::engine {
//     //////////////////////////////////////
//     //////////////////////////////////////
//     inline auto make_sprite(entities::EntityManager& world, const gpu::ImageView& texture, const math::fvec2& size) noexcept
//       -> entities::Entity {
//         const auto e = world.make_entity();

//    auto& sprite_component          = world.add_component<SpriteComponent>(e);
//    sprite_component.sprite.texture = as_opt_ref(texture);
//    for (auto&& vertex : sprite_component.sprite.vertices) {
//        vertex.position.x *= size.x;
//        vertex.position.y *= size.y;
//    }

//    return e;
// }

//    //////////////////////////////////////
//    //////////////////////////////////////
//    inline SpriteRenderSystem::SpriteRenderSystem(const Renderer&           renderer,
//                                                  const math::Extent2<f32>& viewport,
//                                                  entities::EntityManager&  manager)
//        : System { manager, 0, { SpriteComponent::TYPE } },
//          m_renderer { SpriteRenderer::create(renderer, viewport).value() } { // TODO handle error
//    }

//    //////////////////////////////////////
//    //////////////////////////////////////
//    inline SpriteRenderSystem::~SpriteRenderSystem() = default;

//    //////////////////////////////////////
//    //////////////////////////////////////
//    inline SpriteRenderSystem::SpriteRenderSystem(SpriteRenderSystem&&) noexcept = default;

//    //////////////////////////////////////
//    //////////////////////////////////////
//    inline auto SpriteRenderSystem::operator=(SpriteRenderSystem&&) noexcept -> SpriteRenderSystem& = default;

//    //////////////////////////////////////
//    //////////////////////////////////////
//    inline auto SpriteRenderSystem::update(fsecond _) -> void {
//    }

//    //////////////////////////////////////
//    //////////////////////////////////////
//    inline auto SpriteRenderSystem::update_framegraph(FrameGraphBuilder& graph) noexcept -> void {
//        m_renderer.update_framegraph(graph);
//    }

//    //////////////////////////////////////
//    //////////////////////////////////////
//    inline auto SpriteRenderSystem::on_message_received(const entities::Message& message) -> void {
//        if (message.id == entities::EntityManager::ADDED_ENTITY_MESSAGE_ID) {
//            for (auto&& e : message.entities) {
//                if (not m_manager->has_component<SpriteComponent>(e)) continue;

//    const auto& sprite_component = m_manager->getComponent<SpriteComponent>(e);
//    const auto  id               = m_renderer.add_sprite(sprite_component.sprite);
//    m_sprite_map[id]             = e;
// }
// } else if (message.id == entities::EntityManager::REMOVED_ENTITY_MESSAGE_ID) {
// for (auto&& e : message.entities) {
//    if (not m_manager->has_component<SpriteComponent>(e)) continue;

//    const auto id = m_sprite_map[e];
//    m_renderer.removeSprite(id);
// }
// }
// }
// } // namespace stormkit::engine
