#!/bin/bash
CMAKE_SOURCE_DIR=$1
CMAKE_BINARY_DIR=$2
if [ ! -f "${CMAKE_BINARY_DIR}/deps/v8/BUILD.gn" ]
then
  rm -rf ${CMAKE_BINARY_DIR}/deps/v8
  ${CMAKE_SOURCE_DIR}/cmake/v8_wrap.sh ${CMAKE_BINARY_DIR}/deps/depot_tools fetch v8
else
  mkdir -p ${CMAKE_BINARY_DIR}/deps/v8/include
  ${CMAKE_SOURCE_DIR}/cmake/v8_wrap.sh ${CMAKE_BINARY_DIR}/deps/depot_tools gclient sync
fi
