// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/try_expected.hpp>

#include <stormkit/lua/lua.hpp>

module stormkit.engine;

import std;

import stormkit.core;
import stormkit.wsi;
import stormkit.gpu;
import stormkit.log;
import stormkit.entities;
import stormkit.image;
import stormkit.lua;

import :core;

namespace stdfs = std::filesystem;

namespace stormkit::engine {
    auto Application::do_init(std::string_view      application_name,
                              stdfs::path&&         main_lua_file,
                              const math::uextent2& window_extent,
                              std::string&&         window_title) noexcept -> Expected<void> {
        expects(stdfs::is_regular_file(main_lua_file));
        m_window = wsi::Window::allocate_and_open(std::move(window_title),
                                                  window_extent,
                                                  wsi::WindowFlag::DEFAULT | wsi::WindowFlag::EXTERNAL_CONTEXT);

        m_lua_engine = lua::Engine::create(main_lua_file,
                                           {
                                             .log      = true,
                                             .image    = true,
                                             .entities = true,
                                             .wsi      = true,
                                             .gpu      = true,
                                           });
        m_renderer   = TryAssert(Renderer::allocate(application_name, m_thread_pool, as_opt_ref(m_window)),
                                 "Failed to initialize renderer. ❌");

        m_world = core::allocate_unsafe<entities::EntityManager>();

        auto& global_state     = m_lua_engine->global_state();
        auto  engine_table     = global_state["engine"].get_or_create<sol::table>();
        engine_table["window"] = std::ref(*m_window);
        // engine_table["world"] = std::ref(*m_world);

        Return {};
    }

    auto Application::run() -> void {
        auto framegraph_mutex = std::mutex {};
        auto rebuild_graph    = std::atomic_bool { true };

        auto lua_started = false;
        m_renderer->start_rendering(framegraph_mutex, rebuild_graph);
        m_window->event_loop([&] mutable {
            if (not lua_started) {
                auto _      = m_thread_pool
                                .post_task<void>([this] noexcept { TryAssert(m_lua_engine->lua_main(), "lua runtime error!"); });
                lua_started = true;
                ilog("Lua engine stared. ✓");
            }

            m_world->step(fsecond { 0 });

            m_renderer->render_frame(framegraph_mutex, rebuild_graph, m_build_frame);
            // if (m_surf0ace->needRecreate()) {
            // m_surface->recreate();
            // do_initPerFrameObjects();
            //}
        });
    }
} // namespace stormkit::engine
