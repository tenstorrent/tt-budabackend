#!/bin/bash
#source ./scripts/regr_utils.sh
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[0;33m';
INFO='\033[1;33m'
ENDL='\033[0m\n' # No color, newline
PASS="${GRN}<PASSED>${ENDL}"
FAIL="${RED}<FAILED>${ENDL}"
WAIVE="${YLW}<WAIVED>${ENDL}"

# ----------------------------------------------------------------------------------------------------------
# Parse arguments -- getopt style (enables multi-letter options)
# ----------------------------------------------------------------------------------------------------------
DIRECTORY=""    # Required
SEED=0  # Not required
ARCH=$ARCH_NAME     # Required (either 'grayskull' or 'wormhole')
TIMEOUT=120  # By default
HARD_RESET=0    # Choose if we use soft reset or not (soft reset is used on machines with no reset dongle)
CHIP_ID=-1  # ID of card reserved through reserve_device.py on multi-card machines
FIRST_TEST=1    # Starting from 1 to NUM_NETLISTS
KEEP_LOG=0  # Delete log files at the beginning
RUN_FAILED_ONLY=0   # Run only netlists that were failed previously. Depends on the log files
USE_BIN_STIMULI=0   # Whether to use bin stimuli provided by PyBuda for input queues or randomly generated based on stimulus-config.
DETERMINISM_TESTS=""    # Whether to run the tests as determinism tests
PIPEGEN_VERSION_SELECTOR="" # Select which pipegen version to use to compile the tests (by default use pipegen v2)

# Read command-line arguments
SHORT_OPTS=d:,s::,f::,a:,l,r,h
LONG_OPTS=dir:,arch:,seed::,start::,chip-id:,timeout:,use-bin-stimuli,hard-reset,determinism-tests,keep-log,rfo,help
OPTS=$(getopt --options $SHORT_OPTS --longoptions $LONG_OPTS --name "$(basename "$0")" -- "$@")
eval set -- $OPTS

while [[ $# -gt 0 ]]
do
    case "$1" in
        -d|--dir)
            case "$2" in
                "")
                    DIRECTORY=$DIRECTORY
                    shift 2 # shift past argument and value
                    ;;
                *)
                    DIRECTORY=$2
                    shift 2 # shift past argument and value
                    ;;
            esac ;;

        -s|--seed)
            case "$2" in
                "")
                    SEED=$SEED
                    shift 2 # shift past argument and value
                    ;;
                *)
                    SEED=$2
                    shift 2 # shift past argument and value
                    ;;
            esac ;;

        -a|--arch)
            case "$2" in
                "")
                    ARCH=$ARCH
                    shift 2 # shift past argument and value
                    ;;
                *)
                    ARCH=$2
                    shift 2 # shift past argument and value
                    ;;
            esac ;;

        -f|--start)
            case "$2" in
                "")
                    FIRST_TEST=$FIRST_TEST
                    shift 2 # shift past argument and value
                    ;;
                *)
                    FIRST_TEST=$2
                    shift 2 # shift past argument and value
                    ;;
            esac ;;

        --chip-id)
            case "$2" in
                "")
                    CHIP_ID=$CHIP_ID
                    shift 2
                    ;;
                *)
                    CHIP_ID=$2
                    shift 2
                    ;; # shift past argument and value
            esac ;;

        --timeout)
            case "$2" in
                "")
                    TIMEOUT=$TIMEOUT
                    shift 2
                    ;;
                *)
                    TIMEOUT=$2
                    shift 2
                    ;; # shift past argument and value
            esac ;;

        --use-bin-stimuli)
            USE_BIN_STIMULI=1
            shift # shift past argument
            ;;

        --hard-reset)
            HARD_RESET=1
            shift # shift past argument
            ;;

        --determinism-tests)
            DETERMINISM_TESTS="--determinism-test"
            shift # shift past argument
            ;;

        -l|--keep-log)
            KEEP_LOG=1
            shift # shift past argument
            ;;

        -r|--rfo)
            RUN_FAILED_ONLY=1
            shift # shift past argument
            ;;

        -h|--help)
            echo "Usage: $0"
            echo "Example: ./verif/graph_tests/run_tests.sh --dir=configs --arch=grayskull --chip-id=0 --timeout=60"
            echo ""
            echo "    [-d<directory> | --dir=<directory>]"
            echo "    [-s<seed> | --seed=<seed>]"
            echo "    [-a<arch> | --arch=<arch>]"
            echo "    [--chip-id=<CHIP_ID>]"
            echo "    [--timeout=<timeout-sec>]"
            echo "    [--use-bin-stimuli=<path-to-bins>]"
            echo "    [--hard-reset=<no_soft_reset>"
            echo "    [--determinism-tests=<run-as-determinism-tests>"
            echo "    [-l | --keep-log]"
            echo "    [-r | --rfo]"
            echo "    [-f<test_index> | --start=<test_index>]"
            echo ""
            echo "    <d | dir>             - name of the directory with netlists to be used as test input. MANDATORY."
            echo "    <s | seed>            - seed to be used in the test for generating tensors. OPTIONAL. Default: $SEED."
            echo "    <a | arch>            - on which chip to run tests (grayskull or wormhole or wormhole_b0). MANDATORY."
            echo "    <chip-id>             - ID of chip reserved through reserve_device.py on multi-chip machines. Needs to be provided if soft-reset is passed. MANDATORY. Default: $CHIP_ID"
            echo "    <timeout>             - timeout in secs to wait until test finishes run, otherwise declared it as a hang. OPTIONAL. Default: $TIMEOUT"
            echo "    <use-bin-stimuli>     - use bin stimuli for input queues generated from PyBuda (otherwise, randomly generated stimuli based on stimulus-config will be used instead). OPTIONAL. Default: false"
            echo "    <hard-reset>          - flag which indicates we don't want to use soft reset on reserved chip. Soft reset is used on machines with no reset dongle. OPTIONAL. Default: false"
            echo "    <determinism-tests>   - run the tests as determinism tests, meaning only validate that the backend is producing consitnent output across test runs. OPTIONAL. Default: false"
            echo "    <l | keep-log>        - do not delete log directory and files if they exist. OPTIONAL. Default: false"
            echo "    <r | rfo>             - run only previously failed test (based on summary.log file in logs directory). OPTIONAL. Default: false"
            echo "    <f | start>           - first test index (starting at 1, and out of N tests) to be run. OPTIONAL. Default: $FIRST_TEST"

            exit 1
            ;;

        *)  # skip other arguments
            shift # shift past argument
            ;;
    esac
done
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Check parsed arguments
# ----------------------------------------------------------------------------------------------------------
# Check if DIRECTORY exists
if [[ ! -d $DIRECTORY || $DIRECTORY == "" ]]
then
    echo "ERROR: -d or --dir argument has to be provided as a path to an existing directory. Got '$DIRECTORY' as the argument"
    exit 1
fi

# Check if SEED is a positive integer number
re='^[0-9]+$'
if ! [[ $SEED =~ $re && $SEED -ge 0 ]]
then
    echo "ERROR: -s or --seed argument has to be a positive integer number. Got '$SEED' as the argument"
    exit 1
fi

# Check if $FIRST_TEST is a positive integer number
if ! [[ $FIRST_TEST -gt 0 ]]
then
    echo "ERROR: -f or --start argument has to be a positive non-zero integer number. Got '$FIRST_TEST' as the argument"
    exit 1
fi

# Check if $ARCH is either "grayskull" or "wormhole"
if ! [[ $ARCH == "grayskull" || $ARCH == "wormhole" || $ARCH == "wormhole_b0" ]]
then
    echo "ERROR: -a or --arch argument has to be one of ['grayskull', 'wormhole', 'wormhole_b0']. Got '$ARCH' as the argument"
    exit 1
fi

# Check if $CHIP_ID argument is provided if using soft reset
if [[ $HARD_RESET -eq 0 && $CHIP_ID -eq -1 ]]
then
    echo "ERROR: --chip-id argument has to be provided if soft reset is used"
    exit 1
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Print parsed arguments
# ----------------------------------------------------------------------------------------------------------
echo "Arguments:"
echo "  DIRECTORY=$DIRECTORY"
echo "  ARCH=$ARCH"
echo "  HARD_RESET=$HARD_RESET"
echo "  CHIP_ID=$CHIP_ID"
echo "  SEED=$SEED"
echo "  KEEP_LOG=$KEEP_LOG"
echo "  FIRST_TEST=$FIRST_TEST"
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
 # Search for netlists in the directory
# ----------------------------------------------------------------------------------------------------------
if [[ $RUN_FAILED_ONLY -lt 1 ]]
then
    NETLISTS=(`find $DIRECTORY -type f -regex ".*netlist_.*yaml\|.*_netlist.yaml"`)
else
    NETLISTS=(`grep FAILED $DIRECTORY/logs/summary.log | sed -e 's/:.*//'`)
    # TODO: If multiple summary.log files or in non log directory (e.g. due to -l), need to enable to point to the desired summary.log
fi
NUM_NETLISTS=${#NETLISTS[@]}

if [[ $NUM_NETLISTS -lt 1 ]]
then
    echo "Error: No netlists found in $DIRECTORY directory."
    exit 1
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Remove logs from previous runs of the same set of tests
# ----------------------------------------------------------------------------------------------------------
if [[ $KEEP_LOG -lt 1 ]]
then
    rm -fdr $DIRECTORY/logs
else
    mv $DIRECTORY/logs $DIRECTORY/logs.saved_at_`date +%Y%m%d_%H%M%S`
fi

mkdir $DIRECTORY/logs
touch $DIRECTORY/logs/summary.log
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Depending on type of reset, different command is executed.
# ----------------------------------------------------------------------------------------------------------
reset_device() {
    if [[ $HARD_RESET == 0 ]]
    then
        ./device/bin/silicon/tensix-reset pci:$CHIP_ID
    else
        ./device/bin/silicon/reset.sh
    fi

    # Wormhole needs booting after reset
    if [[ $ARCH == "wormhole" || $ARCH == "wormhole_b0" ]]
    then
        ./device/bin/silicon/wormhole/boot
    fi
}
# ----------------------------------------------------------------------------------------------------------

# ----------------------------------------------------------------------------------------------------------
# Print intro message header
# ----------------------------------------------------------------------------------------------------------
printf -- "==============================================================================================\n"
printf "Running $(($NUM_NETLISTS-$FIRST_TEST+1)) tests from $DIRECTORY directory\n"
printf -- "----------------------------------------------------------------------------------------------\n"
printf "\n"
printf "Resetting device at the beginning...\n"
reset_device >> $DIRECTORY/logs/initial_reset.log 2>&1
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Execute each test in a loop. Reset device after each test run. Log result to file and write to console.
# ----------------------------------------------------------------------------------------------------------
test_status=$PASS # Initialize test status

for ((i=$(($FIRST_TEST-1)); i<$NUM_NETLISTS; i++))
do
    TEST_NAME=${NETLISTS[$i]}
    LOG_NAME="${TEST_NAME##*/}"
    LOG_NAME="${LOG_NAME%.[^.]*}"

    # Run a test, print message, and collect return code
    if [[ $USE_BIN_STIMULI == 1 ]]
    then
        # bin files are located in the same dir as netlist
        PATH_TO_BINS=`echo $TEST_NAME | grep -oP '.*(?=/)'`
        TEST_CMD="ARCH_NAME=$ARCH ./build/test/verif/graph_tests/test_graph --netlist $TEST_NAME --bin-path $PATH_TO_BINS --seed $SEED --silicon $DETERMINISM_TESTS --timeout $TIMEOUT $PIPEGEN_VERSION_SELECTOR"
    else
        TEST_CMD="ARCH_NAME=$ARCH ./build/test/verif/graph_tests/test_graph --netlist $TEST_NAME --seed $SEED --silicon $DETERMINISM_TESTS --timeout $TIMEOUT $PIPEGEN_VERSION_SELECTOR"
    fi

    printf "\n"
    printf 'Running test %4d/%d: %s' "$((i+1))" "$NUM_NETLISTS" "$TEST_CMD"

    # Script to continue running even if a test fail
    set +e

    # Dump entire netlist on top of log file
    cat $TEST_NAME >> $DIRECTORY/logs/$LOG_NAME.log 2>&1
    printf "\n\n" >> $DIRECTORY/logs/$LOG_NAME.log 2>&1

    # Run a test in a separate shell to capture core dump messages to the log file
    # sed command removes color codes from strings to make log more readable as a file
    sh -c "$TEST_CMD" 2>&1 | sed -r "s/\x1B\[[0-9;]*[JKmsu]//g" >> $DIRECTORY/logs/$LOG_NAME.log
    # Get return code of first cmd in pipeline
    RETCODE="${PIPESTATUS[0]}"

    # Script to exit if a command fail
    set -e

    # Print test run result to console output and to the summary file
    if [ $RETCODE -eq 0 ]
    then
        printf "  $PASS"
        printf "$TEST_NAME: $PASS" >> $DIRECTORY/logs/summary.log
    else
        printf "  $FAIL"
        printf "$TEST_NAME: $FAIL" >> $DIRECTORY/logs/summary.log
        test_status=$FAIL
        printf "Logfile: %s\n" $DIRECTORY/logs/$LOG_NAME.log
        # Reset device after the test run to leave it in a clean state for the next run
        printf "Resetting device after a failure...\n"
        reset_device >> $DIRECTORY/logs/$LOG_NAME.log 2>&1
    fi
done
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Print summary
# ----------------------------------------------------------------------------------------------------------
printf -- "----------------------------------------------------------------------------------------------\n"
printf "\n"
printf "Summary: $test_status\n"
printf -- "----------------------------------------------------------------------------------------------\n"
printf "Details:\n"
cat $DIRECTORY/logs/summary.log
printf -- "==============================================================================================\n"

# Reset device at the end
printf "Resetting device at the end of script...\n"
reset_device >> $DIRECTORY/logs/$LOG_NAME.log 2>&1

printf "Done."