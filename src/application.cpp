// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/try_expected.hpp>

module stormkit.engine;

import std;

import stormkit.core;
import stormkit.wsi;
import stormkit.gpu;
import stormkit.log;
import stormkit.entities;
import stormkit.image;
import stormkit.luau;

import :core;

namespace stdfs = std::filesystem;

namespace stormkit::engine {
    auto Application::do_init(std::string_view          application_name,
                              stdfs::path&&             main_lua_file,
                              const math::Extent2<u32>& window_extent,
                              std::string&&             window_title) noexcept -> Expected<void> {
        expects(stdfs::is_regular_file(main_lua_file));
        m_window = wsi::Window::open(std::move(window_title),
                                     window_extent,
                                     wsi::WindowFlag::DEFAULT | wsi::WindowFlag::EXTERNAL_CONTEXT);

        auto engine = luau::Engine::create(main_lua_file);
        log::lua::init_lua(engine.global_namespace());
        entities::lua::init_lua(engine.global_namespace());
        image::lua::init_lua(engine.global_namespace());
        wsi::lua::init_lua(engine.global_namespace());
        gpu::lua::init_lua(engine.global_namespace());

        m_renderer = TryAssert(Renderer::create(application_name, m_thread_pool, as_opt_ref(m_window)),
                               "Failed to initialize renderer. ❌");

        m_world = entities::EntityManager {};

        Return {};
    }

    auto Application::run() -> void {
        auto framegraph_mutex = std::mutex {};
        auto rebuild_graph    = std::atomic_bool { true };

        bool rendering_started = false;

        m_renderer->start_rendering(framegraph_mutex, rebuild_graph);
        m_window->event_loop([&] {
            m_world->step(Secondf { 0 });

            m_renderer->render_frame(framegraph_mutex, rebuild_graph, m_build_frame);
            // if (m_surf0ace->needRecreate()) {
            // m_surface->recreate();
            // do_initPerFrameObjects();
            //}
        });
    }
} // namespace stormkit::engine
