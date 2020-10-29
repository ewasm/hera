if(ProjectWasmerIncluded)
    return()
endif()
set(ProjectWasmerIncluded TRUE)
include(ExternalProject)
include(GNUInstallDirs)

# TODO: if find LLVM 8.0+ use make capi-llvm to get better permformance
set(WASMER_BUILD_COMMAND make capi)
ExternalProject_Add(wasmer
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://github.com/wasmerio/wasmer.git
        GIT_TAG 63a2d8129c7ad5ca670d041859de45e01292dd12
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${WASMER_BUILD_COMMAND}
        INSTALL_COMMAND ""
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <SOURCE_DIR>/target/release/libwasmer_runtime_c_api.a
)

ExternalProject_Get_Property(wasmer SOURCE_DIR)
set(WASMER_INCLUDE_DIRS ${SOURCE_DIR}/lib/)
file(MAKE_DIRECTORY ${WASMER_INCLUDE_DIRS})  # Must exist.
add_library(WASMER::runtim STATIC IMPORTED)
set(WASMER_RUNTIME_LIBRARIES ${SOURCE_DIR}/target/release/libwasmer_runtime_c_api.a)
set_property(TARGET WASMER::runtim PROPERTY IMPORTED_LOCATION ${WASMER_RUNTIME_LIBRARIES})
set_property(TARGET WASMER::runtim PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${WASMER_INCLUDE_DIRS})

add_dependencies(WASMER::runtim wasmer)
