#pragma once
#include "exception.h"
#include <quickjs.h>
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
            JSAtom atom = JS_NewAtomLen(ctx, name.data(), name.size());
            if (atom == JS_ATOM_NULL)
                return JS_EXCEPTION;

            JSValue ret = JS_GetProperty(ctx, this_obj, atom);
            JS_FreeAtom(ctx, atom);
            return ret;
        }

        static void set(JSContext* ctx, JSValue this_obj, std::string_view name, JSValue val)
        {
            JSAtom atom = JS_NewAtomLen(ctx, name.data(), name.size());
            if (atom == JS_ATOM_NULL)
            {
                JS_FreeValue(ctx, val);
                throw exception(ctx);
            }

            int res = JS_SetProperty(ctx, this_obj, atom, val);
            JS_FreeAtom(ctx, atom);

            if (res < 0)
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
