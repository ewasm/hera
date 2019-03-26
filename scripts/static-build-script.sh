
#git clone --single-branch --branch benchmarking-static-lib https://github.com/ewasm/hera
# cd hera
# git submodule update --init

mkdir -p build
cd build
cmake -DBUILD_SHARED_LIBS=OFF -DHERA_BINARYEN=ON -DHERA_WABT=ON -DHERA_WAVM=ON -DHERA_DEBUGGING=OFF ..
cmake --build .

cd deps/src/binaryen-build/lib/
for f in *.a; do ar x $f; done
ar r -s -c libbinaryenfull.a *.o
rm *.o
cd ../../../../


cd deps/src/wavm-build/lib
for f in *.a; do ar x $f; done
ar r -s -c libwavm.a *.o
rm *.o
cd ../../../../

#  apt-get install llvm-6.0-dev
# llvm .so file is needed for libwavm.a to be usable

# some flags might be unnecessary
#cgo LDFLAGS: /root/hera/build/src/libhera.a  -L/root/hera/build/src/ -lhera -L/root/hera/build/deps/src/wabt-build/ -lwabt  /root/hera/build/deps/src/binaryen-build/lib/libbinaryenfull.a /root/hera/build/deps/src/wavm-build/lib/libwavm.a -L/usr/lib/llvm-6.0/build/lib/ -lLLVM  -rdynamic -lstdc++ -Wl,-unresolved-symbols=ignore-all
 