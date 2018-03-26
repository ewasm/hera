
get_filename_component(evm_include_dir .. ABSOLUTE)

find_package(Threads REQUIRED)

add_library(hera
    eei.cpp eei.h
    evm-instructions.cpp evm-instructions.h
    hera.cpp hera.h
)

option(HERA_DEBUGGING "Display debugging messages during execution." ON)
if(HERA_DEBUGGING)
  target_compile_definitions(hera PRIVATE HERA_DEBUGGING=1)
endif()

option(HERA_METERING_CONTRACT "Run metering through the metering contract" OFF)
if(HERA_METERING_CONTRACT)
  target_compile_definitions(hera PRIVATE HERA_METERING_CONTRACT=1)
endif()

option(HERA_EVM2WASM "Build with evm2wasm auto-translation" OFF)
if(HERA_EVM2WASM)
  target_compile_definitions(hera PRIVATE HERA_EVM2WASM=1)
endif()

target_include_directories(hera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${evm_include_dir})
target_link_libraries(hera PRIVATE evmc binaryen::binaryen Threads::Threads)
if(NOT WIN32)
  if(CABLE_COMPILER_GNU)
    set_target_properties(hera PROPERTIES LINK_FLAGS "-Wl,--no-undefined")
  elseif(CABLE_COMPILER_CLANG)
    set_target_properties(hera PROPERTIES LINK_FLAGS "-Wl,-undefined,error")
  endif()
endif()
