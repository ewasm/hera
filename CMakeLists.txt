cmake_minimum_required(VERSION 3.5)

include(cmake/cable/bootstrap.cmake)
include(CableBuildInfo)
include(CableBuildType)
include(CableCompilerSettings)
include(CableToolchains)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

cable_configure_toolchain(DEFAULT cxx11-pic)

set(CMAKE_DEBUG_POSTFIX "")
set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of the 3rd party packages")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/evm2wasm/cmake/HunterGate.cmake)
    include(evm2wasm/cmake/HunterGate.cmake)
    HunterGate(
        URL "https://github.com/ruslo/hunter/archive/v0.21.19.tar.gz"
        SHA1 "5ead1e069b437930d0de8a21824b20fb52b37b50"
    )
endif()

project(hera)
set(PROJECT_VERSION 0.1.0)

cable_set_build_type(DEFAULT RelWithDebInfo CONFIGURATION_TYPES Debug;Release;RelWithDebInfo)

cable_configure_compiler()
# TODO: fix the warnings instead
add_compile_options(-Wno-pedantic)

include(ProjectBinaryen)

cable_add_buildinfo_library(PROJECT_NAME hera)

option(HERA_WABT "Build with wabt" OFF)
if (HERA_WABT)
    include(ProjectWabt)
endif()

option(HERA_WAVM "Build with WAVM" OFF)
if (HERA_WAVM)
    include(ProjectWAVM)
endif()

add_subdirectory(evmc)
add_subdirectory(evm2wasm)
add_subdirectory(src)


install(DIRECTORY include/hera DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(heraConfigVersion.cmake COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    cmake/Config.cmake.in
    heraConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hera
)

if(BUILD_SHARED_LIBS)
    # TODO: To enable for static libs, hera-buildinfo library must be modified in Cable.
    install(
        EXPORT heraTargets
        NAMESPACE hera::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hera
    )
endif()

install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/heraConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/heraConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hera
)
