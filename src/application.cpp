// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/try_expected.hpp>

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;
import :application.world;

namespace stdfs = std::filesystem;

namespace stormkit::engine {
    ////////////////////////////////////////
    ////////////////////////////////////////
    auto World::add_system(std::string name, std::vector<std::string> types, sol::table opt) noexcept -> void {
        auto update = opt.get<std::optional<sol::protected_function>>("update");
        expects(update.has_value(), std::format("No update closure supplied for system {}!", name));

        auto _closures = entities::System::Closures {
            .update =
              [update = *std::move(update)](auto& world, auto delta, const auto& entities) {
                  lua::luacall(update, world, delta.count(), sol::as_table(entities));
              },
        };

        auto pre_update = opt.get<std::optional<sol::protected_function>>("pre_update");
        if (pre_update.has_value())
            _closures.pre_update = [pre_update = *std::move(pre_update)](auto& world, const auto& entities) {
                lua::luacall(pre_update, world, sol::as_table(entities));
            };
        auto post_update = opt.get<std::optional<sol::protected_function>>("post_update");
        if (post_update.has_value())
            _closures.post_update = [post_update = *std::move(post_update)](auto& world, const auto& entities) {
                lua::luacall(post_update, world, sol::as_table(entities));
            };

        auto world = m_world.write();
        world->add_system(std::move(name),
                          types | stdv::transform([](const auto& type) static noexcept {
                              return hash(type);
                          }) | stdr::to<std::vector>(),
                          std::move(_closures));
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto Application::do_init(std::string_view      application_name,
                              stdfs::path&&         lua_dir,
                              const math::uextent2& window_extent,
                              std::string&&         window_title) noexcept -> Expected<void> {
        m_application_logger = log::Module { application_name };

        m_window     = wsi::Window::allocate_and_open(std::move(window_title),
                                                      window_extent,
                                                      wsi::WindowFlag::DEFAULT | wsi::WindowFlag::EXTERNAL_CONTEXT);
        m_renderer   = Try(Renderer::create(application_name, m_thread_pool, as_opt_ref(m_window))
                             .transform_error([](auto) static noexcept { return ApplicationError::FailedToInitializeRenderer; }));
        m_world      = {};
        m_lua_engine = LuaEngine::create(std::move(lua_dir));

        set_current_thread_name("stormkit:main_thread");

        Return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto Application::run() -> void {
        EXPECTS(m_renderer.initialized());
        EXPECTS(m_window != nullptr);
        EXPECTS(m_lua_engine.initialized());

        m_lua_engine->prepend_binder([this](auto& global_state) noexcept {
            auto engine_table = global_state["stormkit"].template get_or_create<sol::table>();

            bind_world(engine_table);

            engine_table["world"]     = World { world() };
            engine_table["resources"] = std::ref(renderer().resources());

            bind_common_components(engine_table);
        });

        auto window_is_open = std::atomic_bool { true };
        m_render_thread     = std::jthread { bind_front(&Application::render_thread, this, std::ref(window_is_open)) };

        auto reload_lua = std::atomic_bool { false };
        m_lua_thread    = std::jthread { bind_front(&Application::lua_thread, this, std::ref(reload_lua)) };

        m_window->on<wsi::EventType::CLOSED>([this, &window_is_open] noexcept {
            window_is_open = false;

            m_render_thread.get_stop_source().request_stop();
            m_lua_thread.get_stop_source().request_stop();

            m_render_thread.join();
            m_lua_thread.join();

            return true;
        });
        m_window->on<wsi::EventType::KEY_DOWN>([this, &reload_lua](auto, auto key, auto) noexcept {
            if (key == wsi::Key::ESCAPE) m_window->close();
            else if (key == wsi::Key::F1)
                reload_lua = true;
        });

        m_window->event_loop([&] mutable {
            m_world.write()->step(fsecond { 0 });

            m_renderer->build_frame(m_build_frame);
        });
    }

    auto Application::render_thread(std::atomic_bool& window_is_open, std::stop_token stop_token) noexcept -> void {
        set_current_thread_name("stormkit:render_thread");

        dlog("Render thread: started. ✓");
        while (not stop_token.stop_requested()) {
            if (not window_is_open) std::this_thread::yield();
            else
                m_renderer->do_render();
        }

        TryAssert(m_renderer->device().wait_idle(), "Failed to wait for device idle!");
        dlog("Render thread: stopped. ✓");
    }

    auto Application::lua_thread(std::atomic_bool& reload_lua, std::stop_token stop_token) noexcept -> void {
        set_current_thread_name("stormkit:lua_thread");

        dlog("Lua thread: started. ✓");
        while (not stop_token.stop_requested()) {
            ilog("Lua engine: boot. ✓");
            auto state = m_lua_engine->boot();
            reload_lua = false;

            while (not reload_lua and not stop_token.stop_requested()) { std::this_thread::yield(); }

            dlog("World: entities cleared. ✓");
            auto world = m_world.write();
            world->destroy_all_entities();
            world->flush();
        }
        dlog("Lua thread: stopped. ✓");
    }

} // namespace stormkit::engine
