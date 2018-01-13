cmake_minimum_required(VERSION 3.5)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Require C++11.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS Off)

add_compile_options(-Wall -Wextra -Wconversion -Wno-sign-conversion -Wno-unknown-pragmas)

project(hera)

option(HERA_EVM2WASM "Build with evm2wasm auto-translation" OFF)

include(ProjectBinaryen)

if(HERA_EVM2WASM)
    add_subdirectory(evm2wasm)
endif()
add_subdirectory(src)
