#pragma once
#include "quickjs_fwd.h"
#include <source_location>
#include <utility>

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
        explicit exception(JSContext* ctx, std::source_location loc = std::source_location::current()) noexcept
            : m_ctx(ctx), m_location(loc) {}

        template<typename... Args>
        exception(JSContext* ctx, JSErrorEnum error, const char* fmt, Args&&... args)
            : exception(ctx, error, std::source_location::current(), fmt, std::forward<Args>(args)...) {}

        /** Get the associated context. */
        context& get_context() const;

        /** Clears and returns the occurred exception. */
        value get_value() const;

        /** Get the source location where this exception was thrown. */
        const std::source_location& location() const noexcept;
    private:
        JSContext* m_ctx;
        std::source_location m_location;

        exception(JSContext* ctx, JSErrorEnum error, std::source_location loc, const char* fmt, ...);
    };
}
