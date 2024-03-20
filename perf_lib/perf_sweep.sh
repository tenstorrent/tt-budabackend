#!/bin/bash

if [ -z $ROOT ]; then export ROOT=`git rev-parse --show-toplevel`; fi
cd "$ROOT/verif/template_netlist/"

echo "Entering directory $PWD"
echo "Creating environment and installing requirements"

python3.8 -m venv env
bash -c "source env/bin/activate && pip3.8 install -r requirements.txt --no-cache-dir"
source env/bin/activate

cd "$ROOT"
set -x
echo "Entering directory $PWD"
echo "Building loader/tests"
make loader/tests
echo "Running perf sweep"
./perf_lib/perf_sweep.py "$@"