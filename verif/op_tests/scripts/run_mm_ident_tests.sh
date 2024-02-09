#!/bin/bash
source ./scripts/regr_utils.sh

directory=
device=

if [ "$#" -eq  "0" ]
    then	  
    directory=sample-out
    device=grayskull
elif [ "$#" -eq  "1" ]
    then	
    directory=$1
    device=grayskull
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
    DIR_NAME="$(dirname "${TEST_NAME}")"
    LOG_NAME=$(basename $DIR_NAME)
    LOG_NAME="${LOG_NAME%.[^.]*}"
    TEST_PARAMS=""
    TEST_CMD="ARCH_NAME=$device ./build/test/verif/op_tests/test_op --netlist $TEST_NAME --seed 0 --silicon --timeout 200 --arch $device "
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
done
set -e
printf "\n"
printf "All tests completed $test_status"
