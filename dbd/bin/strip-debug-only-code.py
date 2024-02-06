#!/usr/bin/env python3
"""
  Usage: strip-debug-only-code.py <folder>
"""
from docopt import docopt
from tabulate import tabulate
import os

def process_file(file_path, is_debug):
    with open(file_path, 'r') as f:
        lines = f.readlines()

    new_lines = []
    inside_debug_block = False
    lines_removed = 0

    for line in lines:
        if line.strip() == "# __DEBUG_ONLY__":
            inside_debug_block = True
            if not is_debug:
                lines_removed += 1
                continue
        elif line.strip() == "# __END_DEBUG_ONLY__":
            inside_debug_block = False
            if not is_debug:
                lines_removed += 1
                continue

        if inside_debug_block and not is_debug:
            lines_removed += 1
            continue
        new_lines.append(line)

    with open(file_path, 'w') as f:
        f.writelines(new_lines)

    return lines_removed

def traverse_dir(root_folder, is_debug):
    summary = []
    for root, _, files in os.walk(root_folder):
        for file in files:
            if file.endswith(('.txt', '.md', '.py', '.sh', '.json')):
                full_path = os.path.join(root, file)
                lines_removed = process_file(full_path, is_debug)
                if lines_removed > 0:
                    summary.append([full_path, lines_removed])

    print(tabulate(summary, headers=['File', 'Debug-only Lines Removed']))

if __name__ == "__main__":
    args = docopt(__doc__)
    is_debug = os.environ.get('CONFIG') == 'debug'
    assert not is_debug, "This script should only be run in non-debug mode"
    traverse_dir(args['<folder>'], is_debug)
