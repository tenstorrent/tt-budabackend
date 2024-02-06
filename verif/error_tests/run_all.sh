#!/bin/bash
source ./scripts/regr_utils.sh
ARCH_NAME="${ARCH_NAME:-grayskull}"
echo "Building ARCH_NAME=$ARCH_NAME make verif/error_tests"
ARCH_NAME=$ARCH_NAME make verif/error_tests
VERIF_ERROR_TESTS=(
    # default
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/default_invalid_netlist.yaml > ./build/test/verif/error_tests/output.txt"

    # defects_fused_op
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/defects/fused_op/netlist_fused_op_with_matmul_ublock_bcast_issue_1135.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/defects/fused_op/netlist_fused_op_with_rc_broadcast_issue_1134.yaml >> ./build/test/verif/error_tests/output.txt"

    # invalid
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/netlist_ublock_cannot_fit_into_dest.yaml >> ./build/test/verif/error_tests/output.txt"

    # invalid_fused_op
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_ublock_cannot_fit_into_dest.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_bcast_invalid_input.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_bcast_invalid_mblock_c.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_bcast_invalid_mblock_r.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_matmul_invalid_output.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_matmul_intermed_input_1.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_invalid_dimensions.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_matmul_dest_input.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_with_dest_input_1_gs.yaml  >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_invalid_intermed_buffer_usage.yaml >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_missing_pop_on_intermed.yaml  >> ./build/test/verif/error_tests/output.txt"
    "./build/test/verif/error_tests/test_errors --netlist verif/error_tests/netlists/invalid/fused_op/netlist_fused_op_matmul_invalid_m_k_u_kt.yaml >> ./build/test/verif/error_tests/output.txt"
)

NUM_TESTS=${#VERIF_ERROR_TESTS[@]}

result=0
status=$PASS
for ((i=0; i<$NUM_TESTS; i++))
do
    TEST_CMD=${VERIF_ERROR_TESTS[$i]}
    echo "Running ARCH_NAME=$ARCH_NAME ./$TEST_CMD"
    ARCH_NAME=$ARCH_NAME bash -c "$TEST_CMD"
    RETCODE=$?
    if [ $RETCODE -eq 0 ]; then
        printf "  $PASS"
    else
        printf "  $FAIL"
        status=$FAIL
        result=3
    fi
done
printf "\n"
printf "All tests completed $status"
#exit $result
