#pragma once
#include "value.h"
#include <filesystem>

namespace qjs
{
    namespace detail
    {
        std::optional<std::string> read_file(const std::filesystem::path& filepath);
        std::string to_uri(std::string_view filename);
    }

    /** Wrapper over JSContext * ctx
     *  Calls JS_SetContextOpaque(ctx, this); on construction and JS_FreeContext on destruction
     */
    class context
    {
        friend class module;
    public:
        /** Data type returned by the module loader function. */
        struct module_data
        {
            std::optional<std::string> source, url;
            module_data() = default;
            explicit module_data(std::optional<std::string> source)
                : source(std::move(source)) {}
            module_data(std::optional<std::string> url, std::optional<std::string> source)
                : source(std::move(source)), url(std::move(url)) {}
        };

        JSContext* ctx;

        /** Function called to obtain the source of a module. */
        std::function<module_data(std::string_view)> module_loader = [](std::string_view filename) {
            return module_data(detail::to_uri(filename), detail::read_file(filename));
        };

        /** Callback triggered when a Promise rejection won't ever be handled. */
        std::function<void(value)> on_unhandled_promise_rejection;

        explicit context(runtime& rt);
        explicit context(JSRuntime* rt);
        explicit context(JSContext* ctx);
        ~context();

        context(const context&) = delete;
        context(context&&) = default;

        template<typename Function>
        void enqueue_job(Function&& job)
        {
            JSValue job_val = js_traits<std::function<void()>>::wrap(ctx, std::forward<Function>(job));
            int err = JS_EnqueueJob(ctx, [](JSContext* ctx, int argc, JSValueConst* argv){
                try
                {
                    assert(argc >= 1);
                    js_traits<std::function<void()>>::unwrap(ctx, argv[0])();
                }
                catch (const exception&)
                {
                    return JS_EXCEPTION;
                }
                catch (const std::exception& err)
                {
                    JS_ThrowInternalError(ctx, "%s", err.what());
                    return JS_EXCEPTION;
                }
                catch (...)
                {
                    JS_ThrowInternalError(ctx, "Unknown error");
                    return JS_EXCEPTION;
                }
                return JS_UNDEFINED;
            }, 1, &job_val);
            JS_FreeValue(ctx, job_val);
            if(err < 0)
                throw exception(ctx);
        }

        /** Create module and return a reference to it. */
        module& add_module(const char* name);

        /** Returns `globalThis`. */
        value global()
        {
            return value(ctx, JS_GetGlobalObject(ctx));
        }

        /** Returns new Object(). */
        value new_object()
        {
            return value(ctx, JS_NewObject(ctx));
        }

        /** Returns JS value converted from C++ object `val`. */
        template <typename T>
        value new_value(T&& val)
        {
            return value(ctx, std::forward<T>(val));
        }

        /** Returns current exception associated with context and clears it.
         *  Should be called when qjs::exception is caught.
         */
        value get_exception()
        {
            return value(ctx, JS_GetException(ctx));
        }

        /** Register class T for conversions to/from std::shared_ptr<T> to work.
         *  Wherever possible, module.register_class<T>("T")... should be used instead.
         *  @tparam T Class type
         *  @param name Class name in JS engine
         *  @param proto JS class prototype or JS_UNDEFINED. Can be created with class_registrar.
        */
        template <class T>
        void register_class(const char* name, JSValue proto = JS_NULL)
        {
            js_traits<std::shared_ptr<T>>::register_class(ctx, name, proto);
        }

        /// @see JS_Eval
        value eval(std::string_view buffer, const char* filename = "<eval>", int flags = 0);

        value eval_file(const char* filename, int flags = 0);

        /// @see JS_ParseJSON
        value from_json(std::string_view buffer, const char* filename = "<fromJSON>");

        /** Get qjs::context from JSContext opaque pointer */
        static context& get(JSContext* ctx);
    private:
        std::vector<module> m_modules;

        void init();
    };

    /** Module wrapper
     *  Workaround for lack of opaque pointer for module load function.
     */
    class module
    {
        using nvp = std::pair<const char*, value>;
    public:
        module(JSContext* ctx, const char* name);
        module(const module&) = delete;
        module(module&&) = default;

        module& add(const char* name, JSValue&& value);

        template<typename T> requires (has_js_traits<T> || detail::any_invocable<T>)
        module& add(const char* name, T&& value)
        {
            if constexpr (detail::any_invocable<T>)
                return add(name, js_traits<fwrapper<T>>::wrap(m_ctx, { std::forward<T>(value), name }));
            else
                return add(name, js_traits<T>::wrap(m_ctx, std::forward<T>(value)));
        }

        template<typename T> requires std::is_class_v<T>
        class_registrar<T> register_class(const char* name)
        {
            return class_registrar<T>(name, context::get(m_ctx), this);
        }
    private:
        JSContext* m_ctx;
        JSModuleDef* m_def;
        std::vector<nvp> m_exports;
        const char* m_name;

        static bool set_export(JSContext* ctx, JSModuleDef* m, const nvp& e);
    };

    /** Helper class to register class members and constructors.
     *  See fun, constructor.
     *  Actual registration occurs at object destruction.
     */
    template<typename T> requires std::is_class_v<T>
    class class_registrar
    {
    public:
        class_registrar(const char* name, context& context, module* module = nullptr)
            : m_ctor(JS_NULL),
              m_context(context),
              m_module(module),
              m_name(name),
              m_prototype(context.new_object()) {}
        class_registrar(const class_registrar&) = delete;

        ~class_registrar()
        {
            m_context.register_class<T>(m_name, std::move(m_prototype));
        }

        /** Sets the base class.
         *  @tparam B The base class in question
         */
        template<typename B>
            requires (std::is_class_v<B> && !std::same_as<B, T>)
        class_registrar& base()
        {
            assert(js_traits<std::shared_ptr<B>>::QJSClassId && "base class is not registered");
            js_traits<std::shared_ptr<T>>::template ensureCanCastToBase<B>();

            JSValue base_proto = JS_GetClassProto(m_context.ctx, js_traits<std::shared_ptr<B>>::QJSClassId);
            int err = JS_SetPrototype(m_context.ctx, m_prototype.v, base_proto);
            JS_FreeValue(m_context.ctx, base_proto);

            if (err < 0)
                throw exception(m_context.ctx);

            return *this;
        }

        /** Add class constructor.
         *  @tparam Args Constructor arguments
         *  @param name Constructor name (if not specified, class name will be used)
         */
        template<typename... Args> requires std::constructible_from<T, Args...>
        class_registrar& constructor(const char* name = nullptr)
        {
            if (!name)
                name = m_name;
            m_ctor = m_context.new_value(ctor_wrapper<T, Args...> { name });
            JS_SetConstructor(m_context.ctx, m_ctor.v, m_prototype.v);
            if (m_module)
                m_module->add(name, value(m_ctor));
            return *this;
        }

        /** Add free function. */
        template<typename F>
            requires (std::is_function_v<std::remove_pointer_t<F>> ||
                      requires { &std::remove_reference_t<F>::operator(); })
        class_registrar& function(F&& f, const char* name)
        {
            m_prototype[name] = std::forward<F>(f);
            return *this;
        }

        /** All qjs::Value members of T should be marked by mark<> for QuickJS garbage collector
         *  so that the cycle removal algorithm can find the other objects referenced by this object.
         */
        template <value T::* V>
        class_registrar& mark()
        {
            js_traits<std::shared_ptr<T>>::mark_offsets.push_back(V);
            return *this;
        }

        /** Add class member function or class member variable.
         *  Example:
         *  struct T { int var; int func(); }
         *  qjs::module& module = context.add_module("module");
         *  module.register_class<T>("T").member<&T::var>("var").member<&T::func>("func");
         */
        template<auto M> requires std::is_member_pointer_v<decltype(M)>
        class_registrar& member(const char* name)
        {
            js_traits<std::shared_ptr<T>>::template ensure_can_cast_to_base<M>(m_context.ctx);
            if constexpr (std::is_member_function_pointer_v<decltype(M)>)
                m_prototype[name] = fwrapper<decltype(M), true> { M, name };
            else
                m_prototype.add_member<M>(name);
            return *this;
        }

        /** Add a property with custom getter and setter.
         *  Example:
         *  module.register_class<T>("T").property<&T::getX, &T::setX>("x");
         */
        template<auto FGet, auto FSet = nullptr>
        class_registrar& property(const char* name)
        {
            js_traits<std::shared_ptr<T>>::template ensureCanCastToBase<FGet>();
            js_traits<std::shared_ptr<T>>::template ensureCanCastToBase<FSet>();
            if constexpr (std::is_null_pointer_v<decltype(FSet)>)
                m_prototype.add_getter<FGet>(name);
            else
                m_prototype.add_getter_setter<FGet, FSet>(name);
            return *this;
        }

        /** Add a static member or function to the last added constructor.
         *  Example:
         *  struct T { static int var; static int func(); }
         *  module.register_class<T>("T").constructor<>("T").static_member<&T::var>("var").static_member<&T::func>("func");
         */
        template<auto M> requires detail::maybe_static_member_v<decltype(M)>
        class_registrar& static_member(const char* name)
        {
            assert(!JS_IsNull(m_ctor.v) && "You should call .constructor before .static_member");
            js_traits<std::shared_ptr<T>>::template ensure_can_cast_to_base<M>(m_context.ctx);
            m_ctor.add_member<M>(name);
            return *this;
        }
    private:
        value m_ctor;
        context& m_context;
        module* m_module;
        const char* m_name;
        value m_prototype;
    };
}
