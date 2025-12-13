cmake_minimum_required(VERSION 3.23)
project(quickjs++ LANGUAGES CXX)

add_subdirectory(quickjs)

add_library(quickjs++)

set_target_properties(quickjs++
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)

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
