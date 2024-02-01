# Net2Pipe and Pipegen Blackbox Testing

This test executes  [net2pipe](#net2pipe) and [pipegen2](#pipegen) on a set of predefined netlists. Outputs are compared with pregenerated baseline outputs which are stored in binary files together with the netlists located in: `verif/pipegen_tests/netlists/{ARCH_NAME}/{push|nightly}/baseline.zip"`.

Test can fail either due to a bug, or due to a valid change in output files. If there is a change in output, baseline.zip files need to be updated for both push and nightly for architectures that your change affects.

**Attention: You need to build for appropriate hardware architecture for which you are running tests or generating baseline files! Pipegen produces different outputs with different architecture builds.**

### TSG - Daily/Nightly tests are failing
To fix tests, you have to update baseline files.
```
./verif/pipegen_tests/netlists/grayskull/push/baseline.zip
./verif/pipegen_tests/netlists/grayskull/nightly/baseline.zip
./verif/pipegen_tests/netlists/wormhole_b0/push/baseline.zip
./verif/pipegen_tests/netlists/wormhole_b0/nightly/baseline.zip
```
Below are commands that you need to run to generate new baseline files.
``` bash
cd ~/work/budabackend
export ARCH_NAME=grayskull
make clean
make -j16
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command unpack --base-dir ./verif/pipegen_tests/netlists/grayskull/push/ --out-dir ./build/test/n2pgn/base_dir
ARCH_NAME=grayskull python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --base-dir ./build/test/n2pgn/base_dir --arch grayskull
cp build/test/n2pgn/base_dir/baseline.zip verif/pipegen_tests/netlists/grayskull/push
rm -rf build/test/n2pgn
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command unpack --base-dir ./verif/pipegen_tests/netlists/grayskull/nightly/ --out-dir ./build/test/n2pgn/base_dir
ARCH_NAME=grayskull python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --base-dir ./build/test/n2pgn/base_dir --arch grayskull
cp build/test/n2pgn/base_dir/baseline.zip verif/pipegen_tests/netlists/grayskull/nightly
export ARCH_NAME=wormhole_b0
make clean
make -j16
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command unpack --base-dir ./verif/pipegen_tests/netlists/wormhole_b0/push/ --out-dir ./build/test/n2pgn/base_dir
ARCH_NAME=wormhole_b0 python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --base-dir ./build/test/n2pgn/base_dir --arch wormhole_b0
cp build/test/n2pgn/base_dir/baseline.zip verif/pipegen_tests/netlists/wormhole_b0/push
rm -rf build/test/n2pgn
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command unpack --base-dir ./verif/pipegen_tests/netlists/wormhole_b0/nightly/ --out-dir ./build/test/n2pgn/base_dir
ARCH_NAME=wormhole_b0 python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --base-dir ./build/test/n2pgn/base_dir --arch wormhole_b0
cp build/test/n2pgn/base_dir/baseline.zip verif/pipegen_tests/netlists/wormhole_b0/nightly
```

## Unpacking Binary

Here is an example how to unpack pregenerated baseline data for Grayskull:
```
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command unpack --base-dir ./verif/pipegen_tests/netlists/grayskull/push --out-dir ./build/test/n2pgn/base_dir
```

## Running Tests

To run test locally, first unpack the binary baseline data into the `--base-dir` and then execute the test with the following command:
```
ARCH_NAME=grayskull python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command test --base-dir ./build/test/n2pgn/base_dir --out-dir ./build/test/n2pgn/out_dir --arch grayskull
```

## Creating packed binary

To create new baseline binary, first create a text file with the list of netlist paths and/or paths to directories containing the netlists that you want to generate the baseline from, and then run the following command:
```
ARCH_NAME=grayskull python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update --base-dir ./build/test/n2pgn/base_dir --netlists-file ./netlist.txt --arch grayskull
```

`--netlists-file` is a text file with the list of netlist paths and/or paths to directories containing the netlists.

Make sure that you've built binaries for appropriate hardware architecture! Pipegen produces different outputs with different architecture builds.

New binary is stored in ```./build/test/n2pgn/base_dir/baseline.zip```

## Updating packed binary with new netlists

To update existing binary with new netlists, first unpack the existing binary data into the `--base-dir` and then run the following command:
```
ARCH_NAME=grayskull python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update --base-dir ./build/test/n2pgn/base_dir --netlists-file ./netlist.txt --arch grayskull
```

`--netlists-file` is a text file with the list of new netlist paths and/or paths to directories containing the new netlists that you want to add to the baseline.

It will check if each netlist already exists in `--base-dir`, and if not then it will add it and pack it and its results into the updated binary baseline file.

Make sure that you've built binaries for appropriate hardware architecture! Pipegen produces different outputs with different architecture builds.

Updated binary is stored in ```./build/test/n2pgn/base_dir/baseline.zip```

## Updating packed binary results only

If you've made a change in net2pipe or pipegen code which affects their output, you need to update the baseline files with new net2pipe/pipegen results using same netlists that are already packed into the baseline files. Baseline files need to be updated **for both push and nightly for architectures that your change affects**.

To do so, first unpack the existing binary data into the `--base-dir` and then run the following command:
```
ARCH_NAME=grayskull python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --base-dir ./build/test/n2pgn/base_dir --arch grayskull
```

Make sure that you've built binaries for appropriate hardware architecture! Pipegen produces different outputs with different architecture builds.

Updated binary is stored in ```./build/test/n2pgn/base_dir/baseline.zip```

## What to do when some netlists in the baseline files are no longer valid

Sometimes net2pipe or pipegen logic can change so that some netlists that were valid before are no longer valid, for example they are hitting some valid assertions that were introduced.

When baseline file is generated (or updated with new outputs) script packs only the netlists that pass both net2pipe and pipegen into the new baseline file (together with their outputs). So this would be the best way to handle situations like this:
1. Unpack the old baseline file using steps explained [here](#unpacking-binary)
2. Update it with new results using steps explained [here](#updating-packed-binary-results-only)
3. Check the list of the netlists that failed to be included in the updated baseline file (those in the .log file with “Failed” line below net2pipe and pipegen commands). List of failed netlists is also printed in the console output after step 2.
    - If there are not too many of such netlists, make sure that all of those are now considered invalid netlists, and not some valid netlists that fail because of some regression.
    - If there are a lot of such netlists then alert @mrakita or @rpavlovic to regenerate the netlists with new constraints and create new baseline files.
4. Commit your new baseline files with your changes in net2pipe and pipegen, even if they are missing a lot of netlists. The goal of these tests is to block the regressions, not the valid changes from going in.

## Testing between two local changes

Usually you are testing net2pipe and pipegen between clean build and build with your changes, but there are some cases when you want to test net2pipe and pipegen outputs between two local builds:
- When clean build is not actually clean, i.e. some change is commited to master which affected net2pipe/pipegen outputs but new baseline binary was not commited with the change.
- When you've made a change in your local branch which affected net2pipe/pipegen outputs, and then you continue working on that branch and want to check if you made any regressions in your latest commits.

You can run the test in these cases easily by first creating new baseline results with your version of "clean" build, and then testing your future builds on to that version of the baseline:
1. Sync to the commit with which you want to create baseline results (latest master or last commit in your branch where you knew results were good)
2. Generate new baseline results using steps explained [here](#updating-packed-binary-results-only)
3. Move the generated baseline binary out of the build folder if you've generated it there, so it's not removed later during `make clean`
4. Sync to the commit you want to test (or apply your local changes), build etc
5. Unpack the baseline results generated in step 2 using steps explained [here](#unpacking-binary)
6. Run the test using steps explained [here](#running-tests)

Make sure that you've built binaries for appropriate hardware architecture! Pipegen produces different outputs with different architecture builds.

## CI run

There are multiple CI configurations:
- [grayskull-push](../../ci/test-lists/pipegen_grayskull_model_push.yaml)
- [grayskull-nightly](../../ci/test-lists/pipegen_grayskull_model_nightly.yaml)
- [wormhole-push](../../ci/test-lists/pipegen_wormhole_model_push.yaml)
- [wormhole-nightly](../../ci/test-lists/pipegen_wormhole_model_nightly.yaml)

<br/>To run CI locally you can execute following commands, specifying appropriate testlist, tag and architecture:

```
* Build
python3 ci/run.py build --local-run --arch_name grayskull --device_runner model --tag push

* Run tests
python3 ci/run.py run --local-run --test pipegen_grayskull_model_push --run_entire_testlist --device_runner model --tag push --arch_name grayskull
```

## Net2Pipe

To build net2pipe execute ```make src/net2pipe```.
Build has dependency on <span style="color: #7393B3">ARCH_NAME</span>.

### Net2Pipe Command and Usage

```
./build/bin/net2pipe <input netlist yaml> <output directory> <soc descriptor yaml> <(optional) cluster descriptor file>

ex.
./build/bin/net2pipe verif/graph_tests/netlists/netlist_encoder_x4.yaml output_dir soc_descriptors/grayskull_10x12.yaml

./build/bin/net2pipe verif/multichip_tests/wh_multichip/netlist_wh_multichip_dram_fork_1_to_00.yaml output_dir soc_descriptors/wormhole_8x10.yaml wormhole_2chip_cluster.yaml
```

#### Soc Descriptors

soc descriptior - defines location of cores, dram, ethernet, memory sizes, packer/unpacker version...<br/>
- [Grayskull   - grayskull_120_arch](../../soc_descriptors/grayskull_10x12.yaml)
- [Wormhole    - wormhole_8x10](../../soc_descriptors/wormhole_8x10.yaml)
- [Wormhole b0 - wormhole_b0_80_arch](../../soc_descriptors/wormhole_b0_8x10.yaml)

#### Cluster Descriptors

cluster descriptor - defines ethernet connections between chips, chips_with_mmio, number of chips<br/>
- [Grayskull]() automatically created in runtime based on number of target devices in netlist.
- [Wormhole - wormhole_2chip_cluster](../../wormhole_2chip_cluster.yaml)
- [Wormhole - wormhole_4chip_square_cluster](../../wormhole_4chip_square_cluster.yaml)

#### Output from Net2Pipe
```
	output_directory/netlist_queues.yaml - not used by pipegen
	output_directory/temporal_epoch_0/overlay/pipegen.yaml
	.
	.
	.
	output_directory/temporal_epoch_n/overlay/pipegen.yaml
```

## Pipegen
To build pipegen ```make src/pipegen2```. Build has dependency on <span style="color: #7393B3">ARCH_NAME</span>.

### Pipegen command and usage
```
pipegen2 <input pipe graph yaml> <soc descriptor yaml> <output blob yaml> <epoch start phase> <perf dump level>

ex.
./build/bin/pipegen2 ./output/temporal_epoch_0/overlay/pipegen.yaml ./soc_descriptors/grayskull_10x12.yaml ./output/temporal_epoch_0/overlay/blob.yaml 0 0

./build/bin/pipegen2 ./output/temporal_epoch_0/overlay/pipegen.yaml ./soc_descriptors/wormhole_8x10.yaml  ./output/temporal_epoch_0/overlay/blob.yaml 0 0
```

- input pipe graph yaml - file generated in net2pipe ex. ```"output_directory/temporal_epoch_0/overlay/pipegen.yaml"```
- soc decriptor yaml    - same as for [net2pipe](#soc-descriptors)
- output blob yaml      - output file ex. ```"output_directory/temporal_epoch_0/blob.yaml"```
- epoch start phase     - epoch_id -> for temporal_epoch_0 folder it is 0
- perf dump level       - 0 | 1

### Pipegen output

Pipegen output is blob yaml file. ex. ```"output_directory/temporal_epoch_0/blob.yaml"```
