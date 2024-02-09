#!/bin/bash

device=

if [ "$#" -eq  "0" ]
    then	  
    device=wormhole_b0
else
    device=$1
fi

echo "Device set to $device" 

rm -fdr build/test/logs/determinism
mkdir -p build/test/logs/determinism

TEST_CMDS=("--netlist verif/op_tests/netlists/netlist_stream_ptr_wrap.yaml --silicon --num-loops 1000 --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_large_grid.yaml --silicon --num-loops 3000 --determinism-tests --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_transpose.yaml --silicon --num-loops 3000 --determinism-tests --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_acc_reconfig_df_wh.yaml --silicon --num-loops 3000 --determinism-tests --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_l1_acc.yaml --silicon --num-loops 3000 --determinism-tests --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_z_accumulate.yaml --silicon --num-loops 2000 --determinism-tests --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_min_buffer_input.yaml --silicon --num-loops 2000 --determinism-tests --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_with_bias.yaml --silicon --num-loops 2000 --determinism-tests --seed 0 --arch $device" "--netlist verif/op_tests/netlists/netlist_matmul_gradient_op.yaml --silicon --num-loops 2000 --determinism-tests --seed 0 --arch $device")

result=0
test_status=$PASS

NUM_TEST_CMDS=${#TEST_CMDS[@]}

set +e
for ((i=0; i<$NUM_TEST_CMDS; i++))
do
    TEST_CMD=${TEST_CMDS[$i]}
    echo "Running ARCH_NAME=$device ./build/test/verif/op_tests/test_op $TEST_CMD"
    bash -c "ARCH_NAME=$device ./build/test/verif/op_tests/test_op $TEST_CMD" > build/test/logs/determinism/test_$i.log
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
