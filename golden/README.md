# Golden is one of the backend targets for FE to run to.  
Meant to model only the functional aspects of a graph. This does not model any buffers/pipes and can be run entirely off the netlist.  The netlist is parsed and a graph is constructured from the parsed structures, which is then evaluated to get the final result.  This is the "golden" result with which backend assumes is always correct.  Golden is evaluated against pytorch in the front end to ensure correctness throughout the stack.

# Build
make golden

# Testing
make verif/netlist_tests

## local script
The following command can be used to run all the golden specific unit tests
`./verif/netlist_tests/run_all.sh`
The following command can be used to run all the integration tests
`./verif/directed_tests/run_all.sh`
