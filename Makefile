libbinaryen:
	mkdir -p binaryen/build
	cd binaryen/build && cmake .. && make

libhera: libbinaryen
	clang++ -shared -std=c++11 -Ibinaryen/src -o libhera.so hera.cpp -Lbinaryen/build/lib -lbinaryen

all: libhera
