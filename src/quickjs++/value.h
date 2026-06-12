#pragma once
#include "js_traits.h"
#include "property_traits.h"
#include <cassert>

namespace qjs
{
    namespace detail
    {
        template<typename Key>
        struct property_proxy
        {
            JSContext* ctx;
            Key key;
            JSValue this_obj;

            ~property_proxy() noexcept { JS_FreeValue(ctx, this_obj); }

            /** Conversion helper function. */
            template<typename T>
            T as() const { return unwrap_free<T>(ctx, property_traits<Key>::get(ctx, this_obj, key)); }

            /** Implicit converion to value. */
            operator value() const; // defined later due to ::value being incomplete at this point

            template<typename T> requires has_js_traits<std::decay_t<T>>
            property_proxy& operator=(T&& val)
            {
                property_traits<Key>::set(ctx, this_obj, key,
                    js_traits<std::decay_t<T>>::wrap(ctx, std::forward<T>(val)));
                return *this;
            }

            template<detail::any_invocable F> requires std::same_as<Key, const char*>
            property_proxy& operator=(F&& f)
            {
                property_traits<Key>::set(ctx, this_obj, key,
                    js_traits<fwrapper<F>>::wrap(ctx, fwrapper<F> { std::forward<F>(f), key }));
                return *this;
            }

            // ensure C strings are decayed with overload
            property_proxy& operator=(const char* s)
            {
                property_traits<Key>::set(ctx, this_obj, key, js_traits<const char*>::wrap(ctx, s));
                return *this;
            }

            template<typename Key2>
            property_proxy<Key2> operator[](Key2 key2) const
            {
                return { ctx, std::move(key2), as<JSValue>() };
            }

            /** Forwarded value functions, where applicable. */
            template<typename Key2, typename Value>
            std::unordered_map<Key2, Value> properties() const
            {
                return as<value>().template properties<Key2, Value>();
            }

            std::string to_json() const { return as<value>().to_json(); }
            std::string to_json(const value& replacer) const { return as<value>().to_json(replacer); }
            std::string to_json(const value& replacer, const value& space) const { return as<value>().to_json(replacer, space); }

            // defined later due to ::value being incomplete at this point
            value eval_this(std::string_view buffer, const char* filename = "<evalThis>", int flags = 0) const;

            template<typename F, typename... Args>
            void invoke_then(F&& callback, Args&&... args) const
            {
                return as<value>().invoke_then(std::forward<F>(callback), std::forward<Args>(args)...);
            }
        };

        /** Creates getters/setters for a class member variable to be used with the JavaScript runtime. */
        template <auto M>
        struct get_set {};

        // M -  member object
        template <class T, typename R, R T::*M>
        struct get_set<M>
        {
            inline static constexpr bool is_const_v = std::is_const_v<R>;
            static const R& get(const std::shared_ptr<T>& ptr) { return *ptr.*M; }
            static R& set(std::shared_ptr<T> ptr, R value) { return *ptr.*M = std::move(value); }
        };

        // M - static member object
        template <typename R, R *M>
        struct get_set<M>
        {
            inline static constexpr bool is_const_v = std::is_const_v<R>;
            static const R& get(bool) { return *M; }
            static R& set(bool, R value) { return *M = std::move(value); }
        };
    }

    /** JSValue with RAAI semantics.
     *  A wrapper over (JSContext* ctx, JSValue v).
     *  Calls JS_FreeValue(ctx, v) on destruction. Can be copied and moved.
     *  A JSValue can be released by either JSValue x = std::move(value); or JSValue x = value.release(), then the qjs::value becomes invalid and FreeValue won't be called
     *  Can be converted to C++ type, for example: auto string = value.as<std::string>(); qjs::exception would be thrown on error
     *  Properties can be accessed (read/write): value["property1"] = 1; value[2] = "2";
     */
    class value
    {
    public:
        JSContext* ctx;
        JSValue v;

        /** Creates a JS value converted from a C++ object. context::new_value is preferred. */
        template<typename T>
            requires (!std::same_as<T, JSValue> && has_js_traits<std::decay_t<T>>)
        value(JSContext* ctx, T&& val)
            : ctx(ctx), v(js_traits<std::decay_t<T>>::wrap(ctx, std::forward<T>(val)))
        {
            if (JS_IsException(v))
                throw exception(ctx);
        }

        explicit value(JSValue&& val) noexcept
            : ctx(nullptr), v(std::move(val)) {}

        value(JSContext* ctx, JSValue&& val)
            : ctx(ctx), v(std::move(val))
        {
            if (JS_IsException(v))
                throw exception(ctx);
        }

        value(const value& val) noexcept
        {
            ctx = val.ctx;
            v = JS_DupValue(ctx, val.v);
        }

        value(value&& val) noexcept
            : ctx(val.ctx), v(val.v)
        {
            val.ctx = nullptr;
            val.v = JS_UNDEFINED;
        }

        ~value() noexcept
        {
            if (ctx && JS_VALUE_GET_TAG(v) != JS_TAG_MODULE)
                JS_FreeValue(ctx, v);
        }

        value& operator=(value val) noexcept
        {
            std::swap(ctx, val.ctx);
            std::swap(v, val.v);
            return *this;
        }

        bool operator==(JSValueConst other) const noexcept
        {
            return JS_VALUE_GET_TAG(v) == JS_VALUE_GET_TAG(other) &&
                   JS_VALUE_GET_PTR(v) == JS_VALUE_GET_PTR(other);
        }

        bool operator!=(JSValueConst other) const noexcept
        {
            return !((*this) == other);
        }

        bool operator==(const value& val) const noexcept { return (*this == val.v); }
        bool operator!=(const value& val) const noexcept { return !((*this) == val); }

        /** Conversion helper function.
         *  @tparam T Type to convert to.
         *  @return Type returned by js_traits<std::decay_t<T>>::unwrap that should be implicitly convertible to T.
         */
        template<typename T> requires has_js_traits<std::decay_t<T>>
        auto as() const { return js_traits<std::decay_t<T>>::unwrap(ctx, v); }

        JSValue release() noexcept
        {
            ctx = nullptr;
            return v;
        }

        /** Implicit conversion to JSValue (rvalue only). Example: JSValue v = std::move(value); */
        operator JSValue()&& noexcept { return release(); }

        /** Access JS properties. Returns proxy type which is implicitly convertible to this type. */
        template<typename Key> requires has_property_traits<std::decay_t<Key>>
        detail::property_proxy<Key> operator[](Key key) const
        {
            assert(ctx && "Trying to access properties of value with no context");
            return { ctx, std::move(key), JS_DupValue(ctx, v) };
        }

        template<typename Key, typename Value>
        std::unordered_map<Key, Value> properties() const
        {
            return detail::get_properties<Key, Value>(ctx, v);
        }

        // add_getter_setter<&T::get_member, &T::set_member>("member");
        template<auto FGet, auto FSet>
        value& add_getter_setter(const char* name)
        {
            JSAtom prop = JS_NewAtom(ctx, name);
            using fgetter = fwrapper<decltype(FGet), true>;
            using fsetter = fwrapper<decltype(FSet), true>;
            int ret = JS_DefinePropertyGetSet(ctx, v, prop,
                js_traits<fgetter>::wrap(ctx, fgetter { FGet, name }),
                js_traits<fsetter>::wrap(ctx, fsetter { FSet, name }),
                JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE | JS_PROP_ENUMERABLE);
            JS_FreeAtom(ctx, prop);
            if (ret < 0)
                throw exception(ctx);
            return *this;
        }

        // add_getter<&T::get_member>("member");
        template<auto FGet>
        value& add_getter(const char* name)
        {
            JSAtom prop = JS_NewAtom(ctx, name);
            using fgetter = fwrapper<decltype(FGet), true>;
            int ret = JS_DefinePropertyGetSet(ctx, v, prop,
                js_traits<fgetter>::wrap(ctx, fgetter { FGet, name }),
                JS_UNDEFINED, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
            JS_FreeAtom(ctx, prop);
            if (ret < 0)
                throw exception(ctx);
            return *this;
        }

        // add<&T::member>("member"); OR add<&T::static_member>("static_member");
        template<auto M> requires (std::is_member_pointer_v<decltype(M)> || std::is_pointer_v<decltype(M)>)
        value& add_member(const char* name)
        {
            constexpr bool is_membfunc = std::is_member_function_pointer_v<decltype(M)>;
            if constexpr (std::is_function_v<std::remove_pointer_t<decltype(M)>> || is_membfunc)
            {
                (*this)[name] = fwrapper<decltype(M), is_membfunc> { M, name };
                return *this;
            }
            else if constexpr (std::is_object_v<decltype(M)>)
            {
                using GetSet = detail::get_set<M>;
                if constexpr (GetSet::is_const_v)
                    return add_getter<GetSet::get>(name);
                else
                    return add_getter_setter<GetSet::get, GetSet::set>(name);
            }
        }

        std::string to_json(const value& replacer = value(JS_UNDEFINED), const value& space = value(JS_UNDEFINED)) const
        {
            assert(ctx);
            assert(!replacer.ctx || ctx == replacer.ctx);
            assert(!space.ctx || ctx == space.ctx);
            return detail::unwrap_free<std::string>(ctx, JS_JSONStringify(ctx, v, replacer.v, space.v));
        }

        /** Same as context::eval() but with this value as 'this'. */
        value eval_this(std::string_view buffer, const char* filename = "<evalThis>", int flags = 0) const
        {
            assert(buffer.data()[buffer.size()] == '\0' && "eval buffer is not null-terminated"); // JS_Eval requirement
            assert(ctx);
            return value(ctx, JS_EvalThis(ctx, v, buffer.data(), buffer.size(), filename, flags));
        }

        /** Invoke the stored value if it is a callable object or Promise.
         *
         *  If the value is a callable object, it is invoked with the provided arguments.
         *  If the value is a Promise, it is awaited.
         *  Once the operation completes, the given callback is invoked with the result, if any.
         *
         *  @param callback Function to be called once the operation finishes with the result, if any.
         *  @param args Arguments to pass to the callable object (ignored for Promises).
         */
        template<typename F, typename... Args>
        void invoke_then(F&& callback, Args&&... args) const
        {
            using Traits = function_traits<F>;
            if constexpr (Traits::arity > 0)
            {
                return invoke_then_impl<typename Traits::template arg<0>>(
                    std::forward<F>(callback), std::forward<Args>(args)...);
            }
            else
            {
                return invoke_then_impl<void>(
                    std::forward<F>(callback), std::forward<Args>(args)...);
            }
        }
    private:
        template<typename R, typename F, typename... Args>
            requires std::is_void_v<R> || std::invocable<F, std::remove_reference_t<R>&&>
        void invoke_then_impl(F&& callback, Args&&... args) const
        {
            JSPromiseStateEnum promiseState = JS_PromiseState(ctx, v);
            if (promiseState == JS_PROMISE_PENDING)
            {
                handle_pending_promise<R>(std::forward<F>(callback), ctx, v);
            }
            else if (promiseState == JS_PROMISE_FULFILLED)
            {
                if constexpr (std::is_void_v<R>)
                    callback();
                else
                    callback(detail::unwrap_free<std::decay_t<R>>(ctx, JS_PromiseResult(ctx, v)));
            }
            else if (promiseState == JS_PROMISE_REJECTED)
            {
                JS_Throw(ctx, JS_PromiseResult(ctx, v));
                throw exception(ctx);
            }
            else if (JS_IsAsyncFunction(v))
            {
                // async functions will return a pending promise that we can immediately handle
                value promise = as<std::function<value(Args...)>>()(std::forward<Args>(args)...);
                handle_pending_promise<R>(std::forward<F>(callback), ctx, promise.v);
            }
            else if (JS_IsFunction(ctx, v))
            {
                if constexpr (std::is_void_v<R>)
                {
                    callback();
                }
                else
                {
                    value result = as<std::function<value(Args...)>>()(std::forward<Args>(args)...);
                    callback(result.as<R>());
                }
            }
            else
            {
                throw exception(ctx, JS_TYPE_ERROR, "Value is not callable");
            }
        }

        template<typename R, typename F>
        static void handle_pending_promise(F&& callback, JSContext* ctx, JSValueConst v)
        {
            auto cbptr = new std::decay_t<F>(std::forward<F>(callback));

            JSCClosure* closure = [](JSContext* ctx, JSValueConst, int, JSValueConst* argv, int, void* opaque) {
                if (auto cb = static_cast<std::decay_t<F>*>(opaque))
                {
                    if constexpr (std::is_void_v<R>)
                        (*cb)();
                    else
                        (*cb)(js_traits<std::decay_t<R>>::unwrap(ctx, argv[0]));
                }
                return JS_NULL;
            };

            JSCClosureFinalizerFunc* finalizer = [](void* p) {
                delete static_cast<std::decay_t<F>*>(p);
            };

            JSValue then = JS_NewCClosure(ctx, closure, nullptr, finalizer, 0, 0, cbptr);
            JSAtom atom = JS_NewAtom(ctx, "then");
            JSValue ret = JS_Invoke(ctx, v, atom, 1, &then);

            JS_FreeAtom(ctx, atom);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, then);
        }
    };

    template<typename Key>
    detail::property_proxy<Key>::operator value() const { return as<value>(); }

    template<typename Key>
    value detail::property_proxy<Key>::eval_this(std::string_view buffer, const char* filename, int flags) const
    {
        return as<value>().eval_this(buffer, filename, flags);
    }
}
