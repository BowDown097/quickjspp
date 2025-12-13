#pragma once
#include "function_wrapping.h"
#include <functional>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace qjs
{
    namespace detail
    {
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

        template<typename Key, typename Value>
        std::unordered_map<Key, Value> get_properties(JSContext* ctx, JSValueConst v)
        {
            if (!JS_IsObject(v))
            {
                JS_ThrowTypeError(ctx, "Value is not an object");
                throw exception(ctx);
            }

            JSPropertyEnum* props;
            uint32_t length;

            if (JS_GetOwnPropertyNames(ctx, &props, &length, v, JS_GPN_STRING_MASK) != 0)
            {
                JS_ThrowInternalError(ctx, "Could not get properties of value");
                throw exception(ctx);
            }

            std::unordered_map<Key, Value> result;
            result.reserve(length);

            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue key = JS_AtomToValue(ctx, props[i].atom);
                JSValue value = JS_GetProperty(ctx, v, props[i].atom);
                result.emplace(unwrap_free<Key>(ctx, key), unwrap_free<Value>(ctx, value));
                JS_FreeAtom(ctx, props[i].atom);
            }

            return result;
        }
    }

    /** JavaScript conversion traits. Describes how to convert type T to/from JSValue. */
    template<typename T>
    struct js_traits
    {
        js_traits() = delete;
        js_traits(const js_traits&) = delete;

        /** Create an object of C++ type T given a context and value.
         *  This function is intentionally not implemented. User should implement this function for their own type.
         *  @param val This value is passed as JSValueConst, so it should be freed by the caller.
         *  @throws exception
         */
        static T unwrap(JSContext* ctx, JSValueConst val) = delete;

        /** Create JSValue from an object of type T and context.
         *  This function is intentionally not implemented. User should implement this function for their own type.
         *  @return JSValue which should be freed by the caller, or JS_EXCEPTION in case of error.
         */
        static JSValue wrap(JSContext * ctx, T value) = delete;
    };

    /** Conversion traits for JSValue (identity). */
    template<>
    struct js_traits<JSValue>
    {
        static JSValue unwrap(JSContext* ctx, JSValueConst val) noexcept
        {
            return JS_DupValue(ctx, val);
        }

        static JSValue wrap(JSContext* ctx, JSValue&& val) noexcept
        {
            return val;
        }
    };

    /** Conversion traits for Value. */
    template<>
    struct js_traits<value>
    {
        static value unwrap(JSContext* ctx, JSValueConst val);
        static JSValue wrap(JSContext* ctx, value val) noexcept;
    };

    /** Conversion traits for boolean. */
    template<>
    struct js_traits<bool>
    {
        static bool unwrap(JSContext* ctx, JSValueConst val) noexcept
        {
            return JS_ToBool(ctx, val) > 0;
        }

        static JSValue wrap(JSContext* ctx, bool val) noexcept
        {
            return JS_NewBool(ctx, val);
        }
    };

    /** Conversion traits for void. */
    template<>
    struct js_traits<void>
    {
        static void unwrap(JSContext* ctx, JSValueConst val)
        {
            if (JS_IsException(val))
                throw exception(ctx);
        }

        static JSValue wrap(JSContext* ctx, JSValueConst val)
        {
            JS_ThrowTypeError(ctx, "Can't wrap a void value");
            throw exception(ctx);
        }
    };

    /** Conversion traits from std::string_view and to detail::jsstring_view. */
    template<>
    struct js_traits<std::string_view>
    {
        static detail::jsstring_view unwrap(JSContext* ctx, JSValueConst val)
        {
            std::size_t length;
            if (const char* data = JS_ToCStringLen(ctx, &length, val))
                return detail::jsstring_view(ctx, data, length);
            else
                throw exception(ctx);
        }

        static JSValue wrap(JSContext* ctx, std::string_view val) noexcept
        {
            return JS_NewStringLen(ctx, val.data(), val.size());
        }
    };

    /** Conversion traits for std::string.
     *  Slower - prefer std::string_view trait whenever possible.
     */
    template<>
    struct js_traits<std::string>
    {
        static std::string unwrap(JSContext* ctx, JSValueConst val)
        {
            detail::jsstring_view str = js_traits<std::string_view>::unwrap(ctx, val);
            return std::string(str.data(), str.size());
        }

        static JSValue wrap(JSContext* ctx, const std::string& val) noexcept
        {
            return JS_NewStringLen(ctx, val.c_str(), val.size());
        }
    };

    /** Conversion traits for const char*. */
    template<>
    struct js_traits<const char*>
    {
        static detail::jsstring_view unwrap(JSContext* ctx, JSValueConst val)
        {
            return js_traits<std::string_view>::unwrap(ctx, val);
        }

        static JSValue wrap(JSContext* ctx, const char* val) noexcept
        {
            return JS_NewString(ctx, val);
        }
    };

    /** Conversion traits for integers.
     *  Will be disabled for uint64_t if JS_NAN_BOXING is enabled, since JSValue becomes aliased to it.
     */
    template<std::integral Integer> requires (sizeof(Integer) <= sizeof(int64_t))
#if defined(JS_NAN_BOXING) && JS_NAN_BOXING
        && (!std::same_as<Integer, uint64_t>)
#endif
    class js_traits<Integer>
    {
    public:
        static Integer unwrap(JSContext* ctx, JSValueConst val)
        {
            if constexpr (sizeof(Integer) > sizeof(int32_t))
                return unwrap_integer<JS_ToInt64, int64_t>(ctx, val);
            else if constexpr (std::is_unsigned_v<Integer>)
                return unwrap_integer<JS_ToUint32, uint32_t>(ctx, val);
            else
                return unwrap_integer<JS_ToInt32, int32_t>(ctx, val);
        }

        static JSValue wrap(JSContext* ctx, Integer val)
        {
            if constexpr (sizeof(Integer) > sizeof(int32_t))
                return wrap_integer<JS_NewInt64, int64_t>(ctx, val);
            else if constexpr (std::is_unsigned_v<Integer>)
                return wrap_integer<JS_NewUint32, uint32_t>(ctx, val);
            else
                return wrap_integer<JS_NewInt32, int32_t>(ctx, val);
        }
    private:
        template<auto F, typename R>
        inline static Integer unwrap_integer(JSContext* ctx, JSValueConst val)
        {
            R result;
            if (F(ctx, &result, val) != 0)
                throw exception(ctx);

            if (!std::in_range<Integer>(result))
            {
                JS_ThrowRangeError(ctx, "Could not unwrap integer into %s", typeid(Integer).name());
                throw exception(ctx);
            }

            return static_cast<Integer>(result);
        }

        template<auto F, typename R>
        inline static JSValue wrap_integer(JSContext* ctx, Integer val)
        {
            if (!std::in_range<R>(val))
            {
                JS_ThrowRangeError(ctx, "Could not wrap integer into %s", typeid(R).name());
                throw exception(ctx);
            }

            return F(ctx, static_cast<R>(val));
        }
    };

    /** Conversion traits for floating point values. */
    template<std::floating_point Float> requires (sizeof(Float) <= sizeof(double))
    struct js_traits<Float>
    {
        static Float unwrap(JSContext* ctx, JSValueConst val)
        {
            double result;
            if (JS_ToFloat64(ctx, &result, val) != 0)
                throw exception(ctx);
            return static_cast<Float>(result);
        }

        static JSValue wrap(JSContext* ctx, Float val) noexcept
        {
            return JS_NewFloat64(ctx, static_cast<double>(val));
        }
    };

    /** Conversion traits for enums. */
    template<typename Enum> requires std::is_enum_v<Enum>
    struct js_traits<Enum>
    {
        using T = std::underlying_type_t<Enum>;

        static Enum unwrap(JSContext* ctx, JSValueConst val)
        {
            return static_cast<Enum>(js_traits<T>::unwrap(ctx, val));
        }

        static JSValue wrap(JSContext* ctx, Enum val) noexcept
        {
            return js_traits<T>::wrap(ctx, static_cast<T>(val));
        }
    };

    /** Conversion traits for std::pair. */
    template<typename U, typename V>
    struct js_traits<std::pair<U, V>>
    {
        static std::pair<U, V> unwrap(JSContext* ctx, JSValueConst val)
        {
            int64_t length;
            if (JS_GetLength(ctx, val, &length) != 0)
            {
                JS_ThrowTypeError(ctx, "js_traits<%s>::unwrap expects array", typeid(std::pair<U, V>).name());
                throw exception(ctx);
            }
            if (length != 2)
            {
                JS_ThrowTypeError(ctx, "js_traits<%s>::unwrap expected array of length 2, got %d",
                                  typeid(std::pair<U, V>).name(), length);
                throw exception(ctx);
            }

            return std::make_pair(
                detail::unwrap_free<U>(ctx, JS_GetPropertyUint32(ctx, val, 0)),
                detail::unwrap_free<V>(ctx, JS_GetPropertyUint32(ctx, val, 1)));
        }

        static JSValue wrap(JSContext* ctx, const std::pair<U, V>& val) noexcept
        {
            try
            {
                JSValue result = JS_NewArray(ctx);
                JS_SetPropertyUint32(ctx, result, 0, js_traits<U>::wrap(ctx, val.first));
                JS_SetPropertyUint32(ctx, result, 1, js_traits<V>::wrap(ctx, val.second));
                return result;
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
    };

    /** Conversion traits for std::optional.
     *  Unlike other types, will return nullopt on unwrap failure rather than throw.
     *  Converts nullopt to null.
     */
    template<typename T>
    struct js_traits<std::optional<T>>
    {
        static auto unwrap(JSContext* ctx, JSValueConst val) noexcept
            -> std::optional<decltype(js_traits<std::decay_t<T>>::unwrap(ctx, val))>
        {
            try
            {
                if (JS_IsNull(val))
                    return std::nullopt;
                return js_traits<std::decay_t<T>>::unwrap(ctx, val);
            }
            catch (const exception&)
            {
                // ignore and clear exception
                JS_FreeValue(ctx, JS_GetException(ctx));
            }
            return std::nullopt;
        }

        static JSValue wrap(JSContext* ctx, const std::optional<T>& val) noexcept
        {
            if (val)
                return js_traits<std::decay_t<T>>::wrap(ctx, val.value());
            return JS_NULL;
        }
    };

    /** Conversion traits for unmapped containers <-> JS arrays. */
    template<std::ranges::input_range Range>
        requires (!detail::is_specialization_of_v<std::ranges::range_value_t<Range>, std::pair>)
    struct js_traits<Range>
    {
        using value_type = std::ranges::range_value_t<Range>;

        static Range unwrap(JSContext* ctx, JSValueConst val)
        {
            int64_t length;
            if (JS_GetLength(ctx, val, &length) != 0)
            {
                JS_ThrowTypeError(ctx, "js_traits<%s>::unwrap expects array", typeid(Range).name());
                throw exception(ctx);
            }

            auto transform = [&](int64_t i) { return detail::unwrap_free<value_type>(ctx, JS_GetPropertyInt64(ctx, val, i)); };
        #ifdef __cpp_lib_ranges_to_container
            return std::views::iota(0LL, length) | std::views::transform(transform) | std::ranges::to<Range>();
        #else
            auto range = std::views::iota(0LL, length) | std::views::transform(transform) | std::views::common;
            return Range(std::ranges::begin(range), std::ranges::end(range));
        #endif
        }

        static JSValue wrap(JSContext* ctx, const Range& val)
        {
            JSValue result = JS_NewArray(ctx);
            for (int64_t i = 0; i < std::ranges::size(val); ++i)
                JS_SetPropertyInt64(ctx, result, i, js_traits<value_type>::wrap(ctx, val[i]));
            return result;
        }
    };

    /** Conversion traits for mapped containers <-> JS objects. */
    template<std::ranges::input_range Range>
        requires detail::is_specialization_of_v<std::ranges::range_value_t<Range>, std::pair>
    struct js_traits<Range>
    {
        using value_type = std::ranges::range_value_t<Range>;

        static Range unwrap(JSContext* ctx, JSValueConst val)
        {
            using K = std::remove_const_t<typename value_type::first_type>;
            using V = value_type::second_type;

            std::unordered_map<K, V> props = detail::get_properties<K, V>(ctx, val);
        #ifdef __cpp_lib_ranges_to_container
            return std::ranges::to<Range>(props);
        #else
            return Range(props.begin(), props.end());
        #endif
        }

        static JSValue wrap(JSContext* ctx, const Range& val)
        {
            using K = std::remove_const_t<typename value_type::first_type>;
            using V = value_type::second_type;

            JSValue result = JS_NewObject(ctx);
            for (auto it = std::ranges::begin(val); it != std::ranges::end(val); ++it)
            {
                JSValue wrapped = js_traits<V>::wrap(ctx, it->second);
                property_traits<K>::set(ctx, result, it->first, wrapped);
                JS_FreeValue(ctx, wrapped);
            }
            return result;
        }
    };

    /** Conversion traits for functions in fwrappers. */
    template<detail::any_invocable Function, bool PassThis>
    struct js_traits<fwrapper<Function, PassThis>>
    {
        static fwrapper<Function, PassThis> unwrap(JSContext* ctx, JSValueConst val)
        {
            JS_ThrowTypeError(ctx, "Can't unwrap function wrapper");
            throw exception(ctx);
        }

        static JSValue wrap(JSContext* ctx, fwrapper<Function, PassThis> val) noexcept
        {
            auto fptr = new std::decay_t<Function>(std::move(val.function));

            JSCClosure* closure = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int, void* opaque) {
                if (auto function = static_cast<std::decay_t<Function>*>(opaque))
                    return detail::wrap_call<PassThis>(ctx, *function, this_val, argc, argv);
                return JS_NULL;
            };

            JSCClosureFinalizerFunc* finalizer = [](void* p) {
                delete static_cast<std::decay_t<Function>*>(p);
            };

            return JS_NewCClosure(ctx, closure, val.name, finalizer, function_traits<Function>::arity, 0, fptr);
        }
    };

    /** Conversion traits for std::function. */
    template<typename R, typename... Args>
    struct js_traits<std::function<R(Args...)>>
    {
        static auto unwrap(JSContext* ctx, JSValueConst val)
        {
            if constexpr (sizeof...(Args) == 0)
            {
                return [ctx, func_obj = JS_DupValue(ctx, val)]() -> R {
                    JSValue result = JS_Call(ctx, func_obj, JS_UNDEFINED, 0, nullptr);
                    JS_FreeValue(ctx, func_obj);
                    if (JS_IsException(result))
                        throw exception(ctx);
                    return detail::unwrap_free<R>(ctx, result);
                };
            }
            else
            {
                return [ctx, func_obj = JS_DupValue(ctx, val)](Args... args) -> R {
                    JSValue argv[sizeof...(Args)];
                    detail::wrap_args(ctx, argv, std::forward<decltype(args)>(args)...);
                    JSValue result = JS_Call(ctx, func_obj, JS_UNDEFINED, sizeof...(Args), argv);
                    JS_FreeValue(ctx, func_obj);
                    for (std::size_t i = 0; i < sizeof...(Args); ++i) JS_FreeValue(ctx, argv[i]);
                    if (JS_IsException(result))
                        throw exception(ctx);
                    return detail::unwrap_free<R>(ctx, result);
                };
            }
        }

        static JSValue wrap(JSContext* ctx, std::function<R(Args...)>&& val)
        {
            JS_ThrowTypeError(ctx, "Can't wrap std::function");
            throw exception(ctx);
        }
    };

    /** Conversion traits for ctor_wrapper. */
    template<typename T, typename... Args> requires std::constructible_from<T, Args...>
    struct js_traits<ctor_wrapper<T, Args...>>
    {
        static ctor_wrapper<T, Args...> unwrap(JSContext* ctx, JSValueConst val)
        {
            JS_ThrowTypeError(ctx, "Can't wrap constructor wrapper");
            throw exception(ctx);
        }

        static JSValue wrap(JSContext* ctx, ctor_wrapper<T, Args...> val) noexcept
        {
            return JS_NewCFunction2(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) noexcept -> JSValue {
                JSValue proto = detail::get_property_prototype(ctx, this_val);
                if (JS_IsException(proto))
                    return proto;

                if (!js_traits<std::shared_ptr<T>>::is_registered())
                    js_traits<std::shared_ptr<T>>::register_class(ctx);

                JSValue jsobj = JS_NewObjectProtoClass(ctx, proto, js_traits<std::shared_ptr<T>>::qjs_class_id);
                JS_FreeValue(ctx, proto);
                if (JS_IsException(jsobj))
                    return jsobj;

                try
                {
                    std::shared_ptr<T> ptr = std::apply(std::make_shared<T, Args...>, detail::unwrap_args<std::tuple<Args...>>(ctx, argc, argv));
                    JS_SetOpaque(jsobj, new std::shared_ptr<T>(std::move(ptr)));
                    return jsobj;
                }
                catch (const exception&)
                {
                    JS_FreeValue(ctx, jsobj);
                    return JS_EXCEPTION;
                }
                catch (const std::exception& ex)
                {
                    JS_FreeValue(ctx, jsobj);
                    JS_ThrowInternalError(ctx, "%s", ex.what());
                    return JS_EXCEPTION;
                }
                catch (...)
                {
                    JS_FreeValue(ctx, jsobj);
                    JS_ThrowInternalError(ctx, "Unknown error");
                    return JS_EXCEPTION;
                }
            }, val.name, sizeof...(Args), JS_CFUNC_constructor, 0);
        }
    };

    /** Conversion traits for std::shared_ptr<T>. Empty shared_ptr corresponds to JS_NULL.
     *  T should be registered to a context before conversions.
     */
    template<typename T>
    struct js_traits<std::shared_ptr<T>>
    {
        /// Registered class id in QuickJS.
        inline static JSClassID qjs_class_id = 0;

        /// Signature of the function to obtain the std::shared_ptr from the JSValue.
        using ptr_cast_fcn_t = std::function<std::shared_ptr<T>(JSContext*, JSValueConst)>;

        /// Used by registerDerivedClass to register new derived classes with this class' base type.
        inline static std::function<void(JSClassID, ptr_cast_fcn_t)> register_with_base;

        /// Mapping between derived class' JSClassID and function to obtain the std::shared_ptr from the JSValue.
        inline static std::unordered_map<JSClassID, ptr_cast_fcn_t> ptr_cast_fcn_map;

        static bool is_registered()
        {
            return qjs_class_id != 0;
        }

        /** Register a class as a derived class.
         *
         *  @tparam D Type of the derived class.
         *  @param derived_class_id Class ID of the derived class.
         *  @param ptr_cast_fcn Function to obtain a std::shared_ptr from the JSValue.
         */
        template<typename D> requires (std::derived_from<D, T> && !std::same_as<D, T>)
        static void register_derived_class(JSClassID derived_class_id, ptr_cast_fcn_t ptr_cast_fcn)
        {
            using derived_ptr_cast_fcn_t = typename js_traits<std::shared_ptr<D>>::ptr_cast_fcn_t;

            // Register how to obtain the std::shared_ptr from the derived class.
            ptr_cast_fcn_map[derived_class_id] = ptr_cast_fcn;

            // Propagate the registration to our base class (if any).
            if (register_with_base)
                register_with_base(derived_class_id, ptr_cast_fcn);

            // Instrument the derived class so that it can propagate new derived classes to us.
            auto old_register_with_base = js_traits<std::shared_ptr<D>>::registerWithBase;
            js_traits<std::shared_ptr<D>>::registerWithBase =
                [old_register_with_base = std::move(old_register_with_base)]
                (JSClassID derived_class_id, derived_ptr_cast_fcn_t derived_ptr_cast_fcn) {
                    if (old_register_with_base)
                        old_register_with_base(derived_class_id, derived_ptr_cast_fcn);
                    register_derived_class<D>(derived_class_id, [derived_cast_fcn = std::move(derived_ptr_cast_fcn)](JSContext* ctx, JSValueConst v) {
                        return std::shared_ptr<T>(derived_cast_fcn(ctx, v));
                    });
                };
        }

        template <typename B> requires std::same_as<B, T> || std::is_void_v<B>
        static void ensure_can_cast_to_base(JSContext*) { }

        template <typename B>
            requires (!std::same_as<B, T> && std::derived_from<T, B>)
        static void ensure_can_cast_to_base(JSContext* ctx)
        {
            if (!is_registered())
                JS_NewClassID(JS_GetRuntime(ctx), &qjs_class_id);
            js_traits<std::shared_ptr<B>>::template register_derived_class<T>(qjs_class_id, unwrap);
        }

        template <auto M>
        static void ensure_can_cast_to_base(JSContext* ctx)
        {
            ensure_can_cast_to_base<detail::class_from_member_pointer_t<decltype(M)>>(ctx);
        }

        /** Stores offsets to qjs::value members of T.
         *  These values should be marked by class_registrar::mark for QuickJS garbage collector
         *  so that the cycle removal algorithm can find the other objects referenced by this object.
         */
        static inline std::vector<value T::*> mark_offsets;

        /** Register class in QuickJS context.
         *
         *  @param ctx QuickJS context
         *  @param name Class name
         *  @param proto Class prototype or JS_NULL
         *  @param call Pointer to QuickJS call function. See QuickJS documentation for more info.
         *  @param exotic Pointer to QuickJS exotic methods. See QuickJS documentation for more info.
         *  @throws exception
         */
        static void register_class(
            JSContext* ctx, const char* name = nullptr, JSValue proto = JS_NULL,
            JSClassCall* call = nullptr, JSClassExoticMethods* exotic = nullptr)
        {
            if (!name)
                name = typeid(T).name();

            JSRuntime* rt = JS_GetRuntime(ctx);
            if (!is_registered())
                JS_NewClassID(rt, &qjs_class_id);

            if (!JS_IsRegisteredClass(rt, qjs_class_id))
            {
                JSClassGCMark* marker{};
                if (!mark_offsets.empty())
                {
                    marker = [](JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
                        auto pptr = static_cast<std::shared_ptr<T>*>(JS_GetOpaque(val, qjs_class_id));
                        const T* ptr = pptr->get();
                        for (value T::* member : mark_offsets)
                            JS_MarkValue(rt, (*ptr.*member).v, mark_func);
                    };
                }

                JSClassDef class_def {
                    .class_name = name,
                    .finalizer = [](JSRuntime* rt, JSValue val) noexcept {
                        delete static_cast<std::shared_ptr<T>*>(JS_GetOpaque(val, qjs_class_id));
                    },
                    .gc_mark = marker,
                    .call = call,
                    .exotic = exotic
                };

                if (JS_NewClass(rt, qjs_class_id, &class_def) < 0)
                {
                    JS_ThrowInternalError(ctx, "Could not register class %s", name);
                    throw exception(ctx);
                }
            }

            JS_SetClassProto(ctx, qjs_class_id, proto);
        }

        static std::shared_ptr<T> unwrap(JSContext* ctx, JSValueConst val)
        {
            std::shared_ptr<T> ptr;
            if (JS_IsNull(val))
                return ptr;

            JSClassID class_id = JS_GetClassID(val);
            if (class_id == qjs_class_id) // of class T
            {
                ptr = *static_cast<std::shared_ptr<T>*>(JS_GetOpaque2(ctx, val, class_id));
            }
            else if (ptr_cast_fcn_map.count(class_id)) // of a class derived from T
            {
                ptr = ptr_cast_fcn_map[class_id](ctx, val);
            }
            else // none of the above
            {
                JS_ThrowTypeError(ctx, "Expected type %s, got object with class ID %d", typeid(T).name(), class_id);
                throw exception(ctx);
            }

            if (!ptr)
            {
                JS_ThrowInternalError(ctx, "Object's opaque pointer is null");
                throw exception(ctx);
            }

            return ptr;
        }

        /** Create a JSValue from std::shared_ptr<T>.
         *  Creates an object with class if #qjs_class_id and sets its opaque pointer to a new copy of #ptr.
         */
        static JSValue wrap(JSContext* ctx, std::shared_ptr<T> val)
        {
            if (!val)
                return JS_NULL;
            if (!is_registered())
                register_class(ctx);

            JSValue jsobj = JS_NewObjectClass(ctx, qjs_class_id);
            if (!JS_IsException(jsobj))
                JS_SetOpaque(jsobj, new std::shared_ptr<T>(std::move(val)));

            return jsobj;
        }
    };

    /** Conversion traits for non-owning pointers to class T. nullptr corresponds to JS_NULL. */
    template<typename T> requires std::is_class_v<T>
    struct js_traits<T*>
    {
        static T* unwrap(JSContext* ctx, JSValueConst val)
        {
            if (JS_IsNull(val))
                return nullptr;
            return js_traits<std::shared_ptr<T>>::unwrap(ctx, val).get();
        }

        static JSValue wrap(JSContext* ctx, T* val)
        {
            if (!val)
                return JS_NULL;
            if (!js_traits<std::shared_ptr<T>>::is_registered())
                js_traits<std::shared_ptr<T>>::register_class(ctx);

            JSValue jsobj = JS_NewObjectClass(ctx, js_traits<std::shared_ptr<T>>::qjs_class_id);
            if (JS_IsException(jsobj))
                return jsobj;

            auto pptr = new std::shared_ptr<T>(val, [](T*) {});
            JS_SetOpaque(jsobj, pptr);
            return jsobj;
        }
    };
}
