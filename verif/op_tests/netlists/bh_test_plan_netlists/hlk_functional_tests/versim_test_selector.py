#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
#

import os
import random
import shutil
import argparse

def select_random_yaml_files(source_dir, destination_dir, num_files):
    # List all files in the source directory
    files = [f for f in os.listdir(source_dir) if f.endswith('.yaml')]
    
    # Select num_files random files
    selected_files = random.sample(files, min(num_files, len(files)))

    # Remove destination dir from previous run
    if os.path.exists(destination_dir):
        shutil.rmtree(destination_dir)

    # Create clean destination directory
    os.makedirs(destination_dir)

    # Copy the selected files to the destination directory
    for i, file_name in enumerate(selected_files):
        test_dir = os.path.join(destination_dir, f"test{i}")
        if not os.path.exists(test_dir):
            os.makedirs(test_dir)
        shutil.copy(os.path.join(source_dir, file_name), os.path.join(test_dir, file_name))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Select and copy random YAML files from a source directory to a destination directory.')
    parser.add_argument('--input_dir_path', required=True, help='Path to the source directory')
    parser.add_argument('--output_dir_path', required=True, help='Path to the destination directory')
    parser.add_argument('--num_tests_to_select', required=True, help='How many tests to select')
    args = parser.parse_args()

    input_dir_path = args.input_dir_path
    output_dir_path = args.output_dir_path

    select_random_yaml_files(input_dir_path, output_dir_path, int(args.num_tests_to_select))