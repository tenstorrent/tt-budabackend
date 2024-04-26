
git_root="$(git rev-parse --show-toplevel)"
vcs_dir=
device=

if [ "$#" -eq  "0" ]
    then
    device=grayskull  
    vcs_dir=${git_root}/verif/op_tests/constraints/vcs
    
elif [ "$#" -eq  "1" ]
    then
    device=$1	
    vcs_dir=${git_root}/verif/op_tests/constraints/vcs
else
    device=$1
    vcs_dir=$2
fi

echo "Generating all op tests for ARCH=${device} in VCS DIR=${vcs_dir}"
cd ${vcs_dir}

make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=unary/unary_op_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=binary/binary_op_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=200 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=nightly ARGS='+bias=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=unary/unary_op_with_fd_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=binary/binary_op_with_fd_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=matmul/matmul_op_with_fd_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=matmul/matmul_gradient_acc_op_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=binary/binary_op_constraints.sv TLIST=gradient-acc.nightly ARGS='+gradient_acc=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=unary/unary_op_constraints.sv TLIST=gradient-acc.nightly ARGS='+gradient_acc=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=unary/unary_op_constraints.sv TLIST=push
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=binary/binary_op_constraints.sv TLIST=push
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=5 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=push ARGS='+bias=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=3 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=accumulate-z.push ARGS='+accumulate_z=1' 
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=3 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=min-buffer-input.push
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=25 SEED=0 C=reduce/reduce_op_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=10 SEED=0 C=splice/splice_op_constraints.sv TLIST=hor.nightly ARGS='+direction=0'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=10 SEED=0 C=splice/splice_op_constraints.sv TLIST=ver.nightly ARGS='+direction=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=100 SEED=0 C=matmul/matmul_ident_op_constraints.sv TLIST=nightly
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=accumulate-z.nightly ARGS='+accumulate_z=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=20 SEED=0 C=matmul/matmul_op_constraints.sv TLIST=min-buffer-input.nightly
# make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 SEED=0 C=topk/topk_op_constraints.sv TLIST=nightly
cd -
