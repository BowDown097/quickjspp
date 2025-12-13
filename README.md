quickjs++ is a C++20 wrapper around [quickjs-ng](https://github.com/quickjs-ng/quickjs), heavily inspired by [quickjspp](https://github.com/ftk/quickjspp), allowing seamless usage of the conveniences and features that modern C++ has to offer.

QuickJS is a small and embeddable JavaScript engine. It aims to support the latest [ECMAScript](https://tc39.es/ecma262/) specification.

# Example
```cpp
#include <format>
#include <iostream>
#include <quickjs++.h>

struct my_class
{
    int member_variable{};

    my_class() = default;

    my_class(const std::vector<int>& ints)
        : member_variable(!ints.empty() ? ints.front() : 0)
    {
        for (int i : ints)
            std::cout << i;
        std::cout << std::endl;
    }

    std::string member_function(const std::string& s)
    {
        return std::format("Hello {}, I am holding {}", s, member_variable);
    }
};

void println(const qjs::rest<std::string>& args)
{
    for (const std::string& arg : args)
        std::cout << arg << ' ';
    std::cout << std::endl;
}

int main()
{
    qjs::runtime runtime;
    qjs::context context(runtime);
    try
    {
        // MyModule module + exports
        qjs::module& module = context.add_module("MyModule");
        module.add("println", println);
        module.register_class<my_class>("MyClass")
            .constructor<>()
            .constructor<const std::vector<int>&>("MyClassA")
            .member<&my_class::member_variable>("member_variable")
            .member<&my_class::member_function>("member_function");
        // import module
        context.eval(R"xxx(
            import * as my from 'MyModule';
            globalThis.my = my;
        )xxx", "<import>", JS_EVAL_TYPE_MODULE);
        // evaluate js code
        context.eval(R"xxx(
            let v1 = new my.MyClass();
            v1.member_variable = 500;
            let v2 = new my.MyClassA([1, 2, 3]);
            function my_callback(str) {
                my.println("at callback:", v2.member_function(str));
            }
        )xxx");
        // callback
        auto cb = context.eval("my_callback").as<std::function<void(const std::string&)>>();
        cb("world");
    }
    catch (const qjs::exception& ex)
    {
        qjs::value ex_val = ex.get_value();
        std::cerr << ex_val.as<std::string_view>() << std::endl;
        std::cerr << ex.location().file_name() << ':' << ex.location().line() << std::endl;
        return EXIT_FAILURE;
    }
}
```

# Installation
The easiest way to use this library is to use CMake's ``add_subdirectory`` command on the root directory of this project then link to the ``quickjs++`` target it creates.
