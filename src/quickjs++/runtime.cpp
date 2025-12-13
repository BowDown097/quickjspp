#include "runtime.h"
#include "context.h"

namespace qjs
{
    runtime::runtime()
    {
        if (!(rt = JS_NewRuntime()))
            throw std::runtime_error("Cannot create runtime");

        JS_SetHostPromiseRejectionTracker(rt, promise_rejection_tracker, nullptr);
        JS_SetModuleLoaderFunc(rt, nullptr, module_loader, nullptr);
    }

    runtime::~runtime()
    {
        JS_FreeRuntime(rt);
    }

    context* runtime::execute_pending_job()
    {
        JSContext* ctx;
        int err = JS_ExecutePendingJob(rt, &ctx);
        if (err == 0) // no job to run
            return nullptr;
        else if (err < 0) // job failed
            throw exception(ctx);
        return &context::get(ctx);
    }

    bool runtime::is_job_pending() const
    {
        return JS_IsJobPending(rt);
    }

    JSModuleDef* runtime::module_loader(JSContext* ctx, const char* module_name, void* opaque)
    {
        context::module_data data;
        context& context = context::get(ctx);

        try
        {
            if (context.module_loader)
                data = context.module_loader(module_name);

            if (!data.source)
            {
                JS_ThrowReferenceError(ctx, "Could not load module filename '%s'", module_name);
                return nullptr;
            }

            if (!data.url)
                data.url = module_name;

            value func_val = context.eval(data.source.value(), module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
            assert(JS_VALUE_GET_TAG(func_val.v) == JS_TAG_MODULE);
            JSModuleDef* m = reinterpret_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val.v));

            // set import.meta
            value meta = context.new_value(JS_GetImportMeta(ctx, m));
            meta["url"] = *data.url;
            meta["main"] = false;

            return m;
        }
        catch (const exception&)
        {
            return nullptr;
        }
        catch (const std::exception& ex)
        {
            JS_ThrowInternalError(ctx, "%s", ex.what());
            return nullptr;
        }
        catch (...)
        {
            JS_ThrowInternalError(ctx, "Unknown error");
            return nullptr;
        }
    }

    void runtime::promise_rejection_tracker(
        JSContext* ctx, JSValueConst promise, JSValueConst reason, bool is_handled, void* opaque)
    {
        context& context = context::get(ctx);
        if (context.on_unhandled_promise_rejection)
            context.on_unhandled_promise_rejection(context.new_value(JS_DupValue(ctx, reason)));
    }
}
