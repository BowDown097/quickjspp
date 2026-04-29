cmake_minimum_required(VERSION 3.23)
project(quickjs++ LANGUAGES CXX)

option(QJS_USE_NAMEOF "Use nameof library for resolving typenames" ON)

add_library(quickjs++)

add_subdirectory(deps/quickjs)

if(QJS_USE_NAMEOF)
    add_subdirectory(deps/nameof)
    target_compile_definitions(quickjs++ PUBLIC QJS_USING_NAMEOF)
    target_link_libraries(quickjs++ PUBLIC nameof)
endif()

target_compile_features(quickjs++ PUBLIC cxx_std_20)

target_sources(quickjs++
    PRIVATE
        src/quickjs++/context.cpp
        src/quickjs++/exception.cpp
        src/quickjs++/js_traits.cpp
        src/quickjs++/runtime.cpp
    PUBLIC
        FILE_SET HEADERS FILES
            src/quickjs++.h
            src/quickjs++/context.h
            src/quickjs++/exception.h
            src/quickjs++/function_traits.h
            src/quickjs++/function_wrapping.h
            src/quickjs++/js_traits.h
            src/quickjs++/quickjs_fwd.h
            src/quickjs++/property_traits.h
            src/quickjs++/runtime.h
            src/quickjs++/utility.h
            src/quickjs++/value.h)

target_include_directories(quickjs++ PUBLIC src)
target_link_libraries(quickjs++ PUBLIC qjs)
