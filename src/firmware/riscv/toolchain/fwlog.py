#! /usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

# NOTE: This file is tightly coupled with fw_debug.h

import argparse
import re
import sys
import shlex
import os
import subprocess
from pathlib import PurePath


def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)
    sys.exit(1)


parser = argparse.ArgumentParser(
    description="Goes through dependency file (.d), parse for FWLOG/FWEVENT/etc directives and creates a fwlog file"
)
parser.add_argument(
    "--depfile",
    nargs="+",
    type=str,
    help="Where to save scripts. If missing, the script will be sent to stdout",
)
parser.add_argument(
    "--path", nargs=1, type=str, help="Search path for relative path in .dep files"
)
args = parser.parse_args()

comment_matcher = re.compile(r"//.*")  # match single line comments, block comments can be error prone to parse hence not supported
log_matcher = re.compile(r'(FWLOG_HASH|TT_LOG|TT_LLK_DUMP|TT_LOG_NB|TT_PAUSE|TT_RISC_ASSERT|TT_DUMP_ASSERT|TT_DUMP_LOG)(_ALWAYS|_NSE)?\s*\([^"]*["]([^"]*)["]')

NUM_BITS_FOR_FILE_HASH = 12
NUM_BITS_FOR_LINE_NUM = 32 - NUM_BITS_FOR_FILE_HASH
MAX_LINE_NUM = 1 << NUM_BITS_FOR_LINE_NUM

# This function must match the const_hash_file C++ function in fw_debug.h
def strhash(s):
    h = 5381
    for c in s:
        h = (h * 33 + ord(c)) % 2 ** 32
    h = h & ((1 << NUM_BITS_FOR_FILE_HASH) - 1)
    return h


all_file_list = []
all_files_str = ""

search_path = ""
if not args.path is None:
    search_path = args.path[0]


if "ROOT" in os.environ:
    root = os.environ["ROOT"]
else:
    root = subprocess.getoutput("git rev-parse --show-toplevel")

# Get the list of dependencies
all_file_list = set(
    subprocess.getoutput(
        f"grep -h {root} {' '.join(args.depfile)} | grep -v ':' | sort | sed 's/.\\\\s*$//' | sed 's/^\\s*//' | uniq "
    ).split()
)

# Then also get the first line, which needs a different parse. This is often the ckernel filename
# and may appear on second line if it is a long name, so account for that. Not a great fix.
first_line_list = subprocess.getoutput(
    f"head -q -n 2 {' '.join(args.depfile)} | sed 's/.*:.//' | sort | sed 's/.\\\\s*$//' | grep -v '^\/' | uniq "
).split()
all_file_list.update(first_line_list)

hashes_obtained = dict()

for filename in sorted(all_file_list):
    header_and_source = []
    header_and_source.append(filename)

    if filename.endswith(".h"):
        header_and_source.append(filename.replace(".h", ".cc"))
        header_and_source.append(filename.replace(".h", ".cpp"))

    for file_to_open in header_and_source:
        if not file_to_open.startswith("/") and not search_path == "":
            file_to_open = search_path + "/" + file_to_open

        if not os.path.isfile(file_to_open):
            continue

        with open(file_to_open, encoding="utf-8") as f:
            fwlog_used = False
            file_to_open = os.path.basename(file_to_open)

            line_num = 0
            for line in f:
                line_num = line_num + 1
                m = log_matcher.search(line)
                c = comment_matcher.search(line)
                if m and not c:
                    fwlog_used = True
                    macro_type = m.group(1)
                    fmt_string = m.group(3)
                    locator = (strhash(fmt_string) << NUM_BITS_FOR_LINE_NUM) | line_num

                    print(
                        "%s %d %d 0x%x %s %s"
                        % (
                            file_to_open,
                            strhash(file_to_open),
                            line_num,
                            locator,
                            macro_type,
                            fmt_string,
                        )
                    )

            if line_num > MAX_LINE_NUM:
                errmsg = (
                    "File '%s' has too many lines to be handled by FWLOG (%d). Maximum allowed: %d."
                    % (file_to_open, line_num, MAX_LINE_NUM)
                )
                errmsg = errmsg + "\n  ==> Try reducing NUM_BITS_FOR_FILE_HASH"
                eprint(errmsg)

            if fwlog_used:
                # Don't need to check line -- is implied equal because locator isn't lossy for line number
                if locator in hashes_obtained and hashes_obtained[locator][0] != fmt_string:
                    errmsg = (
                        "ERROR: duplicate hash (%d) found for files: \n  %s and \n  %s"
                        % (locator, file_to_open, hashes_obtained[locator])
                    )
                    errmsg = (
                        errmsg
                        + "\n  ==> Try changing the hash function (in both fw_debug.h and fwlog.py)"
                    )
                    eprint(errmsg)
                hashes_obtained.setdefault(locator, (fmt_string, line_num, []))[2].append(
                    file_to_open
                )
