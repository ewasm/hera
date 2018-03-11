#!/usr/bin/env bash

set -e

if [ "$1" == "build" ]
then
  git clone --recursive https://github.com/ethereum/cpp-ethereum
  cd cpp-ethereum

  rm -rf hera
  ln -s `pwd`/../. hera

  mkdir build
  cd build
  cmake -DHERA=ON ..
  make
fi

git clone https://github.com/ewasm/tests -b wasm-tests --single-branch

testeth -t GeneralStateTests/stEWASMTests -- --testpath ./tests --vm hera --singlenet "Byzantium"
