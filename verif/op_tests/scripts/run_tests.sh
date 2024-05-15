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

echo "Write to output-dir $directory" 
echo "Device set to $device" 

ARCH_NAME=$device make verif/op_tests

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
    if (([[ $TEST_NAME == *"wormhole"* ]] && [[ $device != "wormhole"* ]]) ||
        ([[ $TEST_NAME == *"grayskull"* ]] && [[ $device != "grayskull" ]])); then        
        echo "Skipping test $TEST_NAME ..."
    else 
        LOG_NAME="${TEST_NAME##*/}"
        LOG_NAME="${LOG_NAME%.[^.]*}"
        TEST_PARAMS=""
        TEST_CMD="ARCH_NAME=$device ./build/test/verif/op_tests/test_op --netlist $TEST_NAME --seed 0 --silicon --timeout 200 --arch $device"
        echo "Running $TEST_CMD"
        bash -c "ARCH_NAME=$device ./build/test/verif/op_tests/test_op --netlist $TEST_NAME --seed 0 --silicon --timeout 200 --arch $device" >  $directory/logs/$LOG_NAME.log
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
    fi    
done
set -e
printf "\n"
printf "All tests completed $test_status"
