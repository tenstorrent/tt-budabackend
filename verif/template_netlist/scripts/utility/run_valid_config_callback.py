# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import yaml
import importlib
from z3 import *

from util import get_git_root

def run_valid_config_callback(module_name: str, test_configs_yaml: str):
    repo_root = get_git_root()

    with open(f"{repo_root}/{test_configs_yaml}", "r") as f:
        model_vars_list = yaml.safe_load(f.read())

    if not isinstance(model_vars_list, list):
        model_vars_list = [model_vars_list]

    for model_vars in model_vars_list:
        # Had to add it since it is missing from model_vars and is needed for resource
        # constraints
        model_vars["brcast_var"] = 1

        test_module = importlib.import_module(module_name)
        test_module.constraint_model(Solver(), {}, model_vars["arch_name"])
        valid_config_callback = getattr(test_module, 'valid_config_callback', None)

        print(f"Running test config id: {model_vars['test_config_id']}")
        if valid_config_callback:
            valid_config_callback(model_vars, True)

def main():
    parser = argparse.ArgumentParser(description="")
    parser.add_argument('--module-name', required=True, help="Test Module Name")
    parser.add_argument('--test-config-yaml', required=True, help="Test configs yaml")
    args = parser.parse_args()

    run_valid_config_callback(args.module_name, args.test_config_yaml)

if __name__ == "__main__":
    main()