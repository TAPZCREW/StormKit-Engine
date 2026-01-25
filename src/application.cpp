// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module stormkit.engine;

import std;

import stormkit;

import :core;

namespace stormkit::engine {
    Application::Application(std::string_view          application_name,
                             const math::Extent2<u32>& window_extent,
                             std::string               window_title,
                             PrivateTag) {
        m_window = wsi::Window::open(std::move(window_title),
                                     window_extent,
                                     wsi::WindowFlag::DEFAULT | wsi::WindowFlag::EXTERNAL_CONTEXT);

        m_renderer = Renderer::create(application_name, as_opt_ref(m_window))
                       .transform_error(core::monadic::assert("Failed to initialize renderer"))
                       .value();

        m_world = entities::EntityManager {};
    }

    auto Application::run() -> void {
        auto framegraph_mutex = std::mutex {};
        auto rebuild_graph    = std::atomic_bool { true };

        m_renderer->start_rendering(framegraph_mutex, rebuild_graph);
        m_window->event_loop([&] {
            m_renderer->update_framegraph(framegraph_mutex, rebuild_graph, m_update_framegraph);
            m_world->step(Secondf { 0 });
            // if (m_surf0ace->needRecreate()) {
            // m_surface->recreate();
            // do_initPerFrameObjects();
            //}
        });
    }
} // namespace stormkit::engine
