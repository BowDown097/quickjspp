#pragma once
#include "quickjs_fwd.h"

namespace qjs
{
    /** Thin wrapper over JSRuntime* rt.
     *  Calls JS_FreeRuntime on destruction. noncopyable.
     */
    class runtime
    {
    public:
        JSRuntime* rt;

        runtime();
        runtime(const runtime&) = delete;

        ~runtime();

        /** @return Pointer to qjs::context of the executed job, or nullptr if no job is pending. */
        context* execute_pending_job();

        bool is_job_pending() const;
    private:
        static JSModuleDef* module_loader(JSContext* ctx, const char* module_name, void* opaque);
        static void promise_rejection_tracker(
            JSContext* ctx, JSValueConst promise, JSValueConst reason, bool is_handled, void* opaque);
    };
}
