#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage: python generate_netlists_from_configs.py --template-yaml <path-to-template-yaml-from-repo-root> --test-configs-folder <path-to-folder-from-repo-root> --output-dir <path-to-folder-from-repo-root>
"""
import argparse
import os

from util import get_git_root

def main():
    parser = argparse.ArgumentParser()
    
    parser.add_argument("--template-yaml", required=True, type=str, help="<path-to-template-yaml-from-repo-root>")
    parser.add_argument("--test-configs-folder", required=True, type=str, help="<path-to-folder-from-repo-root>")
    parser.add_argument("--output-dir", required=True, type=str, help="<path-to-folder-from-repo-root>")
    args = parser.parse_args()

    root = get_git_root()

    for config in os.listdir(args.test_configs_folder):
        os.system(f"python {root}/verif/template_netlist/generate_netlists.py --template-yaml {root}/{args.template_yaml} --test-configs-yaml {root}/{args.test_configs_folder}/{config} --output-dir {root}/{args.output_dir}")

if __name__=="__main__":
    main()
