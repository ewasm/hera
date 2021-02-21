include(../evmc/cmake/EVMC.cmake)

if(TARGET evmc::evmc-vmtester)
    evmc_add_vm_test(NAME evmc-check TARGET hera)
endif()

if(HERA_FUZZING)
    add_subdirectory(fuzzing)
endif()
