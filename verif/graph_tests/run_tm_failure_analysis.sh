#!/bin/bash
source ./scripts/regr_utils.sh


# ----------------------------------------------------------------------------------------------------------
# Parse arguments -- getopt style (enables multi-letter options)
# ----------------------------------------------------------------------------------------------------------
LOG_DIRECTORY=verif/graph_tests/netlists/test_dram_input_datacopy_multiple_tms_and_reblock_June11/logs

# Read command-line arguments
opts=$(getopt -o l::n::h --long log-dir::,netlist-dir::,help:: -n "$(basename "$0")" -- "$@")
eval set -- $opts

while [[ $# -gt 0 ]]; do
  case "$1" in
    -l|--log-dir)
      case "$2" in
        "") LOG_DIRECTORY=$LOG_DIRECTORY ; shift 2 ;; #default
         *) LOG_DIRECTORY=$2             ; shift 2 ;; #passed argument
      esac ;;

    -n|--netlist-dir)
      case "$2" in
        "") NETLIST_DIRECTORY=$LOG_DIRECTORY/.. ; shift 2 ;; #default is the parent of log directory
         *) NETLIST_DIRECTORY=$2                ; shift 2 ;; #passed argument
      esac ;;

    -h|--help)
      echo "Usage: $0 [-l<log directory> | --log-dir=<log directory>] [-n<netlist directory> | --netlist-dir=<netlist directory>]"
      echo "   <log directory> - name of the directory with execution logs to be analyzed. Default: $LOG_DIRECTORY."
      echo "   <netlist directory> - name of the directory with netlists related to execution logs to be analyzed. Default: $NETLIST_DIRECTORY."

      shift
      exit 1
      ;;

    *) #skip other arguments
      shift
      break
      ;;
  esac
done

# if not specified netlist directory is always a parent of log directory
if [[ $NETLIST_DIRECTORY -eq "" ]] ; then
    NETLIST_DIRECTORY=$LOG_DIRECTORY/..
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Check parsed arguments
# ----------------------------------------------------------------------------------------------------------
# Check if LOG_DIRECTORY exists
if [[ ! -d $LOG_DIRECTORY ]] ; then
   echo "Error: -l or --log-dir argument has to be a path to an existing directory execution logs. Got '$LOG_DIRECTORY' as the argument"
   exit 1
fi

# Check if NETLIST_DIRECTORY exists
if [[ ! -d $NETLIST_DIRECTORY ]] ; then
   echo "Error: -n or --netlist-dir argument has to be a path to an existing directory with netlists. Got '$NETLIST_DIRECTORY' as the argument"
   exit 1
fi

# Check if summary.log exists
if [[ ! -f $LOG_DIRECTORY/summary.log ]] ; then
   echo "Error: -l or --log-dir argument has to be a path to a directory that contains summary.log and multiple netlist_*.log files. Got '$LOG_DIRECTORY' as the argument"
   exit 1
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Print parsed arguments
# ----------------------------------------------------------------------------------------------------------
#echo "Arguments:"
#echo "  LOG_DIRECTORY=$LOG_DIRECTORY"
#echo "  NETLIST_DIRECTORY=$NETLIST_DIRECTORY"
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Grep for failed, passed, and total tests in log files
# ----------------------------------------------------------------------------------------------------------
FAILED_GUIDS=(`grep "FAILED" $LOG_DIRECTORY/summary.log | sed -e 's/\.yaml.*//' | sed -e 's/.*netlist_//'`)
NUM_FAILURES=${#FAILED_GUIDS[@]}

# Grep for passed tests in log files
PASSED_GUIDS=(`grep "PASSED" $LOG_DIRECTORY/summary.log | sed -e 's/\.yaml.*//' | sed -e 's/.*netlist_//'`)
NUM_PASSES=${#PASSED_GUIDS[@]}

# Grep for passed tests in log files
TOTAL_TESTS=(`cat $LOG_DIRECTORY/summary.log | wc -l`)
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Print intro message header
# ----------------------------------------------------------------------------------------------------------
printf -- "\n"
printf -- "=============================================================================================="
printf -- "=============================================================================================="
printf -- "==============================================================================================\n"
printf -- "Analyzing failed tests from:\n"
printf -- "  LOG_DIRECTORY     = $LOG_DIRECTORY\n"
printf -- "  NETLIST_DIRECTORY = $NETLIST_DIRECTORY\n"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------\n"
printf -- '%-60s| %-80s| %-30s| %-s\n' "Netlist" "TMs" "Error" "Error details"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------\n"
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Process all logs and netlists
# ----------------------------------------------------------------------------------------------------------
for ((i=0; i<$NUM_FAILURES; i++))
do
    # Construct netlist and log file path
    NETLIST=$NETLIST_DIRECTORY/test_${FAILED_GUIDS[$i]}/netlist_${FAILED_GUIDS[$i]}.yaml
    LOG=$LOG_DIRECTORY/netlist_${FAILED_GUIDS[$i]}.log

    # Check if netlist file exists
    if [[ ! -f $NETLIST ]] ; then
       printf "Warning: Netlist $NETLIST not found. Skipping...\n"

    else
       # Grep TMs from failed netlist
       TMS=`grep _tms: $NETLIST | grep -m 1 -v "#" | sed -e 's/.*_tms: //'`

       # Errors
       ERROR="Unknown"
       ERROR_DETAILS="None"

       # Grep generic error
       GENERIC_ERROR=`grep -m 1 "ERROR" $LOG | sed -e 's/.*ERROR: //'`
       if [[ $GENERIC_ERROR != "" ]]; then
           ERROR="Generic error"
           ERROR_DETAILS=$GENERIC_ERROR
       fi

       # FATAL errors
       FATAL_ERROR=`grep 'FATAL' $LOG | sed -e 's/.*FATAL  .* Running //' | sed -e 's/command failed.*/command failed/'`
       if [[ $FATAL_ERROR != "" ]]; then
           ERROR="FATAL"
           ERROR_DETAILS=$FATAL_ERROR
       fi

       # Grep data mismatch errors
       MISMATCH_ERROR=`grep -m 1 "Fatal.*Test Failed" $LOG`
       if [[ $MISMATCH_ERROR != "" ]]; then
           ERROR="Output data mismatch"
           ERROR_DETAILS=""
       fi

       # Grep net2pipe TM errors
       TM_ERROR=`grep -m 1 "ERROR: TM ERROR" $LOG | sed -e 's/.*TM ERROR/TM ERROR/'`
       if [[ $TM_ERROR != "" ]]; then
           ERROR="Net2pipe assert"
           ERROR_DETAILS=$TM_ERROR
       fi

       # Grep net2pipe errors
       N2P_ERROR=`grep -m 1 "NET2PIPE-ERROR" $LOG`
       if [[ $N2P_ERROR != "" ]]; then
           ERROR="Net2pipe assert"
           ERROR_DETAILS=$N2P_ERROR
       fi

       # Pipegen errors
       PG_ERROR=`grep -m 1 "<PIPEGEN-ERROR>" $LOG`
       if [[ $PG_ERROR != "" ]]; then
           ERROR="Pipegen error"
           ERROR_DETAILS=$PG_ERROR
       fi

       # Golden errors
       GOLDEN_ERROR=`grep -m 1 "Error" $LOG | grep "Golden" | sed -e 's/.*| //'`
       if [[ $GOLDEN_ERROR != "" ]]; then
           ERROR="Golden error"
           ERROR_DETAILS=$GOLDEN_ERROR
       fi

       # Ruby errors
       RUBY_ERROR=`grep Error $LOG | grep -m1 ruby | sed -e 's/.*Error //'`
       if [[ $RUBY_ERROR != "" ]]; then
           ERROR="Ruby error"
           ERROR_DETAILS=$RUBY_ERROR
       fi

       # Forking ruby errors
       RUBY_ERROR=`grep -m1 "Error! " $LOG | sed -e 's/.*Error! //'`
       if [[ $RUBY_ERROR != "" ]]; then
           ERROR="Ruby error"
           ERROR_DETAILS=$RUBY_ERROR
       fi

       # Segmentation faults
       SEG_FAULT_ERROR=`grep -A1 'Segmentation fault' $LOG | grep FATAL | sed -e 's/.*FATAL  .* Running //' | sed -e 's/command failed.*/command failed/'`
       if [[ $SEG_FAULT_ERROR != "" ]]; then
           ERROR="Segmentation fault"
           ERROR_DETAILS=$SEG_FAULT_ERROR
       fi

       # PCC missmatch errors
       PCC_ERROR=`grep "ERROR" $LOG | grep -m1 "allclose" | sed -e 's/.*allclose/allclose/'`
       if [[ $PCC_ERROR != "" ]]; then
           ERROR="PCC mismatch error"
           ERROR_DETAILS=$PCC_ERROR
       fi

       # Grep Timeout errors
       TO_ERROR=`grep -m 1 "TIMEOUT ERROR" $LOG`
       if [[ $TO_ERROR != "" ]]; then
           ERROR="Test timeout"
           ERROR_DETAILS=""
       fi


       printf '%-60s| %-80s| %-30s| %-s\n' "netlist_${FAILED_GUIDS[$i]}.yaml" "$TMS" "$ERROR" "$ERROR_DETAILS"
    fi
done

printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------\n"

printf "\n"
printf "Summary:\n"
printf -- "  %4d out of %d tests $PASS" "$NUM_PASSES" "$TOTAL_TESTS"
printf -- "  %4d out of %d tests $FAIL" "$NUM_FAILURES" "$TOTAL_TESTS"

printf -- "=============================================================================================="
printf -- "=============================================================================================="
printf -- "==============================================================================================\n"
# ----------------------------------------------------------------------------------------------------------
