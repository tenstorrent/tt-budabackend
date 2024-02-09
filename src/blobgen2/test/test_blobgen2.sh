#!/bin/bash

# ----------------------------------------------------------------------------------------------------------------------
# Fixed script constants
# ----------------------------------------------------------------------------------------------------------------------
RED='\033[0;31m'; GRN='\033[0;32m'; NOCOL='\033[0m';
PASS="${GRN}<PASSED>${NOCOL}"
FAIL="${RED}<FAILED>${NOCOL}"

TEST_RUNNER="src/blobgen2/test/blobgen2.sh"
# ----------------------------------------------------------------------------------------------------------------------

# ----------------------------------------------------------------------------------------------------------------------
# Tests setup
# ----------------------------------------------------------------------------------------------------------------------
TEST_BLOB_YAMLS=(
    "src/blobgen2/test/inputs/dram_datacopy/blob.yaml"
    "src/blobgen2/test/inputs/dram_datacopy_pipe_scatter/blob.yaml"
    "src/blobgen2/test/inputs/datacopy_datacopy_pipe_scatter/blob.yaml"
)
NUM_TESTS=${#TEST_BLOB_YAMLS[@]}

TEST_LOGS=(
    "tt_build/blobgen2/test/dram_datacopy.log"
    "tt_build/blobgen2/test/dram_datacopy_pipe_scatter.log"
    "tt_build/blobgen2/test/datacopy_datacopy_pipe_scatter.log"
)
NUM_LOGS=${#TEST_LOGS[@]}

if [ $NUM_TESTS -ne $NUM_LOGS ]; then
    printf "Error: Number of tests in TEST_BLOB_YAMLS must be the same as the number of logs in TEST_LOGS.\n"
    exit 1
fi

SUMMARY_LOG=tt_build/blobgen2/test/test_blobgen2_summary.log

QUIET=0

DRY_RUN=0
# ----------------------------------------------------------------------------------------------------------------------

# ----------------------------------------------------------------------------------------------------------------------
# Print test setup
# ----------------------------------------------------------------------------------------------------------------------
printf -- "\n"
printf -- "---------------------------------------------------------------------------------------------------\n"
printf -- "Test list:\n"
printf -- "---------------------------------------------------------------------------------------------------\n"
for ((i=0; i<$NUM_TESTS; i++))
do
    printf -- "${TEST_BLOB_YAMLS[$i]}\n"
done
printf -- "---------------------------------------------------------------------------------------------------\n"
# ----------------------------------------------------------------------------------------------------------------------

# ----------------------------------------------------------------------------------------------------------------------
# Exit if dry run
# ----------------------------------------------------------------------------------------------------------------------
if [ $DRY_RUN -eq 1 ]; then
    exit 0;
fi
# ----------------------------------------------------------------------------------------------------------------------

# ----------------------------------------------------------------------------------------------------------------------
# Run tests
# ----------------------------------------------------------------------------------------------------------------------
for ((i=0; i<$NUM_TESTS; i++))
do
    printf -- "\n"
    printf -- "---------------------------------------------------------------------------------------------------\n"
    printf -- " Executing test $(($i+1)).\n"
    printf -- "---------------------------------------------------------------------------------------------------\n"

    # Create log directory if not existing
    mkdir -p $(dirname ${TEST_LOGS[$i]})

    # Execute a test a save the return code
    TEST_BLOB_YAML=${TEST_BLOB_YAMLS[$i]}
    TEST_CMD="$TEST_RUNNER $TEST_BLOB_YAML"
    sh -c "$TEST_CMD" 2>&1 > ${TEST_LOGS[$i]}
    RUNCODE[$i]=$?

    # Compare input and output blob.yaml and save the return code
    OUT_BLOB_YAML=$(echo $TEST_BLOB_YAML | sed -e 's/blob.yaml/out_blob.yaml/')
    diff $TEST_BLOB_YAML $OUT_BLOB_YAML
    TESTCODE[$i]=$?

    # Dump blob.yaml if not quiet execution
    if [ $QUIET -eq 0 ]; then
        printf "\n"
        cat ${TEST_LOGS[$i]}
    fi
done
# ----------------------------------------------------------------------------------------------------------------------

# ----------------------------------------------------------------------------------------------------------------------
# Print test status summary
# ----------------------------------------------------------------------------------------------------------------------
printf -- "\n"
printf -- "---------------------------------------------------------------------------------------------------\n"
printf -- " Summary:\n"
printf -- "---------------------------------------------------------------------------------------------------\n"
rm -f $SUMMARY_LOG
for ((i=0; i<$NUM_TESTS; i++))
do
    if [[ ${RUNCODE[$i]} -eq 0 && ${TESTCODE[$i]} -eq 0 ]]; then
        TEST_SUMMARY="$PASS: ${TEST_BLOB_YAMLS[$i]}.\n"
    else
        if [ ${RUNCODE[$i]} -gt 0 ]; then
            TEST_SUMMARY="$FAIL execution: ${TEST_BLOB_YAMLS[$i]} (retcode ${RUNCODE[$i]})\n"
        else
            TEST_SUMMARY="$FAIL comparison: ${TEST_BLOB_YAMLS[$i]} (retcode ${TESTCODE[$i]})\n"
        fi
    fi
    printf "$TEST_SUMMARY" | tee -a $SUMMARY_LOG
done
printf -- "---------------------------------------------------------------------------------------------------\n"
# ----------------------------------------------------------------------------------------------------------------------
