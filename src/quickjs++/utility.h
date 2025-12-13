#pragma once
#include "quickjs_fwd.h"
#include <quickjs/quickjs.h>
#include <vector>

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

        /** Slight optimization over JS_GetPropertyStr(ctx, this_obj, "prototype") with a constant atom. */
        inline JSValue get_property_prototype(JSContext* ctx, JSValueConst this_obj)
        {
            static const JSAtom prop = JS_NewAtom(ctx, "prototype");
            return JS_GetProperty(ctx, this_obj, prop);
        }

        /** Slight optimization over JS_Invoke(ctx, this_val, JS_NewAtom(ctx, "then"), 1, func) with a constant atom. */
        inline void invoke_on_then(JSContext* ctx, JSValue this_val, JSValue* func)
        {
            static const JSAtom atom = JS_NewAtom(ctx, "then");
            JS_Invoke(ctx, this_val, atom, 1, func);
        }

        template<std::integral T, T Min, T Max> requires (Max >= Min)
        constexpr auto make_integer_range()
        {
            return []<T... Is>(std::integer_sequence<T, Is...>) {
                return std::integer_sequence<T, Is + Min...>{};
            }(std::make_integer_sequence<T, Max - Min + 1>{});
        }

        template<std::size_t Min, std::size_t Max> requires (Max >= Min)
        constexpr auto make_index_range()
        {
            return make_integer_range<std::size_t, Min, Max>();
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
