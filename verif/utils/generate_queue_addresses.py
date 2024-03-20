# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys

## TODO(snijjar): make this entry size aware
def generate_pattern(start, total_entries, num_dram_channels, increment):
    result = []
    for i in range(total_entries):
        first = i % num_dram_channels
        second = start + (i // num_dram_channels) * increment
        result.append(f"[{first}, 0x{second:08X}]")
    return result

def print_pattern(pattern):
    line_len = 0
    entries_on_line = 0
    for i, entry in enumerate(pattern):
        if i > 0:
            print(", ", end="")
            line_len += 2
        if line_len + len(entry) > 120 or entries_on_line > 6:
            entries_on_line = 0
            print()
            line_len = 0
        print(entry, end="")
        line_len += len(entry)
        entries_on_line += 1
    print()

if __name__ == '__main__':
    if len(sys.argv) != 5:
        print(f"Usage: python {sys.argv[0]} <start> <total_entries> <dram_channels> <increment>")
        sys.exit(1)
    start = int(sys.argv[1], 16)
    total_entries = int(sys.argv[2])
    num_dram_channels = int(sys.argv[3])
    increment = int(sys.argv[4], 16)
    pattern = generate_pattern(start, total_entries, num_dram_channels, increment)
    print_pattern(pattern)