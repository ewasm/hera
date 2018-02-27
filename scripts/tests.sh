#!/usr/bin/env bash

set -e

WORKING_DIR=$(pwd)
echo "running tests.sh inside working dir: $WORKING_DIR"

echo "listing files:"
ls -al

echo "fetch ewasm tests."
git clone https://github.com/hugo-dc/tests --branch gas-usage --single-branch

echo "run ewasm tests."
testeth -t GeneralStateTests/stEWASMTests -- --testpath ./tests --vm hera --singlenet "Byzantium"
