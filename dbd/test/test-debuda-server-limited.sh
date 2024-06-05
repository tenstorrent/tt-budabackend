#!/bin/bash

set -e
TEST_NAME="$1"

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


# Use netlist core locations
CORE_LOC_11="0,0"
CORE_LOC_22="1,1"

# These commands are only for limited mode.
declare -a COMMAND_LIST
COMMAND_LIST=()
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
COMMAND_LIST+=("wxy $CORE_LOC_11 0 0xabcd")
COMMAND_LIST+=("full-dump")
COMMAND_LIST+=("exit")

# TODO: Check caching and export

JOINED_COMMANDS=$(IFS=";"; echo "${COMMAND_LIST[*]}")


echo "BASH: Starting server"
$COVERAGE_CMD dbd/debuda.py debuda_test --server --test &
SERVER_PID=$!

# Wait for server to start
sleep 6

timeout 30 $COVERAGE_CMD dbd/debuda.py --remote --test --commands "$JOINED_COMMANDS"

if [ "$COV" = "1" ]; then
    coverage report --sort=cover
    coverage lcov
fi

echo ""
echo "All limited commands passed for test $TEST_NAME"

echo "BASH: Killing server"
kill $SERVER_PID 2>/dev/null

