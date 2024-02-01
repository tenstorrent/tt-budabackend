
git_root="$(git rev-parse --show-toplevel)"
vcs_dir=${git_root}/verif/op_tests/constraints/vcs
# only supported is wormhole_b0
device=wormhole_b0

echo "Generating all int8 op tests for ARCH=${device} in VCS DIR=${vcs_dir}"
cd ${vcs_dir}

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5  SEED=0 C=binary/binary_op_constraints.sv TLIST=int8.push ARGS='+int8=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=binary/binary_op_constraints.sv TLIST=int8.nightly ARGS='+int8=1'

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5  SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.push ARGS='+int8=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.nightly ARGS='+int8=1'

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5  SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.bias.push ARGS='+bias=1 +int8=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.bias.nightly ARGS='+bias=1 +int8=1'

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=3  SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.accumulate-z.push ARGS='+accumulate_z=1 +int8=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=15  SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.accumulate-z.nightly ARGS='+accumulate_z=1 +int8=1'

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=3  SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.bias.accumulate-z.push ARGS='+bias=1 +accumulate_z=1 +int8=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=15  SEED=0 C=matmul/matmul_op_constraints.sv TLIST=int8.bias.accumulate-z.nightly ARGS='+bias=1 +accumulate_z=1 +int8=1'

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=10 SEED=0 C=splice/splice_op_constraints.sv TLIST=int8.hor.nightly ARGS='+direction=0 +int8=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=10 SEED=0 C=splice/splice_op_constraints.sv TLIST=int8.ver.nightly ARGS='+direction=1 +int8=1'

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=matmul/matmul_ident_op_constraints.sv TLIST=int8.push ARGS='+int8=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=100 SEED=0 C=matmul/matmul_ident_op_constraints.sv TLIST=int8.nightly ARGS='+int8=1'

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=reduce/reduce_op_constraints.sv TLIST=int8.dim-r.nightly ARGS='+int8=1 +reduce_dim=r'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=reduce/reduce_op_constraints.sv TLIST=int8.dim-c.nightly ARGS='+int8=1 +reduce_dim=c'
# Disabled until bug is fixed
# make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=reduce/reduce_op_constraints.sv TLIST=int8.dim-z.nightly ARGS='+int8=1 +reduce_dim=z'
cd -
