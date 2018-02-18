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

WORKING_DIR=$(pwd)
echo "running tests.sh inside working dir: $WORKING_DIR"

echo "listing files:"
ls -al

echo "fetch ewasm tests."
git clone https://github.com/ewasm/tests -b wasm-tests --single-branch

echo "run ewasm tests."
${TESTETH} -t GeneralStateTests/stEWASMTests -- --testpath ./tests --vm hera --singlenet "Byzantium"
