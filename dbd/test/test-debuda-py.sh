#!/bin/bash
THIS_SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
TMP_OUT_FILE=build/test/dbd-out.tmp
echo "TMP_OUT_FILE=$TMP_OUT_FILE"

# set default value for TIMEOUT
if [ -z "$TEST_RUN_TIMEOUT" ]; then TEST_RUN_TIMEOUT=500; fi
echo "TEST_RUN_TIMEOUT=$TEST_RUN_TIMEOUT"

mkdir -p build/test

touch $TMP_OUT_FILE

if [ "$1" = "skip-build" ]; then
    echo Skipping build used for CI and tests
else
    echo Building 'make build_hw'...
    make -j32 build_hw >> $TMP_OUT_FILE 2>&1

    echo Building verif/op_tests ...
    make -j32 verif/op_tests >> $TMP_OUT_FILE 2>&1

    echo Building debuda-server-standalone ...
    make -j32 dbd
fi

echo Installing Python dependencies ...
pip install -r dbd/requirements.txt
mkdir -p debuda_test

##################################################################################################################################################
NETLIST_FILE=verif/op_tests/netlists/netlist_matmul_op_with_fd.yaml
#
# 1. Sanity
#
# ┌────────┐  0   ┌───────┐  0   ┌─────┐  0   ┌───────┐  0   ┌────────┐
# │ input0 │ ───▶ │ f_op0 │ ───▶ │ op2 │ ───▶ │ d_op3 │ ───▶ │ output │
# └────────┘      └───────┘      └─────┘      └───────┘      └────────┘
#                                  ▲
#                                  │
#                                  │
# ┌────────┐  0   ┌───────┐  1     │
# │ input1 │ ───▶ │ f_op1 │ ───────┘
# └────────┘      └───────┘
echo Running op_tests/test_op on $NETLIST_FILE ...
./build/test/verif/op_tests/test_op --outdir debuda_test --netlist $NETLIST_FILE --seed 0 --silicon --timeout $TEST_RUN_TIMEOUT
if [ $? -ne 0 ]; then
    echo Error in running ./build/test/verif/op_tests/test_op
    exit 1
fi
source $THIS_SCRIPT_DIR/test-run-all-debuda-commands.sh "netlist_matmul_op_with_fd" --server-cache=through


##################################################################################################################################################
NETLIST_FILE=dbd/test/netlists/netlist_multi_matmul_perf.yaml
#
# 2. Hang analysis on a passing test
#
#      ┌────────┐  0   ┌───────┐  0   ┌─────────┐  0   ┌──────┐  0   ┌───────┐  0   ┌────────┐
#      │ input0 │ ───▶ │ f_op0 │ ───▶ │ matmul1 │ ───▶ │ add1 │ ───▶ │ d_op3 │ ───▶ │ output │
#      └────────┘      └───────┘      └─────────┘      └──────┘      └───────┘      └────────┘
#                        │              ▲                ▲
#   ┌────────────────────┘              │                │
#   │                                   │                │
#   │  ┌────────┐  0   ┌───────┐  1     │                │
#   │  │ input1 │ ───▶ │ f_op1 │ ───────┘                │
#   │  └────────┘      └───────┘                         │
#   │                    │                               │
#   │                    │ 0                             │
#   │                    ▼                               │
#   │                  ┌───────┐  1   ┌─────────┐  1     │
#   │                  │ recip │ ───▶ │ matmul2 │ ───────┘
#   │                  └───────┘      └─────────┘
#   │   0                               ▲
#   └───────────────────────────────────┘
echo Running op_tests/test_op on $NETLIST_FILE ...
./build/test/verif/op_tests/test_op --outdir debuda_test --netlist $NETLIST_FILE --seed 0 --silicon --timeout $TEST_RUN_TIMEOUT
if [ $? -ne 0 ]; then
    echo Error in running ./build/test/verif/op_tests/test_op
    exit 1
fi

source $THIS_SCRIPT_DIR/test-run-all-debuda-commands.sh "netlist_multi_matmul_perf" --server-cache=through

##################################################################################################################################################
# Test with server cache enabled
source $THIS_SCRIPT_DIR/test-run-all-debuda-commands.sh "netlist_multi_matmul_perf" --server-cache=on

##################################################################################################################################################
# Test with no server cache
source $THIS_SCRIPT_DIR/test-run-all-debuda-commands.sh "netlist_multi_matmul_perf"

##################################################################################################################################################
# Test remote on local w/ limited context
source $THIS_SCRIPT_DIR/test-debuda-server-limited.sh "netlist_multi_matmul_perf-remote"

echo Done
