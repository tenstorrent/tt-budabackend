# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from enum import Enum
from io import TextIOWrapper
from ruamel.yaml import YAML

from test_modules.common.constants import *

class VarCount(Enum):
    """
    Defines if variable value in a netlist should
    be replaced with one template key or multiple.
    """
    Single = 0
    Multiple = 1

ARCH_VAR_KEY = 'arch'

# Variables that will be templated.
VARS = {
    'entries' : VarCount.Single,
    'grid_size' : VarCount.Multiple,
    't' : VarCount.Single,
    'mblock' : VarCount.Multiple,
    'ublock' : VarCount.Multiple,
    'df' : VarCount.Single,
    'loc' : VarCount.Single,
    'dram' : VarCount.Single,
    'grid_loc' : VarCount.Multiple,
    'in_df' : VarCount.Multiple,
    'out_df' : VarCount.Single,
    'intermed_df' : VarCount.Single,
    'acc_df' : VarCount.Single,
    'input_count' : VarCount.Single,
    'm_k' : VarCount.Single,
    'u_kt' : VarCount.Single,
    'r' : VarCount.Single,
    'c' : VarCount.Single,
    'z' : VarCount.Single,
    'prologue' : VarCount.Single,
    'minibatch_count' : VarCount.Single,
    'microbatch_count' : VarCount.Single,
    'microbatch_size' : VarCount.Single,
    'untilize_output' : VarCount.Single,
    'hslice' : VarCount.Single,
    'hstack' : VarCount.Single,
    'vslice' : VarCount.Single,
    'vstack' : VarCount.Single,
    'buf_size_mb': VarCount.Single,
    'tile_broadcast': VarCount.Single,
    ARCH_VAR_KEY : VarCount.Single,
}

VARS_TEMPLATE_VALUES = {
    ARCH_VAR_KEY : ARCH_VAR_NAME
}

template_var_index = 0

def get_var_template_inc_str(var_name: str) -> str:
    """Returns template string with auto incremented index."""
    global template_var_index
    template_var_index += 1
    return f"{TEMPLATE_VAR_PREFIX}{var_name}_var_{template_var_index}"

def get_var_template_str(var_name: str) -> str:
    """
    Returns template string for given variable.

    If template string value for given variable is specified in
    `VARS_TEMPLATE_VALUES` it will be used, otherwise auto incremented
    index will be used.

    Parameters
    ----------
    var_name: str
        Name of the variable.
    """
    if var_name in VARS_TEMPLATE_VALUES:
        return f"{TEMPLATE_VAR_PREFIX}{VARS_TEMPLATE_VALUES[var_name]}"
    else:
        return get_var_template_inc_str(var_name)

def load_netlist(yaml: YAML, netlist_path: str) -> dict:
    """Loads netlist yaml and returns its dictionary.

    Parameters
    ----------
    yaml: YAML
        Yaml reader.
    netlist_path: str
        Path to the netlist file.

    Returns
    -------
    dict
    """
    with open(netlist_path, 'r') as netlist_yaml_file:
        return yaml.load(netlist_yaml_file)

def change_template_params(netlist_object: object):
    """Goes recursively through the netlist object and its children
    and changes values that correspond to variables defined in 'VARS'
    dict with generated template keys.

    Parameters
    ----------
    netlist_object: object
        Netlist object, can be either a dictionary or a list.
    """
    if isinstance(netlist_object, dict):
        for node_name, node_content in netlist_object.items():
            if node_name in VARS:
                var_count = VARS[node_name]
                if var_count == VarCount.Single:
                    netlist_object[node_name] = get_var_template_str(node_name)
                else:
                    netlist_object[node_name] = [
                        get_var_template_inc_str(f"{node_name}_{idx}")
                        for idx in range(len(node_content))
                    ]
            else:
                change_template_params(node_content)
    elif isinstance(netlist_object, list):
        for list_element in netlist_object:
            change_template_params(list_element)

def remove_from_dict(dict_key: str, var_dict: dict):
    """Removes item with given key, if it exists in the dictionary.

    Parameters
    ----------
    dict_key: str
        Key of item to remove from dictionary.
    var_dict: dict
        Dictionary from which to remove the item.
    """
    if dict_key in var_dict.keys():
        var_dict.pop(dict_key)

def fix_queue_locations(netlist: dict):
    """Replaces queue location item keys in the netlist template
    with generated template strings.

    Parameters
    ----------
    netlist: dict
        Netlist yaml dictionary.
    """
    for _, queue_dict in netlist['queues'].items():
        location = queue_dict['loc']
        remove_from_dict('dram', queue_dict)
        remove_from_dict('host', queue_dict)
        queue_dict[location] = get_var_template_inc_str('loc')

def is_constant_queue(queue_name: str) -> bool:
    """Returns true if queue with a given name is a constant queue.

    Parameters
    ----------
    queue_name: str
        Queue name.

    Returns
    -------
    bool
    """
    return (queue_name.startswith("constant_") or
            queue_name.startswith("lc.") or
            queue_name.startswith("input_constant_"))

def add_constant_queue_broadcast(op_params: dict, input_index: int):
    """Adds templated broadcast attribute to op, for a given op input
    corresponding to a constant queue.

    Parameters
    ----------
    op_params: dict
        Op parameters dictionary.
    input_index: int
        Op input index.
    """
    input_tms_key = f"input_{input_index}_tms"
    if not input_tms_key in op_params:
        op_params[input_tms_key] = []

    for tm_dict in op_params[input_tms_key]:
        tm_name, tm_params = next(iter(tm_dict.items()))
        if tm_name == "broadcast" and "z" in tm_params:
            # Template should be already added
            return

    brcst_z_dict = {'broadcast': {'z': get_var_template_inc_str('z')}}
    op_params[input_tms_key].append(brcst_z_dict)

def add_constant_queues_broadcast(netlist: dict):
    """Adds templated broadcast attribute to ops having constant queues
    on their input.

    We need to add broadcast by z template for constant queues to be able
    to define constraints for optimization of L1 space for their t dimension.

    Parameters
    ----------
    netlist: dict
        Netlist yaml dictionary.
    """
    for _, graph_dict in netlist["graphs"].items():
        for _, op_params in graph_dict.items():
            if not isinstance(op_params, dict):
                # Skipping graph attributes
                continue

            for i in range(len(op_params['inputs'])):
                if is_constant_queue(op_params['inputs'][i]):
                    add_constant_queue_broadcast(op_params, i)

def add_untilize_output(netlist: dict):
    """Adds templated 'untilize_output' attribute to output ops.

    Parameters
    ----------
    netlist: dict
        Netlist yaml dictionary.
    """
    output_op_names = set()
    for q_name, queue_dict in netlist["queues"].items():
        q_input = queue_dict['input']
        if is_output_queue(q_name, netlist):
            output_op_names.add(q_input)

    for _, graph_dict in netlist["graphs"].items():
        for op_name, op_params in graph_dict.items():
            if op_name not in output_op_names or not isinstance(op_params, dict):
                continue
            if 'untilize_output' not in op_params:
                op_params['untilize_output'] = \
                    get_var_template_inc_str('untilize_output')

def add_test_config(netlist: dict):
    """Adds templated test-config attributes to netlist.

    Parameters
    ----------
    netlist: dict
        Netlist yaml dictionary.
    """
    test_config_key = 'test-config'
    if not test_config_key in netlist:
        netlist[test_config_key] = {}

    test_args_key = 'test-args'
    if not test_args_key in netlist[test_config_key]:
        netlist[test_config_key][test_args_key] = {}

    minibatch_count_key = 'minibatch_count'
    if not minibatch_count_key in netlist[test_config_key][test_args_key]:
        netlist[test_config_key][test_args_key][minibatch_count_key] = \
            get_var_template_inc_str(minibatch_count_key)

    microbatch_count_key = 'microbatch_count'
    if not microbatch_count_key in netlist[test_config_key][test_args_key]:
        netlist[test_config_key][test_args_key][microbatch_count_key] = \
            get_var_template_inc_str(microbatch_count_key)

    microbatch_size_key = 'microbatch_size'
    if not microbatch_size_key in netlist[test_config_key][test_args_key]:
        netlist[test_config_key][test_args_key][microbatch_size_key] = \
            get_var_template_inc_str(microbatch_size_key)

def is_input_queue(q_name: str, q_dict: dict) -> bool:
    """Returns true if queue with a given name is an input queue.

    Parameters
    ----------
    q_name: str
        Queue name.
    q_dict: dict
        Queue parameters dictionary.

    Returns
    -------
    bool
    """
    return (q_dict['input'] == "HOST" and q_dict['type'] == "queue" and
            not q_name.startswith("constant_") and not q_name.startswith("lc."))

def is_output_queue(q_name: str, netlist: dict) -> bool:
    """Returns true if queue with a given name is an output queue.

    Output queue does not feed any operation in the netlist.

    Parameters
    ----------
    q_name: str
        Queue name.
    netlist: dict
        Netlist yaml dictionary.
    """
    for _, graph_dict in netlist["graphs"].items():
        for _, op_params in graph_dict.items():
            if not isinstance(op_params, dict):
                continue
            if q_name in op_params['inputs']:
                return False
    return True

def write_template_header(netlist: dict, output_yaml_file: TextIOWrapper):
    """Writes header comment with some templated variables to the
    template netlist.

    Parameters
    ----------
    netlist: dict
        Netlist yaml dictionary.
    output_yaml_file: TextIOWrapper
        Template netlist file writer.
    """
    output_yaml_file.write(f"# test_config_id={TEMPLATE_VAR_PREFIX}test_config_id\n")

    for q_name, q_dict in netlist["queues"].items():
        if is_input_queue(q_name, q_dict):
            output_yaml_file.write(f"# {q_name}_{SIZE_R_VAR_NAME}={TEMPLATE_VAR_PREFIX}{q_name}_{SIZE_R_VAR_NAME}\n")
            output_yaml_file.write(f"# {q_name}_{SIZE_C_VAR_NAME}={TEMPLATE_VAR_PREFIX}{q_name}_{SIZE_C_VAR_NAME}\n")
    output_yaml_file.write("\n")

def save_netlist_template(yaml: YAML, netlist: dict, template_path: str):
    """Saves netlist template to file at a given path.

    Parameters
    ----------
    yaml: YAML
        Yaml writer.
    netlist: dict
        Netlist yaml dictionary.
    template_path: str
        Path to the output netlist template file.
    """
    with open(template_path, 'w') as output_yaml_file:
        write_template_header(netlist, output_yaml_file)
        yaml.dump(netlist, output_yaml_file)


if __name__ == "__main__":
    """Reads netlist from the given path and replaces variables
    from VARS list with generated template strings, to be used
    with graph tests generator.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("--netlist-path", required=True, type=str)
    parser.add_argument("--template-path", required=True, type=str)
    args = parser.parse_args()

    yaml = YAML(typ='rt')
    yaml.width = 200

    netlist = load_netlist(yaml, args.netlist_path)

    change_template_params(netlist)
    fix_queue_locations(netlist)
    add_constant_queues_broadcast(netlist)
    add_untilize_output(netlist)
    add_test_config(netlist)
    save_netlist_template(yaml, netlist, args.template_path)