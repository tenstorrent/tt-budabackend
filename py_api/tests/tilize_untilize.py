#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import time
import torch
import eager_backend.backend_api as be_api

from timeit import default_timer as timer
from test_utils import py_desc, py_tensor, block_tensor, unblock_tensor
from eager_backend import DataFormat, BackendType, BackendDevice, BackendStatusCode, IOType, IOLocation
from eager_backend.backend_api import Backend, BackendConfig, PytorchTensorDesc
import os

def main():
    target_arch = BackendDevice.Grayskull
    if(os.getenv("ARCH_NAME") == "wormhole_b0"):
        target_arch = BackendDevice.Wormhole_B0
    elif(os.getenv("ARCH_NAME") == "wormhole"):
        target_arch = BackendDevice.Wormhole
    
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-check', action='store_true')
    args = parser.parse_args()

    target_devices = {0}
    config = be_api.get_runtime_config(target_arch)
    backend = Backend(config, target_devices)

    # torch.set_printoptions(profile="full")

    # Define input/output tensors and locations, matching netlist quueues
    act = {
        'data': torch.empty([384,1024], dtype=torch.bfloat16),
        'chan': torch.tensor([[0,1],[2,3]], dtype=torch.int32),
        'addr': torch.tensor([[0x30000000,0x30000000],[0x30000000,0x30000000]], dtype=torch.int32),
        'loc': IOLocation.Dram
    }
    out_dq = {
        'data': torch.zeros([384,1024], dtype=torch.bfloat16),
        'chan': torch.tensor([[0,1],[2,3]], dtype=torch.int32),
        'addr': torch.tensor([[0x20000000,0x20000000],[0x20000000,0x20000000]], dtype=torch.int32),
        'loc': IOLocation.Dram
    }
    out_hq = {
        'data': torch.zeros([384,1024], dtype=torch.bfloat16),
        'chan': torch.tensor([[0,0],[0,0]], dtype=torch.int32),
        'addr': torch.tensor([[0x0,0x10000000],[0x20000000,0x30000000]], dtype=torch.int32),
        'loc': IOLocation.Host
    }

    # Randomize inputs
    # act['data'].uniform_(-100.0, 100.0)
    act['data'] = torch.arange(0, 384*1024, dtype=torch.bfloat16).reshape(384,1024)

    #be_api.initialize_child_process(target_arch, target_devices)

    # Things that the IO APIs do not know about, need explicit user input
    chip_id = 0
    queue_entries = 256
    microbatch_size = 128

    # ------------------------------------------------------------------
    # Initialize queues
    # ------------------------------------------------------------------
    be_api.init_queue(act['loc'], chip_id, py_desc(act['chan']), py_desc(act['addr']), queue_entries)
    be_api.init_queue(out_dq['loc'], chip_id, py_desc(out_dq['chan']), py_desc(out_dq['addr']), queue_entries)
    be_api.init_queue(out_hq['loc'], chip_id, py_desc(out_hq['chan']), py_desc(out_hq['addr']), queue_entries)

    nbytes = act['data'].nelement() * act['data'].element_size()

    # benchmark block/unblock operations on CPU
    start = timer()
    for _ in range(microbatch_size):
        block_act = block_tensor(act['data'])
    duration = timer() - start
    print(f"PERF: python tilize time = {duration:.2f}, {microbatch_size/(duration):.2f} samples/s, {microbatch_size*nbytes/(duration*1024**3):.2f} GB/s")

    start = timer()
    for _ in range(microbatch_size):
        unblock_tensor(out_hq['data'])
    duration = timer() - start
    print(f"PERF: python untilize time = {duration:.2f}, {microbatch_size/(duration):.2f} samples/s, {microbatch_size*nbytes/(duration*1024**3):.2f} GB/s")

    # push microbatch of activations
    start = timer()
    for _ in range(microbatch_size):
        block_act = block_tensor(act['data'])
        be_api.push_tensor(IOLocation.Dram, chip_id, py_desc(act['chan']), py_desc(act['addr']), py_desc(block_act), IOType.Queue, -1)
    duration = timer() - start
    print(f"PERF: python push time = {duration:.2f}, {microbatch_size/(duration):.2f} samples/s, {microbatch_size*nbytes/(duration*1024**3):.2f} GB/s")

    # ------------------------------------------------------------------
    # Compile and run netlists
    # ------------------------------------------------------------------
    status = backend.compile_and_run_netlist("loader/tests/net_basic/netlist_tilized_hq.yaml", {})
    assert status == BackendStatusCode.Success

    # ------------------------------------------------------------------
    # Pop outputs and not args.skip_check results
    # ------------------------------------------------------------------
    expected = act['data']

    # Tilized DRAM output
    untilized_output = False
    start = timer()
    for _ in range(microbatch_size):
        out_desc = py_desc(out_dq['data'])
        be_api.get_tensor(out_dq['loc'], chip_id, py_desc(out_dq['chan']), py_desc(out_dq['addr']), out_desc, IOType.Queue, -1, untilized_output)
        be_api.pop_tensor(out_dq['loc'], chip_id, py_desc(out_dq['chan']), py_desc(out_dq['addr']))
        out_observed = py_tensor(out_desc)
        if not untilized_output:
            out_observed = unblock_tensor(out_observed)
        if not args.skip_check:
            out_expected = expected.to(out_observed.dtype)
            assert torch.allclose(out_observed, out_expected, .05, .05)
        be_api.free_tensor(out_desc)
    duration = timer() - start
    print(f"PERF: python dq pop time = {duration:.2f}, {microbatch_size/(duration):.2f} samples/s, {microbatch_size*nbytes/(duration*1024**3):.2f} GB/s")

    # Tilized Host output
    untilized_output = False
    start = timer()
    for _ in range(microbatch_size):
        out_desc = py_desc(out_hq['data'])
        be_api.get_tensor(out_hq['loc'], chip_id, py_desc(out_hq['chan']), py_desc(out_hq['addr']), out_desc, IOType.Queue, -1, untilized_output)
        be_api.pop_tensor(out_hq['loc'], chip_id, py_desc(out_hq['chan']), py_desc(out_hq['addr']))
        out_observed = py_tensor(out_desc)
        if not untilized_output:
            out_observed = unblock_tensor(out_observed)
        if not args.skip_check:
            out_expected = expected.to(out_observed.dtype)
            torch.allclose(out_observed, out_expected, .05, .05)
        be_api.free_tensor(out_desc)
    duration = timer() - start
    print(f"PERF: python hq pop time = {duration:.2f}, {microbatch_size/(duration):.2f} samples/s, {microbatch_size*nbytes/(duration*1024**3):.2f} GB/s")

    # ------------------------------------------------------------------
    # Shutdown backend and processes
    # ------------------------------------------------------------------

    # Note: free_tensor calls above are optional but is a good pratice to free memory as soon as possible
    # otherwise when finish_child_process is called all tensors associated with that process will be freed
    be_api.finish_child_process()

    # Calls shutdown sequences and releases all handles (refcount) to backend
    backend.destroy()
    backend = None

if __name__ == "__main__":
    main()
