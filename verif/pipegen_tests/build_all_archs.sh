#!/bin/bash
# Script which produces builds for all supported architectures, in the 
# structure expected by scripts in verif/pipegen_tests.
# Note that some build artifacts do end up in the default build directory,
# so we need to clean it out between each build.
build_dir="build_archs"
OUT=build make clean &&
OUT=$build_dir/grayskull make clean &&
OUT=$build_dir/grayskull ARCH_NAME=grayskull make -j$(nproc) --debug build_hw &&
OUT=build make clean &&
OUT=$build_dir/wormhole_b0 make clean &&
OUT=$build_dir/wormhole_b0 ARCH_NAME=wormhole_b0 make -j$(nproc) --debug build_hw &&
OUT=build make clean

if [ $? -eq 0 ]; then
  echo -e "\n\nAll builds successful!"
else
  echo -e "\n\nSomething failed, builds stopped."
fi