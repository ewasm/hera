#!/bin/sh

sed -iE 's/SHARED//' CMakeLists.txt
sed -iE 's/-Werror//' CMakeLists.txt
