#!/bin/bash
# Minimal compile and test for silicon driver
set -e
TARGET_EXE=build/bin/debuda-server-standalone

echo Compiling $TARGET_EXE
make dbd
echo To run debuda_stub enter: $TARGET_EXE