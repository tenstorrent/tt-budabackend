#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os
import argparse
import importlib
import random
import re

from util import *

def remove_args_from_tms(input_tms: str):
    updated_tms = re.sub('broadcast: {z: ', 'broadcast_z: {', input_tms)
    updated_tms = re.sub('broadcast: {r: ', 'broadcast_r: {', updated_tms)
    updated_tms = re.sub('broadcast: {c: ', 'broadcast_c: {', updated_tms)
    updated_tms = re.sub(': {[0-9]+},', ',', updated_tms)
    updated_tms = re.sub(': {[0-9]+}]', '', updated_tms)
    updated_tms = re.sub('\[|\]', '', updated_tms)
    return updated_tms

def shorten_tms_and_add_to_dict(configs: dict, tms_key: str, tms_short_key: str):
    for cfg in configs:
        tms = cfg.get(tms_key)

        if (tms is not None):
            tms_names = remove_args_from_tms(tms)
            cfg[tms_short_key] = tms_names

    return

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--test_configs_yaml", required=True, type=str)

    args = parser.parse_args()

    # get abs path to yaml file
    git_root = get_git_root()
    test_configs_file = f"{git_root}/{args.test_configs_yaml}"

    # load yaml file
    configs = load_yaml_test_configs(test_configs_file)

    if (configs is not None):
        # do custom processing before printing anything
        shorten_tms_and_add_to_dict(configs, 'input0_tms', 'input0_tms_short')
        shorten_tms_and_add_to_dict(configs, 'input1_tms', 'input1_tms_short')
#        for cfg in configs:
#            tms0 = cfg.get('input0_tms')
#            tms1 = cfg.get('input1_tms')
#
#            if (tms0 is not None):
#                tms_names = remove_args_from_tms(tms0)
#                cfg['input0_tms_short'] = tms_names
#
#            if (tms1 is not None):
#                tms_names = remove_args_from_tms(tms1)
#                cfg['input1_tms_short'] = tms_names


        # print header
        key_cnt = 0
        for key in configs[0]:
            if (key_cnt > 0): print(f'|', end='')
            print (f'{key}', end = '')
            key_cnt = key_cnt + 1
        print("")

        # print values
        for cfg in configs:
            val_cnt = 0
            for (key,val) in cfg.items():
                if (val_cnt > 0): print(f'|', end='')
                print (f"{val}", end = '')
                val_cnt = val_cnt + 1

            print("")


    else:
        print (f"Provided list of test configs {test_configs_file} is empty\n")
        return


if __name__=="__main__":
    main()
