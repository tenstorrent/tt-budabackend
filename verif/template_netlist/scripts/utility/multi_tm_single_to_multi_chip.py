# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from itertools import repeat
import multiprocessing as mp
import os
from typing import Dict, Iterator, List, Union

import yaml

from util import create_netlist_from_single_config


CHIP_ID_FIELD = "target_device"
PRODUCER_CHIP_ID = 0
CONSUMER_CHIP_ID = 1
PRODUCER_GRAPH_NAME = f"graph_producer_chip_{PRODUCER_CHIP_ID}"
CONSUMER_GRAPH_NAME = f"graph_consumer_chip_{CONSUMER_CHIP_ID}"


def read_netlist_paths_from_test_dir(test_dir: str) -> Iterator[str]:
    for test_netlist in os.listdir(test_dir):
        # skip logs dir and anything which is not a directory
        dir_path = os.path.join(test_dir, test_netlist)
        if not os.path.isdir(dir_path) or test_netlist == "logs":
            continue
        
        netlist_id = test_netlist.split("_")[1]
        netlist_path = os.path.join(dir_path, f"netlist_{netlist_id}.yaml")
        yield netlist_path


def convert_netlist(single_chip_netlist: Dict) -> Dict:
    multi_chip_netlist = {}
    test_ops = set()
    # copy fields
    multi_chip_netlist['devices'] = single_chip_netlist['devices']
    multi_chip_netlist['queues'] = single_chip_netlist['queues']
    multi_chip_netlist['test-config'] = single_chip_netlist['test-config']

    # parse ops
    multichip_graphs = {
        PRODUCER_GRAPH_NAME: {
            CHIP_ID_FIELD: PRODUCER_CHIP_ID
        },
        CONSUMER_GRAPH_NAME: {
            CHIP_ID_FIELD: CONSUMER_CHIP_ID
        }
    }
    for g_name, g_dict in single_chip_netlist['graphs'].items():
        input_count = g_dict['input_count']
        multichip_graphs[CONSUMER_GRAPH_NAME]['input_count'] = input_count
        multichip_graphs[PRODUCER_GRAPH_NAME]['input_count'] = input_count
        for op_name, op_params in g_dict.items():
            if not isinstance(op_params, dict):
                continue
            if 'input_0_tms' in op_params:
                test_ops.add(op_name)
                multichip_graphs[CONSUMER_GRAPH_NAME][op_name] = op_params
            else:
                multichip_graphs[PRODUCER_GRAPH_NAME][op_name] = op_params

    multi_chip_netlist['graphs'] = multichip_graphs

    # parse queues
    queue_to_target_device = {}
    for q_name, q_dict in single_chip_netlist['queues'].items():
        target_device = PRODUCER_CHIP_ID
        if any(x in test_ops for x in q_dict['input']):
            target_device = CONSUMER_CHIP_ID
        multi_chip_netlist['queues'][q_name][CHIP_ID_FIELD] = target_device
        queue_to_target_device[q_name] = target_device

    # prase programs
    multi_chip_netlist['programs'] = []
    for program in single_chip_netlist['programs']:
        for p_name, p_dict in program.items():
            program = {
                p_name: []
            }
            for single_dict in p_dict:
                if "execute" in single_dict:
                    queue_settings = single_dict['execute']['queue_settings']
                    # execute instruction for producer queue
                    producer_execute = {
                        "execute": {
                            "graph_name": PRODUCER_GRAPH_NAME,
                            "queue_settings": {

                            }
                        }
                    }
                    # execute instruction for consumer queue
                    consumer_execute = {
                        "execute": {
                            "graph_name": CONSUMER_GRAPH_NAME,
                            "queue_settings": {

                            }
                        }
                    }
                    for q_name, q_setting in queue_settings.items():
                        if queue_to_target_device[q_name] == PRODUCER_CHIP_ID:
                            producer_execute['execute']['queue_settings'][q_name] = q_setting
                        else:
                            consumer_execute['execute']['queue_settings'][q_name] = q_setting
                    program[p_name].append(producer_execute)
                    program[p_name].append(consumer_execute)
                else:
                    program[p_name].append(single_dict)
            multi_chip_netlist['programs'].append(program)
    return multi_chip_netlist


def fix_dram_buffers(buffers: List) -> str:
    return "[" + ", ".join([f"[{channel}, {hex(addr)}]" for (channel, addr) in buffers]) + "]"


def fix_tms(tms: List) -> str:
    def _format_tm(tm: Union[Dict, str]) -> str:
        # transpose
        if isinstance(tm, str):
            return tm

        for tm_name, tm_factor in tm.items():
            if (not isinstance(tm_factor, int)):
                for tm_factor_name, tm_factor_value in tm_factor.items():
                    if tm_name == "broadcast":
                        return f"broadcast: {{{tm_factor_name}: {tm_factor_value}}}"
                    else:
                        return f"{tm_name}: {{{tm_factor_name}}}"
            else:
                return f"{tm_name}: {tm_factor}"

    return "[" + ", ".join([_format_tm(tm) for tm in tms]) + "]"


def extract_model_vars_from_netlist(netlist_dict: Dict, template_yaml: str) -> Dict:
    with open(template_yaml, "r") as template_file:
        template_dict = yaml.safe_load(template_file.read())

    config = {}
    def _dfs(netlist, template):
        # tms and dram locations
        if isinstance(template, str):
            template_key = template.lstrip("$TEMPLATE_")
            netlist_key = netlist
            if "dram_buffers" in template_key:
                netlist_key = fix_dram_buffers(netlist)
            elif "_tms" in template_key:
                netlist_key = fix_tms(netlist)
            config[template_key] = netlist_key
            return          
        elif isinstance(template, list):
            for idx, key in enumerate(template):
                _dfs(netlist[idx], key)
        elif isinstance(template, dict):
            for key in template:
                # if key is a template, we take the real key from the config dictionary
                if (key.startswith("$TEMPLATE_")):
                    config_key = key.lstrip("$TEMPLATE_")
                    _dfs(netlist[config[config_key]], template[key])
                else:
                    _dfs(netlist[key], template[key])

    _dfs(netlist_dict, template_dict)
    return config


def write_output_netlist(multi_chip_netlist: Dict, netlist_path: str, output_dir: str, template_yaml: str) -> None:
    config = extract_model_vars_from_netlist(multi_chip_netlist, template_yaml)
    test_name = os.path.basename(os.path.dirname(netlist_path))
    create_netlist_from_single_config(
        template_yaml=template_yaml,
        output_dir=output_dir,
        config=config,
        test_id=test_name.lstrip("test_")
    )


def run_convert_netlist_worker(netlist_path: str, output_dir: str, template_yaml: str) -> None:
    with open(netlist_path, "r") as file:
        single_chip_netlist = yaml.safe_load(file.read())
    multi_chip_netlist = convert_netlist(single_chip_netlist)
    write_output_netlist(multi_chip_netlist, netlist_path, output_dir, template_yaml)


def convert_multi_tm_single_to_multi_chip(test_dir: str, output_dir: str, template_yaml: str) -> None:
    os.makedirs(output_dir, exist_ok=True)
    with mp.Pool() as pool:
        pool.starmap(
            run_convert_netlist_worker,
            zip(
                read_netlist_paths_from_test_dir(test_dir),
                repeat(output_dir),
                repeat(template_yaml)
            )
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test-dir", required=True, type=str)
    parser.add_argument("--template-yaml", default="verif/template_netlist/templates/test_datacopy_datacopy_multiple_tms_and_reblock_multichip.template.yaml", type=str)
    parser.add_argument("--output-dir", default=None, type=str)
    args = parser.parse_args()

    output_dir = args.output_dir or f"{args.test_dir}_multichip"

    convert_multi_tm_single_to_multi_chip(args.test_dir, output_dir, args.template_yaml)


if __name__ == "__main__":
    main()
