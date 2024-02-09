# Performance Model

We use performance model to generate parameters needed for op execution estimation that is used on FE.

`run_sweep.py` script is used to generate performance data for specified ops
and run performance models to obtain constants used in the op model estimation.

To run Python scripts in this folder, you need to set up Python environment.


## Setup

The following script creates Python virtualenv in perf_lib/op_model/scripts/env and installs needed dependencies.
It can be run from Budabackend root as well.
```bash
# can be run from budabackend root as well
source perf_lib/op_model/scripts/create_python_env.sh
```

You also need to build backend with loader and op tests:
```bash
make build_hw loader/tests verif/op_tests
```



## Generate performance parameters

To obtain op model estimation parameters for ops specified in the `default_operations.json` file, run the following command:

```bash
python3 ./perf_lib/op_model/scripts/run_sweep.py
```

The script has 2 phases:
- Generate Performance Data - calls perf_sweep.py script to generate and run netlists for specified ops
- Calculate Model - calls performance_model.py script to obtain parameters

The generated parameters will be dumped into `./build/test/perf_lib/op_model/<arch_name>_params.yaml` and can be copied to the
params used by op model under `perf_lib/op_model/params`.

### Useful run_sweep.py variations

```bash
# run perf tests for all ops defined in default_operations.json
python3 ./perf_lib/op_model/scripts/run_sweep.py

# run only specified type of ops
python3 ./perf_lib/op_model/scripts/run_sweep.py --op-type unary

# run only specified op
python3 ./perf_lib/op_model/scripts/run_sweep.py --op-name sigmoid

# other options
python3 ./perf_lib/op_model/scripts/run_sweep.py --skip-performance-model
python3 ./perf_lib/op_model/scripts/run_sweep.py --skip-perf-sweep
python3 ./perf_lib/op_model/scripts/run_sweep.py --continue-run # continue if previous command was interrupted
python3 ./perf_lib/op_model/scripts/run_sweep.py --force-clean # clean previous temporary data and start from scratch
python3 ./perf_lib/op_model/scripts/run_sweep.py --operations-cfg # pass different configurations file
python3 ./perf_lib/op_model/scripts/run_sweep.py --force-clean --op-name add
```


## Calculate Model

performance_model.py script is used to run linear regression on the performance data and generate parameters
used for the specified op model estimation. Parameters currently in use can be found under `perf_lib/op_model/params`.

To run performance_model.py script after generating data using perf_sweep.py you can do the following:

```bash
python3 ./perf_lib/op_model/scripts/performance_model.py --model matmul scripts/perf_data/matmul-op-performance.csv

python3 ./perf_lib/op_model/scripts/performance_model.py --model eltwise scripts/perf_data/sqrt-op-performance.csv
```


# Sparse matmul estimate evaluation


To compare sparse matmul estimates (v1 or v2) vs real runtime, run following commands:

```bash
make perf_lib/op_model/scripts/sparse_estimate_evaluation

./build/bin/sparse_estimate_evaluation perf_lib/op_model/scripts/perf_data/wormhole_b0/matmul-sparse-op-performance.csv comparison.csv [v1/v2]
```

The output comparison.csv file will contain all the parameters for each example together with estimate, real runtime of the op and their ratio.