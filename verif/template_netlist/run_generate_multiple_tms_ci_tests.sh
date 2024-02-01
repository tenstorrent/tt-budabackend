#!/bin/bash
source ./scripts/regr_utils.sh


# ----------------------------------------------------------------------------------------------------------
# Parse arguments -- getopt style (enables multi-letter options)
# ----------------------------------------------------------------------------------------------------------
# Default arguments values
TEST_MODULE=test_modules.multi_tm_tests.test_dram_input_datacopy_multiple_tms_and_reblock
GEN_SEED=1
GIT_SEED=1
NUM_TESTS_PER_SCRIPT_RUN=320
NUM_SCRIPT_RUNS=1
NUM_TMS_PER_CONN=2
NUM_CONFIGS_PER_TMS_COMBINATION=5 #NUM_TESTS_PER_SCRIPT_RUN/(8^NUM_TMS_PER_CONN)

# Default output directory name
#E.g. OUTPUT_DIR=verif/graph_tests/netlists/z3/test_dram_input_datacopy_multiple_tms_and_reblock_`date +%Y%m%d_%H%M%S`
SHORT_TEST_MODULE=`echo $TEST_MODULE | sed -e 's/.*[.]//g'`
OUTPUT_DIR_PATH=verif/graph_tests/netlists/z3
OUTPUT_DIR_NAME=`echo $SHORT_TEST_MODULE | sed -e "s/multiple_tms/$NUM_TMS_PER_CONN"tms"/"`
OUTPUT_DIR_TIMESTAMP=`date +%Y%m%d_%H%M%S`
OUTPUT_DIR=$OUTPUT_DIR_PATH/$OUTPUT_DIR_NAME"_"$OUTPUT_DIR_TIMESTAMP

# Default target architecture is grayskull
ARCH="grayskull"

# Number of rows harvested on the target architecture chip
HARVESTED_ROWS=0

# Default Z3 configs
Z3_TIMEOUT=0 # infinite timeout
Z3_NUM_RETRIES_ON_TIMEOUT=0 # no retries
Z3_ENABLE_STRATEGY="" # no z3 strategy enabled

# Flag that is set if default output directory name needs to be updated after parsing the arguments
GET_OUTPUT_DIR_LATER=0
GOT_CUSTOM_OUTPUT_DIR=0

# Read command-line arguments
#  - single colon (:) means a value is required for the argument
#  - double colon (::) means a value optional for the argument
#  - no colon means no value is required for the argument
SHORT_OPTS=m:,o:,a:,s:,t:,r:,n:,c:,w:,x:,y:,z,h
LONG_OPTS=module:,out-dir:,arch:,seed:,
LONG_OPTS+=num-tests-per-run:,num-runs:,num-tms-per-connection:,num-configs-per-tms-combination:,harvested-rows:,
LONG_OPTS+=timeout:,num-retries-on-timeout:,enable-strategy,help
opts=$(getopt -o $SHORT_OPTS -l $LONG_OPTS -n "$(basename "$0")" -- "$@")
eval set -- $opts

while [[ $# -gt 0 ]]; do
  case "$1" in
    -m|--module)
      case "$2" in
        "") TEST_MODULE=$TEST_MODULE                ; shift 2 ;; #default
         *) TEST_MODULE=$2 ; GET_OUTPUT_DIR_LATER=1 ; shift 2 ;; #passed argument
      esac ;;

    -o|--out-dir)
      case "$2" in
        "") GET_OUTPUT_DIR_LATER=1                  ; shift 2 ;; #default
         *) OUTPUT_DIR=$2 ; GOT_CUSTOM_OUTPUT_DIR=1 ; shift 2 ;; #passed argument
      esac ;;

    -a|--arch)
      case "$2" in
        "") ARCH="grayskull" ; shift 2 ;; #default
         *) ARCH=$2          ; shift 2 ;; #passed argument
      esac ;;

    -s|--seed)
      case "$2" in
        "") GEN_SEED=1; GIT_SEED=1; SEED='' ; shift 2 ;; #default is to generate a seed based on git commit hash
       "r") GEN_SEED=1; GIT_SEED=0; SEED='' ; shift 2 ;; #generate a random seed between 0 and 1000
         *) GEN_SEED=0;             SEED=$2 ; shift 2 ;; #passed argument
      esac ;;

    -t|--num-tests-per-run)
      case "$2" in
        "") NUM_TESTS_PER_SCRIPT_RUN=$NUM_TESTS_PER_SCRIPT_RUN ; shift 2 ;; #default
         *) NUM_TESTS_PER_SCRIPT_RUN=$2                        ; shift 2 ;; #passed argument
      esac ;;

    -r|--num-runs)
      case "$2" in
        "") NUM_SCRIPT_RUNS=$NUM_SCRIPT_RUNS ; shift 2 ;; #default
         *) NUM_SCRIPT_RUNS=$2               ; shift 2 ;; #passed argument
      esac ;;

    -n|--num-tms-per-connection)
      case "$2" in
        "") NUM_TMS_PER_CONN=$NUM_TMS_PER_CONN           ; shift 2 ;; #default
         *) NUM_TMS_PER_CONN=$2 ; GET_OUTPUT_DIR_LATER=1 ; shift 2 ;; #passed argument
      esac ;;

    -c|--num-configs-per-combination)
      case "$2" in
        "") NUM_CONFIGS_PER_TMS_COMBINATION=$NUM_CONFIGS_PER_TMS_COMBINATION; shift 2 ;; #default
         *) NUM_CONFIGS_PER_TMS_COMBINATION=$2                              ; shift 2 ;; #passed argument
      esac ;;

    -w|--harvested-rows)
      case "$2" in
        "") HARVESTED_ROWS=$HARVESTED_ROWS; shift 2 ;; #default
         *) HARVESTED_ROWS=$2             ; shift 2 ;; #passed argument
      esac ;;

    -x|--timeout)
      case "$2" in
        "") Z3_TIMEOUT=$Z3_TIMEOUT; shift 2 ;; #default
         *) Z3_TIMEOUT=$2         ; shift 2 ;; #passed argument
      esac ;;

    -y|--num-retries-on-timeout)
      case "$2" in
        "") Z3_NUM_RETRIES_ON_TIMEOUT=$Z3_NUM_RETRIES_ON_TIMEOUT; shift 2 ;; #default
         *) Z3_NUM_RETRIES_ON_TIMEOUT=$2                        ; shift 2 ;; #passed argument
      esac ;;

    -z|--enable-strategy)
      Z3_ENABLE_STRATEGY="--enable-strategy"
      shift # shift past argument
      ;;

    -h|--help)
      echo ""
      printf "Usage: $0\n"
      printf "    [-m<test_module | --module<test_module>]\n"
      printf "    [-o<output_directory> | --out-dir=<output_directory>]\n"
      printf "    [-a<architecture> | --arch=<architecture>]\n"
      printf "    [-s<seed> | --seed=<seed> | -sr | -seed=r]\n"
      printf "    [-t<num_tests_per_run> | --num-tests-per-run=<num_tests_per_run>]\n"
      printf "    [-r<num_runs> | --num-runs=<num_runs>]\n"
      printf "    [-n<num_tms_per_connection> | --num-tms-per-connection=<num_tms_per_conn>]\n"
      printf "    [-c<num_configs> | --num-configs_per_tms_combination=<num_configs>]\n"
      printf "    [-w<harvested_rows> | --harvested-rows=<harvested_rows>]\n"
      printf "    [-x<timeout> | --timeout=<timeout>]\n"
      printf "    [-y<num_retries> | --num-retries-on-timeout=<num_retries>]\n"
      printf "    [-z | --enable-strategy]\n"
      printf "    [-h | --help]\n"
      echo ""
      echo "<test_module>       - name of the test module to be executed. Default: $TEST_MODULE."
      echo ""
      echo "<output_directory>  - name of the directory to store generated netlists. Default: a new directory name generated."
      echo ""
      echo "<architecture>      - target device architecture for which to generate tests. Default: $ARCH."
      echo ""
      echo "<seed>              - seed to be used in the generating netlists."
      echo "                      If 'r' specified, seed will be randonly generated number between 1 and 1000."
      echo "                      Default: seed based on latest git commit hash."
      echo ""
      echo "<num_tests_per_run> - number of test netlists to be generated per run of generate_ci_tests.py script. Default: $NUM_TESTS_PER_SCRIPT_RUN."
      echo ""
      echo "<num_runs>          - number of TM combinations to be generated. Default: $NUM_SCRIPT_RUNS."
      echo ""
      echo "<num_tms_per_conn>  - number of TMs on a connection to be generated. Default: $NUM_TMS_PER_CONN."
      echo ""
      echo "<num_configs>       - number test netlists to be generated for each combination of <num_tms_per_conn> TMs. Default: $NUM_CONFIGS_PER_TMS_COMBINATION."
      echo ""
      echo "<harvested_rows>    - Number of harvested rows on the target chip. Default: $HARVESTED_ROWS."
      echo ""
      echo "<timeout>           - timeout in seconds for a single z3 solution finding. Default: $Z3_TIMEOUT(0=infinite)."
      echo ""
      echo "<num_retries>       - number of retries with a new random seed after Z3 timeout. Default: $Z3_NUM_RETRIES_ON_TIMEOUT."
      echo ""
      echo "<enable-strategy>   - enables z3 solving strategy that can reduce generation time in some cases. Default: $Z3_ENABLE_STRATEGY."
      echo ""

      shift
      exit 1
      ;;

    *) #skip other arguments
      shift
      break
      ;;
  esac
done


# Update default output directory name
if [[ $GOT_CUSTOM_OUTPUT_DIR == 0 && $GET_OUTPUT_DIR_LATER == 1 ]] ; then
    SHORT_TEST_MODULE=`echo $TEST_MODULE | sed -e 's/.*[.]//g'`
    OUTPUT_DIR_NAME=`echo $SHORT_TEST_MODULE | sed -e "s/multiple_tms/$NUM_TMS_PER_CONN"tms"/"`
    OUTPUT_DIR=$OUTPUT_DIR_PATH/$OUTPUT_DIR_NAME"_"$OUTPUT_DIR_TIMESTAMP
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Check parsed arguments
# ----------------------------------------------------------------------------------------------------------
# Check if TEST_MODULE exists under verif/template_netlist/test_modules directory
TEST_MODULE_FILE_NAME=`echo $TEST_MODULE | sed -e 's/test_modules\.multi_tm_tests\./verif\/template_netlist\/test_modules\/multi_tm_tests\//'`.py
echo $TEST_MODULE_FILE_NAME
if [[ ! -f $TEST_MODULE_FILE_NAME ]] ; then
   echo "Error: -m or --module argument has to be a path to an existing python module under verif/template_netlist/test_modules directory. Got '$TEST_MODULE' as the argument"
   exit 1
fi

# Check if SEED is a positive integer number
re='^[0-9]+$'
if ! [[ $SEED =~ $re && $SEED -ge 0 || $SEED -eq '' ]] ; then
   echo "Error: -s or --seed argument has to be a positive integer number or not specified at all. Got '$SEED' as the argument"
   exit 1
fi

# Check if NUM_TESTS_PER_SCRIPT_RUN is a positive integer number
re='^[0-9]+$'
if ! [[ $NUM_TESTS_PER_SCRIPT_RUN =~ $re && $NUM_TESTS_PER_SCRIPT_RUN -gt 0 ]] ; then
   echo "Error: -t or --num-tests-per-run argument has to be a positive non-zero integer number. Got '$NUM_TESTS_PER_SCRIPT_RUN' as the argument"
   exit 1
fi

# Check if NUM_SCRIPT_RUNS is a positive integer number
re='^[0-9]+$'
if ! [[ $NUM_SCRIPT_RUNS =~ $re && $NUM_SCRIPT_RUNS -gt 0 ]] ; then
   echo "Error: -c or --num-runs argument has to be a positive non-zero integer number. Got '$NUM_SCRIPT_RUNS' as the argument"
   exit 1
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Log file name
# ----------------------------------------------------------------------------------------------------------
LOG_FILE=tm_gen_out_`date +%Y%m%d_%H%M%S`.log
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Print parsed arguments
# ----------------------------------------------------------------------------------------------------------
echo "Arguments:"
echo "  TEST_MODULE=$TEST_MODULE"
echo "  OUTPUT_DIR=$OUTPUT_DIR"
echo "  ARCH=$ARCH"
echo "  GEN_SEED=$GEN_SEED"
echo "  GIT_SEED=$GIT_SEED"
echo "  SEED=$SEED"
echo "  NUM_TESTS_PER_SCRIPT_RUN=$NUM_TESTS_PER_SCRIPT_RUN"
echo "  NUM_SCRIPT_RUNS=$NUM_SCRIPT_RUNS"
echo "  NUM_TMS_PER_CONN=$NUM_TMS_PER_CONN"
echo "  NUM_CONFIGS_PER_TMS_COMBINATION=$NUM_CONFIGS_PER_TMS_COMBINATION"
echo "  LOG_FILE=$LOG_FILE"
echo "  HARVESTED_ROWS=$HARVESTED_ROWS"
echo "  Z3_TIMEOUT=$Z3_TIMEOUT"
echo "  Z3_NUM_RETRIES_ON_TIMEOUT=$Z3_NUM_RETRIES_ON_TIMEOUT"
echo "  Z3_ENABLE_STRATEGY=$Z3_ENABLE_STRATEGY"
#exit 1
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Create output directory if it does not exist
# ----------------------------------------------------------------------------------------------------------
mkdir -p $OUTPUT_DIR
touch $OUTPUT_DIR/$LOG_FILE
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Generate netlists
# ----------------------------------------------------------------------------------------------------------
for ((i=0; i<$NUM_SCRIPT_RUNS; i++))
do
    # Generate seed for each TM combinations run, if seed was not passed as an argument
    if [[ $GEN_SEED -eq 1 ]] ; then
        if [[ $GIT_SEED -eq 1 ]] ; then
            SEED=$(git rev-parse --short HEAD)
            SEED=$((16#$SEED))
            SEED=${SEED#-}
        else
            SEED=$(($RANDOM % 1000))
        fi
        echo "  SEED=$SEED"
    fi

    # Print an appropriate message
    printf "Generating %d tests in script run %d/%d, with SEED=%d, storing results in %s\n" \
    "$NUM_TESTS_PER_SCRIPT_RUN" "$((i+1))" "$NUM_SCRIPT_RUNS" "$SEED" "$OUTPUT_DIR"

    # Generate netlists
    # TODO: Temporary passing 3 NUM_TMS_PER_CONN variables until all test modules are unified
    NUM_TMS_PER_CONN=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN0=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN1=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN2=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN3=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN4=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN5=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN6=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN7=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN8=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN9=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN10=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN11=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN12=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN13=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN14=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN15=$NUM_TMS_PER_CONN \
    NUM_TMS_PER_CONN16=$NUM_TMS_PER_CONN \
    NUM_CONFIGS_PER_TMS_COMBINATION=$NUM_CONFIGS_PER_TMS_COMBINATION \
    ./verif/template_netlist/generate_ci_tests.py \
        --test-module $TEST_MODULE \
        --output-dir $OUTPUT_DIR \
        --random-seed $SEED \
        --num-tests $NUM_TESTS_PER_SCRIPT_RUN \
        --arch $ARCH \
        --search-type parallel-sweep \
        --verbose \
        --skip-sh-files-gen \
        --harvested-rows $HARVESTED_ROWS \
        --timeout $Z3_TIMEOUT \
        --num-retries-on-timeout $Z3_NUM_RETRIES_ON_TIMEOUT \
        $Z3_ENABLE_STRATEGY \
        2>&1 | tee $OUTPUT_DIR/$LOG_FILE
done
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Cleaning unneeded script and test_config files
# ----------------------------------------------------------------------------------------------------------
#rm $OUTPUT_DIR/*.sh
# ----------------------------------------------------------------------------------------------------------
