#!/usr/bin/env bash

set -e

if [ "$1" == "build" ]
then
(
  git clone --recursive https://github.com/ethereum/cpp-ethereum
  cd cpp-ethereum

  rm -rf hera
  ln -s `pwd`/../. hera

  mkdir build
  cd build
  cmake -DHERA=ON ..
  make
)
  TESTETH=$(pwd)/cpp-ethereum/build/test/testeth
else
  TESTETH=testeth
fi

git clone https://github.com/ewasm/tests -b wasm-tests --single-branch

${TESTETH} -t GeneralStateTests/stEWASMTests -- --testpath ./tests --vm hera --singlenet "Byzantium"
