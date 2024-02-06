#!/bin/bash
RUN_DIR=dbd/run           # Where to store run results
EXPECTED_COVER_PCT=70     # Complain when coverage is below this percantage
COVERAGE_RUN="coverage run --include=**/dbd/**"
# DEBUDA_CMD="dbd/debuda.py --server-cache off"     # Run without reading or writing the cache file
DEBUDA_CMD="dbd/debuda.py --server-cache on" # Read cache, run, save cache
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
    log_file=`pwd`/$1
    cov_command="$2"
    cov_args="$3"
    debuda_cmd="$4"
    debuda_commands_arg="$6"
    $cov_command $cov_args $debuda_cmd "$5" "$debuda_commands_arg" > "$log_file"
    EXIT_CODE="$?"
    if [ "$EXIT_CODE" == "0" ]; then check_logfile_exit_code "$log_file"; else echo "Debuda run failed with exit code $EXIT_CODE (see $log_file)"; tail $log_file; exit $EXIT_CODE; fi
}

ROOT=`pwd`

# Run Debuda
rm -rf $RUN_DIR/dbd-export
mkdir -p $RUN_DIR
unzip dbd/test/exported-runs/$ARCH_NAME-regression/coverage-exact-match.zip -d $RUN_DIR/dbd-export
cd $ROOT/$RUN_DIR/dbd-export
DEBUDA_COMMANDS_TO_RUN="q; hq; dq; abs; abs 1; s 1 1 8;
cdr 1 1; c 1 1; d; t 1 0; d 0; d 1; cdr 1 1; b 10000030000; 0; 0;
brxy 1 1 0 0; help; srs 0; srs 1; srs 2; fd; t 1 1; op-map; self-test;
export;
x"
coverage_run coverage-exact-match.log "$COVERAGE_RUN" "" "../../../$DEBUDA_CMD" --commands "$DEBUDA_COMMANDS_TO_RUN"
coverage report --sort=cover --show-missing | tee coverage-report.txt

cd $ROOT

COVER_PCT=`cat $ROOT/$RUN_DIR/dbd-export/coverage-report.txt | tail -n 1 | awk '{print $4}' | tr -d "%"`
if (( $COVER_PCT < $EXPECTED_COVER_PCT )) ; then echo "FAIL: Coverage ($COVER_PCT) is smaller then expected ($EXPECTED_COVER_PCT)"; exit 1; fi
echo "=== coverage: $COVER_PCT%"
echo "=== cat $ROOT/$RUN_DIR/dbd-export/coverage-report.txt"

# Compare the output with the expected
compare_files="$ROOT/$RUN_DIR/dbd-export/coverage-exact-match.log $ROOT/dbd/test/expected-results/coverage-$ARCH_NAME-exact-match.expected"
echo diff $compare_files
diff $compare_files
compare_exit_code="$?"
if [ "$compare_exit_code" == "0" ]; then echo PASS; else echo FAIL: diff $compare_files; exit "$compare_exit_code"; fi
