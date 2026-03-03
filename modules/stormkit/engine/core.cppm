// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module;

#include <stormkit/core/platform_macro.hpp>
#include <stormkit/core/try_expected.hpp>

#include <stormkit/lua/lua.hpp>

#include <stormkit/engine/api.hpp>

export module stormkit.engine:core;

import std;

import stormkit;

import :renderer;

namespace stdfs = std::filesystem;

export namespace stormkit::engine {
    enum class ApplicationError {
        FailedToInitializeWindow,
        FailedToInitializeRenderer,
    };

    struct STORMKIT_ENGINE_API KeyHandler {
        HashMap<wsi::Key, std::function<void()>> keys;
    };

    class STORMKIT_ENGINE_API Application final {
        struct PrivateTag {};

      public:
        static constexpr auto DEFAULT_WINDOW_TITLE = "StormKit-Engine";

        template<typename T>
        using Expected          = std::expected<T, ApplicationError>;
        using BuildFrameClosure = std::function<void(FrameBuilder&)>;
        using BindToLuaClosure  = std::function<void(sol::state&)>;

        explicit constexpr Application(PrivateTag) noexcept;
        ~Application();

        Application(const Application&)                    = delete;
        auto operator=(const Application&) -> Application& = delete;

        Application(Application&&) noexcept;
        auto operator=(Application&&) noexcept -> Application&;

        static auto create(std::string_view      application_name,
                           const math::uextent2& window_extent,
                           std::string           window_title = DEFAULT_WINDOW_TITLE) noexcept -> Expected<Application>;
        static auto allocate(std::string_view      application_name,
                             const math::uextent2& window_extent,
                             std::string           window_title = DEFAULT_WINDOW_TITLE) noexcept -> Expected<Heap<Application>>;

        auto renderer(this auto& self) noexcept -> decltype(auto);
        auto world(this auto& self) noexcept -> decltype(auto);
        auto window(this auto& self) noexcept -> decltype(auto);

        auto run(stdfs::path boot_lua) -> void;

        auto set_frame_builder(BuildFrameClosure build_frame) noexcept -> void;

        auto add_binder(BindToLuaClosure&& binder) noexcept -> void;

      private:
        auto do_init(std::string_view application_name, const math::uextent2& window_extent, std::string&& window_title) noexcept
          -> Expected<void>;

        ThreadPool                    m_thread_pool;
        Heap<wsi::Window>             m_window;
        Heap<Renderer>                m_renderer;
        Heap<entities::EntityManager> m_world;

        DeferInit<lua::Engine>        m_lua_engine;
        std::vector<BindToLuaClosure> m_binders;

        BuildFrameClosure m_build_frame = monadic::noop();
    };

    constexpr auto as_string(ApplicationError err) -> std::string_view;
    constexpr auto to_string(ApplicationError err) -> std::string;
} // namespace stormkit::engine

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace stormkit::engine {
    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    constexpr Application::Application(PrivateTag) noexcept {};

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Application::~Application() = default;

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline Application::Application(Application&&) noexcept = default;

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::operator=(Application&&) noexcept -> Application& = default;

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::create(std::string_view      application_name,
                                    const math::uextent2& window_extent,
                                    std::string           window_title) noexcept -> Expected<Application> {
        auto app = Application { PrivateTag {} };
        Try(app.do_init(application_name, window_extent, std::move(window_title)));
        Return app;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::allocate(std::string_view      application_name,
                                      const math::uextent2& window_extent,
                                      std::string           window_title) noexcept -> Expected<Heap<Application>> {
        auto app = allocate_unsafe<Application>(PrivateTag {});
        Try(app->do_init(application_name, window_extent, std::move(window_title)));
        Return app;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::renderer(this auto& self) noexcept -> decltype(auto) {
        return std::forward_like<decltype(self)>(*self.m_renderer);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::world(this auto& self) noexcept -> decltype(auto) {
        return std::forward_like<decltype(self)>(*self.m_world);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::window(this auto& self) noexcept -> decltype(auto) {
        return std::forward_like<decltype(self)>(*self.m_window);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::set_frame_builder(BuildFrameClosure build_frame) noexcept -> void {
        m_build_frame = std::move(build_frame);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    inline auto Application::add_binder(BindToLuaClosure&& binder) noexcept -> void {
        m_binders.emplace_back(std::move(binder));
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    constexpr auto as_string(ApplicationError err) -> std::string_view {
        if (err == ApplicationError::FailedToInitializeWindow) return "Failed to create window";
        else if (err == ApplicationError::FailedToInitializeRenderer)
            return "Failed to Initialize Renderer";

        std::unreachable();

        return "";
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    constexpr auto to_string(ApplicationError err) -> std::string {
        return std::string { as_string(err) };
    }
} // namespace stormkit::engine
