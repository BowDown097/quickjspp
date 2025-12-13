#include "js_traits.h"
#include "value.h"

namespace qjs
{
    value js_traits<value>::unwrap(JSContext* ctx, JSValueConst val)
    {
        return value(ctx, JS_DupValue(ctx, val));
    }

    JSValue js_traits<value>::wrap(JSContext* ctx, value val) noexcept
    {
        return val.release();
    }
}
