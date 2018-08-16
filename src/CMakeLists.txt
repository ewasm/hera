find_package(Threads REQUIRED)

set(hera_include_dir ${PROJECT_SOURCE_DIR}/include)
get_filename_component(evmc_include_dir .. ABSOLUTE)

add_library(hera
    ${hera_include_dir}/hera/hera.h
    eei.cpp
    eei.h
    hera.cpp
)

option(HERA_DEBUGGING "Display debugging messages during execution." ON)
if(HERA_DEBUGGING)
  target_compile_definitions(hera PRIVATE HERA_DEBUGGING=1)
endif()

target_include_directories(hera PUBLIC ${hera_include_dir} PRIVATE ${evmc_include_dir})
target_link_libraries(hera PRIVATE hera-buildinfo evmc::evmc evmc::instructions libevm2wasm binaryen::binaryen Threads::Threads)
if(NOT WIN32)
  if(CMAKE_COMPILER_IS_GNUCXX)
    set_target_properties(hera PROPERTIES LINK_FLAGS "-Wl,--no-undefined")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_target_properties(hera PROPERTIES LINK_FLAGS "-Wl,-undefined,error")
  endif()
endif()

if(HERA_WABT)
    target_compile_definitions(hera PRIVATE HERA_WABT=1)
    target_link_libraries(hera PRIVATE wabt::wabt)
endif()

if(HERA_WAVM)
    target_compile_definitions(hera PRIVATE HERA_WAVM=1)
    target_link_libraries(hera PRIVATE wavm::wavm)
endif()
