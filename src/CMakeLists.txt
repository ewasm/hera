
get_filename_component(evm_include_dir .. ABSOLUTE)

add_library(hera
    eei.cpp eei.h
    hera.cpp hera.h
    ${evm_include_dir}/evm.h
)

option(HERA_DEBUGGING "Display debugging messages during execution." ON)
if(HERA_DEBUGGING)
  target_compile_definitions(hera PRIVATE HERA_DEBUGGING=1)
endif()

option(HERA_METERING_CONTRACT "Run metering through the metering contract" OFF)
if(HERA_METERING_CONTRACT)
  target_compile_definitions(hera PRIVATE HERA_METERING_CONTRACT=1)
endif()

target_include_directories(hera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${evm_include_dir})
if(HERA_EVM2WASM)
    target_compile_definitions(hera PRIVATE HERA_EVM2WASM)
    target_link_libraries(hera PRIVATE libevm2wasm)
endif()
target_link_libraries(hera PRIVATE binaryen::binaryen)
