cmake_minimum_required(VERSION 3.24)
project(ZuenaPlugins VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Runtime estático: el .vst3 no depende de que el usuario tenga instalado el redistribuible de Visual C++.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(ZUENA_BUILD_TESTS "Compilar la suite run_tests (Catch2)" ON)

add_subdirectory(third_party/JUCE)

if(ZUENA_BUILD_TESTS)
    add_subdirectory(third_party/Catch2)
    enable_testing()
endif()

add_subdirectory(plugins/ZuenaEQ)
add_subdirectory(plugins/ZuenaLuma)
add_subdirectory(plugins/ZuenaLink)
