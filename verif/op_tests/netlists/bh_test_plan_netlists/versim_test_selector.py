#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
#

import os
import random
import shutil
import argparse

def select_random_yaml_files(source_dir, destination_dir, num_files):
    def has_skip_comment(file_path):
        with open(file_path, 'r') as file:
            first_line = file.readline().strip()
            return first_line.startswith('#SKIP')

    # List all .yaml files recursively in the source directory
    files = []
    for root, _, filenames in os.walk(source_dir):
        for filename in filenames:
            if filename.endswith('.yaml'):
                file_path = os.path.join(root, filename)
                if not has_skip_comment(file_path):
                    files.append(file_path)
    
    # Select num_files random files
    selected_files = random.sample(files, min(num_files, len(files)))

    # Remove destination dir from previous run
    if os.path.exists(destination_dir):
        shutil.rmtree(destination_dir)

    # Create clean destination directory
    os.makedirs(destination_dir)

    # Copy the selected files to the destination directory
    for i, file_path in enumerate(selected_files):
        test_dir = os.path.join(destination_dir, f"test{i}")
        if not os.path.exists(test_dir):
            os.makedirs(test_dir)
        shutil.copy(file_path, os.path.join(test_dir, os.path.basename(file_path)))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Select and copy random YAML files from a source directory to a destination directory.')
    parser.add_argument('--input_dir_path', required=True, help='Path to the source directory')
    parser.add_argument('--output_dir_path', required=True, help='Path to the destination directory')
    parser.add_argument('--num_tests_to_select', required=True, help='How many tests to select')
    args = parser.parse_args()

    input_dir_path = args.input_dir_path
    output_dir_path = args.output_dir_path

    select_random_yaml_files(input_dir_path, output_dir_path, int(args.num_tests_to_select))