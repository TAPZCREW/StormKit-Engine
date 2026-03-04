// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/try_expected.hpp>

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit;

import :core;

namespace stdfs = std::filesystem;

namespace stormkit::engine {
    auto Application::do_init(std::string_view      application_name,
                              const math::uextent2& window_extent,
                              std::string&&         window_title) noexcept -> Expected<void> {
        m_window = wsi::Window::allocate_and_open(std::move(window_title),
                                                  window_extent,
                                                  wsi::WindowFlag::DEFAULT | wsi::WindowFlag::EXTERNAL_CONTEXT);

        m_renderer = TryAssert(Renderer::allocate(application_name, m_thread_pool, as_opt_ref(m_window)),
                               "Failed to initialize renderer. ❌");

        m_world = core::allocate_unsafe<entities::EntityManager>();

        Return {};
    }

    auto Application::run(stdfs::path boot_lua) -> void {
        expects(stdfs::is_regular_file(boot_lua));

        auto window_is_open = std::atomic_bool { true };

        m_window->on<wsi::EventType::CLOSED>([&window_is_open] noexcept {
            window_is_open = false;
            return true;
        });
        m_window->on<wsi::EventType::KEY_DOWN>([this](auto, auto key, auto) noexcept {
            if (key == wsi::Key::ESCAPE) m_window->close();
        });

        auto lua_started = false;
        m_renderer->start_rendering(window_is_open);
        m_window->event_loop([&] mutable {
            if (not lua_started) {
                auto _      = m_thread_pool.post_task<void>([this, &boot_lua] mutable noexcept {
                    lua::Engine::run(std::move(boot_lua),
                                     {
                                       .log      = true,
                                       .image    = true,
                                       .entities = true,
                                       .wsi      = true,
                                       .gpu      = true,
                                     },
                                     [this](auto& global_state) mutable noexcept {
                                         auto engine_table = global_state["stormkit"].template get_or_create<sol::table>();
                                         bind_common_components(engine_table);
                                         engine_table["window"]    = std::ref(*m_window);
                                         engine_table["world"]     = std::ref(*m_world);
                                         engine_table["resources"] = std::ref(m_renderer->resources());

                                         for (auto&& binder : m_binders) binder(global_state);
                                     });
                });
                lua_started = true;
                ilog("Lua engine stared. ✓");
            }

            if (window_is_open) {
                m_world->step(fsecond { 0 });
                m_renderer->build_frame(m_build_frame);
            }
        });
    }
} // namespace stormkit::engine
