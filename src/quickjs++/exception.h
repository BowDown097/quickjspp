#pragma once
#include "quickjs_fwd.h"
#include <source_location>

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

        /** Get the associated context. */
        context& get_context() const;

        /** Clears and returns the occurred exception. */
        value get_value() const;

        /** Get the source location where this exception was thrown. */
        const std::source_location& location() const noexcept;
    private:
        JSContext* m_ctx;
        std::source_location m_location;
    };
}
