#pragma once
#include "quickjs_fwd.h"
#include <source_location>

/** Basic reimplementation of the private JSErrorEnum with publicly exposed error types. */
enum JSErrorEnum
{
    JS_INTERNAL_ERROR,
    JS_PLAIN_ERROR,
    JS_RANGE_ERROR,
    JS_REFERENCE_ERROR,
    JS_SYNTAX_ERROR,
    JS_TYPE_ERROR
};

namespace qjs
{
    /** Exception type.
     *  Indicates that exception has occured in JS context.
     */
    class exception
    {
    public:
        explicit exception(JSContext* ctx) noexcept : m_ctx(ctx) {}
        exception(JSContext* ctx, JSErrorEnum error, const char* fmt, ...) noexcept;

        /** Get the associated context. */
        context& get_context() const;

        /** Clears and returns the occurred exception. */
        value get_value() const;

        /** Get the source location where this exception was thrown. */
        const std::source_location& location() const noexcept;
    private:
        JSContext* m_ctx;
        std::source_location m_location = std::source_location::current();
    };
}
