# Perf Library

This document outlines various performance flows / features available in the Budabackend.  This is mostly infrastructure for either test runtime, post-processing, test generation, or performance analysis.

# Enabling Performance Trace

Runtime Performance Infrastructure includes the process of collecting information from each thread in hardware (Unpacker-Math-Packer-Ncrisc) during runtime, and a post-process for interpreting the data that has been collected. The output of the post-processor can be visualized using the perf-gui.

A quick summary of how to enable performance trace is provided in this section. More detailed description of each config is included in the following sections.

To enable the performance trace, following command-line arguments can be used:

- **`--dump-perf-events`**:
  - Enables the performance monitoring. Each thread has **640**B in perf-level 0 and **4**KB in perf-level 1 and perf-level 2, reserved in l1. If this region becomes full at any point during the epoch, that thread will stop recording. At the end of each epoch, this buffer is copied to DRAM. In the next epoch the thread will resume from the beginning of the buffer.
- **`--dump-perf-events-intermediate`**:
  - Enables the performance monitoring with intermediate dumps to DRAM. Will allow for more information to be recorded in each epoch.
- **`--perf-level 0(or 1 or 2)`** (default: 0):
  - Determines the granularity of the information that will be recorded during runtime. In level-0 each thread will have 640B and in level-1 4KB of space in l1 to record performance data.
  - --perf-level 0: Light   Ncrisc,   Light   Trisc
  - --perf-level 1: Light   Ncrisc,   Verbose Trisc
  - --perf-level 2: Verbose Ncrisc,   Verbose Trisc
- **`--perf-op-mode`**:
  - Any op in the graph can be configured in different decoupling modes.
    - The format of the input config: **`--perf-op-mode <op_name_0>:<decouple_0>-<decouple_1>,<op_name_1>:<decouple_0>`**
      - Also valid with the same effect: **`--perf-op-mode <op_name_0>:<decouple_0>,<op_name_0>:<decouple_1>,<op_name_1>:<decouple_0>`**
    - e.g.: `--perf-op-mode unary_feeder:MathPack,unary_drainer:UnpMath` (This config is an example of how to run an op in `triplet-mode`. More on this in the following sections).
    - UnpMath decoupling:
      - Unpacker will do nops.
      - Math assumes there's always data available in Src registers.
    - MathPack decoupling:
      - Math will always assume Dst registers are free to write into.
      - Packer will always assume there is data in Dst to pack.
- **`--perf-target-inputs`**:
  - Specifies the input indices to record the performance info for
  - By default in perf-level 0 we record the first and last inputs, for level-1 and level-2 we record the first, last, and middle inputs.
  - e.g. `--perf-target-inputs 3,5:8,-1,-2,1`:
    - In this example, assuming the total-input-count = 16, we will record performance for inputs: 1,3,5,6,7,14,15
- **`--perf-output-dir`**:
  - Overrides where the perf_results directory will be generated.
- **`--measure-steady-state`**:
  - Measures throughput for middle inputs
- **`--triplet-mode`**: Will place the op with the name following this option in triplet mode. In triplet mode, all producer-ops of the target op will be MathPack decoupled and all the consumer-ops will be UnpMath decoupled. To ensure complete decoupling of the target op from the rest of the graph, additionally following ops will be decoupled as well:
  - If any of the consumer/producer ops use intermediate buffers (currently fused ops and matmul), all UnpMath-MathPack will be decoupled.
  - If MathPack is decoupled for a producer-op, UnpMath of all its other consumer ops will be decoupled as well.
  - If UnpMath is decoupled for a consumer-op, MathPack of all its other producer ops will be decoupled as well.

# Backend Profiler
Backend records events in runtime and compile time to profile the host while the device is running.

With release build, only a few events will be recorded. 

To enable the verbose backend events:
```
make clean
BACKEND_PROFILER_EN=1 DEVICE_RUNNER=Silicon make (Add ARCH_NAME=wormhole to compile for wh)
```
Currently every event name has a suffix specifying the pid and thread id.

The description for some of the important events:
- `hw-tilize`: The fast tilize and push tensor to dram on the device
- `pop-output`: Pops the tensor and frees up space in output queue. Finishes after the data is available in output queue.
- `get-untilized-output`: Waits and untilizes the output tensor (if not already untilized by the device). Finishes after the data is available in output queue.
- `run-program`: The start and end of the run_program api. Host does not necessarily wait for device to finish the program before exiting run_program.
- `device-runtime`: The period when device was opened by the backend. Device is not necessarily active during this time.
- `wait-for-idle`: Host waiting for device to execute all the epochs that were queued.
- `send-epoch-command`: Host sending the epoch-command for all cores to the device. The epoch command queue for each core, currently has 32 entries.

# Forcing aiclk in Silicon tests

The aiclk can be forced for tests using:
- **`--target-aiclk <target aiclk>`**: Will force the clock frequency in MHz. e.g.: `--target-aiclk 1200`

# Performance Reports

Performance post-processor will generate the following reports:
The path to the output of post-processor: `$ROOT/tt_build/<Test-Output>/perf_results/`

There will be two directories here:
- `host`
- `device`: The file `decoupling_info.txt` in this directory will describe all decoupling/triplet modes executed.

- Under perf-mode directory, there will be one directory for each program that was executed.
- Under each program directory, there will be one directory for every graph that was executed in that program:
```
perf_results/device/<program_dir_name>/<graph_dir_name>/
```

For each graph, post-processor generates the following files:

- `perf_postprocess.json`: Detailed report of all events for all the cores/threads.
- `runtime_table.json`: A summary report with op-level information such as runtime (first unpack to last pack), math-utilization, input/output bandwidth, and model numbers.
- `op_perf_table.json`: A report of runtime information at op-level. The information for each core in runtime_table.json is processed and aggregated for all cores that run the same op.
- `cores_to_ops.json`: A description of each active physical core in this epoch. Such as, op-name, feeder ops, drainer ops, logical-core-id, input/output data-formats, ...
- `device_perf_dump ... .yaml`: The raw data extracted from dram for each core that was active in this epoch.

# Performance-GUI

The performance visualizer can be used to plot the information collected during runtime.

The routeagui can be installed using a dmg file on machines with mac os. We regularly post a new routeagui dmg with latest changes to the perf-gui on the `perf-debug-tools` channel.

Alternatively, routeagui with the perf changes can be opened on a branch:
- Clone the repo
- `yarn` 
- `yarn start`

Routeagui requires a `workspace` to be selected initially.

Perf-GUI is one of five visualization modes in Routeagui. Therefore, to be able to open Perf-GUI, we must select a workspace.

1. In the vertical toolbar on the left, select the perf-dump section (the last icon at the very bottom of the toolbar)
2. There are two ways to open performance reports:
    - From a local machine: Use `Local Selection` in the Perf-GUI to browse your local machine and open any perf-dir.
    - From remote machine: You can open the performance reports that exist under the test-output directory that was selected in step 1

Note: The remote selection can take a while for bigger graphs. It is recommended to zip the perf_results directory on the remote machine, scp to local machine, and then open the directory locally. Copying the directory, without compressing it first, will take much longer. E.g:
```
tar czf tt_build/tmp_tar.tar.gz tt_build/test_out/perf_results 
```
and on local machine
```
scp aus-gs-02:<PATH_TO_REPO>/tt_build/tmp_tar.tar.gz ~/Downloads
```

# Performance Check
If `performance-check` section is added to the netlist, and the test is run with perf-trace enabled, perf-check will automatically get executed.

Example-1:
```
performance-check:
  config-0:
    graph-name: fwd_0
    program-name: run_fwd
    tensors-per-second:
      expected: 16000
      rtol: 0.10
    cycles-per-tensor:
      expected: 70835
      rtol: 0.05
```
Example-2:
```
performance-check:
  config-0:
    graph-name: test_op
    program-name: program0
    target-cores: []
    target-inputs: [0]
    target-ops: [target_op]
    math-utilization:
      expected: 0.599
      rtol: 0.1
    execution-cycles:
      expected: 2300
      rtol: 0.1
```
There can be multiple entries under **`performance-check`** (config-0 in these examples). Each entry will contain a separate check for the current netlist.

There are two types of checks we can perform currently:

1- Graph-level checks (In example-1)
- Two performance metrics are supported at the moment:
  - **tensors-per-second**: Average number of tensors (inputs) to process in one second
  - **cycles-per-tensor**: Average number of cycles it takes to process one tensor (input)
- The check will be performed on all epochs with name **`graph-name`** which run under a program with name **`program-name`**

2- Core-level checks (In example-2)
- Two performance metrics are supported at the moment:
  - **math-utilization**: For each input `(total-number-of-cycles-with-fpu-sfpu-active) / (first-unpack-to-last-pack)`
  - **execution-cycles**: first-unpack-to-last-pack
- The check will be performed on all epochs with name **`graph-name`** which run under a program with name **`program-name`**
- The check will be performed on the union of all the physical core coordinates "**`target-cores`**" and all cores that run the "**`target-ops`**" op name

The relative tolerance (rtol) is the ratio of difference allowed around the target: valid: `(expected - rtol\*expected) to (expected + rtol\*expected)`

# Performance Sweeps
We can run a sweep of randomly generated op-tests using the `perf_lib/perf_sweep.py` script:
- For all ops, all constraints are under `verif/templated_netlist/test_modules/test_perf.py`
- The sweep parameters are specified in `perf_lib/sweep_params.py`
- All valid configs will be generated using the z3 solver
- These configs will then go through a netlist_generator
  
The perf sweep can be run on each module using the following command:
```
./perf_lib/perf_sweep.py --netlists-output-dir perf_lib/matmul_test/ --run-light-mode --op-type matmul
```
By default this script will run the test with `Feeder + Op + Drainer` **with** following decouplings:
- Math and Packer will be decoupled in feeders
- Unpack and math will be decoupled in drainers

This script accepts the following arguments:
- **`--netlists-output-dir`**: Specifies the output directory of the sweeps
- **`--op-type`**: The type of op to run the sweep for
- **`--run-all-modes`**: Run perf sweep twice:
  - `Feeder + Op + Drainer` **without** any decouplings
  - Single op with prologue enabled
<!-- - **`--tm-test`**: Must only be used with module `test_feeder_drainer_unary_tms.py`. In this mode, for each tm test, we run an equivalent test without any tms. The target-op dimensions are exactly the same between the two tests. This is to find the overhead of the TM on performance of the op. -->
- **`--run-light-mode`**: Run perf sweep with intermediate dump and perf-level 1. Larger overhead on the silicon run.
- **`--verbose-perf-results`**: Dumps perf results for both the best core and worst core in the grid. If this is not enabled, only the worst core will be included in the report.
- **`--run-prologue`**: Instead of running the test with feeders and drainers with decoupling, the target op will be fed by inputs all in prologue mode and with a drainer in decoupling mode.

Under the `netlists-output-dir` directory:
- One sub-directory will be created for each mode that was run:
  - `feeder_drainer_without_decouplings`
  - `single_op_prologue`
  - `feeder_drainer_with_decouplings`
- Following files will also be created:
  - `perf_sweep_summary.csv`: A **sorted** list of each set of test parameter and the corresponding perf-results. The order of the perf-results is:
    - In `run-all-modes` mode:
      1. Best core for feeder_drainer without decouplings
      2. Worst core for feeder_drainer without decouplings
      3. Best core for single op with prologue
      4. worst core for single op with prologue
    - Without `run-all-modes` mode:
      1. Best core for feeder_drainer with decouplings
      2. Worst core for feeder_drainer with decouplings
    - In `tm-test` mode:
      1. Best core for test with TMs
      2. Worst core for test with TMs
      3. Best core for test without TMs
      4. Worst core for test without TMs
  - `test_configs.yaml`: All test configs

* The best and worst cores are determined based on the execution cycles.

Under each subdirectory:
- `test_#`: For each test, this directory will contain a netlist and the test output log
- `perf_sweep_summary.csv`: A **sorted** list of each set of test parameters and the corresponding perf-results. The order of the perf-results is:
  1. Best core for the current mode
  2. Worst core for the current mode
- `perf_sweep_summary.yaml`: An unsorted table of each entry and the corresponding perf results

<!-- # Op performance test
Op performance tests can be added using the following script:
```
./perf_lib/op_perf_test.py --op-name matmul
```
This script will accept the following arguments:
- **`--netlists-output-dir`**: The netlist output directory path
- **`--op-name`**: The name of the test config:
  - This script will search for the test-config file under `"perf_lib/op_tests/{state.op_name}.{state.arch_name}.yaml"`
- **`--skip-run`**: Only generate the netlists and skip silicon test
- Environment variable **`ARCH_NAME`** for selecting between **`grayskull`** and **`wormhole`**

The test-config must contain:

 **`sweep-parameters`**: Each entry under this section specifies information for a single op test. And has the following fields:
  - **`test-parameters`**: Must specify a value for each variable in **`perf_fixed_vars`** of the corresponding module
  - **`performance-metrics`**: We can specify **`math-utilization`** or **`execution-cycles`** targets in this section -->

<!-- # Performance of performance-trace
Enabling performance trace, will add extra workload on each thread. On bert-large the performance changes after enabling perf-dump was measured as follows:

**Performance config**          ->  **Runtime-Increase-Percentage**

Single-dump     Level-0	    -> **4.00%** This is the default mode using --perf-dump-events

Single-dump     Level-1	    -> **9.77%**

Intermediate    Level-0	    -> **4.57%**

Intermediate    Level-1	    -> **11.17%** -->

# Performance Analysis

This tool will run the original graph initially and then loop through all the ops in target epochs and puts each one in triplet mode. e.g.
```
ARCH_NAME=grayskull  DEVICE_RUNNER=Silicon ./perf_lib/perf_analysis.py --results-output-dir perf_lib/bert_large_lofi_fused_analysis/ --target-program run_fwd --target-graphs fwd_3,fwd_0 --timeout 120  --  ./build/test/verif/netlist_tests/test_inference --netlist perf_lib/graph_tests/inference/bert_large_lofi_bfp8b_v2.yaml --backend Silicon --seed 0 --golden Invalid --O 3 --io-timeout 100 --dump-perf-events --perf-target-inputs 56:72
```
The command to run this script consists of two parts and they are separated by `--`:
- **`<Perf_analysis script cmdline options> -- <Budabackend test cmdline options>`**

Perf_analysis script cmdline options include:
- **`--results-output-dir`**: The output directory for the results of the analysis and perf output directory of each run.
- **`--timeout`**: The timeout for the tests in triplet mode. This will not be applied to the original run of the test.
- **`--target-program`**: The target program name to run the triplets for.
- **`--target-graphs`**: The target graph names to run the triplets for. There will be one directory created for each epoch.
- **`--reset-silicon-after-hang`**: **This option should only be used on machines with a reset-dongle**. When this option is enabled, if any of the tests with triplet mode hangs, the board is reset and the script will move on to the next op. If this mode is not enabled, the script will exit with error.
- **`--exclude-op-list`**: The list of ops to skip when running the script. These ops will not be placed in triplet mode.

## Output reports:
One directory will be created for each target-graph. Under each one:
- There will be the perf output directories of each run.
- **all_triplet_modes.json**: The sorted list of all the cores from largest runtime to the smallest. The first ops are the worst performing ones.

**original_triplet_diff.json**: The diff between each core in original mode and triplet mode.

# Run a Pybuda Test from Budabackend
To build backend for grayskull and run pybuda tests:
```
DEVICE_RUNNER=Silicon make 
DEVICE_RUNNER=Silicon make verif/netlist_tests
```
For wormhole:
```
DEVICE_RUNNER=Silicon ARCH_NAME=wormhole make 
DEVICE_RUNNER=Silicon ARCH_NAME=wormhole make verif/netlist_tests
```

Take the netlist generated by pybuda and append this section at the bottom and modify the io-config part to list the inputs and outputs for the current netlist:
```
test-config:
  comparison-config:
    type: AllCloseHw
    atol: 0.01
    rtol: 0.15
    check_pct: 0.50
    check_pcc: 0.92
    verbosity: Concise
  stimulus-config:
    type: Normal
    normal_mean: 0.0
    normal_stddev: 0.1
  io-config:
    inputs: [attention_mask, hidden_states]
    outputs: [bert_encoders.output_layernorm_1271]
```
For inference tests, run:
```
./build/test/verif/netlist_tests/test_inference --netlist <PATH_TO_NETLIST> --backend Silicon --seed 0 --golden Invalid --O 3 --io-timeout 100 
```
Any of the perf flags described above can be appended to this command.

# Debug Mailbox

There are 32 (16 bit) values reserved for each thread.
These registers can be dumped into a yaml file using the following command:
```
./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/debug_mailbox.py
```
These registers will also be dumped automatically if Silicon/Versim runs hit a timeout (in spatial-2 this automatic dump is not functional). We can always dump these registers using **`--dump-debug-mailbox`** cmdline flag.

The path for output yaml file when dumped at runtime: **`tt_build/<TEST_NAME>/<silicon/versim>_debug_mailbox_device_<#>.yaml`**

This is particularly helpful for debugging hangs. For example, we can insert the APIs mentioned below, to record values in different parts of the kernels to help find the place and the reason trisc is hanging. We need to set the **`TT_BACKEND_TIMEOUT`** environment variable to something reasonably short. e.g. **`TT_BACKEND_TIMEOUT=15`**

To dump debug mailbox in debuda prompt:
```
cdr command in debuda prompt (core-debug-registers)
```
Existing APIs for recording values in mailbox:

Record a value in a specific index:
  
- **`record_mailbox_value_with_index`**`(uint8_t index, uint16_t event_value)`

Record a value and increment the index:
  
- **`record_mailbox_value`**`(uint16_t event_value)`

Reset all values in mailbox (for a specific thread) to a certain number:
  
- **`clear_mailbox_values`**`(uint16_t value = 0)`
