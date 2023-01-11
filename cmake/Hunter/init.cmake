
set(HUNTER_CONFIGURATION_TYPES Release
    CACHE STRING "Build type of the Hunter packages")

include(HunterGate)

HunterGate(
    URL "https://github.com/cpp-pm/hunter/archive/v0.24.0.tar.gz"
    SHA1 "a3d7f4372b1dcd52faa6ff4a3bd5358e1d0e5efd"
)
