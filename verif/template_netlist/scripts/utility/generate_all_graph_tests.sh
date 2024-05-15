# Necessary env variables: SEED, NUM_CONFIGS
FOLDER_NAME="./configs_$(date +"%Y_%m_%d_%I_%M_%p")"
mkdir $FOLDER_NAME
TEST_CMD=""
#
SECONDS=0
TEST_NAME=add_and_norm_train
ARCH=grayskull
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=add_and_norm_train
ARCH=wormhole
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=add_and_norm
ARCH=grayskull
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=add_and_norm
ARCH=wormhole
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=feedforward_train
ARCH=grayskull
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=feedforward_train
ARCH=wormhole
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=feedforward
ARCH=grayskull
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=feedforward
ARCH=wormhole
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=mha
ARCH=grayskull
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=mha
ARCH=wormhole
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=simple_train
ARCH=grayskull
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=simple_train
ARCH=wormhole
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=softmax
ARCH=grayskull
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#
SECONDS=0
TEST_NAME=softmax
ARCH=wormhole
echo "Generating $TEST_NAME $ARCH seed=$SEED" | tee $FOLDER_NAME/$TEST_NAME_$ARCH.log
python generate_tests.py --module-name test_modules.graph_tests.test_$TEST_NAME --output-dir $FOLDER_NAME/$TEST_NAME/$ARCH --max-num-configs $NUM_CONFIGS --dry-run --dump-config-yaml --random-seed $SEED --arch $ARCH --verbose --use-parallel-sweep >> $FOLDER_NAME/$TEST_NAME_$ARCH.log
echo "Time elapsed for $TEST_NAME $ARCH: $SECONDS" | tee -a $FOLDER_NAME/$TEST_NAME_$ARCH.log
#