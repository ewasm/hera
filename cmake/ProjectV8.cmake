#
# based on https://github.com/aarlt/v8-cmake
#

if (ProjectV8Included)
    return()
endif ()
set(ProjectV8Included TRUE)

include(ExternalProject)

set(V8_LIBRARY ${CMAKE_BINARY_DIR}/deps/v8/out.gn/x64.release.sample/obj/${CMAKE_STATIC_LIBRARY_PREFIX}v8_monolith${CMAKE_STATIC_LIBRARY_SUFFIX})
set(V8_INCLUDE ${CMAKE_BINARY_DIR}/deps/v8/include)

ExternalProject_Add(depot_tools
    GIT_REPOSITORY https://chromium.googlesource.com/chromium/tools/depot_tools.git
    GIT_TAG bc23ca13f1b3b684d9c2a127f33b618a71644829
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/depot_tools"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    TEST_COMMAND ""
    BUILD_BYPRODUCTS "${V8_LIBRARY}"
    BUILD_BYPRODUCTS "${V8_INCLUDE}"
    )

ExternalProject_Add_Step(depot_tools fetch
    COMMAND ${CMAKE_SOURCE_DIR}/cmake/v8_fetch.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}
    COMMENT "v8: fetch"
    DEPENDEES install
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/deps"
    )

ExternalProject_Add_Step(depot_tools v8gen
    COMMAND ${CMAKE_BINARY_DIR}/deps/v8/tools/dev/v8gen.py -vv x64.release.sample
    COMMENT "v8: v8gen.py x64.release.sample"
    DEPENDEES fetch
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/deps/v8"
    )

ExternalProject_Add_Step(depot_tools ninja
    COMMAND ${CMAKE_BINARY_DIR}/deps/depot_tools/ninja -v -C out.gn/x64.release.sample v8_monolith
    COMMENT "v8: ninja -v -C out.gn/x64.release.sample v8_monolith"
    DEPENDEES v8gen
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/deps/v8"
    )

add_library(v8::v8 STATIC IMPORTED)

set_target_properties(
    v8::v8
    PROPERTIES
    IMPORTED_CONFIGURATIONS Release
    IMPORTED_LOCATION_RELEASE ${V8_LIBRARY}
)
file(MAKE_DIRECTORY ${V8_INCLUDE})
target_include_directories(v8::v8 INTERFACE
    $<BUILD_INTERFACE:${V8_INCLUDE}>
    $<INSTALL_INTERFACE:include>
)

add_dependencies(v8::v8 depot_tools)
