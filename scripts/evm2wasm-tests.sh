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

echo "fetch ethereum tests."
git clone https://github.com/ethereum/tests

echo "install evm2wasm deps"
cd evm2wasm
npm install
EVM2WASM_BIN=$(pwd)/bin
export PATH=${EVM2WASM_BIN}:$PATH

echo "run evm2wasm tests"
${TESTETH} -t GeneralStateTests/stExample -- --testpath ${WORKING_DIR}/tests --singlenet "Byzantium" --singletest "add11" --vm hera --evmc evm2wasm.js=true
${TESTETH} -t GeneralStateTests/stStackTests -- --testpath ${WORKING_DIR}/tests --singlenet "Byzantium" --vm hera --evmc evm2wasm.js=true
