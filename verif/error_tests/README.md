# Error Handling Tests

This folder contains a suite of tests designed to check if errors are correctly caught by the system. These tests cover a range of scenarios and edge cases that may cause errors or unexpected behavior in the system.

Each test is designed to intentionally trigger an error and then verify that the system responds appropriately, by returning an error message.

The goal of these tests is to ensure that the system can handle errors in a robust and predictable way, and to identify any areas where error handling may be insufficient or incomplete.

## Tests - Netlists

Folder structure for the netlists is following:

```
netlists
|___ defects (folder containing netlists which are not yet supported by the system and has active defects assigned)
     |___ feature_1 (folder containing netlists for feature 1)
            |___ description_issue_1.yaml (netlist which is not yet supported and has defect 1 assigned)
            |___ description_issue_2.yaml (netlist which is not yet supported and has defect 2 assigned)
     |___ feature_2 (folder containing netlists for feature 2)    
|___ invalid (folder containing netlists which are invalid)
        |___ error_netlist_1.yaml (netlist which is invalid and should trigger error message 1)
        |___ error_netlist_2.yaml (netlist which is invalid and should trigger error message 2)
        |___ error_netlist_3.yaml (netlist which is invalid and should trigger error message 3) 
```

After defect is fixed, netlist should be moved to proper folder under `verif\op_tests\netlists` folder.

## Building and Running Tests

Currently, test is only parsing netlist, and expects to fail with error message.

```
make verif/error_tests
ARCH_NAME=arch_name ./build/verif/error_tests/test_errors --netlist filename

ex.
ARCH_NAME=grayskull ./build/test/verif/error_tests/test_errors --netlist ./verif/error_tests/netlists/invalid/default_invalid_netlist.yaml
```

To run all tests, use `./verif/error_tests/run_all.sh` script.


## CI

**TODO**: Add CI support for these tests.<br>
Currently, CI is not running these tests. To run these tests, you need to run them manually.

