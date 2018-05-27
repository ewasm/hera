cmake_minimum_required(VERSION 3.5)

include(cmake/cable/bootstrap.cmake)

include(CableBuildType)
include(CableCompilerSettings)
include(CableToolchains)

cable_configure_toolchain(DEFAULT cxx11)

project(hera)

cable_set_build_type(DEFAULT RelWithDebInfo CONFIGURATION_TYPES Release RelWithDebInfo Debug)

cable_configure_compiler()

include(ProjectBinaryen)

add_subdirectory(evmc)

add_subdirectory(src)
