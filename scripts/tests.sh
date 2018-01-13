#!/usr/bin/env bash

set -e

git clone --recursive https://github.com/ethereum/cpp-ethereum
cd cpp-ethereum

rm -rf hera
ln -s `pwd`/../. hera

mkdir build
cd build
cmake -DHERA=ON ..
make

git clone --recursive https://github.com/ewasm/tests -b wasm-tests

ETHEREUM_TEST_PATH='`pwd`/tests`' test/testeth -t GeneralStateTests/stEWASMTests -- --filltests --vm hera --singlenet "Byzantium"
