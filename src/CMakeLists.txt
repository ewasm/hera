
get_filename_component(hera_include_dir .. ABSOLUTE)

add_library(hera
    eei.cpp eei.h
    hera.cpp hera.h
    ${hera_include_dir}/evm.h
)

target_include_directories(hera PRIVATE ${hera_include_dir})
target_link_libraries(hera PRIVATE binaryen::binaryen)
