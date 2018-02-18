#!/usr/bin/env bash

set -e

WORKING_DIR=$(pwd)
echo "running tests.sh inside working dir: $WORKING_DIR"
echo "listing files:"
ls -al
cd ..
git clone --recursive https://github.com/ethereum/cpp-ethereum
cd cpp-ethereum
rm -rf hera
echo "linking hera dir: $WORKING_DIR"
ln -s $WORKING_DIR hera

echo "run build commands."
mkdir build && cd build
cmake -DHERA=ON ..
cmake --build .

echo "fetch ewasm tests."
git clone --recursive https://github.com/ewasm/tests -b wasm-tests

echo "run ewasm tests."
./test/testeth -t GeneralStateTests/stEWASMTests -- --testpath ./tests --vm hera --singlenet "Byzantium"
