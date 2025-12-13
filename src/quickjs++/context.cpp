#include "context.h"
#include "runtime.h"
#include <algorithm>
#include <fstream>

namespace qjs
{
    namespace detail
    {
        std::optional<std::string> read_file(const std::filesystem::path& filepath)
        {
            if (!std::filesystem::exists(filepath))
                return std::nullopt;

            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::string buffer(size, '\0');
            file.read(buffer.data(), size);
            return buffer;
        }

        std::string to_uri(std::string_view filename)
        {
            if (filename.find("://") != std::string::npos)
                return std::string(filename);

            std::string abspath = std::filesystem::absolute(filename).generic_string();
            // magic for drives on Windows
        #ifdef _WIN32
            abspath.insert(abspath.begin(), '/');
        #endif

            return "file://" + abspath;
        }
    }

    context::context(runtime& rt) : context(rt.rt) {}

    context::context(JSRuntime* rt)
    {
        if (!(ctx = JS_NewContext(rt)))
            throw std::runtime_error("Cannot create context");
        init();
    }

    context::context(JSContext* ctx) : ctx(ctx)
    {
        init();
    }

    context::~context()
    {
        // We need to run the GC to flush finalization of any pending unhandled
        // rejected promises before we free the context, as they depend on it's
        // opaque value.
        JS_RunGC(JS_GetRuntime(ctx));

        m_modules.clear();
        JS_FreeContext(ctx);
    }

    module& context::add_module(const char* name)
    {
        m_modules.emplace_back(ctx, name);
        return m_modules.back();
    }

    value context::eval(std::string_view buffer, const char* filename, int flags)
    {
        JSValue v = JS_Eval(ctx, buffer.data(), buffer.size(), filename, flags);

        // For some time now module loads can return a (rejected) promise on
        // failure. Keep old compatibility API for quickjspp's eval.
        if (JS_PromiseState(ctx, v) == JS_PROMISE_REJECTED)
        {
            JSValue result = JS_PromiseResult(ctx, v);
            if (JS_IsError(result))
            {
                JS_FreeValue(ctx, v);
                v = JS_Throw(ctx, result);
            }
            else
            {
                JS_FreeValue(ctx, result);
            }
        }

        return value(ctx, std::move(v));
    }

    value context::eval_file(const char* filename, int flags)
    {
        std::optional<std::string> data = detail::read_file(filename);
        if (!data)
            throw std::runtime_error(std::string("Can't read file: ") + filename);
        return eval(data.value(), filename, flags);
    }

    value context::from_json(std::string_view buffer, const char* filename)
    {
        return value(ctx, JS_ParseJSON(ctx, buffer.data(), buffer.size(), filename));
    }

    context& context::get(JSContext* ctx)
    {
        return *static_cast<context*>(JS_GetContextOpaque(ctx));
    }

    void context::init()
    {
        JS_SetContextOpaque(ctx, this);
    }

    module::module(JSContext* ctx, const char* name)
        : m_ctx(ctx), m_name(name)
    {
        m_def = JS_NewCModule(ctx, name, [](JSContext* ctx, JSModuleDef* m) noexcept {
            std::vector<module>& modules = context::get(ctx).m_modules;
            auto it = std::ranges::find(modules, m, &module::m_def);
            auto set_export = std::bind_front(&module::set_export, ctx, m);
            return (it != modules.end() && std::ranges::all_of(it->m_exports, set_export)) ? 0 : -1;
        });

        if (!m_def)
            throw exception(ctx);
    }

    module& module::add(const char* name, JSValue&& value)
    {
        m_exports.emplace_back(name, qjs::value(m_ctx, std::move(value)));
        JS_AddModuleExport(m_ctx, m_def, name);
        return *this;
    }

    bool module::set_export(JSContext* ctx, JSModuleDef* m, const nvp& e)
    {
        return JS_SetModuleExport(ctx, m, e.first, JS_DupValue(ctx, e.second.v)) == 0;
    }
}
