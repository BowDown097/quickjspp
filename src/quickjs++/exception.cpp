#include "exception.h"
#include "context.h"
#include <cstdarg>

namespace qjs
{
    exception::exception(JSContext* ctx, JSErrorEnum error, const char* fmt, ...) noexcept
        : m_ctx(ctx)
    {
        va_list args;
        va_start(args, fmt);

        switch (error)
        {
        case JS_INTERNAL_ERROR: JS_ThrowInternalError(ctx, fmt, args); break;
        case JS_PLAIN_ERROR: JS_ThrowPlainError(ctx, fmt, args); break;
        case JS_RANGE_ERROR: JS_ThrowRangeError(ctx, fmt, args); break;
        case JS_REFERENCE_ERROR: JS_ThrowReferenceError(ctx, fmt, args); break;
        case JS_SYNTAX_ERROR: JS_ThrowSyntaxError(ctx, fmt, args); break;
        case JS_TYPE_ERROR: JS_ThrowTypeError(ctx, fmt, args); break;
        }

        va_end(args);
    }

    context& exception::get_context() const
    {
        return context::get(m_ctx);
    }

    value exception::get_value() const
    {
        return get_context().get_exception();
    }

    const std::source_location& exception::location() const noexcept
    {
        return m_location;
    }
}
