git_root="$(git rev-parse --show-toplevel)"
vcs_dir=${git_root}/verif/op_tests/constraints/vcs
device=blackhole_emulator

echo "Generating blackhole emulator op tests for in VCS DIR=${vcs_dir}"
cd ${vcs_dir}

make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/unary/unary_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=unary +ops_config=single'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/unary/unary_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=unary +ops_config=drainer'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/binary/binary_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=binary +ops_config=single'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/binary/binary_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=binary +ops_config=drainer'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/binary_sfpu/binary_sfpu_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=binary_sfpu +ops_config=single +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/binary_sfpu/binary_sfpu_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=binary_sfpu +ops_config=drainer +int8=1'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/matmul/matmul_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=matmul +ops_config=single'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/matmul/matmul_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=matmul +ops_config=drainer'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/matmul_ident/matmul_ident_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=matmul_ident +ops_config=single'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/matmul_ident/matmul_ident_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=matmul_ident +ops_config=drainer'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/depthwise/depthwise_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=depthwise +ops_config=single'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/depthwise/depthwise_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=depthwise +ops_config=drainer'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/reduce/reduce_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=reduce +ops_config=single'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/reduce/reduce_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=reduce +ops_config=drainer'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/splice/splice_single_configs HARVESTED_ROWS=2 LOOPS=12 SEED=0 C=main.sv ARGS='+op=splice +ops_config=single'
make DEVICE=${device} TARGET_FILE=test_configs/blackhole/emulator/splice/splice_drainer_configs HARVESTED_ROWS=2 LOOPS=6 SEED=0 C=main.sv ARGS='+op=splice +ops_config=drainer'