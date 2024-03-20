#!/bin/bash
source ./scripts/regr_utils.sh
ARCH_NAME="${ARCH_NAME:-grayskull}"
backend="${backend:-Golden}"
echo "Building ARCH_NAME=$ARCH_NAME make verif/netlist_tests"
ARCH_NAME=$ARCH_NAME make verif/netlist_tests
VERIF_GOLDEN_GENERAL_TESTS=(
    # Sanity
    "build/test/verif/netlist_tests/test_unary_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_unary_pybuda_api --backend $backend --netlist verif/netlist_tests/netlists/basic/netlist_unary_row_vector.yaml --num-loops 3"
    "build/test/verif/netlist_tests/test_unary_pybuda_api --backend $backend --netlist verif/netlist_tests/netlists/basic/netlist_unary_col_vector.yaml --num-loops 3"
    "build/test/verif/netlist_tests/test_unary_with_tm_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_unary_with_repeat_broadcast_tm_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_unary_single_tile_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_nary_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_nary_t_concat_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_binary_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_binary_single_tile_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_matmul_pybuda_api --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_matmul_with_bias_pybuda_api --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_reduce_pybuda_api --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_fused_op_pybuda_api --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_fused_op_pybuda_api --backend $backend --netlist verif/netlist_tests/netlists/basic/netlist_fused_op_with_tm.yaml --num-loops 3" 
    "build/test/verif/netlist_tests/test_fused_op_pybuda_api --backend $backend --netlist verif/netlist_tests/netlists/basic/netlist_fused_op_vector_mode.yaml --num-loops 3" 
    "build/test/verif/netlist_tests/test_batched_pybuda_api --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_back_to_back_tests_pybuda_api --backend $backend --num-loops 3"

    # Rams    
    "build/test/verif/netlist_tests/test_ram --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_parameter_queue --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_accumulation --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_gradient_accumulation --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_dual_view_ram --backend $backend --num-loops 3"

    # Programs   
    "build/test/verif/netlist_tests/test_static_var --backend $backend --num-loops 3"  
    "build/test/verif/netlist_tests/test_runtime_params --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_program_loops --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_program_with_multiple_graphs --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_host_loops_with_multiple_programs --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_program_with_multiple_independent_graphs --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_host_loops_with_multiple_independent_programs --backend $backend --num-loops 3" 
    "build/test/verif/netlist_tests/test_out_of_order_program_execution --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_sanity_training_program --backend $backend --num-loops 3"
    
    # Queues 
    "build/test/verif/netlist_tests/test_queue --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_constant_queue --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_queue --netlist verif/netlist_tests/netlists/basic_queue/netlist_queue_allocation.yaml --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_relay_queue --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_relay_queue --backend $backend --netlist verif/netlist_tests/netlists/basic_queue/netlist_relay_queue_multi_input_counts.yaml --num-loops 3"
    "build/test/verif/netlist_tests/test_relay_queue --backend $backend --netlist verif/netlist_tests/netlists/basic_queue/netlist_relay_queue_multi_input_counts_dynamic_queue.yaml --num-loops 3"

    # Graphs
    "build/test/verif/netlist_tests/test_input_count --backend $backend --num-loops 3"
)

VERIF_GOLDEN_WORMHOLE_SPECIFIC_TESTS=(
    # Graphs
    "build/test/verif/netlist_tests/test_temporal_graph --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_temporal_graph --netlist verif/netlist_tests/netlists/basic_graph/netlist_temporal_graph_multiple_queues.yaml --backend $backend --num-loops 3"
    "build/test/verif/netlist_tests/test_multiple_independent_devices --backend $backend --num-loops 3"

    # Sanity
    "build/test/verif/netlist_tests/test_fused_op_pybuda_api --backend $backend  --netlist verif/netlist_tests/netlists/basic/netlist_fused_op_dest_to_srcb.yaml --num-loops 3" 
)

if [ $ARCH_NAME == "grayskull" ]; then
    VERIF_GOLDEN_TESTS=("${VERIF_GOLDEN_GENERAL_TESTS[@]}")
else 
    VERIF_GOLDEN_TESTS=("${VERIF_GOLDEN_GENERAL_TESTS[@]}" "${VERIF_GOLDEN_WORMHOLE_SPECIFIC_TESTS[@]}")
fi
NUM_TESTS=${#VERIF_GOLDEN_TESTS[@]}

result=0
status=$PASS
for ((i=0; i<$NUM_TESTS; i++))
do
    TEST_CMD=${VERIF_GOLDEN_TESTS[$i]}
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
