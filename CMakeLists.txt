cmake_minimum_required(VERSION 3.8)

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/evmc/.git OR NOT EXISTS ${CMAKE_SOURCE_DIR}/cmake/cable/.git)
    message(FATAL_ERROR "Git submodules not initialized, execute:\n  git submodule update --init")
endif()

include(cmake/cable/bootstrap.cmake)
include(CableBuildInfo)
include(CableBuildType)
include(CableCompilerSettings)
include(CableToolchains)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

include(Hunter/init)

cable_configure_toolchain(DEFAULT cxx17-pic)

set(CMAKE_DEBUG_POSTFIX "")

project(hera)
set(PROJECT_VERSION 0.2.5-alpha.0)

cable_set_build_type(DEFAULT RelWithDebInfo CONFIGURATION_TYPES Debug;Release;RelWithDebInfo)

cable_configure_compiler()
if(CABLE_COMPILER_GNULIKE)
    # TODO: fix the warnings instead
    add_compile_options(-Wno-pedantic)
endif()


cable_add_buildinfo_library(PROJECT_NAME hera EXPORT heraTargets)

option(BUILD_SHARED_LIBS "Build libraries as shared" ON)

if(CABLE_COMPILER_CLANG)
    option(HERA_FUZZING "Build Hera fuzzer" OFF)
endif()
if(HERA_FUZZING)
    set(fuzzer_flags -fsanitize=fuzzer,undefined,address)
    add_compile_options(${fuzzer_flags})
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${fuzzer_flags}")
endif()

option(HERA_BINARYEN "Build with binaryen" OFF)
if (HERA_BINARYEN)
    include(ProjectBinaryen)
endif()

option(HERA_WABT "Build with wabt" ON)
if (HERA_WABT)
    include(ProjectWabt)
endif()

option(HERA_WAVM "Build with WAVM" OFF)
if (HERA_WAVM)
    include(ProjectWAVM)
endif()

if (NOT (HERA_BINARYEN OR HERA_WABT OR HERA_WAVM))
    message(FATAL_ERROR "At least one one engine must be enabled.")
endif()

add_subdirectory(evmc)
add_subdirectory(src)
add_subdirectory(test)


install(DIRECTORY include/hera DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(heraConfigVersion.cmake COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    cmake/Config.cmake.in
    heraConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hera
)

install(
    EXPORT heraTargets
    NAMESPACE hera::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hera
)

install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/heraConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/heraConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hera
)

if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_FILE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)
include(CPack)
