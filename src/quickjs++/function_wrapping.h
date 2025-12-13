#pragma once
#include "exception.h"
#include "function_traits.h"
#include "utility.h"

namespace qjs
{
    namespace detail
    {
        template <typename T, std::size_t I, std::size_t NArgs>
        struct unwrap_arg_impl
        {
            static auto unwrap(JSContext* ctx, int argc, JSValueConst* argv)
            {
                if (argc <= I)
                {
                    JS_ThrowTypeError(ctx, "Expected at least %lu arguments but received %d",
                                      (unsigned long)NArgs, argc);
                    throw exception(ctx);
                }
                return js_traits<T>::unwrap(ctx, argv[I]);
            }
        };

        template <typename T, std::size_t I, std::size_t NArgs>
        struct unwrap_arg_impl<rest<T>, I, NArgs>
        {
            static rest<T> unwrap(JSContext* ctx, int argc, JSValueConst* argv)
            {
                static_assert(I == NArgs - 1, "The `rest` argument must be the last function argument.");
                rest<T> result;
                result.reserve(argc - I);
                for (std::size_t i = I; i < argc; ++i)
                    result.push_back(js_traits<T>::unwrap(ctx, argv[i]));
                return result;
            }
        };

        template <typename Tuple, std::size_t Offset, std::size_t... Is>
        auto unwrap_args_impl(JSContext* ctx, int argc, JSValueConst* argv, std::index_sequence<Is...>)
        {
            return std::make_tuple(unwrap_arg_impl<
                std::decay_t<std::tuple_element_t<Is, Tuple>>, Is - Offset, sizeof...(Is)>::unwrap(ctx, argc, argv)...);
        }

        template <typename Tuple, std::size_t TupleOffset = 0>
        auto unwrap_args(JSContext* ctx, int argc, JSValueConst* argv)
        {
            constexpr std::size_t TupleSize = std::tuple_size_v<Tuple>;
            if constexpr (TupleOffset >= TupleSize)
            {
                return std::tuple<>();
            }
            else
            {
                return unwrap_args_impl<Tuple, TupleOffset>(ctx, argc, argv,
                    detail::make_index_range<TupleOffset, TupleSize - 1>());
            }
        }

        template<bool PassThis, typename Function>
        auto apply_unwrapped(Function&& f, JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
        {
            using Traits = function_traits<Function>;
            using Args = typename Traits::args;
            if constexpr (std::is_member_function_pointer_v<std::remove_reference_t<Function>>)
            {
                using Owner = typename Traits::owner_type;
                if constexpr (PassThis)
                {
                    return std::apply(std::forward<Function>(f), std::tuple_cat(
                        std::make_tuple(unwrap_arg_impl<Owner, 0, 1>::unwrap(ctx, 1, &this_val)),
                        unwrap_args<Args>(ctx, argc, argv)
                    ));
                }
                else
                {
                    return std::apply(std::forward<Function>(f), std::tuple_cat(
                        std::make_tuple(unwrap_arg_impl<Owner, 0, 1>::unwrap(ctx, 1, &argv[0])),
                        unwrap_args<Args>(ctx, argc - 1, argv + 1)
                    ));
                }
            }
            else if constexpr (PassThis)
            {
                using FirstArg = std::decay_t<std::tuple_element_t<0, Args>>;
                return std::apply(std::forward<Function>(f), std::tuple_cat(
                    std::make_tuple(unwrap_arg_impl<FirstArg, 0, 1>::unwrap(ctx, 1, &this_val)),
                    unwrap_args<Args, 1>(ctx, argc, argv)
                ));
            }
            else
            {
                return std::apply(std::forward<Function>(f), unwrap_args<Args>(ctx, argc, argv));
            }
        }

        template<bool PassThis, typename Function>
        JSValue wrap_call(JSContext* ctx, Function&& f, JSValueConst this_val, int argc, JSValueConst* argv)
        {
            using R = typename function_traits<Function>::result_type;
            try
            {
                if constexpr (std::is_void_v<R>)
                {
                    apply_unwrapped<PassThis>(std::forward<Function>(f), ctx, this_val, argc, argv);
                    return JS_NULL;
                }
                else
                {
                    return js_traits<std::decay_t<R>>::wrap(ctx,
                        apply_unwrapped<PassThis>(std::forward<Function>(f), ctx, this_val, argc, argv));
                }
            }
            catch (const exception&)
            {
                return JS_EXCEPTION;
            }
            catch (const std::exception& ex)
            {
                JS_ThrowInternalError(ctx, "%s", ex.what());
                return JS_EXCEPTION;
            }
            catch (...)
            {
                JS_ThrowInternalError(ctx, "Unknown error");
                return JS_EXCEPTION;
            }
        }

        template <typename... Args>
        void wrap_args(JSContext* ctx, JSValue* argv, Args&&... args)
        {
            std::size_t i = 0;
            ((argv[i++] = js_traits<std::decay_t<Args>>::wrap(ctx, std::forward<Args>(args))), ...);
        }
    }
}
