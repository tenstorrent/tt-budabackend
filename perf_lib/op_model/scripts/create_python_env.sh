#!/bin/bash

if [ -z $ROOT ]; then export ROOT=`git rev-parse --show-toplevel`; fi

echo "Creating environment and installing requirements"

python3.8 -m venv $ROOT/perf_lib/op_model/scripts/env

source $ROOT/perf_lib/op_model/scripts/env/bin/activate

# perf sweep calls template_netlist modules to generate configurations
pip3.8 install -r $ROOT/verif/template_netlist/requirements.txt -r $ROOT/perf_lib/op_model/scripts/requirements.txt --no-cache-dir
