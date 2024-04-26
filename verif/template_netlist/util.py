# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import subprocess
from pathlib import Path
from string import Template
import hashlib
import yaml
import random
from enum import Enum
from typing import Dict, List, Optional
from dataclasses import dataclass

try:
    from yaml import (
        Loader,
    )  # If we can find the loader module, use it since its safer. May not always find it due to version incompatibilities
except:
    pass


# Has to be kept consistent with the same class under perf_lib/perf_test_base.py
class PerfTestMode(Enum):
    prologue = 1
    feeder_drainer = 2
    single_op = 3


# Has to be kept consistent with the class op_type under perf_lib/perf_test_base.py


class PerfTestType(Enum):
    Matmul = 1
    Binary = 2
    Unary = 3
    TM = 4
    Dram = 5
    MatmulBias = 6
    Fused0 = 7
    Fused1 = 8
    Fused2 = 9
    Pcie = 10
    MatmulSparse = 11
    ReduceMax = 12
    Splice = 13
    MatmulSparseNzCounts = 14
    Tilizer              = 15
    MatmulQuant          = 16
    MatmulBiasQuant      = 17
    Quant                = 18


# Has to be kept consistent with the class ReblockTMPerfLib under perf_lib/perf_test_base.py
class ReblockTM(Enum):
    Normal = 1
    Gather = 2
    Hstack = 3
    Broadcast_r = 4


class DramAssignment(Enum):
    Normal = 1
    RoundRobin = 2
    Random = 3


@dataclass
class PerfOpSweepConfig:
    sweep_en: bool = False
    sweep_vars: Dict[str, List[int]] = None
    op_type: PerfTestType = None
    perf_test_mode: PerfTestMode = PerfTestMode.prologue
    reblocking_mode: ReblockTM = ReblockTM.Normal
    dram_assignment_mode: DramAssignment = DramAssignment.Normal
    skip_constraint_solver: bool = False


def get_num_input_operands(test_type: PerfTestType):
    if test_type in [
        PerfTestType.Unary,
        PerfTestType.TM,
        PerfTestType.Dram,
        PerfTestType.Pcie,
        PerfTestType.ReduceMax,
        PerfTestType.Tilizer,
    ]:
        return 1
    elif test_type in [
        PerfTestType.Matmul, 
        PerfTestType.Binary,
        PerfTestType.Quant,
    ]:
        return 2
    elif test_type in [
        PerfTestType.MatmulBias,
        PerfTestType.Fused0,
        PerfTestType.MatmulSparse,
        PerfTestType.MatmulSparseNzCounts,
        PerfTestType.Splice,
        PerfTestType.MatmulQuant,
    ]:
        return 3
    elif test_type in [
        PerfTestType.MatmulBiasQuant
    ]:
        return 4
    elif test_type == PerfTestType.Fused2:
        return 5
    else:
        raise ValueError(
            f"Invalid test_type. This function is missing the number of operands for test_type {test_type.name}"
        )


def is_matmul_op(test_type):
    if test_type in [
        PerfTestType.Matmul,
        PerfTestType.MatmulBias,
        PerfTestType.MatmulSparse,
        PerfTestType.MatmulSparseNzCounts,
        PerfTestType.MatmulQuant,
        PerfTestType.MatmulBiasQuant,
    ]:
        return True
    else:
        return False


def has_inner_dim(test_type):
    if is_matmul_op(test_type) or test_type == PerfTestType.ReduceMax:
        return True
    else:
        return False


def is_binary_op(test_type: PerfTestType):
    num_input_operands = get_num_input_operands(test_type)
    return num_input_operands > 1


"""
Utility functions
"""


def get_git_root():
    def normalize_string(string):
        return string.decode("utf-8").strip()

    result = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True)
    if result.returncode == 0:
        return Path(normalize_string(result.stdout))
    raise RuntimeError("Must be run from inside git repo!")


def load_yaml_test_configs(yamlfile):
    with open(yamlfile, "r") as input_yaml_file:
        yaml_configs = yaml.safe_load(input_yaml_file.read())
    if not isinstance(yaml_configs, list):
        yaml_configs = [yaml_configs]
    return yaml_configs


def dump_config_yaml(config: Dict, config_dir_path: Optional[str]) -> None:
    """Potentially saves the config dict to the dump config dir.

    Parameters
    ----------
    config:
        Config dict, i.e. processed solver solution.
    """
    if not config_dir_path:
        return

    config_hash = hashlib.md5(str(config).encode()).hexdigest()
    config_file_path = os.path.join(config_dir_path, f"config_{config_hash}.yaml")

    with open(config_file_path, "w") as config_file:
        yaml.dump(config, config_file)


class TestType(Enum):
    TmTest = 0
    GraphTest = 1


def run_graph_test(*, netlist_yaml, outdir, dry_run=False):
    cmd = [
        "./build/test/verif/graph_tests/test_graph",
        "--netlist",
        f"{outdir}/{netlist_yaml}",
        "--silicon",
        "--timeout",
        "500",
    ]
    cmd_str = " ".join(cmd)

    return run_test_cmd(cmd_str, outdir, dry_run)


def run_tm_test(
    *, netlist_yaml, outdir, num_inputs=1, dry_run=False, determinism_tests=False, num_loops=None
):
    cmd = []

    with open(os.path.join(outdir, netlist_yaml), "r") as file:
        try:
            netlist_info = yaml.load(
                file, Loader=Loader
            )  # Use the loader if its been imported (safer)
        except:
            netlist_info = yaml.load(file)
    if "test-config" in netlist_info and "harvest-config" in netlist_info["test-config"]:
        harvest_config = 0
        for i in netlist_info["test-config"]["harvest-config"]:
            harvest_config += 2 ** int(i)
        cmd += ["TT_BACKEND_HARVESTED_ROWS=" + str(harvest_config)]

    if not determinism_tests:
        timeout_str = "45"
    else:
        timeout_str = "2000"
    cmd += [
        "timeout",
        timeout_str,
        "./build/test/verif/tm_tests/test_tm_main",
        "--randomize",
        "--netlist",
        f"{outdir}/{netlist_yaml}",
        "--outdir",
        f"{outdir}/",
        "--silicon",
    ]

    if determinism_tests:
        cmd += ["--determinism-tests", "--num-loops", str(num_loops)]
    elif num_loops:
        cmd += ["--num-loops", str(num_loops)]
    cmd_str = " ".join(cmd)

    return run_test_cmd(cmd_str, outdir, dry_run)


def run_test_cmd(cmd, outdir, dry_run):
    if dry_run:
        return (cmd, None)

    env = os.environ.copy()
    reset_sub_proc = subprocess.run(
        [
            "sudo",
            "device/bin/silicon/reset.sh",
        ],
        env=env,
        cwd=get_git_root(),
        capture_output=True,
        universal_newlines=True,
    )

    if reset_sub_proc.returncode != 0:
        print("BOARD RESET FAILURE")

    p = subprocess.run(
        cmd, env=env, cwd=get_git_root(), capture_output=True, universal_newlines=True
    )  # stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    # print(p.stdout)
    # print(p.stderr)

    with open(f"{outdir}/run.log", "w") as output_log_file:
        output_log_file.write(p.stdout + p.stderr)

    # print(f"Result: {'pass' if p.returncode == 0 else 'fail'}")
    return (cmd, p.returncode == 0)


def create_netlist_from_single_config(
    *, template_yaml, output_dir, config, test_id, use_shared_dir=False
):
    def get_subs(template_dict):
        return {f"TEMPLATE_{key}": val for (key, val) in template_dict.items()}

    with open(template_yaml, "r") as file:
        data = file.read()

        text_output = Template(data).safe_substitute(get_subs(config))
        if use_shared_dir:
            test_dir = f"{output_dir}/"
        else:
            test_dir = f"{output_dir}/test_{test_id}"
        os.makedirs(test_dir, exist_ok=True)
        yaml_filename = f"netlist_{test_id}.yaml"
        test_yaml = f"{test_dir}/{yaml_filename}"
        with open(test_yaml, "w") as output_yaml_file:
            output_yaml_file.write(text_output)

    return (test_dir, yaml_filename)


def create_netlists(
    *, template_yaml, output_dir, configs, use_shared_dir=False, use_hash=1, command=None
):
    def get_subs(template_dict):
        return {f"TEMPLATE_{key}": val for (key, val) in template_dict.items()}

    test_list = []
    with open(template_yaml, "r") as file:
        data = file.read()
        id = 0
        for sol in configs:
            text_output = Template(data).safe_substitute(get_subs(sol))
            md5hash = hashlib.md5(text_output.encode()).hexdigest()
            if use_shared_dir:
                test_dir = f"{output_dir}/"
            else:
                if use_hash == 1:
                    test_dir = f"{output_dir}/test_{md5hash}"
                else:
                    test_dir = f"{output_dir}/test_{id}"
            os.makedirs(test_dir, exist_ok=True)
            if use_hash == 1:
                yaml_filename = f"netlist_{md5hash}.yaml"
            else:
                yaml_filename = f"netlist_{id}.yaml"
            test_yaml = f"{test_dir}/{yaml_filename}"
            id = id + 1
            if command:
                text_output += "# Generation Command: " + command
            with open(test_yaml, "w") as output_yaml_file:
                output_yaml_file.write(text_output)

            test_list.append((test_dir, yaml_filename))
    return test_list


def generate_test_commands(
    netlist_test_info, test_type=TestType.TmTest, determinism_tests=False, num_loops=None
):
    cmds = []
    print(netlist_test_info)
    for odir, yaml in netlist_test_info:
        cmdline, _ = (
            run_tm_test(
                netlist_yaml=yaml,
                outdir=odir,
                dry_run=True,
                determinism_tests=determinism_tests,
                num_loops=num_loops,
            )
            if test_type == TestType.TmTest
            else run_graph_test(netlist_yaml=yaml, outdir=odir, dry_run=True)
        )
        cmds.append(cmdline)
    return cmds


def grep_tm_test_error_summary(*, outdir):
    env = os.environ.copy()
    cmd = [
        "grep",
        "-E",
        "(ERROR|FATAL|Error|Fatal)",
        f"{outdir}/run.log",
        f"{outdir}/net2pipe.log",
        f"{outdir}/temporal_epoch_0/overlay/pipegen.log",
    ]

    p = subprocess.run(cmd, env=env, capture_output=True, universal_newlines=True)
    return p.stdout


"""
DRAM Helper functions
"""


def default_dram_string(
    num_buffers,
    channel,
    min_buffer_size=0x800000,
    generate_random_addr=True,
    check_buf_size=None,
    start_addr=None,
    dram_assignment=None,
):
    if generate_random_addr:
        return (
            "["
            + ", ".join(
                [
                    f"[{chan}, 0x{addr:08x}]"
                    for chan, addr in generate_dram_channels_and_addresses(
                        num_buffers, min_buffer_size, channel, check_buf_size
                    )
                ]
            )
            + "]"
        )
    else:
        dram_assignment_valid = [True]
        return (
            "["
            + ", ".join(
                [
                    f"[{chan}, 0x{addr:08x}]"
                    for chan, addr in generate_dram_channels_and_deterministic_addresses(
                        num_buffers,
                        min_buffer_size,
                        channel,
                        check_buf_size,
                        start_addr,
                        dram_assignment,
                        dram_assignment_valid,
                    )
                ]
            )
            + "]",
            dram_assignment_valid[0],
        )


def positive_integers_with_sum(n, total):
    ls = [0]
    rv = []
    while len(ls) < n:
        c = random.randint(0, total)
        ls.append(c)
    ls = sorted(ls)
    ls.append(total)
    for i in range(1, len(ls)):
        rv.append(ls[i] - ls[i - 1])
    return rv


def check_addresses_for_overlap(chan_addr_map, min_size, upper_limit):
    chan_map = []
    channel_id = chan_addr_map[0][0]
    for p in chan_addr_map:
        buf_channel = p[0]
        buf_addr = p[1]
        assert channel_id == buf_channel, "All addresses must be within the same channel"
        for addr in chan_map:
            if buf_addr >= addr:
                assert buf_addr >= (
                    addr + min_size
                ), f"Overlap found for buffer at addr 0x'{buf_addr:x}', size=0x'{min_size:x}', with buffer 0x'{addr:x}', size='0x{min_size:x}'"
            else:
                assert buf_addr <= (
                    addr - min_size
                ), f"Overlap found for buffer at addr 0x'{buf_addr:x}', size=0x'{min_size:x}', with buffer 0x'{addr:x}', size='0x{min_size:x}'"
            assert (
                buf_addr + min_size < upper_limit
            ), f"Buffer at addr 0x'{buf_addr:x}, size 0x'{min_size:x}' exceeds max dram channel space"
        chan_map.append(buf_addr)


def generate_dram_channels_and_addresses(num_buffers, min_buffer_size, channels, check_buf_size):
    lower_limit = 0x10000000
    upper_limit = 0x40000000
    rv = []

    min_buffer_size += 32  # Just in case the rounding down brings two addresses too close.
    indices = [
        x + min_buffer_size
        for x in positive_integers_with_sum(
            num_buffers, (upper_limit - lower_limit) - num_buffers * min_buffer_size
        )
    ]
    start = lower_limit
    for i in indices:
        addr = random.randint(start, i + start - min_buffer_size)
        addr = addr - (addr % 32)  # Ensure addresses are aligned to 32 bytes.
        rv.append((channels, addr))
        start += i

    if check_buf_size is not None:
        check_addresses_for_overlap(rv, check_buf_size, upper_limit)

    return rv


def get_channel(
    dram_assignment: DramAssignment,
    channels: List[int],
    current_channel_idx: int,
):
    num_channels = len(channels)
    assert num_channels > 0
    if dram_assignment == DramAssignment.Random:
        new_channel_idx = random.randint(0, num_channels - 1)
        return channels[new_channel_idx]
    elif dram_assignment == DramAssignment.RoundRobin:
        new_channel_idx = current_channel_idx + 1
        if new_channel_idx == num_channels:
            new_channel_idx = 0
        return channels[new_channel_idx]
    elif dram_assignment == DramAssignment.Normal:
        return channels[0]
    else:
        raise ValueError("Invalid dram assignment scheme")


def generate_dram_channels_and_deterministic_addresses(
    num_buffers,
    min_buffer_size,
    channels,
    check_buf_size,
    start_addr_each_channel,
    dram_assignment,
    dram_assignment_valid,
):
    lower_limit = 0x10000000
    upper_limit = 0x40000000

    channels_num_times_used = {i: 0 for i in start_addr_each_channel.keys()}

    assert start_addr_each_channel is not None

    rv = []
    if not type(channels) is list:
        channels = [channels]

    if len(channels) == 0:
        dram_assignment_valid[0] = False
        print("Not enough dram space for current config")
        return rv
    min_buffer_size += 32  # Just in case the rounding down brings two addresses too close.
    current_channel = get_channel(dram_assignment, channels, -1)
    # channel_idx = get_channel_idx(dram_assignment, -1, len(channels))
    start_addr = start_addr_each_channel[current_channel]
    start_32B = start_addr + ((32 - start_addr) % 32)
    while start_32B + min_buffer_size >= upper_limit:
        current_channel_idx = channels.index(current_channel)
        channels.remove(current_channel)
        current_channel_idx -= 1
        if len(channels) == 0:
            dram_assignment_valid[0] = False
            print("Not enough dram space for current config")
            return rv
        new_channel = get_channel(dram_assignment, channels, current_channel_idx)
        current_channel = new_channel
        start_addr = start_addr_each_channel[current_channel]
        start_32B = start_addr + ((32 - start_addr) % 32)

    for i in range(num_buffers):
        rv.append((current_channel, start_32B))
        channels_num_times_used[current_channel] += 1

        start_32B += min_buffer_size
        start_addr_each_channel[current_channel] = start_32B

        current_channel_idx = channels.index(current_channel)
        # We should have enough space for next buffer
        # Otherwise remove the channel from the list
        if start_32B + min_buffer_size >= upper_limit:
            channels.remove(current_channel)
            current_channel_idx -= 1
            if len(channels) == 0:
                dram_assignment_valid[0] = False
                print("Not enough dram space for current config")
                return rv
        new_channel = get_channel(dram_assignment, channels, current_channel_idx)
        current_channel = new_channel
        start_addr = start_addr_each_channel[current_channel]
        start_32B = start_addr + ((32 - start_addr) % 32)

    if check_buf_size is not None:
        check_addresses_for_overlap(rv, check_buf_size, upper_limit)

    if dram_assignment != DramAssignment.Normal:
        print("Number of buffers for each channel:")
        for channel, times_used in channels_num_times_used.items():
            print("channel = ", channel, " Number of buffers = ", times_used)

    return rv
