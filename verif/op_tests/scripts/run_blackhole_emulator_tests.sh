#!/bin/bash
source ./scripts/regr_utils.sh

directory=
device=$ARCH_NAME

if [ -z "$device" ]; then
    device=grayskull
fi

if [ "$#" -eq  "0" ]
    then	  
    directory=sample-out
elif [ "$#" -eq  "1" ]
    then	
    directory=$1
else
    directory=$1
    device=$2
fi

device=blackhole

echo "Write to output-dir $directory" 
echo "Device set to $device" 

ARCH_NAME=$device make -j build; make -j verif/op_tests

NETLISTS=(`find $directory -type f -name "*.yaml"`)
rm -fdr $directory/logs
mkdir $directory/logs

NUM_NETLISTS=${#NETLISTS[@]}

result=0
test_status=$PASS

set +e
for ((i=0; i<$NUM_NETLISTS; i++))
do
    TEST_NAME=${NETLISTS[$i]}
    LOG_NAME="$(basename "$(dirname "$(dirname "$(dirname "$TEST_NAME")")")")-$(basename "$(dirname "$(dirname "$TEST_NAME")")")-$(basename "$TEST_NAME" .yaml)"
    if [ -f "$directory/logs/$LOG_NAME" ]; then
        echo "Skipping test $TEST_NAME as corresponding log file exists..."
        continue  # Skip to the next iteration of the loop
    fi
    if (([[ $TEST_NAME == *"wormhole"* ]] && [[ $device != "wormhole"* ]]) ||
        ([[ $TEST_NAME == *"grayskull"* ]] && [[ $device != "grayskull" ]])); then        
        echo "Skipping test $TEST_NAME ..."
    else 
        LOG_NAME="${TEST_NAME##*/}"
        LOG_NAME="${LOG_NAME%.[^.]*}"
        TEST_PARAMS=""
        TEST_CMD="./build/test/verif/op_tests/test_op --netlist $TEST_NAME --seed 0 --emulation --device-desc ./device/emulation_1x2_arch.yaml 2>&1"
        echo "Running $TEST_CMD"
        bash -c "./build/test/verif/op_tests/test_op --netlist $TEST_NAME --seed 0 --emulation --device-desc ./device/emulation_1x2_arch.yaml 2>&1" >  $directory/logs/$LOG_NAME.log
        RETCODE=$?
        if [ $RETCODE -eq 0 ]; then
            printf "  $PASS"
        else
            printf "  $FAIL"
            #./device/bin/silicon/reset.sh
	        wait
            if [ $device = "wormhole" ]; then
                ./device/bin/silicon/wormhole/boot
	        fi       
            test_status=$FAIL
            result=1
        fi

        OP_NAME=$(grep -oP 'op: \{type: \K[^,]+' "$TEST_NAME")
        if grep -q 'identity: true' "$TEST_NAME"; then
            OP_NAME="sparse matmul"
        fi
        OP_GRID_SIZE=$(grep -oP 'op: \{type: '"$OP_NAME"', grid_size: \[\K[^\]]+' "$TEST_NAME" | tr -d '[:space:]')
        if $OP_NAME -eq "sparse matmul"; then
            OP_GRID_SIZE=$(grep -oP 'op: \{type: matmul, grid_size: \[\K[^\]]+' "$TEST_NAME" | tr -d '[:space:]')
        fi   
        DRAINER_NAME=$(grep -oP 'drainer: \{type: \K[^,]+' "$TEST_NAME")
        DRAINER_GRID_SIZE=$(grep -oP 'drainer: \{type: '"$DRAINER_NAME"', grid_size: \[\K[^\]]+' "$TEST_NAME" | tr -d '[:space:]')

        QUEUE_NAMES=$(grep -oP '\w+:\s*\{type: queue' "$TEST_NAME" | cut -d ':' -f 1)
        DATA_FORMATS=""
        for QUEUE_NAME in $QUEUE_NAMES; do
            QUEUE_DF=$(grep -oP "${QUEUE_NAME}:\s*\{type: queue.*?df:\s*\K\S+" "$TEST_NAME" | tr -d '[:space:]}')
            DATA_FORMATS+="$QUEUE_NAME,$QUEUE_DF"
        done


        RET="FAILED"
        if [ $RETCODE -eq 0 ]; then
            RET="PASSED"
        fi

        # Combine ops and drainers
        combined="$OP_NAME,[$OP_GRID_SIZE]"
        if [ -n "$DRAINER_NAME" ]; then
            combined+=",$DRAINER_NAME,[$DRAINER_GRID_SIZE]"
        fi
        current_date_time=$(date)
        echo "Current date and time: $current_date_time"
        echo "$TEST_NAME,$DATA_FORMATS$combined,$RET" >> blackhole_results.csv
    fi    
done
set -e
printf "\n"
printf "All tests completed $test_status"
