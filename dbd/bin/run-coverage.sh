#!/bin/bash
RUN_DIR=dbd/run           # Where to store run results
EXPECTED_COVER_PCT=70     # Complain when coverage is below this percantage
COVERAGE_RUN="coverage run --include=dbd/**"
# DEBUDA_CMD="dbd/debuda.py --server-cache off"     # Run without reading or writing the cache file
DEBUDA_CMD="dbd/debuda.py --server-cache through" # Read cache, run, save cache
# DEBUDA_CMD="dbd/debuda.py --server-cache on"      # Do not read cache, run, save cache
# set -x # To show all commands run
set -o pipefail # Propagate exit codes through pipes

if [ "$ARCH_NAME" != "wormhole" ] && [ "$ARCH_NAME" != "grayskull" ]; then
    echo "ARCH_NAME '$ARCH_NAME' is not valid"
    exit 1
fi

# Checks whether Debuda run failed
check_logfile_exit_code () {
    log_file="$1"
    DBD_EXIT_CODE=`cat $log_file | grep "Exiting with code " | awk '{print $4}'`
    if [ "$DBD_EXIT_CODE" == "0" ]; then echo "Debuda run OK"; else
        echo "FAIL: Debuda run failed with exit code '$DBD_EXIT_CODE' (see $log_file)";
        exit $DBD_EXIT_CODE
    fi
}

# Run one coverage command
coverage_run () {
    log_file=$1
    cov_command="$2"
    cov_args="$3"
    debuda_cmd="$4"
    debuda_commands_arg="$6"
    $cov_command $cov_args $debuda_cmd "$5" "$debuda_commands_arg" | tee "$log_file"
    check_logfile_exit_code "$log_file"
}

# When SKIP_RUN is defined, only existing files will be used - good for testing.
if [ "$SKIP_RUN" == "" ]; then
# Setup environment
rm -rf $RUN_DIR
rm *.pkl
mkdir -p $RUN_DIR
sudo pip install coverage
make -j8 && make -j8 verif/op_tests/test_op
device/bin/silicon/reset.sh
if [ "$ARCH_NAME" == "wormhole" ]; then ./device/bin/silicon/wormhole/boot; fi

# Apply the patch to cause a hang
git apply dbd/test/inject-errors/sfpu_reciprocal-infinite-spin-$ARCH_NAME.patch

# Run the test
./build/test/verif/op_tests/test_op --netlist dbd/test/netlists/netlist_multi_matmul_perf.yaml --seed 0 --silicon --timeout 30 | tee $RUN_DIR/test_op_run.txt
fi

# Run Debuda
## This debuda run command sequence is supposed to return exact same text always
coverage_run $RUN_DIR/coverage-exact-match.log "$COVERAGE_RUN" ""       "$DEBUDA_CMD" --commands "hq; dq; abs; abs 1; s 1 1 8; cdr 1 1; c 1 1; d; t 1 0; d 0; d 1; cdr 1 1; b 10000030000; 0; 0; eq; brxy 1 1 0 0; help; srs 0; srs 1; srs 2; fd ; t 1 1; op-map; export $RUN_DIR/coverage-exact-match.zip; x"

# Undo the patch
git apply -R dbd/test/inject-errors/sfpu_reciprocal-infinite-spin-$ARCH_NAME.patch

# Check coverage
coverage report --sort=cover --show-missing | tee $RUN_DIR/coverage-report.txt
COVER_PCT=`cat $RUN_DIR/coverage-report.txt | tail -n 1 | awk '{print $4}' | tr -d "%"`
if (( $COVER_PCT < $EXPECTED_COVER_PCT )) ; then echo "FAIL: Coverage ($COVER_PCT) is smaller then expected ($EXPECTED_COVER_PCT)"; exit 1; fi

# Show what needs to be done to set the current run as expected run
echo "To set the current run as expected:"
echo cp $RUN_DIR/coverage-exact-match.log dbd/test/expected-results/coverage-$ARCH_NAME-exact-match.expected
echo cp $RUN_DIR/coverage-exact-match.zip dbd/test/exported-runs/$ARCH_NAME-regression/coverage-exact-match.zip
echo "To run offline test:"
echo ARCH_NAME=$ARCH_NAME dbd/bin/run-offline-tests.sh
echo "Then, copy the expected log file from the offline run again:"
echo cp $RUN_DIR/dbd-export/coverage-exact-match.log dbd/test/expected-results/coverage-$ARCH_NAME-exact-match.expected
