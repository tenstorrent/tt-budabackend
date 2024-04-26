
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1.weekly ARGS='+define+NUM_FORKS=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2.weekly ARGS='+define+NUM_FORKS=2'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4.weekly ARGS='+define+NUM_FORKS=4'

make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-transpose.weekly ARGS='+define+NUM_FORKS=1 +transpose=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-transpose.weekly ARGS='+define+NUM_FORKS=2 +transpose=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-transpose.weekly ARGS='+define+NUM_FORKS=4 +transpose=1'

make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-hslice.weekly ARGS='+define+NUM_FORKS=1 +hslice=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-hslice.weekly ARGS='+define+NUM_FORKS=2 +hslice=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-hslice.weekly ARGS='+define+NUM_FORKS=4 +hslice=1'

make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-hstack.weekly ARGS='+define+NUM_FORKS=1 +hstack=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-hstack.weekly ARGS='+define+NUM_FORKS=2 +hstack=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-hstack.weekly ARGS='+define+NUM_FORKS=4 +hstack=1'

make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-vslice.weekly ARGS='+define+NUM_FORKS=1 +vslice=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-vslice.weekly ARGS='+define+NUM_FORKS=2 +vslice=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-vslice.weekly ARGS='+define+NUM_FORKS=4 +vslice=1'

make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-vstack.weekly ARGS='+define+NUM_FORKS=1 +vstack=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-vstack.weekly ARGS='+define+NUM_FORKS=2 +vstack=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-vstack.weekly ARGS='+define+NUM_FORKS=4 +vstack=1'

make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork1-bcast.weekly ARGS='+define+NUM_FORKS=1 +bcast=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork2-bcast.weekly ARGS='+define+NUM_FORKS=2 +bcast=1'
make LOOPS=20 C=tm/test_datacopy_fork_reblock_datacopy_constraints.sv TLIST=fork4-bcast.weekly ARGS='+define+NUM_FORKS=4 +bcast=1'
