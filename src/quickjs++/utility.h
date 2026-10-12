#pragma once
#include "quickjs_fwd.h"
#include <nameof.hpp>
#include <quickjs.h>
#include <vector>

template<auto V> inline constexpr auto _qjs_enum_name_storage = ::nameof::nameof_enum<V>();
template<auto M> inline constexpr auto _qjs_member_name_storage = ::nameof::nameof_member<M>();
template<auto V> inline constexpr auto _qjs_pointer_name_storage = ::nameof::nameof_pointer<V>();
template<typename T> inline constexpr auto _qjs_short_name_storage = ::nameof::nameof_short_type<T>();

template<typename T>
[[nodiscard]] consteval const char* qjs_nameof() noexcept
{ return ::nameof::nameof_type<T>().data(); }

template<auto V> requires std::is_enum_v<decltype(V)>
[[nodiscard]] consteval const char* qjs_nameof_enum() noexcept
{ return _qjs_enum_name_storage<V>.data(); }

template<auto M> requires std::is_member_pointer_v<decltype(M)>
[[nodiscard]] consteval const char* qjs_nameof_member() noexcept
{ return _qjs_member_name_storage<M>.data(); }

template<auto V> requires std::is_pointer_v<decltype(V)>
[[nodiscard]] consteval const char* qjs_nameof_pointer() noexcept
{ return _qjs_pointer_name_storage<V>.data(); }

template<typename T>
[[nodiscard]] consteval const char* qjs_short_nameof() noexcept
{ return _qjs_short_name_storage<T>.data(); }

namespace qjs
{
    template<typename T>
    struct rest : std::vector<T>
    {
        using std::vector<T>::vector;
        using std::vector<T>::operator=;
    };

    /** RAII wrapper for a JS runtime value. */
    template<typename T, auto Dtor>
        requires std::is_invocable_v<decltype(Dtor), JSContext*, T>
    class scoped_value
    {
    public:
        scoped_value(JSContext* ctx, T v) noexcept
            : m_ctx(ctx), m_value(v) {}

        scoped_value(scoped_value&& other) noexcept
            : m_ctx(std::exchange(other.m_ctx, nullptr)),
              m_value(other.m_value) {}

        scoped_value(const scoped_value&) = delete;

        ~scoped_value() noexcept
        {
            reset();
        }

        scoped_value& operator=(scoped_value&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                m_ctx = std::exchange(other.m_ctx, nullptr);
                m_value = other.m_value;
            }

            return *this;
        }

        scoped_value& operator=(const scoped_value&) = delete;

        T& get() & noexcept { return m_value; }
        const T& get() const& noexcept { return m_value; }

        explicit operator bool() const noexcept
            requires std::is_pointer_v<T>
        {
            return m_ctx && m_value;
        }

        T release() noexcept
        {
            m_ctx = nullptr;
            return m_value;
        }
    private:
        void reset() noexcept
        {
            if (m_ctx)
            {
                Dtor(m_ctx, m_value);
                m_ctx = nullptr;
            }
        }

        JSContext* m_ctx;
        T m_value;
    };

    namespace detail
    {
        /** Type trait that evalutes to true if `T` is an instantiation of the class template `Primary`. */
        template<class T, template<class...> class Primary>
        struct is_specialization_of : std::false_type {};
        template<template<class...> class Primary, class... Args>
        struct is_specialization_of<Primary<Args...>, Primary> : std::true_type {};
        template<class T, template<class...> class Primary>
        inline constexpr bool is_specialization_of_v = is_specialization_of<T, Primary>::value;

        /** Helper trait to obtain `T` in `T::*` expressions */
        template<typename T>
        struct class_from_member_pointer { using type = void; };
        template<typename T, typename U>
        struct class_from_member_pointer<T U::*> { using type = U; };
        template<typename T>
        using class_from_member_pointer_t = typename class_from_member_pointer<T>::type;

        /** Concept satisfied by any callable type. */
        template <typename F>
        concept any_invocable =
            std::is_function_v<std::remove_pointer_t<std::decay_t<F>>> ||
            std::is_member_function_pointer_v<std::remove_reference_t<F>> ||
            requires { &std::remove_reference_t<F>::operator(); };

        /** Helper function to essentially convert between ranges. */
        template<typename C, std::ranges::input_range R, typename... Args>
        requires (!std::ranges::view<C>)
        [[nodiscard]] constexpr C make_from_range(R&& r, Args&&... args)
        {
            if constexpr (std::constructible_from<C, R, Args...>)
            {
                return C(std::forward<R>(r), std::forward<Args>(args)...);
            }
            else if constexpr (std::constructible_from<C,
                std::ranges::iterator_t<R>, std::ranges::sentinel_t<R>, Args...>)
            {
                return C(std::ranges::begin(r), std::ranges::end(r), std::forward<Args>(args)...);
            }
            else if constexpr (std::constructible_from<C, Args...>)
            {
                C c(std::forward<Args>(args)...);
                if constexpr (std::ranges::sized_range<R> && requires(std::ranges::range_size_t<C> n) { c.reserve(n); })
                    c.reserve(static_cast<std::ranges::range_size_t<C>>(std::ranges::size(r)));

                for (auto it = std::ranges::begin(r); it != std::ranges::end(r); ++it)
                {
                    auto&& elem = *it;
                    using elem_t = decltype(elem);

                    if constexpr (requires(C& c) { c.emplace_back(std::declval<elem_t>()); })
                        c.emplace_back(std::forward<elem_t>(elem));
                    else if constexpr (requires(C& c) { c.push_back(std::declval<elem_t>()); })
                        c.push_back(std::forward<elem_t>(elem));
                    else if constexpr (requires(C& c) { c.emplace(c.end(), std::declval<elem_t>()); })
                        c.emplace(c.end(), std::forward<elem_t>(elem));
                    else if constexpr (requires(C& c) { c.insert(c.end(), std::declval<elem_t>()); })
                        c.insert(c.end(), std::forward<elem_t>(elem));
                    else
                        static_assert(false, "No insertion method found for the given container");
                }

                return c;
            }
            else
            {
                static_assert(false, "Cannot create container from this range");
            }
        }

        /** Helper function to convert and then free JSValue. */
        template<typename T>
        T unwrap_free(JSContext* ctx, JSValue val)
        {
            scoped_value<JSValue, JS_FreeValue> _val(ctx, val);
            return js_traits<std::decay_t<T>>::unwrap(ctx, val);
        }

        /** Immutable view over a QuickJS string which frees the string on destruction. */
        template<typename CharT, auto Dtor, typename DtorInT = CharT>
        class jsbasic_string_view : public std::basic_string_view<CharT>
        {
        public:
            jsbasic_string_view(JSContext* ctx, const CharT* data, std::size_t len) noexcept
                : std::basic_string_view<CharT>(data, len), m_ctx(ctx) {}

            jsbasic_string_view(JSContext* ctx, const DtorInT* data, std::size_t len) noexcept
                requires (!std::same_as<CharT, DtorInT>)
                : std::basic_string_view<CharT>(reinterpret_cast<const CharT*>(data), len), m_ctx(ctx) {}

            // copying disabled to prevent double free
            jsbasic_string_view(jsbasic_string_view&&) = default;
            jsbasic_string_view& operator=(jsbasic_string_view&&) = default;
            jsbasic_string_view(const jsbasic_string_view&) = delete;
            jsbasic_string_view& operator=(const jsbasic_string_view&) = delete;

            ~jsbasic_string_view() noexcept
            {
                if (m_ctx)
                    Dtor(m_ctx, reinterpret_cast<const DtorInT*>(this->data()));
            }

            operator const CharT*() const noexcept { return this->data(); }
        private:
            JSContext* m_ctx;
        };

        using jsstring_view = jsbasic_string_view<char, JS_FreeCString>;
        using jsu16string_view = jsbasic_string_view<char16_t, JS_FreeCStringUTF16, uint16_t>;
    }

    /** A wrapper type for constructor of type T with arguments Args.
     *  Compilation fails if no such constructor is defined.
     *  @tparam Args Arguments for the constructor.
     */
    template<typename T, typename... Args> requires std::constructible_from<T, Args...>
    struct ctor_wrapper
    {
        const char* name{};
    };

    /** A wrapper type for general callables (functions).
     *  @tparam Function Type of the callable entity.
     *  @tparam PassThis If true, passes JavaScript "this" value as first argument where applicable.
     */
    template<detail::any_invocable Function, bool PassThis = false>
    struct fwrapper
    {
        Function function{};
        const char* name{};
    };

    /** Concept satisfied by any type that has a proper associated implementation of js_traits. */
    template<typename T>
    concept has_js_traits = requires(JSContext* ctx, JSValueConst val) {
        { js_traits<T>::unwrap(ctx, val) } -> std::convertible_to<T>;
        { js_traits<T>::wrap(ctx, std::declval<T>()) } -> std::same_as<JSValue>;
    };

    /** Concept satisfied by any type that has a proper associated implementation of property_traits. */
    template<typename T>
    concept has_property_traits = requires(JSContext* ctx, JSValue this_obj, JSValue val) {
        { property_traits<T>::get(ctx, this_obj, std::declval<T>()) } -> std::same_as<JSValue>;
        { property_traits<T>::set(ctx, this_obj, std::declval<T>(), val) } -> std::same_as<void>;
    };
}
