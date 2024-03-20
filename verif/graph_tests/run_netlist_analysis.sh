#!/bin/bash
source ./scripts/regr_utils.sh


# ----------------------------------------------------------------------------------------------------------
# Parse arguments -- getopt style (enables multi-letter options)
# ----------------------------------------------------------------------------------------------------------
DIRECTORY=verif/graph_tests/netlists/test_dram_input_datacopy_multiple_tms_and_reblock_June11

# Read command-line arguments
opts=$(getopt -o d::h --long dir::,help:: -n "$(basename "$0")" -- "$@")
eval set -- $opts

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--dir)
      case "$2" in
        "") DIRECTORY=$DIRECTORY ; shift 2 ;; #default
         *) DIRECTORY=$2         ; shift 2 ;; #passed argument
      esac ;;

    -h|--help)
      echo "Usage: $0 [-d<directory> | --dir=<directory>] [s<directory> | --seed=<seed>] [-f<test_index> | --start=<test_index>]"
      echo "   <directory> - name of the directory with netlists to be analyzed. Default: $DIRECTORY."

      shift
      exit 1
      ;;

    *) #skip other arguments
      shift
      break
      ;;
  esac
done
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Check parsed arguments
# ----------------------------------------------------------------------------------------------------------
# Check if DIRECTORY exists
if [[ ! -d $DIRECTORY ]] ; then
   echo "Error: -d or --dir argument has to be a path to an existing directory. Got '$DIRECTORY' as the argument"
   exit 1
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Print parsed arguments
# ----------------------------------------------------------------------------------------------------------
echo "Arguments:"
echo "  DIRECTORY=$DIRECTORY"
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Search for netlists in the directory
# ----------------------------------------------------------------------------------------------------------
NETLISTS=(`find $DIRECTORY -type f -name "netlist_*.yaml" | sort`)
NUM_NETLISTS=${#NETLISTS[@]}

if [[ $NUM_NETLISTS -lt 1 ]]
then
echo "Error: No netlists found in $DIRECTORY directory."
exit 1
fi
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Print intro message header
# ----------------------------------------------------------------------------------------------------------
printf -- "=============================================================================================="
printf -- "=============================================================================================="
printf -- "==============================================================================================\n"
printf "Analyzing netlists from $DIRECTORY directory\n"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------\n"
printf "\n"
printf '%-46s| %-50s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-11s| %-14s\n' "netlist" "TMs" "mbtch" "ubtch" "in_cnt" "in_t" "in_rt" "in_ct" "out_t" "out_rt" "out_ct" "out_tiles" "out/in tiles"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------"
printf -- "----------------------------------------------------------------------------------------------\n"
# ----------------------------------------------------------------------------------------------------------


# ----------------------------------------------------------------------------------------------------------
# Process all logs and netlists
# ----------------------------------------------------------------------------------------------------------
TOTAL_TILES=0
for ((i=0; i<$NUM_NETLISTS; i++))
#for ((i=0; i<2; i++))
do
    # Take a netlist
    NETLIST=${NETLISTS[$i]}
    SHORT_NETLIST=`echo $NETLIST | sed -e 's/.*netlist_/netlist_/'`

    # Minibatch count
    MB_COUNT=`grep "minibatch_count:" $NETLIST | sed -e 's/ #.*//' | sed -e 's/.*: //'`
    if [[ $MB_COUNT -eq "" ]] # In case of parsing old netlists without this parameter
    then
        MB_COUNT=1
    fi

    # Microbatch count
    UB_COUNT=`grep "microbatch_count:" $NETLIST | sed -e 's/ #.*//' | sed -e 's/.*: //'`
    if [[ $UB_COUNT -eq "" ]] # In case of parsing old netlists without this parameter
    then
        UB_COUNT=1
    fi

    # Input count
    IN_COUNT=`grep "input_count:" $NETLIST | sed -e 's/ #.*//' | sed -e 's/.*: //'`

    # Input dram buffer size
    IN_DRAM=`grep "dram_input:$" -A 11 $NETLIST`
    IN_T=`printf "%s" "$IN_DRAM" | grep " t:" | sed -e 's/ #.*//' | sed -e 's/.*: //'`
    IN_MB_R=`printf "%s" "$IN_DRAM" | grep "mblock:" | sed -e 's/.*\[//' | sed -e 's/, .*//'`
    IN_MB_C=`printf "%s" "$IN_DRAM" | grep "mblock:" | sed -e 's/.*, //' | sed -e 's/\].*//'`
    IN_UB_R=`printf "%s" "$IN_DRAM" | grep "ublock:" | sed -e 's/.*\[//' | sed -e 's/, .*//'`
    IN_UB_C=`printf "%s" "$IN_DRAM" | grep "ublock:" | sed -e 's/.*, //' | sed -e 's/\].*//'`
    IN_GS_R=`printf "%s" "$IN_DRAM" | grep "grid_size:" | sed -e 's/.*\[//' | sed -e 's/, .*//'`
    IN_GS_C=`printf "%s" "$IN_DRAM" | grep "grid_size:" | sed -e 's/.*, //' | sed -e 's/\].*//'`
    IN_RT=$(($IN_UB_R*$IN_MB_R*$IN_GS_R))
    IN_CT=$(($IN_UB_C*$IN_MB_C*$IN_GS_C))

    # Output dram buffer size
    OUT_DRAM=`grep "dram_output:$" -A 11 $NETLIST`
    OUT_T=`printf "%s" "$OUT_DRAM" | grep " t:" | sed -e 's/ #.*//' | sed -e 's/.*: //'`
    OUT_MB_R=`printf "%s" "$OUT_DRAM" | grep "mblock:" | sed -e 's/.*\[//' | sed -e 's/, .*//'`
    OUT_MB_C=`printf "%s" "$OUT_DRAM" | grep "mblock:" | sed -e 's/.*, //' | sed -e 's/\].*//'`
    OUT_UB_R=`printf "%s" "$OUT_DRAM" | grep "ublock:" | sed -e 's/.*\[//' | sed -e 's/, .*//'`
    OUT_UB_C=`printf "%s" "$OUT_DRAM" | grep "ublock:" | sed -e 's/.*, //' | sed -e 's/\].*//'`
    OUT_GS_R=`printf "%s" "$OUT_DRAM" | grep "grid_size:" | sed -e 's/.*\[//' | sed -e 's/, .*//'`
    OUT_GS_C=`printf "%s" "$OUT_DRAM" | grep "grid_size:" | sed -e 's/.*, //' | sed -e 's/\].*//'`
    OUT_RT=$(($OUT_UB_R*$OUT_MB_R*$OUT_GS_R))
    OUT_CT=$(($OUT_UB_C*$OUT_MB_C*$OUT_GS_C))

#    echo "MB_COUNT=$MB_COUNT"
#    echo "UB_COUNT=$UB_COUNT"
#    echo "UB_COUNT=$IN_COUNT"
#
#    echo "IN_T=$IN_T"
#    echo "IN_MB_R=$IN_MB_R"
#    echo "IN_MB_C=$IN_MB_C"
#    echo "IN_UB_R=$IN_UB_R"
#    echo "IN_UB_C=$IN_UB_C"
#    echo "IN_GS_R=$IN_GS_R"
#    echo "IN_GS_C=$IN_GS_C"
#    echo "IN_RT=$IN_RT"
#    echo "IN_CT=$IN_CT"
#
#    echo "OUT_T=$OUT_T"
#    echo "OUT_MB_R=$OUT_MB_R"
#    echo "OUT_MB_C=$OUT_MB_C"
#    echo "OUT_UB_R=$OUT_UB_R"
#    echo "OUT_UB_C=$OUT_UB_C"
#    echo "OUT_GS_R=$OUT_GS_R"
#    echo "OUT_GS_C=$OUT_GS_C"
#    echo "OUT_RT=$OUT_RT"
#    echo "OUT_CT=$OUT_CT"

    OUT_TILES=$(($OUT_RT*$OUT_CT*$OUT_T*$IN_COUNT*$UB_COUNT*$MB_COUNT))
    IN_TILES=$(($IN_RT*$IN_CT*$IN_T*$IN_COUNT*$UB_COUNT*$MB_COUNT))

    OUT_DIV_IN_TILES=$(($OUT_TILES/$IN_TILES))

    TOTAL_TILES=$(($TOTAL_TILES + $OUT_TILES))

    # Grep TMs
    TMS=`grep _tms: $NETLIST | sed -e 's/.*_tms: //'`

    # Print a line into the table
    printf '%-46s| %-50s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-7s| %-11s| %-14s\n' "$SHORT_NETLIST" "$TMS" "$MB_COUNT" "$UB_COUNT" "$IN_COUNT" "$IN_T" "$IN_RT" "$IN_CT" "$OUT_T" "$OUT_RT" "$OUT_CT" "$OUT_TILES" "$OUT_DIV_IN_TILES"
done

printf -- "=============================================================================================="
printf -- "=============================================================================================="
printf -- "==============================================================================================\n"

printf -- "Total output tiles: $TOTAL_TILES. Estimated execution time: $(($TOTAL_TILES/500))"
# ----------------------------------------------------------------------------------------------------------
