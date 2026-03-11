// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

#include <stormkit/log/log_macro.hpp>

#include <stormkit/lua/lua.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:lua_engine;

import std;

import stormkit;

namespace stdfs = std::filesystem;

export namespace stormkit::engine {
    class STORMKIT_ENGINE_API LuaEngine {
        struct PrivateTag {};

      public:
        using BindToLuaClosure = std::function<void(sol::state&)>;

        constexpr LuaEngine(stdfs::path&&, PrivateTag) noexcept;
        ~LuaEngine();

        LuaEngine(const LuaEngine&)                    = delete;
        auto operator=(const LuaEngine&) -> LuaEngine& = delete;

        LuaEngine(LuaEngine&&) noexcept;
        auto operator=(LuaEngine&&) noexcept -> LuaEngine&;

        static auto create(stdfs::path lua_dir) noexcept -> LuaEngine;
        static auto allocate(stdfs::path lua_dir) noexcept -> Heap<LuaEngine>;

        auto boot() -> sol::state;

        auto append_binder(BindToLuaClosure&& binder) noexcept -> void;
        auto prepend_binder(BindToLuaClosure&& binder) noexcept -> void;

      private:
        stdfs::path                   m_lua_dir;
        std::vector<BindToLuaClosure> m_binders;
    };
} // namespace stormkit::engine

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace stormkit::engine {
    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    constexpr LuaEngine::LuaEngine(stdfs::path&& lua_dir, PrivateTag) noexcept
        : m_lua_dir { std::move(lua_dir) } {};

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline LuaEngine::~LuaEngine() = default;

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline LuaEngine::LuaEngine(LuaEngine&&) noexcept = default;

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto LuaEngine::operator=(LuaEngine&&) noexcept -> LuaEngine& = default;

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto LuaEngine::create(stdfs::path lua_dir) noexcept -> LuaEngine {
        auto app = LuaEngine { std::move(lua_dir), PrivateTag {} };
        // Try(app.do_init());
        return app;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto LuaEngine::allocate(stdfs::path lua_dir) noexcept -> Heap<LuaEngine> {
        auto app = allocate_unsafe<LuaEngine>(std::move(lua_dir), PrivateTag {});
        // Try(app->do_init());
        return app;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    inline auto LuaEngine::boot() -> sol::state {
        auto lua_engine = lua::Engine::load_from_file(m_lua_dir / "boot.lua",
                                                      {
                                                        .log      = true,
                                                        .image    = true,
                                                        .entities = true,
                                                        .wsi      = true,
                                                        .gpu      = true,
                                                      },
                                                      [this](auto& global_state) noexcept {
                                                          for (auto&& binder : m_binders) binder(global_state);
                                                      });
        return lua_engine.run();
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto LuaEngine::append_binder(BindToLuaClosure&& binder) noexcept -> void {
        m_binders.emplace_back(std::move(binder));
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto LuaEngine::prepend_binder(BindToLuaClosure&& binder) noexcept -> void {
        m_binders.emplace(std::begin(m_binders), std::move(binder));
    }
} // namespace stormkit::engine
