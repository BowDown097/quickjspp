#pragma once
#include <tuple>

namespace qjs
{
    template <class T> struct remove_noexcept
    { using type = T; };
    template <class R, class... A> struct remove_noexcept<R(*)(A...) noexcept>
    { using type = R(*)(A...); };
    template <class C, class R, class... A> struct remove_noexcept<R(C::*)(A...) noexcept>
    { using type = R(C::*)(A...); };
    template <class C, class R, class... A> struct remove_noexcept<R(C::*)(A...) const noexcept>
    { using type = R(C::*)(A...) const; };

    template<typename T>
    using remove_noexcept_t = typename remove_noexcept<T>::type;

    template <typename T>
    struct function_traits_impl : function_traits_impl<decltype(&T::operator())> {};

    template <typename ReturnType, typename... Args>
    struct function_traits_impl<ReturnType(Args...)>
    {
        static constexpr std::size_t arity = sizeof...(Args);

        using result_type = ReturnType;
        using function_type = ReturnType(Args...);
        using args = std::tuple<Args...>;

        template <std::size_t I>
        using arg = std::tuple_element_t<I, args>;
    };

    template <typename ReturnType, typename... Args>
    struct function_traits_impl<ReturnType(*)(Args...)> : function_traits_impl<ReturnType(Args...)> {};

    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits_impl<ReturnType(ClassType::*)(Args...)> : function_traits_impl<ReturnType(Args...)>
    { using owner_type = ClassType*; };

    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits_impl<ReturnType(ClassType::*)(Args...) const> : function_traits_impl<ReturnType(Args...)>
    { using owner_type = ClassType*; };

    template<typename T>
    struct function_traits : function_traits_impl<remove_noexcept_t<std::remove_reference_t<T>>> {};
}
