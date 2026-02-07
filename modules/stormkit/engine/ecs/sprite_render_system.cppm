// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>

export module stormkit.engine:ecs.sprite_render_system;

// import std;

// import stormkit.core;
// import stormkit.entities;
// import stormkit.gpu;

// import :renderer;
// import :sprite_renderer;

// using namespace stormkit::entities::literals;

// export namespace stormkit::engine {
//     struct PositionComponent: entities::Component {
//         static constexpr Type TYPE     = "PositionComponent"_component_type;
//         math::fvec2           position = { 0.f, 0.f };
//     };

//    struct SpriteComponent: entities::Component {
//        static constexpr Type TYPE = "SpriteComponent"_component_type;
//        Sprite                sprite;
//    };

//    static_assert(core::meta::DerivedFrom<SpriteComponent, entities::Component>);

//    auto make_sprite(entities::EntityManager& world, const gpu::ImageView& texture, const math::fvec2& size) noexcept
//      -> entities::Entity;

//    class SpriteRenderSystem final: public entities::System {
//      public:
//        SpriteRenderSystem(const Renderer& renderer, const math::Extent2<f32>& viewport, entities::EntityManager& manager);
//        ~SpriteRenderSystem() final;

//    SpriteRenderSystem(const SpriteRenderSystem&)                    = delete;
//    auto operator=(const SpriteRenderSystem&) -> SpriteRenderSystem& = delete;

//    SpriteRenderSystem(SpriteRenderSystem&&) noexcept;
//    auto operator=(SpriteRenderSystem&&) noexcept -> SpriteRenderSystem&;

//    auto update(fsecond delta) -> void final;
//    auto update_framegraph(FrameGraphBuilder& graph) noexcept -> void;

//    private:
//      auto on_message_received(const entities::Message& message) -> void final;

//    SpriteRenderer m_renderer;

//    HashMap<entities::Entity, u32> m_sprite_map;
// };
// } // namespace stormkit::engine

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
