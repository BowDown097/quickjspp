#pragma once
#include "quickjs_fwd.h"
#include <nameof.hpp>
#include <quickjs.h>
#include <vector>

template<typename T>
[[nodiscard]] constexpr const char* qjs_nameof() noexcept { return ::nameof::nameof_type<T>().data(); }

namespace qjs
{
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
        template<typename C, std::ranges::input_range R> requires (!std::ranges::view<C>)
        C make_from_range(R&& r)
        {
        #ifdef __cpp_lib_ranges_to_container
            return std::ranges::to<C>(std::forward<R>(r));
        #else
            if constexpr (std::constructible_from<C, std::ranges::iterator_t<R>, std::ranges::sentinel_t<R>>)
            {
                return C(std::ranges::begin(r), std::ranges::end(r));
            }
            else if constexpr (requires(C& c) { c.push_back(std::declval<std::ranges::range_value_t<R>>()); })
            {
                C c;
                if constexpr (std::ranges::sized_range<R> && requires(C& c, std::ranges::range_size_t<C> n) { c.reserve(n); })
                    c.reserve(static_cast<std::ranges::range_size_t<C>>(std::ranges::size(r)));
                for (auto it = std::ranges::begin(r); it != std::ranges::end(r); ++it)
                    c.push_back(*it);
                return c;
            }
            else
            {
                static_assert(sizeof(C) == 0, "Cannot create container from this range");
            }
        #endif
        }

        /** Helper function to convert and then free JSValue. */
        template<typename T>
        T unwrap_free(JSContext* ctx, JSValueConst val)
        {
            if constexpr (std::is_void_v<T>)
            {
                JS_FreeValue(ctx, val);
                return js_traits<std::decay_t<T>>::unwrap(ctx, val);
            }
            else
            {
                try
                {
                    T result = js_traits<std::decay_t<T>>::unwrap(ctx, val);
                    JS_FreeValue(ctx, val);
                    return result;
                }
                catch (...)
                {
                    JS_FreeValue(ctx, val);
                    throw;
                }
            }
        }

        /** Immutable view over a QuickJS string which frees the string on destruction. */
        class jsstring_view : public std::string_view
        {
        public:
            jsstring_view(JSContext* ctx, const char* data, std::size_t len)
                : std::string_view(data, len), m_ctx(ctx) {}
            jsstring_view(const jsstring_view&) = delete;

            ~jsstring_view()
            {
                if (m_ctx)
                    JS_FreeCString(m_ctx, data());
            }

            operator const char*() const { return data(); }
        private:
            JSContext* m_ctx;
        };
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

    template<typename T>
    struct rest : std::vector<T>
    {
        using std::vector<T>::vector;
        using std::vector<T>::operator=;
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
