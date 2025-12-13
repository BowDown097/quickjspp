#pragma once
#include "exception.h"
#include <quickjs/quickjs.h>
#include <string_view>

namespace qjs
{
    /** Traits for accessing object properties.
     *  @tparam Key Property key type (integer or string).
     */
    template<typename Key>
    struct property_traits
    {
        property_traits() = delete;
        property_traits(const property_traits&) = delete;
    };

    template<std::convertible_to<std::string_view> String>
    struct property_traits<String>
    {
        static JSValue get(JSContext* ctx, JSValue this_obj, std::string_view name) noexcept
        {
            return JS_GetPropertyStr(ctx, this_obj, name.data());
        }

        static void set(JSContext* ctx, JSValue this_obj, std::string_view name, JSValue val)
        {
            if (JS_SetPropertyStr(ctx, this_obj, name.data(), val) < 0)
                throw exception(ctx);
        }
    };

    // signed values or > uint32 sized values -> int64_t (signed -> unsigned conversion is scary)
    // <= uint32 unsigned values -> uint32_t
    template<std::integral Integer> requires (sizeof(Integer) <= sizeof(int64_t))
    struct property_traits<Integer>
    {
        static JSValue get(JSContext* ctx, JSValue this_obj, Integer idx) noexcept
        {
            if constexpr (std::numeric_limits<Integer>::max() > UINT32_MAX || std::is_signed_v<Integer>)
                return JS_GetPropertyInt64(ctx, this_obj, static_cast<int64_t>(idx));
            else
                return JS_GetPropertyUint32(ctx, this_obj, static_cast<uint32_t>(idx));
        }

        static void set(JSContext* ctx, JSValue this_obj, Integer idx, JSValue val)
        {
            if constexpr (std::numeric_limits<Integer>::max() > UINT32_MAX || std::is_signed_v<Integer>)
            {
                if (JS_SetPropertyInt64(ctx, this_obj, static_cast<int64_t>(idx), val) < 0)
                    throw exception(ctx);
            }
            else
            {
                if (JS_SetPropertyUint32(ctx, this_obj, static_cast<uint32_t>(idx), val) < 0)
                    throw exception(ctx);
            }
        }
    };
}
