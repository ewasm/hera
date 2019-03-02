git submodule update --init
mkdir -p build && cd build && cmake -DBUILD_SHARED_LIBS=ON -DHERA_DEBUGGING=OFF .. && make -j4
