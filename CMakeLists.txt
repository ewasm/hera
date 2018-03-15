cmake_minimum_required(VERSION 3.5)

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # The default toolchain file configures compilers and build environment.
    # This configuration is also used by hunter to build dependencies.
    # CMake will cache this value, not need to explictly specify CACHE param.
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/toolchain.cmake)
endif()

set(HERA_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake" CACHE PATH "The path to the cmake directory")
list(APPEND CMAKE_MODULE_PATH ${HERA_CMAKE_DIR})

add_compile_options(-Wall -Wextra -Wconversion -Wno-sign-conversion -Wno-unknown-pragmas)

include(HunterGate)
HunterGate(
    URL "https://github.com/ruslo/hunter/archive/v0.20.9.tar.gz"
    SHA1 "8f7cf6d719d05c49224a4ebeba90c6bb90b90f4a"
    LOCAL
)

project(hera)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
hunter_add_package(Boost COMPONENTS system)
find_package(Boost CONFIG REQUIRED system)

include(ProjectBinaryen)

add_subdirectory(src)
