# Contributing
This folder is formatted using default configuration of Black python formatting, and using isort for sorting imports.

To make the development easier in VSCode, install [Black Formatter extension](https://marketplace.visualstudio.com/items?itemName=ms-python.black-formatter), and [isort extension](https://marketplace.visualstudio.com/items?itemName=ms-python.isort). Open one of the settings.json files:
- Workspace settings, which are at .vscode/settings.json from the root of the opened workspace
- User settings, which can be opened using CMD+SHIFT+P and then "Preferences: Open User Settings (JSON)

Add the following snippet:
```
"[python]": {
	"editor.defaultFormatter": "ms-python.black-formatter",
	"editor.formatOnSave": true,
	"editor.codeActionsOnSave": {
		"source.organizeImports": true
	},
},
"isort.args":["--profile", "black"],
```

# Net2Pipe and Pipegen Blackbox Testing

This test executes  [net2pipe](#net2pipe) and [pipegen2](#pipegen) on a set of predefined netlists. Netlists and their baseline overlay results are stored in zip files located at: `verif/pipegen_tests/netlists/{ARCH_NAME}/{push|nightly}/baseline.zip"`.

Test can fail either due to a bug, or due to a valid change in output files. If there is a change in output, baseline.zip files need to be updated for both push and nightly for architectures that your change affects.

**Attention: You need to build for appropriate hardware architecture for which you are running tests or generating baseline files! Overlay produces different outputs with different architecture builds.**

### TSG - Daily/Nightly tests are failing
To fix tests, you have to update these baseline files.
```
./verif/pipegen_tests/netlists/grayskull/push/baseline.zip
./verif/pipegen_tests/netlists/grayskull/nightly/baseline.zip
./verif/pipegen_tests/netlists/wormhole_b0/push/baseline.zip
./verif/pipegen_tests/netlists/wormhole_b0/nightly/baseline.zip
```
Below are commands that you need to run to update the baseline files with new overlay results, you can change the `--out-dir` to something else.
``` bash
make build_hw -j
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --baseline-file verif/pipegen_tests/netlists/grayskull/push/baseline.zip --out-dir test_pipegen/blackbox_test
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --baseline-file verif/pipegen_tests/netlists/grayskull/nightly/baseline.zip --out-dir test_pipegen/blackbox_test
ARCH_NAME=wormhole_b0 make build_hw -j
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --baseline-file verif/pipegen_tests/netlists/wormhole_b0/push/baseline.zip --out-dir test_pipegen/blackbox_test
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --baseline-file verif/pipegen_tests/netlists/wormhole_b0/nightly/baseline.zip --out-dir test_pipegen/blackbox_test
```

## Running Tests

To run blackbox test locally on the whole baseline you can use the following command:
```
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command test --baseline-file verif/pipegen_tests/netlists/grayskull/push/baseline.zip --out-dir test_pipegen/blackbox_test
```

It will unpack the baseline file into `out_dir`, generate overlay results with your currently built overlay binaries, and compare them with the baseline overlay results. In case you didn't clean the `out_dir` and you already have the baseline file unpacked, the unpacking step will be skipped.

You can pass the `--num-cores X` argument to specify the number of cores used (by default it will use all CPU cores available).

## Updating baseline zip

To create new baseline binary, you can run the following command:
```
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update --baseline-file verif/pipegen_tests/netlists/grayskull/push/baseline.zip --out-dir test_pipegen/blackbox_test
```

It will parse all CI test lists with the same architecture and tag (push/nightly) as the baseline file, and collect all netlists used in those test lists. It will then generate baseline overlay results for those netlists, and pack it all together into new baseline zip.

You can pass the `--num-cores X` argument to specify the number of cores used (by default it will use all CPU cores available).

Make sure that you've built binaries for appropriate hardware architecture! Overlay produces different outputs with different architecture builds.

## Updating baseline zip with new overlay results, keeping the same netlist set

If you've made a change in net2pipe or pipegen code which affects their output and you just want to update the baseline results in zips, you can use the following command:
```
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command update-results --baseline-file verif/pipegen_tests/netlists/grayskull/push/baseline.zip --out-dir test_pipegen/blackbox_test
```

You can pass the `--num-cores X` argument to specify the number of cores used (by default it will use all CPU cores available).

Baseline files need to be updated **for both push and nightly for architectures that your change affects**.

Make sure that you've built binaries for appropriate hardware architecture! Pipegen produces different outputs with different architecture builds.

## Unpacking Baseline zip for inspection

Here is an example how to unpack pregenerated baseline data for Grayskull:
```
python3 ./verif/pipegen_tests/pipegen_blackbox_test.py --command unpack --baseline-file verif/pipegen_tests/netlists/grayskull/push/baseline.zip --out-dir test_pipegen/blackbox_test
```

## What to do when some netlists in the baseline files are no longer valid

Sometimes net2pipe or pipegen logic can change so that some netlists in the baseline zip that were valid before are no longer valid, for example they are hitting some valid assertions that were introduced. In that case baseline files need to be updated with the latest netlists as explained [here](#updating-baseline-zip).

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

soc descriptor - defines location of cores, dram, ethernet, memory sizes, packer/unpacker version...<br/>
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
- soc descriptor yaml   - same as for [net2pipe](#soc-descriptors)
- output blob yaml      - output file ex. ```"output_directory/temporal_epoch_0/blob.yaml"```
- epoch start phase     - epoch_id -> for temporal_epoch_0 folder it is 0
- perf dump level       - 0 | 1

### Pipegen output

Pipegen output is blob yaml file. ex. ```"output_directory/temporal_epoch_0/blob.yaml"```
