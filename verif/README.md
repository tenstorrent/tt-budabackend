# Verification Folder
Central location for testing/verification 

- The main verif folder builds a common verification library that can be used in any tests
- Specific types of tests should be in its own folder

## Verification Library

### Build steps
`make verif`

## Directed tests
Directed tests are meant to test very directed netlists, where the netlists are hand crafted or hardened by a drop from frontend netlist generation. 
As such, there are three main tools which handle different aspects of testing
### test_pytorch_to_golden
tool to compare netlist and pytorch provided by FE team to golden backend
Pytorch is the "reference"
Golden is the target to be tested and verified here. 
```sh
make verif/directed_tests/test_pytorch_to_golden
./build/test/verif/directed_tests/test_pytorch_to_golden --netlist verif/directed_tests/netlist_bert_mha.yaml --pytorch-bin verif/directed_tests/saved_pytorch_tensors/netlist_bert_mha --num-loops 2
```
- Golden will run on netlist
- Pytorch tensors are read in from the location pointed to by `--pytorch-bin verif/directed_tests/saved_pytorch_tensors/netlist_bert_mha`
- The two tensors will be compared for pass fail
   
### test_golden_to_model
tool to compare golden to model backend
Golden is the "reference"
Model is the target to be tested and verified here. -- Meant for Net2Pipe or Model Developers
```sh
make verif/directed_tests/test_golden_to_model
./build/test/verif/directed_tests/test_golden_to_model --netlist verif/directed_tests/netlist_unary_pipes.yaml
```
- Golden will run on netlist
- Model will generate a pipegen.yaml from netlist using net2pipe and then run the model on the pipegen.yaml file.
- The two flows will be compared for pass fail

### test_netlist
Tool to compare golden to any of the three targets (model/versim/silicon)
WIP

### Specific directed tests

Specific directed tests can also be added as a separate `.cpp` file.  They can then be compiled and run using the following flow:
```sh
make verif/directed_tests/*cpp_file_name*
./build/test/verif/directed_tests/*cpp_file_name*
```
