#
# based on https://github.com/aarlt/v8-cmake
#

if(ProjectV8Included)
    return()
endif()
set(ProjectV8Included TRUE)

include(ExternalProject)

ExternalProject_Add(depot_tools
  GIT_REPOSITORY    https://chromium.googlesource.com/chromium/tools/depot_tools.git
  GIT_TAG           bc23ca13f1b3b684d9c2a127f33b618a71644829
  SOURCE_DIR        "${CMAKE_BINARY_DIR}/deps/depot_tools"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND     ""
  INSTALL_COMMAND   ""
  TEST_COMMAND      ""
)
if (NOT EXISTS "${CMAKE_BINARY_DIR}/deps/v8/BUILD.gn")
  ExternalProject_Add_Step(depot_tools fetch
    COMMAND ${CMAKE_SOURCE_DIR}/cmake/v8_wrap.sh ${CMAKE_BINARY_DIR}/deps/depot_tools fetch v8
    COMMENT "fetch v8"
    DEPENDEES install
    WORKING_DIRECTORY  "${CMAKE_BINARY_DIR}/deps"
  )
else()
  ExternalProject_Add_Step(depot_tools fetch
    COMMAND ${CMAKE_SOURCE_DIR}/cmake/v8_wrap.sh ${CMAKE_BINARY_DIR}/deps/depot_tools gclient sync
    COMMENT "gclient sync"
    DEPENDEES install
    WORKING_DIRECTORY  "${CMAKE_BINARY_DIR}/deps/v8"
  )
endif()

ExternalProject_Add_Step(depot_tools v8gen
  COMMAND ${CMAKE_BINARY_DIR}/deps/v8/tools/dev/v8gen.py x64.release.sample
  COMMENT "v8gen.py x64.release.sample"
  DEPENDEES fetch
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/deps/v8"
)

ExternalProject_Add_Step(depot_tools ninja
  COMMAND ${CMAKE_BINARY_DIR}/deps/depot_tools/ninja -C out.gn/x64.release.sample v8_monolith
  COMMENT "ninja -C out.gn/x64.release.sample v8_monolith"
  DEPENDEES v8gen
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/deps/v8"
)

add_library(v8 INTERFACE)
add_library(v8:v8 ALIAS v8)
add_dependencies(v8 v8lib depot_tools)
target_include_directories(v8 INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/deps/v8/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(v8 
    INTERFACE ${CMAKE_BINARY_DIR}/deps/v8/out.gn/x64.release.sample/obj/${CMAKE_STATIC_LIBRARY_PREFIX}v8_monolith${CMAKE_STATIC_LIBRARY_SUFFIX}
)
