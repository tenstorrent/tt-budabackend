#!/bin/bash
source ./scripts/regr_utils.sh

make verif/directed_tests
VERIF_DIRECTED_TESTS=(
    "./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder --num-loops 2"   
    #"./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder_train.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder_train --num-loops 2"
    "./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder_multi_core.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder --num-loops 2"
    #"./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder_train_multi_core.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder_train --num-loops 2"
    "./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder --skip-pybuda-api --num-loops 2"   
    #"./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder_train.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder_train --skip-pybuda-api --num-loops 2"
    "./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder_multi_core.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder --skip-pybuda-api --num-loops 2"
    #"./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_encoder_train_multi_core.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/bert_encoder_train --skip-pybuda-api --num-loops 2"

    "./build/test/verif/directed_tests/test_golden_to_model --netlist verif/directed_tests/netlist_unary_pipes.yaml "
    "./build/test/verif/directed_tests/test_golden_to_model --netlist verif/directed_tests/netlist_single_matmul.yaml "   
    "./build/test/verif/directed_tests/test_golden_to_model --arch wormhole --netlist verif/directed_tests/netlist_unary_pipes.yaml "
    "./build/test/verif/directed_tests/test_golden_to_model --arch wormhole --netlist verif/directed_tests/netlist_single_matmul.yaml"   
)

NUM_TESTS=${#VERIF_DIRECTED_TESTS[@]}

result=0
status=$PASS
for ((i=0; i<$NUM_TESTS; i++))
do
    TEST_NAME=${VERIF_DIRECTED_TESTS[$i]}
    echo "Running $TEST_NAME"
    ./$TEST_NAME
    RETCODE=$?
    if [ $RETCODE -eq 0 ]; then
        printf "  $PASS"
    else
        printf "  $FAIL"
        status=$FAIL
        result=1
    fi
done
printf "\n"
printf "All tests completed $status"
