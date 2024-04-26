#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import time
import torch
import eager_backend.backend_api as be_api
import os

from test_utils import py_desc, py_tensor, block_tensor, unblock_tensor
from eager_backend import DataFormat, BackendType, BackendDevice, BackendStatusCode, IOType, IOLocation
from eager_backend.backend_api import Backend, BackendConfig, PytorchTensorDesc

def main():
    target_arch = BackendDevice.Grayskull
    if(os.getenv("ARCH_NAME") == "wormhole_b0"):
        target_arch = BackendDevice.Wormhole_B0
    elif(os.getenv("ARCH_NAME") == "wormhole"):
        target_arch = BackendDevice.Wormhole

    target_devices = {0}
    config = be_api.get_runtime_config(target_arch)
    backend = Backend(config, target_devices)

    # Define input/output tensors and locations, matching netlist quueues
    act = {
        'data': torch.empty([128, 128], dtype=torch.float32),
        'chan': torch.tensor([[0]], dtype=torch.int32),
        'addr': torch.tensor([[0x11000000]], dtype=torch.int32),
        'loc': IOLocation.Dram
    }
    weight = {
        'data': torch.empty([128, 128], dtype=torch.float32),
        'chan': torch.tensor([[1]], dtype=torch.int32),
        'addr': torch.tensor([[0x21000000]], dtype=torch.int32),
        'loc': IOLocation.Dram
    }
    interm = {
        'data': torch.ones([128, 128], dtype=torch.float16),
        'chan': torch.tensor([[2]], dtype=torch.int32),
        'addr': torch.tensor([[0x31000000]], dtype=torch.int32),
        'loc': IOLocation.Dram
    }
    out_dq_tilized = {
        'data': torch.zeros([128, 128], dtype=torch.bfloat16),
        'chan': torch.tensor([[5]], dtype=torch.int32),
        'addr': torch.tensor([[0x30000000]], dtype=torch.int32),
        'loc': IOLocation.Dram
    }
    out_dq_untilized = {
        'data': torch.zeros([128, 128], dtype=torch.float16),
        'chan': torch.tensor([[0]], dtype=torch.int32),
        'addr': torch.tensor([[0x30000000]], dtype=torch.int32),
        'loc': IOLocation.Dram
    }
    out_hq_untilized = {
        'data': torch.zeros([128, 128], dtype=torch.float16),
        'chan': torch.tensor([[0]], dtype=torch.int32),
        'addr': torch.tensor([[0x0]], dtype=torch.int32),
        'loc': IOLocation.Host
    }

    # Randomize inputs
    act['data'].uniform_(-1.0, 1.0)
    weight['data'].uniform_(-1.0, 1.0)

    #be_api.initialize_child_process(target_arch, target_devices)

    # Things that the IO APIs do not know about, need explicit user input
    chip_id = 0
    ram_entries = 1
    queue_entries = 256
    microbatch_size = 256

    # ------------------------------------------------------------------
    # Initialize queues
    # ------------------------------------------------------------------
    # 2 inputs for MM op
    be_api.init_queue(act['loc'], chip_id, py_desc(act['chan']), py_desc(act['addr']), queue_entries)
    be_api.init_queue(weight['loc'], chip_id, py_desc(weight['chan']), py_desc(weight['addr']), ram_entries)

    # 1 intermediate output for MM, input for unary
    be_api.init_queue(interm['loc'], chip_id, py_desc(interm['chan']), py_desc(interm['addr']), queue_entries)

    # 3 outputs for unary fork op
    be_api.init_queue(out_dq_tilized['loc'], chip_id, py_desc(out_dq_tilized['chan']), py_desc(out_dq_tilized['addr']), queue_entries)
    be_api.init_queue(out_dq_untilized['loc'], chip_id, py_desc(out_dq_untilized['chan']), py_desc(out_dq_untilized['addr']), queue_entries)
    be_api.init_queue(out_hq_untilized['loc'], chip_id, py_desc(out_hq_untilized['chan']), py_desc(out_hq_untilized['addr']), queue_entries)

    # ------------------------------------------------------------------
    # Push inputs and weights
    # ------------------------------------------------------------------
    # push params to ram only once
    block_weight = block_tensor(weight['data'])
    be_api.push_tensor(IOLocation.Dram, chip_id, py_desc(weight['chan']), py_desc(weight['addr']), py_desc(block_weight), IOType.RandomAccess, 0)

    # push microbatch of activations
    for _ in range(microbatch_size):
        block_act = block_tensor(act['data'])
        be_api.push_tensor(IOLocation.Dram, chip_id, py_desc(act['chan']), py_desc(act['addr']), py_desc(block_act), IOType.Queue, -1)

    # ------------------------------------------------------------------
    # Compile and run netlists
    # ------------------------------------------------------------------
    status = backend.compile_and_run_netlist("loader/tests/net_basic/netlist_eager_mm.yaml", {})
    assert status == BackendStatusCode.Success

    status = backend.compile_and_run_netlist("loader/tests/net_basic/netlist_eager_unary.yaml", {})
    assert status == BackendStatusCode.Success

    # ------------------------------------------------------------------
    # Pop outputs and check results
    # ------------------------------------------------------------------
    expected = torch.matmul(act['data'], weight['data'])

    # Untilized DRAM output
    untilized_output = True
    for _ in range(microbatch_size):
        out_desc = py_desc(out_dq_untilized['data'])
        be_api.get_tensor(out_dq_untilized['loc'], chip_id, py_desc(out_dq_untilized['chan']), py_desc(out_dq_untilized['addr']), out_desc, IOType.Queue, -1, untilized_output)
        be_api.pop_tensor(out_dq_untilized['loc'], chip_id, py_desc(out_dq_untilized['chan']), py_desc(out_dq_untilized['addr']))

        out_observed = py_tensor(out_desc)
        out_expected = expected.to(out_observed.dtype)
        assert torch.allclose(out_observed, out_expected, .05, .05)
        be_api.free_tensor(out_desc)

    # Tilized DRAM output
    untilized_output = False
    for _ in range(microbatch_size):
        out_desc = py_desc(out_dq_tilized['data'])
        be_api.get_tensor(out_dq_tilized['loc'], chip_id, py_desc(out_dq_tilized['chan']), py_desc(out_dq_tilized['addr']), out_desc, IOType.Queue, -1, untilized_output)
        be_api.pop_tensor(out_dq_tilized['loc'], chip_id, py_desc(out_dq_tilized['chan']), py_desc(out_dq_tilized['addr']))

        out_observed = unblock_tensor(py_tensor(out_desc))
        out_expected = expected.to(out_observed.dtype)
        assert torch.allclose(out_observed, out_expected, .05, .05)
        be_api.free_tensor(out_desc)


    # Untilized Host output
    untilized_output = True
    for _ in range(microbatch_size):
        out_desc = py_desc(out_hq_untilized['data'])
        be_api.get_tensor(out_hq_untilized['loc'], chip_id, py_desc(out_hq_untilized['chan']), py_desc(out_hq_untilized['addr']), out_desc, IOType.Queue, -1, untilized_output)
        be_api.pop_tensor(out_hq_untilized['loc'], chip_id, py_desc(out_hq_untilized['chan']), py_desc(out_hq_untilized['addr']))

        out_observed = py_tensor(out_desc)
        out_expected = expected.to(out_observed.dtype)
        assert torch.allclose(out_observed, out_expected, .05, .05)
        be_api.free_tensor(out_desc)

    weight_desc = py_desc(weight['data'])
    be_api.get_tensor(weight['loc'], chip_id, py_desc(weight['chan']), py_desc(weight['addr']), weight_desc, IOType.RandomAccess, 0, False)
    assert torch.allclose(unblock_tensor(py_tensor(weight_desc)), weight['data'], .05, .05)
    be_api.free_tensor(weight_desc)

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
