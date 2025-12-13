#pragma once
#include <cstdint>
#include <type_traits>

// forward declarations for quickjs library
using JSAtom = uint32_t;
struct JSClass;
using JSClassID = uint32_t;
struct JSContext;
struct JSModuleDef;
struct JSObject;
struct JSRuntime;

#if defined(JS_CHECK_JSVALUE)
typedef struct JSValue *JSValue;
typedef const struct JSValue *JSValueConst;
#elif defined(JS_NAN_BOXING) && JS_NAN_BOXING
using JSValue = uint64_t;
# define JSValueConst JSValue
#else
struct JSValue;
# define JSValueConst JSValue
#endif

// forward declarations for this library
namespace qjs
{
class context;
class module;
class runtime;
struct value;

template<typename T> requires std::is_class_v<T>
class class_registrar;

template<typename T>
struct js_traits;

template<typename T>
struct property_traits;
}
