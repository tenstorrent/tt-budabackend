- [Graph Test Generator](#graph-test-generator)
  - [Dependencies](#dependencies)
  - [Intro](#intro)
  - [[1] PyByda](#1-pybyda)
  - [[2] **generate_template.py**](#2-generate_templatepy)
  - [[3] **graph_test_base.py**](#3-graph_test_basepy)
    - [What **GraphTestBase** does?](#what-graphtestbase-does)
    - [How it works?](#how-it-works)
    - [Usage](#usage)
    - [Instructions](#instructions)
  - [[4] Generating random netlists](#4-generating-random-netlists)
  - [Supported graphs](#supported-graphs)


# Graph Test Generator

**Graph Test Generator** is a framework that can be used to generate netlists for testing based on almost any netlist created by PyBuda or by hand.

This framework is part of a broader testing framework found at `budabackend/verif/template_netlist`.

The framework consists of two parts:
* `generate_template.py` script that converts a regular netlist to a templated netlist,
* `graph_test_base.py` Python module that enables user to generate random tests for a given templated netlist.

## Dependencies
Create and activate Python virtual environment with needed dependencies:
```
cd budabackend/verif/template_netlist
./create_env
source ./env/bin/activate
```

## Intro

Main goal of this framework is that a user can create a netlist through PyBuda and create valid random netlists based on it. These random netlists have the same structure as the original netlist, but their parameters are changed in random fashion. Those parameters are queue sizes, grid sizes, grid locations for operations, data formats, micro/macro block sizes and others. Such random netlists may be used for testing BudaBackend.

In order to generate random netlists, the framework relies on [Z3](https://github.com/Z3Prover/z3) theorem prover from Microsoft Research.

Steps:
1. Create netlist `xyz.yaml` through PyBuda.
2. Run `generate_template.py` script to convert given netlist into a templated version and save the template in `budabackend/verif/template_netlist/templates`.
3. Inherit from **GraphTestBase** and create a `xyz.py` module specific for this graph test.
4. To generate random tests one can use either of the following scripts:
    - `budabackend/verif/template_netlist/generate_tests.py`,
    - `budabackend/verif/template_netlist/generate_ci_tests.py`,
    - or can write its own test generator based on API exposed by `xyz.py`.

## [1] [PyByda](https://yyz-gitlab.local.tenstorrent.com/tenstorrent/pybuda)

An easy way to create a netlist is to use PyByda's test suite. Run a test in PyBuda simply as following:
```
pytest pybuda/test/test_bert.py::test_mha[inference-no_recompute]
```
However, the focus here is not on PyBuda but on the subsequent steps.

## [2] **generate_template.py**

This script is in `budabackend/verif/template_netlist` directory.

It converts a regular netlist to a templated netlist. Templated netlist is the same as the regular netlist, except that instead of attribute values it contains special strings which will be used as variable names further on. For example:
```
grid_loc: [4, 7] -> grid_loc: [$TEMPLATE_var_288, $TEMPLATE_var_289]
```

It is run with:
```
generate_template.py --netlist-path <netlist_path> --template-path <template_path>
```

`--netlist-path`: location of the regular netlist to be converted into a templated netlist.

`--template-path`: location where to save generated template.

## [3] **graph_test_base.py**

This is module where **GraphTestBase** is defined which is common across all graphs for testing. **GraphTestBase** should be inherited in every graph test module. It expects few arguments to be passed on construction:
* `solver` - object of the z3.Solver type used to define constraint formulas and find solutions for them,
* `svars` - dictionary that is populated with variable names and their z3 representations,
* `general_limits` - object of the `GeneralLimits` type that defines general limits to be used in constraint generation.
* `template_path` - path to the template netlist to be used in constraint generation.

### What **GraphTestBase** does?

It creates set of Z3 constraints so that any solution found by the Z3 Solver will represent a valid netlist.

### How it works?

**GraphTestBase** reads template netlist and makes a graph whose nodes are queues and operations. Then it traverses the graph and makes constraints based on the type of nodes and the design of Tenstorrent device (currently supported Grayskull).

It also makes constraints for each queue based on whether it is input, output, constant or parameter queue.

When making constraints, **GraphTestBase** considers following common node attributes:
* blocking dimensions,
* temporal dimension,
* data formats,
* grid sizes.

Queue specific attributes that are considered:
* prologue,
* location,
* capacity,
* buffer addresses.

Operation specific attributes that are considered:
* grid location,
* L1 memory capacity,
* tms,
* forking factors,
* op specific attributes such as `m_k` and `u_kt`.

### Usage

The intention is that **GraphTestBase** is inherited by every graph test module which will provide required API for the end-user.

Derived graph test module `xyz.py` must have following structure:
```
TEST_TYPE = os.getenv('TEST_TYPE', TestType.GraphTest)
graph_test_base_obj = None
sweep_var_names = []

class XYZ(GraphTestBase):
    def additional_constraints(self):
        # Define specific constraints for XYZ graph.

# Outside of the class scope
def constraint_model(solver: Solver, svars: dict) -> Solver:
    global graph_test_base_obj
    # GeneralLimits() may be swapped with modified list of general limits for xyz graph.
    graph_test_base_obj = XYZ(solver, svars, GeneralLimits(), xyz_template_path)
    graph_test_base_obj.constraint_model()

    # Optionally populate sweep_var_names
    return solver

def extra_config_callback(model_vars: dict) -> dict:
    graph_test_base_obj.postprocess(model_vars)
    return model_vars

def valid_config_callback(model_vars: dict) -> bool:
    return graph_test_base_obj.valid_config_callback(model_vars)
```

Creator of `xyz.py` module is free to add any specific constraint for the XYZ case. It may also override other methods of **GraphTestBase** such as `constrain_input_queue_size`, `constrain_output_queue_size`, `constrain_parameter_queue` and others if needed. Or it can leave the default behaviour if it is suitable for XYZ graph. Bottom line is that creator of `xyz.py` module is free to tweak any constraint as long as it follows limitations of the device.

### Instructions

When adding constraints to the Solver, creator of derived module can access any class member of **GraphTestBase** and use it in a constraint. Let's say for example that user wants to limit operation given by name `xyz_matmul_op` to be placed in the first row:
```
op = self.nodes["xyz_matmul_op"]
self.solver.add(op.grid_loc_y == 0)
```

User may want to use higher precision data formats for large inputs. That can be constrained in the following way:
```
HIGH_PRECISION_INPUT_SIZE_C = ... # threshold for large input size

input_q = self.nodes["input_queue_name"]
self.solver.add(
    Implies(input_q.get_size_c_var() >= HIGH_PRECISION_INPUT_SIZE_C,
            self.data_format >= DataFormat.Float16.value))
```

For more details on how to use Z3 package in Python see this [guide](https://ericpony.github.io/z3py-tutorial/guide-examples.htm).

In a large graph it can be tricky for Z3 Solver to find a solution in reasonable time, so derived module can make some constraints that help Solver. For example, operations that perform scalar broadcast will always fit one core and their locations can be fixed in the first row, so it basically can shrink the Solver's search space.

Functions `constraint_model, extra_config_callback, valid_config_callback` describe API for the end-2-end testing framework. This API is sufficient for the test generator driver `z3_config_generator.py` to work well, but it can be extended and used in other places too.

One thing this framework does not take care of is stimulus-config. It is user's responsibility to add appropriate stimulus-config in the template netlist and create template variable in it if needed. For example, in `verif/template_netlist/templates/test_add_and_norm_v2.template.yaml` stimulus-config contains constants for reduce mean operation, which depends on input size. Since input size is computed by Solver during runtime, these constants are converted to template variable and handled in `extra_config_callback` of `test_add_and_norm_v2.py` module.

`sweep_var_names` is a list of variables that are meant to be sweeped - in other words force Solver to generate solutions with every possible value for vars in this list. Detailed description how this works may be found in `z3_config_generator.py`.

When deriving new test modules, user is kindly asked to follow [Google style guide](https://google.github.io/styleguide/pyguide.html).

## [4] Generating random netlists

When `xyz.py` module is in place, one can generate netlists out of it in few ways.

`generate_tests.py` script will generate netlists and store them in wanted place.
```
$ cd budabackend/verif/template_netlist
$ python generate_tests.py --module-name test_modules.graph_tests.xyz --output-dir <where to put generated files> --max-num-configs <number of configs to generate> --dry-run --dump-config-yaml
```

After generating netlists, one can run a local test (on a lab machine) for each of them. By default, soft reset is used to reset cards. In order to accommodate for multi-chip machines, `--card-id` argument is
mandatory (so that reset affects only intentionally reserved card).
```
$ cd budabackend
$ ./verif/graph_tests/run_tests.sh --dir=<path to generated netlists> --arch=<grayskull|wormhole> --card-id=<reserved-card-id>>
```

If, for whatever reason, one wants to use reset dongle to reset reserved card, `--hard-reset` argument must be passed like:
```
$ cd budabackend
$ ./verif/graph_tests/run_tests.sh --dir=<path to generated netlists> --arch=<grayskull|wormhole> --hard-reset>
```
**Take care** that this will reset the entire card, so it will affect users of other chips on card!

The other way of generating netlists is using `generate_ci_tests.py` script which generates random netlists and shell scripts for each netlist. These shell scripts just add more utility for the CI by printing netlist content during the run, so that debugging of failures is possible.
```
$ cd budabackend/verif/template_netlist
$ python generate_ci_tests.py --test-module test_modules.graph_tests.xyz --output-dir <where to put generated files> --num-tests <number of tests to generate>
```

Another way of generating netlists is to use API of `xyz.py` and create a new script around it. This option is left to user to explore.

## Supported graphs

List of currently supported graphs:
* Feedforward
* Add & Norm
* Multi Head Attention


