#!/usr/bin/env bash

set -e

git clone --recursive https://github.com/ethereum/cpp-ethereum -b hera
cd cpp-ethereum

mkdir build
cd build
cmake -DHERA=ON ..
make

test/testeth -- --vm hera
