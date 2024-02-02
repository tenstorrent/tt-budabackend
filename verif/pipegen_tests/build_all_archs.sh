#!/bin/bash
# Script which produces builds for all supported architectures, in the 
# structure expected by scripts in verif/pipegen_tests.
# Note that some build artifacts do end up in the default build directory,
# so we need to clean it out between each build.
OUT=build_archs make clean &&
OUT=build make clean &&
OUT=build_archs/grayskull ARCH_NAME=grayskull make -j64 --debug build_hw &&
OUT=build make clean &&
OUT=build_archs/wormhole_b0 ARCH_NAME=wormhole_b0 make -j64 --debug build_hw &&
OUT=build make clean

if [ $? -eq 0 ]; then
  echo -e "\n\nAll builds successful!"
else
  echo -e "\n\nSomething failed, builds stopped."
fi