# LLK Testbench - Testing environment for llk's
Meant to test llk 

## Build and Run

LLK test environment is built using the following command
```shell
make 
./out/llk_tb --t=eltwise_unary
```

## New Tests
### New Test Family
If a new test family is added, this needs to be a new folder, see `./tests/<test_family>`
Then the relevant functions need to be updated `./tests/test_list.h`

Each test family is within it's own namespace so that we have complete segregation between test families and will not overlap. The separation tends to be based on how many inputs or outputs there are, as those are hard to programatically change since they rely on a specific overlay graph generation script

###  New Tests
A new test that fits the paradigm of the outputs and inputs can be written by an addition of a test yaml + expected generation code

* test descriptors live in `./tests/<test_family>/test_descriptors/` and they can be specified
* Expected calculations will depend on the test, but mainly involve changing the generate_stimulus_and_expected_tensors function in test_main.cpp and the llk_tensor_ops.cpp