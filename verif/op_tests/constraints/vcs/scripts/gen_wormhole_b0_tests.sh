git_root="$(git rev-parse --show-toplevel)"
vcs_dir=${git_root}/verif/op_tests/constraints/vcs
device=wormhole_b0

echo "Generating wormhole_b0 op tests for in VCS DIR=${vcs_dir}"
cd ${vcs_dir}

make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/unary/unary_single_configs HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=main.sv ARGS='+op=unary'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/unary/unary_gradient_acc_single_configs HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=main.sv ARGS='+op=unary +gradient_acc=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/unary/unary_feeder_drainer_configs HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=main.sv ARGS='+op=unary +ops_config=feeder_drainer'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/binary/binary_single_configs HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=main.sv ARGS='+op=binary'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/binary/binary_gradient_acc_single_configs HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=main.sv ARGS='+op=binary +gradient_acc=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/binary/binary_feeder_drainer_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=binary +ops_config=feeder_drainer'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/binary_sfpu/binary_sfpu_single_configs HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=main.sv ARGS='+op=binary_sfpu'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul/matmul_single_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=matmul'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul/matmul_gradient_acc_single_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=matmul +gradient_acc=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul/matmul_accumulate_z_single_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=matmul +accumulate_z=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul/matmul_feeder_drainer_configs HARVESTED_ROWS=2 LOOPS=10 SEED=0 C=main.sv ARGS='+op=matmul +ops_config=feeder_drainer'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul_ident/matmul_ident_single_configs HARVESTED_ROWS=2 LOOPS=100 SEED=0 C=main.sv ARGS='+op=matmul_ident'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/depthwise/depthwise_single_configs HARVESTED_ROWS=2 LOOPS=25 SEED=0 C=main.sv ARGS='+op=depthwise'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/reduce/reduce_single_configs HARVESTED_ROWS=2 LOOPS=25 SEED=0 C=main.sv ARGS='+op=reduce'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/splice/splice_single_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=splice'