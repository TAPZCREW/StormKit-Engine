// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:ecs.sprite_render_system;

import std;

import stormkit;

import :core;
import :dirty;

export namespace stormkit::engine::pipeline_2d {
    struct TransformComponent {
        math::fvec2 position = { 0.f, 0.f };
        math::fvec2 scale    = { 0.f, 0.f };
        math::fvec2 rotate   = { 0.f, 0.f };

        static constexpr auto component_name() noexcept -> std::string_view { return "TransformComponent"; }

        static constexpr auto type() noexcept -> entities::ComponentType { return hash(component_name()); }
    };

    struct StaticSpriteComponent {
        TextureID texture_id = INVALID_TEXTURE_ID;

        math::fbounding_rect texture_bounds = {};

        static constexpr auto component_name() noexcept -> std::string_view { return "StaticSpriteComponent"; }

        static constexpr auto type() noexcept -> entities::ComponentType { return hash(component_name()); }
    };

    class STORMKIT_ENGINE_API SpriteRenderSystem {
        struct PrivateTag {};

      public:
        struct Sprite {
            entities::Entity e;
            gpu::ImageView   texture;
            gpu::Sampler     sampler;
        };

        SpriteRenderSystem(PrivateTag) noexcept;
        ~SpriteRenderSystem() noexcept;

        SpriteRenderSystem(const SpriteRenderSystem&)                    = delete;
        auto operator=(const SpriteRenderSystem&) -> SpriteRenderSystem& = delete;

        SpriteRenderSystem(SpriteRenderSystem&&) noexcept;
        auto operator=(SpriteRenderSystem&&) noexcept -> SpriteRenderSystem&;

        static auto create(const Renderer&                 renderer,
                           const gpu::RasterPipelineState& initial_state,
                           const gpu::DescriptorSetLayout& camera_descriptor_set) noexcept -> gpu::Expected<SpriteRenderSystem>;

        auto update(entities::EntityManager& world, fsecond delta, const entities::Entities&) noexcept -> void;

        auto on_message_received(const Renderer&           renderer,
                                 entities::EntityManager&  world,
                                 const entities::Message&  message,
                                 const entities::Entities& entities) noexcept -> void;

        auto insert_tasks(const Application&        application,
                          FrameBuilder&             graph,
                          FrameBuilder::ResourceID  backbuffer_id,
                          FrameBuilder::ResourceID  camera_buffer_id,
                          const gpu::DescriptorSet& camera_descriptor_set,
                          u32                       camera_current_offset) noexcept -> void;

        auto sprites() const noexcept -> const std::vector<Sprite>&;

      private:
        auto do_init(const Renderer&, const gpu::RasterPipelineState&, const gpu::DescriptorSetLayout&) noexcept
          -> gpu::Expected<void>;

        auto update_task(const Application&, FrameBuilder&, FrameBuilder::ResourceID) noexcept -> void;
        auto render_static_sprite_task(FrameBuilder&,
                                       FrameBuilder::ResourceID,
                                       FrameBuilder::ResourceID,
                                       FrameBuilder::ResourceID,
                                       const gpu::DescriptorSet&,
                                       u32) noexcept -> void;

        struct {
            DeferInit<gpu::Shader> vertex_shader;
            DeferInit<gpu::Shader> fragment_shader;

            DeferInit<gpu::DescriptorSetLayout> descriptor_layout;
            DeferInit<gpu::DescriptorSet>       descriptor_set;

            DeferInit<gpu::PipelineLayout> pipeline_layout;
            DeferInit<gpu::Pipeline>       pipeline;
        } m_static_sprite_data;

        struct {
            DeferInit<gpu::DescriptorPool> descriptor_pool;

            DeferInit<gpu::Buffer> buffer;
            u32                    current_offset = 0;
        } m_sprite_data;

        using Sprites = Dirtyable<std::vector<Sprite>>;

        Sprites m_sprites = Sprites::create_dirty();
    };
} // namespace stormkit::engine::pipeline_2d

/////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
/////////////////////////////////////////////////////////////////////

namespace stormkit::engine::pipeline_2d {
    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline SpriteRenderSystem::SpriteRenderSystem(PrivateTag) noexcept
        : m_sprites { Dirtyable<std::vector<Sprite>>::create_dirty() } {
    }

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline SpriteRenderSystem::~SpriteRenderSystem() noexcept = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline SpriteRenderSystem::SpriteRenderSystem(SpriteRenderSystem&& other) noexcept = default;

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto SpriteRenderSystem::operator=(SpriteRenderSystem&& other) noexcept -> SpriteRenderSystem& = default;
    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto SpriteRenderSystem::sprites() const noexcept -> const std::vector<Sprite>& {
        return m_sprites.read();
    }

    //////////////////////////////////////
    //////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto SpriteRenderSystem::create(const Renderer&                 renderer,
                                           const gpu::RasterPipelineState& initial_state,
                                           const gpu::DescriptorSetLayout& camera_descriptor_set) noexcept
      -> gpu::Expected<SpriteRenderSystem> {
        auto out = SpriteRenderSystem { PrivateTag {} };
        Try(out.do_init(renderer, initial_state, camera_descriptor_set));
        Return out;
    }
} // namespace stormkit::engine::pipeline_2d
