if(ProjectIntXIncluded)
	return()
endif()

set(ProjectIntXIncluded TRUE)

include(ExternalProject)
include(GNUInstallDirs)

set(prefix ${CMAKE_BINARY_DIR}/deps)
set(source_dir ${prefix}/src/intx)
set(binary_dir ${prefix}/src/intx-build)

set(intx_include_dir ${source_dir}/include/intx)
set(intx_lib ${binary_dir}/libs/intx/libintx.a)

set(patch_command sed -i -e "$ d" ${source_dir}/CMakeLists.txt)
set(build_command cmake --build <BINARY_DIR>)
set(install_command cmake --build <BINARY_DIR> --target install)

ExternalProject_Add(IntX
	PREFIX ${prefix}
	GIT_REPOSITORY https://github.com/chfast/intx.git
	GIT_TAG 1a10f4fc3433d5ce88ea4c6067002680e2ea0385
	CMAKE_ARGS
	-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
	SOURCE_DIR ${source_dir}
	BINARY_DIR ${binary_dir}
	PATCH_COMMAND ${patch_command}
	BUILD_COMMAND ${build_command}
	INSTALL_COMMAND ${install_command}
	INSTALL_DIR ${prefix}/${CMAKE_INSTALL_LIBDIR}
	BUILD_BYPRODUCTS ${intx_lib}
)

add_library(intx::intx STATIC IMPORTED)

file(MAKE_DIRECTORY ${intx_include_dir})
set_target_properties(
	intx::intx
	PROPERTIES
	IMPORTED_LOCATION_RELEASE ${intx_lib}
	INTERFACE_INCLUDE_DIRECTORIES ${intx_include_dir}
	INTERFACE_LINK_LIBRARIES ${intx_lib}
)

add_dependencies(intx::intx IntX)
