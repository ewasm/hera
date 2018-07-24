if(ProjectWAVMIncluded)
    return()
endif()
set(ProjectWAVMIncluded TRUE)

include(ExternalProject)

find_package(LLVM 6.0 REQUIRED CONFIG)
message(STATUS "LLVM: ${LLVM_DIR}")
llvm_map_components_to_libnames(llvm_libs support core passes mcjit native DebugInfoDWARF)

set(prefix ${CMAKE_BINARY_DIR}/deps)
set(source_dir ${prefix}/src/wavm)
set(binary_dir ${prefix}/src/wavm-build)
set(include_dir ${source_dir}/Include)

set(runtime_library ${binary_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}Runtime${CMAKE_STATIC_LIBRARY_SUFFIX})
set(platform_library ${binary_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}Platform${CMAKE_STATIC_LIBRARY_SUFFIX})
set(wasm_library ${binary_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}WASM${CMAKE_STATIC_LIBRARY_SUFFIX})
set(ir_library ${binary_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}IR${CMAKE_STATIC_LIBRARY_SUFFIX})
set(logging_library ${binary_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}Logging${CMAKE_STATIC_LIBRARY_SUFFIX})
set(unwind_library ${binary_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}WAVMUnwind${CMAKE_STATIC_LIBRARY_SUFFIX})

set(other_libraries ${platform_library} ${wasm_library} ${ir_library} ${logging_library} ${unwind_library})

ExternalProject_Add(wavm
    PREFIX ${prefix}
    DOWNLOAD_NAME wavm-a0baaec170b55cc60cfe6bcc6b36add953a065d8.tar.gz
    DOWNLOAD_DIR ${prefix}/downloads
    SOURCE_DIR ${source_dir}
    BINARY_DIR ${binary_dir}
    URL https://github.com/AndrewScheidecker/WAVM/archive/a0baaec170b55cc60cfe6bcc6b36add953a065d8.tar.gz
    URL_HASH SHA256=da184e2c077e257dea82c13b2e5ae1fc03d1dc306a1c9a6f84838cff7390b75a
    PATCH_COMMAND sh ${CMAKE_CURRENT_LIST_DIR}/patch_wavm.sh
    CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_BUILD_TYPE=Release
    -DLLVM_DIR=${LLVM_DIR}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_CXX_FLAGS=-Wno-error
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${runtime_library} ${other_libraries}
)

file(MAKE_DIRECTORY ${include_dir})  # Must exist.


add_library(wavm::wavm STATIC IMPORTED)
set_target_properties(
    wavm::wavm
    PROPERTIES
    IMPORTED_CONFIGURATIONS Release
    IMPORTED_LOCATION_RELEASE ${runtime_library}
    INTERFACE_INCLUDE_DIRECTORIES ${include_dir}
    INTERFACE_LINK_LIBRARIES "${other_libraries};${llvm_libs}"
)

add_dependencies(wavm::wavm wavm)
