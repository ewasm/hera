cmake_minimum_required(VERSION 3.5)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/cxx11-pic.cmake CACHE FILEPATH "CMake toolchain file")

set(CMAKE_DEBUG_POSTFIX "")
set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of the 3rd party packages")

add_compile_options(-Wall -Werror -Wextra -Wconversion -Wno-sign-conversion -Wno-unknown-pragmas)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/evm2wasm/cmake/HunterGate.cmake)
    include(evm2wasm/cmake/HunterGate.cmake)
    HunterGate(
        URL "https://github.com/ruslo/hunter/archive/v0.21.19.tar.gz"
        SHA1 "5ead1e069b437930d0de8a21824b20fb52b37b50"
    )
endif()

project(hera)

include(ProjectBinaryen)
include(ProjectWabt)

option(HERA_WAVM "Build with WAVM" OFF)
if (HERA_WAVM)
    include(ProjectWAVM)
endif()

add_subdirectory(evmc)
add_subdirectory(evm2wasm)
add_subdirectory(src)
