# Generation of single TM tests

Tests inside this folder were generated using generate_ci_tests.py and a set of environment variables and parameters. 

## Environment variables
    - GUIDING_ENABLED - controls if the additional constrains will be added to test generation (1 - on; 0 - off)
    - FORCE_TM_ARGS_GREATER_THAN_ONE - adds the constraint that the of the TM argument will be greater than one, to prevent 
                                    generation of only simple cases
    - NUM_TMS_PER_CONN(i) - regulates the number of TMs allowed per connection i (i can go from 0 to 15)
    - NUM_TMS_PER_CONN - for compatiblity reasons, sometimes this is also used when there is only one TM connection
    - NUM_CONFIGS_PER_TMS_COMBINATION - forces a specific number of configurations to be generated per TM configuration 
                                        (in the generated single_tm tests, this is always set to 1)
    - FORCE_HAS_PAD_UNPAD_SET - forces the genereted testlists to use padding

Arguments are explained in more detail in the `generate_ci_tests.py` source code. However, it is important to note that all of the
test are generated with the `--harvested-rows=2` argument, making them suitable for execution on harvested machines.

## Instructions

### Setup the envornment
To generate the tests themselves, first position into the `verif/template_netlist` folder, and set the environment using the following commands:

```bash
cd verif/template_netlist  # Position into the template_netlist directory
sh create_env.sh           # The creates the environment binary 
source env/bin/activate    # Activates the environment
```

Then, run the following commands for each different netlist configuration:
### input_queue -> datacopy
``` bash
GUIDING_ENABLED=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_CONFIGS_PER_TMS_COMBINATION=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_dram_input_datacopy_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --verbose --log-to-file --harvested-rows=2
```

### input_queue -> matmul
``` bash
GUIDING_ENABLED=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_TMS_PER_CONN0=1 NUM_TMS_PER_CONN1=1  NUM_CONFIGS_PER_TMS_COMBINATION=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_dram_input_matmul_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2
```

### datacopy -> datacopy
``` bash
GUIDING_ENABLED=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_TMS_PER_CONN0=1 NUM_TMS_PER_CONN1=1 NUM_CONFIGS_PER_TMS_COMBINATION=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_datacopy_datacopy_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2
```
 
### datacopy -> matmul
``` bash
GUIDING_ENABLED=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_TMS_PER_CONN0=1 NUM_TMS_PER_CONN1=1  NUM_CONFIGS_PER_TMS_COMBINATION=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_datacopy_matmul_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2
```

### datacopy -> output_queue
``` bash 
GUIDING_ENABLED=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_op_dram_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2 --num-tests=10 --clamp-num-configs
```

## Generation of non-forking netlists

- The commands given above generate single TM tests for grayskull, by changing the arch parameter you can generate the wormhole_b0 ones.
  In addition, all the generated test-list sets also contain the netlist which does not have TMs, for completition. This case can be generated
  by changing the corresponding `NUM_TMS_PER_CONN` and `NUM_TMS_PER_CONN(i)` environment variables.

- To generate test-lists for forking, it is necessary to utilize a different set of test modules, with the FORK_FACTOR environment variable,
setting the corresponding forking degree.

## Generation of forking netlists
Use the following commands to generate forking thest with fork value 2. If generation of tests for different architectures is needed, modify the `--arch` argument. 
### input_queue -> datacopy
``` bash
GUIDING_ENABLED=1 NUM_CONFIGS_PER_TMS_COMBINATION=1  FORK_FACTOR=2 NUM_CONFIGS_PER_TMS_COMBINATION=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_TMS_PER_CONN0=1 NUM_TMS_PER_CONN1=1  python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_dram_input_fork_datacopy_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2
```

### input_queue -> matmul
``` bash
GUIDING_ENABLED=1 NUM_CONFIGS_PER_TMS_COMBINATION=1  FORK_FACTOR=2 NUM_CONFIGS_PER_TMS_COMBINATION=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_TMS_PER_CONN0=1 NUM_TMS_PER_CONN1=1 NUM_TMS_PER_CONN2=1 NUM_TMS_PER_CONN3=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_dram_input_fork_matmul_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2
```

### datacopy -> datacopy
``` bash
GUIDING_ENABLED=1 NUM_CONFIGS_PER_TMS_COMBINATION=1  FORK_FACTOR=2 NUM_CONFIGS_PER_TMS_COMBINATION=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_TMS_PER_CONN0=1 NUM_TMS_PER_CONN1=1 NUM_TMS_PER_CONN2=1 NUM_TMS_PER_CONN3=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_datacopy_fork_datacopy_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2
```

### datacopy -> matmul
``` bash
GUIDING_ENABLED=1 NUM_CONFIGS_PER_TMS_COMBINATION=1  FORK_FACTOR=2 NUM_CONFIGS_PER_TMS_COMBINATION=1 FORCE_TM_ARGS_GREATER_THAN_ONE=1 NUM_TMS_PER_CONN=1 NUM_TMS_PER_CONN0=1 NUM_TMS_PER_CONN1=1 NUM_TMS_PER_CONN2=1 NUM_TMS_PER_CONN3=1 python generate_ci_tests.py --test-module=test_modules.multi_tm_tests.test_datacopy_fork_datacopy_multiple_tms_and_reblock --output-dir=<output_dir> --arch=grayskull --search-type=parallel-sweep --skip-sh-files-gen --log-to-file --verbose --harvested-rows=2
```