#include "exception.h"
#include "context.h"

namespace qjs
{
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
