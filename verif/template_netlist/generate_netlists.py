#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from util import *
import argparse


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--template-yaml",     required=True, type=str)
    parser.add_argument("--test-configs-yaml",  required=True, type=str)
    parser.add_argument("--output-dir",         required=True, type=str)
    parser.add_argument("--determinism-tests", default=False, action = "store_true") 
    parser.add_argument("--num-loops", default=1, type=int) 
    parser.add_argument("--use-hash", default=1, type=int) 
    args = parser.parse_args()

    configs = load_yaml_test_configs(args.test_configs_yaml)
    netlist_test_info = create_netlists(template_yaml=args.template_yaml, output_dir=args.output_dir, configs=configs, use_shared_dir=False, use_hash=args.use_hash)
    print(generate_test_commands(netlist_test_info, determinism_tests = args.determinism_tests, num_loops = args.num_loops))

if __name__=="__main__":
    main()
