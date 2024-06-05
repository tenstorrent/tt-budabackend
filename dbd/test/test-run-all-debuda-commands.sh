#!/bin/bash

set -e
TEST_NAME="$1"
EXTRA_ARGUMENTS="$2"

# If we want to run with coverage, prefix this script with COV=1
# pip install coverage
# To see coverage results in VS code:
#   - Install Code Coverage extension (by Markis Taylor)
#   - Dump the coverage data with: coverage lcov
#   - Point the extension to the generated lcov file (in VS code settings)
#   - Run "Show code coverage" command
if [ "$COV" = "1" ]; then
    COVERAGE_CMD="coverage run --append --include=dbd/**"
else
    COVERAGE_CMD=""
fi

# --test is used to prevent main REPL loop from catching the exception. Instead,
# it will be propagated back to the shell as a non-zero exit code.
run_debuda() {
    # TODO: What's this?
    # if [ -z "$TMP_OUT_FILE" ]; then
    #     # If TMP_OUT_FILE is not set, show the output
    #     timeout 30 $COVERAGE_CMD dbd/debuda.py debuda_test $EXTRA_ARGUMENTS --test --commands "$1"
    # else
    #     timeout 30 $COVERAGE_CMD dbd/debuda.py debuda_test $EXTRA_ARGUMENTS --test --commands "$1"
    # fi
    timeout 30 $COVERAGE_CMD dbd/debuda.py debuda_test $EXTRA_ARGUMENTS --test --commands "$1"
    if [ $? -ne 0 ]; then
        echo "***"
        echo "Error: test failed while running dbd/debuda.py with commands: $1"
        echo "***"
        exit 2
    fi
}

if [[ $EXTRA_ARGUMENTS == *"--server-cache=on"* ]]; then CACHE_ONLY=true; else CACHE_ONLY=false; fi

# Use netlist core locations
CORE_LOC_11="0,0"
CORE_LOC_22="1,1"

# Define the command array. IMPROVE: we could parse commands' help and extract the examples.
declare -a COMMAND_LIST
COMMAND_LIST=()
COMMAND_LIST+=("op-map")
COMMAND_LIST+=("d")
COMMAND_LIST+=("d 0 netlist nocTr")
COMMAND_LIST+=("q")
COMMAND_LIST+=("q input0")
COMMAND_LIST+=("q input0 16 16")
COMMAND_LIST+=("eq")
COMMAND_LIST+=("eq 1")
COMMAND_LIST+=("dq")
COMMAND_LIST+=("p 130000000000")
COMMAND_LIST+=("brxy $CORE_LOC_11 0x0 32 --format i8")
COMMAND_LIST+=("cdr")
COMMAND_LIST+=("cdr $CORE_LOC_11")
COMMAND_LIST+=("srs 0")
COMMAND_LIST+=("srs 1")
COMMAND_LIST+=("srs 2")
COMMAND_LIST+=("ddb 0 32")
COMMAND_LIST+=("ddb 0 16 hex8 $CORE_LOC_11 0")
COMMAND_LIST+=("ddb 0 16 hex16 $CORE_LOC_22 0")
COMMAND_LIST+=("pcir 0")
if [ "$CACHE_ONLY" = "false" ]; then
    COMMAND_LIST+=("wxy $CORE_LOC_11 0 0xabcd")
    COMMAND_LIST+=("full-dump")
fi
COMMAND_LIST+=("ha")
if [ "$ARCH_NAME" = "grayskull" ]; then
    COMMAND_LIST+=("s $CORE_LOC_11 4")
else
    COMMAND_LIST+=("s 20-18 4")
fi
COMMAND_LIST+=("t 1")
COMMAND_LIST+=("t 1 --raw")
if [ "$TEST_NAME" = "" ]; then
    COMMAND_LIST+=("export")  # Finally, we export the dump to try to rerun it
else
    COMMAND_LIST+=("export ${TEST_NAME}.zip")
fi
COMMAND_LIST+=("exit")

# Join the commands
JOINED_COMMANDS=$(IFS=";"; echo "${COMMAND_LIST[*]}")

# Run all commands
run_debuda "$JOINED_COMMANDS"

# run_debuda "gpr"
# gpr needs a core to be hung. To reproduce:
#   git apply dbd/test/inject-errors/sfpu_reciprocal-infinite-spin-wormhole_b0.patch
#   ./build/test/verif/op_tests/test_op --netlist dbd/test/netlists/netlist_multi_matmul_perf.yaml --seed 0 --silicon --timeout 60
#   ctrl-C
# Then find a valid core (using op-map) and run gpr on it.

# Print coverage results and generate lcov file
if [ "$COV" = "1" ]; then
    coverage report --sort=cover
    coverage lcov
fi

echo ""
echo "All commands passed for test $TEST_NAME"
