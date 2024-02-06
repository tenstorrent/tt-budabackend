This folder contains file exported with 'export' command.

## unpack with ./unpack.sh

## gs-x2-fd8f98aca.zip - Kyle's run from checkout fd8f98aca
To rerun:
make -j32 verif/netlist_tests/test_inference
ARCH_NAME=grayskull ./build/test/verif/netlist_tests/test_inference --netlist verif/graph_tests/netlists/netlist_encoder_x4_2gs.yaml --backend Silicon --seed 0 --num-loops 2

## simple-matmul-no-hangs.zip and simple-matmul-with-hangs.zip
A simple netlist ran twice (once with no hangs and once with recip op hanging)

## wh/gs-regression
These are used by the regression tests (look for run-offline-tests.sh and run-coverage.sh)

## bert-large-no-pkl.zip
Large netlist with many epochs. Used for benchmarking YAML read performance. It does not contain any silicon data (pkl)

## wh_multichip_dram_fork_0_to_11-passing.zip
Two chips of WH with one epoch spanning two graphs on the two devices.

