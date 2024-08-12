git_root="$(git rev-parse --show-toplevel)"
vcs_dir=${git_root}/verif/op_tests/constraints/vcs
device=wormhole_b0

echo "Generating all int8 op tests for ARCH=${device} in VCS DIR=${vcs_dir}"
cd ${vcs_dir}

make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/unary/unary_single_int8_configs HARVESTED_ROWS=2 LOOPS=30 SEED=0 C=main.sv ARGS='+op=unary +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/binary/binary_single_int8_configs HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=main.sv ARGS='+op=binary +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/binary/binary_sfpu_single_int8_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=binary_sfpu +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/reduce/reduce_dim_r_single_int8_configs HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=main.sv ARGS='+op=reduce +reduce_dim=r +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/reduce/reduce_dim_c_single_int8_configs HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=main.sv ARGS='+op=reduce +reduce_dim=c +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/reduce/reduce_dim_z_single_int8_configs HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=main.sv ARGS='+op=reduce +reduce_dim=z +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/splice/splice_single_int8_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=splice +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/depthwise/depthwise_single_int8_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=depthwise +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul_ident/matmul_ident_single_int8_configs HARVESTED_ROWS=2 LOOPS=100 SEED=0 C=main.sv ARGS='+op=matmul_ident +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul/matmul_acc_z_single_int8_configs HARVESTED_ROWS=2 LOOPS=30 SEED=0 C=main.sv ARGS='+op=matmul +accumulate_z=1 +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul/matmul_single_int8_configs HARVESTED_ROWS=2 LOOPS=100 SEED=0 C=main.sv ARGS='+op=matmul +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/wormhole_b0/matmul/matmul_single_int8_quant_configs HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=main.sv ARGS='+op=matmul +int8=1 +quantize=1'