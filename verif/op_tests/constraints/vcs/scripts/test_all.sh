git_root="$(git rev-parse --show-toplevel)"
vcs_dir=${git_root}/verif/op_tests/constraints/vcs
device=wormhole_b0

echo "Generating all int8 op tests for ARCH=${device} in VCS DIR=${vcs_dir}"
cd ${vcs_dir}

make DEVICE=${device} TARGET_FILE=configs/wormhole_b0/all_tests.nightly HARVESTED_ROWS=2 LOOPS=1 SEED=0 C=main_program.sv TLIST=int8.quant.nightly ARGS='+op=run_all'
