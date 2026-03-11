module;

#include <stormkit/core/platform_macro.hpp>

export module stormkit.engine:dirty;

import std;

import stormkit;

export namespace stormkit::engine {
    template<typename T, bool THREAD_SAFE = false>
    class Dirtyable {
        using DirtyMarkerType = meta::If<THREAD_SAFE, std::atomic_bool, bool>;

        struct PrivateTag {};

      public:
        template<typename... Args>
        Dirtyable(Args&&..., bool, PrivateTag) noexcept(meta::IsNoexceptConstructible<T, Args...>);

        ~Dirtyable() noexcept;

        Dirtyable(const Dirtyable&)                   = delete;
        auto operator=(const Dirtyable&) -> Dirtyable = delete;

        Dirtyable(Dirtyable&&) noexcept(meta::IsNoexceptMoveConstructible<T>)
            requires(meta::IsMoveConstructible<T>);
        auto operator=(Dirtyable&&) noexcept(meta::IsNoexceptMoveAssignable<T>) -> Dirtyable&
            requires(meta::IsMoveAssignable<T>);

        template<typename... Args>
        static auto create(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>) -> Dirtyable<T, THREAD_SAFE>;
        template<typename... Args>
        static auto create_dirty(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>) -> Dirtyable<T, THREAD_SAFE>;

        template<typename... Args>
        static auto allocate(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>)
          -> Heap<Dirtyable<T, THREAD_SAFE>>;
        template<typename... Args>
        static auto allocate_dirty(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>)
          -> Heap<Dirtyable<T, THREAD_SAFE>>;

        auto mark_dirty() noexcept -> void;
        auto mark_not_dirty() noexcept -> void;
        auto dirty() const noexcept -> bool;

        auto write() & noexcept -> T&;
        auto write() && noexcept -> T&&;
        auto read() const noexcept -> const T&;
        auto read() && noexcept -> T&&;

        operator bool() const noexcept;

      private:
        T               m_value;
        DirtyMarkerType m_dirty;
    };
} // namespace stormkit::engine

/////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
/////////////////////////////////////////////////////////////////////

namespace stormkit::engine {

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    template<typename... Args>
    STORMKIT_FORCE_INLINE
    inline Dirtyable<T, THREAD_SAFE>::Dirtyable(Args&&... args,
                                                bool dirty,
                                                PrivateTag) noexcept(meta::IsNoexceptConstructible<T, Args...>)
        : m_value { std::forward<Args>(args)... }, m_dirty { dirty } {
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline Dirtyable<T, THREAD_SAFE>::~Dirtyable() noexcept = default;

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline Dirtyable<T, THREAD_SAFE>::Dirtyable(Dirtyable&& other) noexcept(meta::IsNoexceptMoveConstructible<T>)
        requires(meta::IsMoveConstructible<T>)
        : m_value { std::move(other.m_value) } {
        if constexpr (meta::Is<std::atomic_bool, decltype(m_dirty)>) {
            m_dirty.store(other.m_dirty.load());
            other.m_dirty.store(false);
        } else
            m_dirty = std::exchange(other.m_dirty, false);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::operator=(Dirtyable&& other) noexcept(meta::IsNoexceptMoveAssignable<T>) -> Dirtyable&
        requires(meta::IsMoveAssignable<T>)
    {
        if (&other == this) [[unlikely]]
            return *this;

        m_value = std::move(other.m_value);
        if constexpr (meta::Is<std::atomic_bool, DirtyMarkerType>) {
            m_dirty.store(other.m_dirty.load());
            other.m_dirty.store(false);
        } else
            m_dirty = std::exchange(other.m_dirty, false);

        return *this;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    template<typename... Args>
        STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::create(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>)
      -> Dirtyable<T, THREAD_SAFE> {
        return Dirtyable { std::forward<Args>(args)..., false, PrivateTag {} };
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    template<typename... Args>
        STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::create_dirty(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>)
      -> Dirtyable<T, THREAD_SAFE> {
        return Dirtyable { std::forward<Args>(args)..., false, PrivateTag {} };
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    template<typename... Args>
        STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::allocate(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>)
      -> Heap<Dirtyable<T, THREAD_SAFE>> {
        return allocate_unsafe<Dirtyable>(std::forward<Args>(args)..., false, PrivateTag {});
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    template<typename... Args>
        STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::allocate_dirty(Args&&... args) noexcept(meta::IsNoexceptConstructible<T, Args...>)
      -> Heap<Dirtyable<T, THREAD_SAFE>> {
        return allocate_unsafe<Dirtyable>(std::forward<Args>(args)..., true, PrivateTag {});
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::mark_dirty() noexcept -> void {
        m_dirty = true;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::mark_not_dirty() noexcept -> void {
        m_dirty = false;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::dirty() const noexcept -> bool {
        return m_dirty;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::write() & noexcept -> T& {
        mark_dirty();
        return m_value;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::write() && noexcept -> T&& {
        return std::move(m_value);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::read() const noexcept -> const T& {
        return m_value;
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline auto Dirtyable<T, THREAD_SAFE>::read() && noexcept -> T&& {
        return std::move(m_value);
    }

    //////////////////////////////////////
    //////////////////////////////////////
    template<typename T, bool THREAD_SAFE>
    STORMKIT_FORCE_INLINE
    inline Dirtyable<T, THREAD_SAFE>::operator bool() const noexcept {
        return dirty();
    }
} // namespace stormkit::engine
