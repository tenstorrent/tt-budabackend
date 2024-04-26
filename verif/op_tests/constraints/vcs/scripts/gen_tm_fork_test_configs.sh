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


make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1.nightly ARGS='+define+NUM_FORKS=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2.nightly ARGS='+define+NUM_FORKS=2'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4.nightly ARGS='+define+NUM_FORKS=4'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-transpose.nightly ARGS='+define+NUM_FORKS=1 +transpose=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-transpose.nightly ARGS='+define+NUM_FORKS=2 +transpose=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-transpose.nightly ARGS='+define+NUM_FORKS=4 +transpose=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-hslice.nightly ARGS='+define+NUM_FORKS=1 +hslice=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-hslice.nightly ARGS='+define+NUM_FORKS=2 +hslice=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-hslice.nightly ARGS='+define+NUM_FORKS=4 +hslice=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-hstack.nightly ARGS='+define+NUM_FORKS=1 +hstack=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-hstack.nightly ARGS='+define+NUM_FORKS=2 +hstack=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-hstack.nightly ARGS='+define+NUM_FORKS=4 +hstack=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-vslice.nightly ARGS='+define+NUM_FORKS=1 +vslice=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-vslice.nightly ARGS='+define+NUM_FORKS=2 +vslice=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-vslice.nightly ARGS='+define+NUM_FORKS=4 +vslice=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-vstack.nightly ARGS='+define+NUM_FORKS=1 +vstack=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-vstack.nightly ARGS='+define+NUM_FORKS=2 +vstack=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-vstack.nightly ARGS='+define+NUM_FORKS=4 +vstack=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-bcast.nightly ARGS='+define+NUM_FORKS=1 +bcast=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-bcast.nightly ARGS='+define+NUM_FORKS=2 +bcast=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-bcast.nightly ARGS='+define+NUM_FORKS=4 +bcast=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-rand.nightly ARGS='+define+NUM_FORKS=2 +rand=1'
make DEVICE=${device} HARVESTED_ROWS=2 LOOPS=50 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-rand.nightly ARGS='+define+NUM_FORKS=4 +rand=1'
