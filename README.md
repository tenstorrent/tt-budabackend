This repository contains the code that interacts with Tenstorrent hardware to provide lower level functionalities. 
It should be automatically built as part of buda, meanwhile you may also build and test the individual modules.


# Build

## Setting up Docker container

It is highly recommended that you build and run buda within a container. Please refer to the documentation in pybuda.

## Steps

1. Checkout the submodules.

```bash
git submodule update --init --recursive
```

2. Set up environment variables properly and run `make`. `ARCH_NAME` should be the name of the architecture of your Tenstorrent hardware, e.g. `"grayskull"`. By default, the code is built in release mode. If necessary, you may use other values of `CONFIG`, including `assert`, `debug`, etc.
A typical build command looks like:
    
```bash
export ARCH_NAME=grayskull

# Build backend
make -j16 build_hw

# Build a test runner
make -j16 verif/netlist_test
```

3. Clean up.
```bash
make clean
```


# Running tests

Here are the commands to build budabackend and run a simple test on a grayskull board:

```bash
export ARCH_NAME=grayskull
make -j16 build_hw
make -j16 verif/graph_tests
./build/test/verif/graph_tests/test_graph --netlist verif/graph_tests/netlists/netlist_softmax_single_tile.yaml --silicon
```