#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage: python generate_netlists_from_configs.py --template-yaml <path-to-template-yaml-from-repo-root> --test-configs-folder <path-to-folder-from-repo-root> --output-dir <path-to-folder-from-repo-root>
"""
import argparse
import os
from generate_netlists import generate_netlists

from util import get_git_root


def generate_netlists_from_configs(template_yaml, test_configs_folder, output_dir):
    root = get_git_root()
    netlist_test_infos = []
    for config in os.listdir(os.path.join(root, test_configs_folder)):
        netlist_test_info = generate_netlists(
            template_yaml=os.path.join(root, template_yaml),
            test_configs_yaml=os.path.join(root, test_configs_folder, config),
            output_dir=os.path.join(root, output_dir),
        )
        netlist_test_infos.append(netlist_test_info)
    return netlist_test_infos


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--template-yaml", required=True, type=str, help="<path-to-template-yaml-from-repo-root>"
    )
    parser.add_argument(
        "--test-configs-folder", required=True, type=str, help="<path-to-folder-from-repo-root>"
    )
    parser.add_argument(
        "--output-dir", required=True, type=str, help="<path-to-folder-from-repo-root>"
    )
    args = parser.parse_args()
    generate_netlists_from_configs(args.template_yaml, args.test_configs_folder, args.output_dir)


if __name__ == "__main__":
    main()
