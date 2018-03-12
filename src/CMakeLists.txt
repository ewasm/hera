
get_filename_component(evm_include_dir .. ABSOLUTE)

find_package(Threads REQUIRED)

add_library(hera
    eei.cpp eei.h
    evm-instructions.cpp evm-instructions.h
    hera.cpp hera.h
    vm.cpp vm.h
)

option(HERA_DEBUGGING "Display debugging messages during execution." ON)
if(HERA_DEBUGGING)
  target_compile_definitions(hera PRIVATE HERA_DEBUGGING=1)
endif()

target_include_directories(hera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${evm_include_dir})
target_link_libraries(hera PRIVATE evmc libevm2wasm binaryen::binaryen Threads::Threads)
if(NOT WIN32)
  if(CMAKE_COMPILER_IS_GNUCXX)
    set_target_properties(hera PROPERTIES LINK_FLAGS "-Wl,--no-undefined")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_target_properties(hera PROPERTIES LINK_FLAGS "-Wl,-undefined,error")
  endif()
endif()
