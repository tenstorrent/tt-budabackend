#!/usr/bin/python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

# A quickish script that aims to extract ops from a graph level run and create standalone-op tests

import argparse
from lib2to3.pytree import BasePattern
import sys
import os, yaml, json, re, subprocess
import pprint
from pathlib import Path
from collections import OrderedDict

ROOT = str(Path(__file__).parent.absolute()) + "/../.."
DEBUG = 0
OP_TEST_YAMLS_DIR = f"{ROOT}/model/tests/op_tests/op_yamls"
pp = pprint.PrettyPrinter(indent=4)

# Various things TODO
# [-] Error checking, return codes
# [-] Testing op other than matmul
# [-] Cleanup of script
# [-] More configuration from command line, perhaps incorporate this into aother flow to automate extract+running.

scope_prefixes = {  "tensor_type": "TensorType::",
                    "data_format": "tt::DataFormat::",
                    "io_type": "tensor_io_type::",
                    "math_fidelity": "MathFidelity::",
                    "reduce_func": "ReduceFunc::",
                    "dim": "Dim::",
                    "manual_batch_size_dim_to_modify": "Dim::"
}

# Parse and decode relavent input/operand operands from input_tensors/output_tensors in gstate, and return in format for test.
def set_input_output_operands(state, op_args):

    name = op_args["name"]

    # Order from test .yaml files
    metadata_config_order = ["data_format", "tensor_type", "requires_grad", "grid_shape", "block_shape", "shape" ]

    # Named input_tensors in gstate, but operands in test .yaml
    tensor_direction_to_test_key = {"input_tensors" : "operands", "output_tensors" : "outputs"}

    test = {}

    # Create nested dicts
    for test_key in tensor_direction_to_test_key.values():
        test[test_key] = {}
        test[test_key] = {}

    for tensor_direction, test_key in tensor_direction_to_test_key.items():

        tensor_list = op_args[tensor_direction]

        # Go through inputs operands
        for tensor_idx in tensor_list:

            tensor_idx_int = int(tensor_idx)

            # Create nested dicts
            if tensor_idx_int not in test[test_key]:
                test[test_key][tensor_idx_int] = {}

            tensor = tensor_list[tensor_idx]

            # Handle special io_type field. OP gives feeders/drainers, DRAM does not. Use tensor_type to derive this.
            tensor_type_val = massage_tensor_metadata("tensor_type", tensor)
            if tensor_type_val in ["TensorType::Activation", "TensorType::Constant"]:
                io_type = "OP"
            elif tensor_type_val in ["TensorType::Parameter"]:
                io_type = "DRAM"
            else:
                assert False , f"unexpected tensor_type {tensor['tensor_type']} for op name: {name}"

            test[test_key][tensor_idx_int]["io_type"] = apply_scope_if_needed("io_type", io_type)

            # Go though standard set of fields that exist in gstate and test yaml
            for key in metadata_config_order:

                # Hack - ignore tensor_type:
                if key in ["tensor_type"]:
                    debug_print(2, "Warning: Skiping tensor_type, has issues if set.")
                    continue

                debug_print(2, f"For {name} tensor_direction: {tensor_direction} tensor_idx: {tensor_idx_int} applying tensor metadata key: {key} val: {tensor[key]}")
                test[test_key][tensor_idx_int][key] = massage_tensor_metadata(key, tensor)

    # Return operands, outputs separately for easy consumption.
    return [test["operands"], test["outputs"]]


def apply_scope_if_needed(field, data):
    scoped_field = f"{scope_prefixes[field]}{data}" if field in scope_prefixes else data
    return scoped_field

# Some data in gstate appears as list instead of dict and are missing scopes. Massage it before passing to test.
def massage_tensor_metadata(key, dict):

    assert key in dict , f"{key} not found in tensor data"
    data = dict[key]

    if key in ["block_shape", "grid_shape"]:
        assert len(data) == 2 , "unexpected size of block_shape or grid_shape param"
        return {'r': data[0], 'c': data[1]}
    elif key in ["shape"]:
        assert len(data) == 4 , "unexpected size of shape param"
        return {'rt': data[0], 'ct': data[1], 'z': data[2], 'w': data[3]}
    else:
        if isinstance(data, bool):
            return data
        else:
            scoped_field = apply_scope_if_needed(key, data)
            return scoped_field


# List the feeder/drainer op names for a given op 
def extract_feeder_drainer_ops(state, op_args: json):

    [op_name, op_type] = get_op_name_and_type(op_args)

    input_nodes = op_args["input_nodes"] if "input_nodes" in op_args else []
    output_nodes = op_args["output_nodes"] if "output_nodes" in op_args else []

    print(f"extract_feeder_drainer_ops() with op_name: {op_name} op_type: {op_type} inputs: {input_nodes} outputs: {output_nodes}")


def get_op_name_and_type(op_args: json):
    op_name = op_args["name"] if "name" in op_args else ""
    op_type = op_args["type"] if "type" in op_args else ""
    return [op_name, op_type]


def get_op_config(op_type: str):

    op_config_yaml_path = f"{OP_TEST_YAMLS_DIR}/{op_type}/op_config.yaml"
    op_config = yaml.safe_load(open(op_config_yaml_path))

    assert op_config , f"Could not find {op_config_yaml_path}"

    return op_config

def get_op_template_args(node: json, op_config: json):

    op_type_with_variant = node["op"]["type"]

    op_variants_list = []

    m = re.match('(tt_.*_op)<(.*)>', op_type_with_variant)
    if m:
        op_type_from_node = m.group(1)
        op_variants_str = m.group(2)
        op_variants_str = re.sub(',', ' ', op_variants_str) # Replace comma with spaces
        op_variants_list = op_variants_str.split()

        op_num_template_args = op_config['num_template_args']
        assert len(op_variants_list) == op_num_template_args , f"Number of template args found ({op_variants_list}) should be: {op_num_template_args}"

    return op_variants_list


# Find all valid ops from global state dump file and process them.
def extract_ops_from_json(data: json, state):

    # FIXME - What to do about other graphs
    graph_nodes = data["graphs"]["graph_id_0"]["nodes"]
    op_count = 1

    for node_name, node in graph_nodes.items():

        # Only process ops with "op_model"
        if "op_model" not in node:
            continue

        [op_name, op_type] = get_op_name_and_type(node["op_model"]["op_args"])

        # Just list the ops, but don't generate anything
        if state.list_all_ops:
            print(f"Found op %-3d op_type: %-40s op_name: %-20s" % (op_count, op_type, op_name))
            if state.dump_node:
                pp.pprint(node)
        else:

            # Just extract the specific ops being targetted
            if (not state.all_ops and state.op_name and op_name not in state.op_name):
                continue
            if (not state.all_ops and state.op_type and op_type not in state.op_type):
                continue

            # If multi-encoder test, just extract encoder0 ops.
            if "encoder" in node_name and "encoder_0" not in node_name:
                continue

            print(f"\nGoing to extract op {op_count} name: {op_name} op_type: {op_type} and create tests.")

            # Informational purposes for now, maybe use later somehow.
            # extract_feeder_drainer_ops(state, node)

            # Generate test contents
            generate_test_config(state, node)

        op_count += 1


# Take test contents and write or inject into test .yaml file in appropriate hierarchy.
def write_test_yaml(state, testname: str, test: json, op_type: str):

    # Find correct directory
    output_dir = f"{OP_TEST_YAMLS_DIR}/{op_type}"

    if (not os.path.exists(output_dir) and not os.path.isdir(output_dir)):
        os.mkdir(output_dir)

    output_yaml_name = state.output_yaml_name

    # Add .yaml suffix in case person forgot.
    if not output_yaml_name.endswith(".yaml"):
        output_yaml_name += ".yaml"

    # Automatically add test_ prefix unless user did it.
    if not output_yaml_name.startswith("test_"):
        output_yaml_name = "test_" + output_yaml_name

    output_yaml_file_path = f"{output_dir}/{output_yaml_name}"

    # Either write yaml_out to new file, or add test to existing file.
    if (os.path.exists(output_yaml_file_path) and os.path.isfile(output_yaml_file_path)):

        existing_yaml = yaml.safe_load(open(output_yaml_file_path)) or {}

        # Add new or straight up replace existing test. But preserve some sections like perf-config
        existing_perf_config = {}

        if testname in existing_yaml:
            debug_print(1, f"INFO: {testname} already existed in {output_yaml_file_path}, replacing.")
            existing_perf_config = existing_yaml[testname]["perf-config"] if "perf-config" in existing_yaml[testname] else {}
        else:
            debug_print(1, f"INFO: {testname} did not already exist in {output_yaml_file_path}, adding.")

        yaml_out = existing_yaml
        yaml_out[testname] = test

        if existing_perf_config:
            yaml_out[testname]['perf-config'] = existing_perf_config

        # Update perf-test field with whatever was specified on command line here.
        yaml_out[testname]['perf-config']['perf-test'] = state.perf_test

        # Write the updated output file
        with open(output_yaml_file_path, 'w') as f:
            print(f"INFO: Writing to {output_yaml_file_path}")
            data = yaml.dump(yaml_out, f, sort_keys=False, indent=4)

    else:

        debug_print(1, f"INFO: {output_yaml_file_path} did not exist, writing new file")

        # Add test under tests hierarchy
        yaml_out = {testname : test}

        with open(output_yaml_file_path, 'w') as f:
            print(f"INFO: Writing to {output_yaml_file_path}")
            data = yaml.dump(yaml_out, f, sort_keys=False, indent=4)

        
# Write out test config for python-generated tests that are specific in .yaml files
def generate_test_config(state, node: json):
    
    test = {}

    op_args = node["op_model"]["op_args"]

    [op_name, op_type] = get_op_name_and_type(op_args)

    op_specific_attributes = {}
    if "op_specific_attributes" in op_args and op_args["op_specific_attributes"] is not None:
        op_specific_attributes = op_args["op_specific_attributes"]

    # Figure out the test name from the path of the json file
    m = re.match("/.*/(\S+)/buda_reports/", state.json)
    graph_name = m.group(1) if m else ""

    perf_test = state.perf_test

    # This will be name of the test in the generated .yaml file
    testname = f"{graph_name}__{op_name}"
    testname = re.sub('[ <>,:.]', '_', testname) # Replace special characters with underscores

    print(f"INFO: creating test: {testname} (from graph_name: {graph_name} and op_name: {op_name}) perf_test: {perf_test}")

    if state.dump_node:
        pp.pprint(node)

    [core_rdim, core_cdim] = [op_args["grid_shape"][0], op_args["grid_shape"][1]]

    # Fill in the test details
    if op_type == "tt_matmul_op":
        input_epochs = 1
    else:
        input_epochs = 1

    # Set some perf-config defaults
    test["perf-config"] = { 'perf-test': perf_test,
                            'device-vs-model-en-checker' : False,
                            'device-vs-model-cycles-rtol': 0.8
                            }
    # Write the basic params
    test["input_epochs"] = input_epochs
    test["core_rdim"] = core_rdim   # Used for target_op dims
    test["core_cdim"] = core_cdim   # Used for target_op dims

    # Need to parse the op config to know about template args
    op_config           = get_op_config(op_type)
    op_variants_list    = get_op_template_args(node, op_config)

    if op_variants_list:
        test['template_args'] = op_variants_list

    # Write any op specific attributes
    for key in op_specific_attributes.keys():
        # Workaround: Most op's op_config have different var-name per attribute, so get mapping here.
        variable_name = op_config['attributes'][key] if 'attributes' in op_config and key in op_config['attributes'] else key
        test[variable_name] = apply_scope_if_needed(key, op_specific_attributes[key])

    # Write any params as specified by op_config, from op_args.
    for param in (op_config['params'] if 'params' in op_config else []):
        if op_args[param]:
            test[param] = apply_scope_if_needed(param, op_args[param])
        else:
            print(f"Warning could not find {param} in {op_name} dump")

    # Write input/output operands
    [operands, outputs] = set_input_output_operands(state, op_args)
    test["operands"] = operands
    test["outputs"] = outputs

    debug_print(1, f"DEBUG op_name: {op_name} test_operands: {operands}")
    debug_print(1, f"DEBUG op_name: {op_name} test_outputs: {outputs}")

    # Write out .yaml file to desired location with tests.
    write_test_yaml(state, testname, test, op_type)
    
    return

# Conditionally print a message if debug enabled.
def debug_print(debug_level: int, msg: str):
    if DEBUG >= debug_level:
        print(msg)

#################################################################################
# Main Program                                                                  #
#################################################################################



def main(args, extra_run_args):
    ret_code = 0
    json_file = args.json
    print(f"Going to parse {json_file}")

    if os.path.isfile(json_file):
        with open(json_file) as input_file:
            data = json.load(input_file)
            extract_ops_from_json(data, args)

    return ret_code


if __name__ == "__main__":
    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--json", type=str, default=None, help="global_state.json file to parse from graph level buda_reports folder in ~/testify typically")
    parser.add_argument("--op-name", default=[], type=str, action="append", help="Specific op name in graph to extract. Can use this flag multiple times.")
    parser.add_argument("--op-type", default=[], type=str, action="append", help="Specific op type in graph to extract. Can use this flag multiple times.")
    parser.add_argument("--all-ops", action="store_true", default=False, help="Extract all ops")
    parser.add_argument("--list-all-ops", action="store_true", default=False, help="Just list all op names and types for informational purposes.")
    parser.add_argument("-o", "--output-yaml-name", default="", type=str, help="Name of .yaml file per op for extracted tests to be written to, eg test_perf_berts.yaml. Will automatically add test_ prefix and .yaml suffix.")
    parser.add_argument("-p", "--perf-test", action="store_true", default=False, help="Mark generated tests as perf-config.perf-test:True for decoupling/measurements")
    parser.add_argument("--dump-node", action="store_true", default=False, help="Dump each node for debug purposes")

    parser.add_argument("--debug", default=0, type=int, help="Debug level verbosity")

    args, extra_run_args = parser.parse_known_args()

    DEBUG = args.debug

    if not args.json:
        assert False, "must pass -json <file>"
    if not os.path.isfile(args.json):
        assert False, f"{args.json} does not exist"

    # if not args.all_ops and not args.op_name and not args.op_type and not args.list_all_ops:
    if not (args.all_ops or args.op_name or args.op_type or args.list_all_ops):
        print("ERROR: Must use wither --all_ops or -op-name <opname> or --op-type <optype> or --list-all-ops")
        exit(1)

    assert args.list_all_ops or args.output_yaml_name , "Must supply --output-yaml-name <filename> for tests"

    ret_code = main(args, extra_run_args)
    sys.exit(ret_code)
