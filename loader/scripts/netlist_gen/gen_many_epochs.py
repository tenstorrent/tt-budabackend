#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

# Really dumb script, but used to generate lots of epochs for netlist_many_epochs.yaml rather than tedious copy/pasting.

def myroundup(n, step):
    return ((n - 1) // step + 1) * step


num_epochs = 34

#################################
# QUEUES                        #
#################################

print("devices:\n  arch: [grayskull, wormhole]\n\n")
print("queues:")

# Completely arbitrary addresses
in_queues_base      = int(0x10000000)
queue_stride        = int(0x100000)

dram_addr = 0

# Input Queues
for epoch_id in range(num_epochs):
    # epoch_id_str = str(epoch_id).zfill(2)
    qname = f"in{epoch_id}"
    input = f"HOST"
    dram_addr = in_queues_base + queue_stride * epoch_id
    assert dram_addr < 0x40000000, "dram_addr exceeds 1GB for GS"

    print(f"  {qname}: {{type: queue, input: {input}, entries: 8, grid_size: [1, 1], t: 1, mblock: [2, 2], ublock: [2, 2], df: Float16, target_device: 0, loc: dram, dram: [[0, {hex(dram_addr)}]]}}")

# Calculate output queues base based on where input queues finished, rounded to nearest 0x10000000 to make it look nice.
out_queues_base = myroundup(dram_addr + queue_stride, 0x10000000)

# Output Queues
for epoch_id in range(num_epochs):
    # epoch_id_str = str(epoch_id).zfill(2)
    qname = f"out{epoch_id}"
    input = f"unary{epoch_id}"
    dram_addr = out_queues_base + queue_stride * epoch_id
    assert dram_addr < 0x40000000, "dram_addr exceeds 1GB for GS"

    print(f"  {qname}: {{type: queue, input: {input}, entries: 8, grid_size: [1, 1], t: 1, mblock: [2, 2], ublock: [2, 2], df: Float16, target_device: 0, loc: dram, dram: [[0, {hex(dram_addr)}]]}}")

print("")
#################################
# GRAPHS                        #
#################################

print("graphs:")

for epoch_id in range(num_epochs):
    # epoch_id_str = str(epoch_id).zfill(2)
    graph_name = f"unary_graph{epoch_id}"
    op_name = f"unary{epoch_id}"
    inputs = f"in{epoch_id}"

    print(f"  {graph_name}:")
    print(f"    target_device: 0")
    print(f"    input_count: 1")
    print(f"    {op_name}: {{type: nop, grid_loc: [0, 0], grid_size: [1, 1], inputs: [{inputs}], in_df: [Float16], acc_df: Float16, out_df: Float16,  intermed_df: Float16, ublock_order: r, buf_size_mb: 2, math_fidelity: HiFi3, untilize_output: false, t: 1, mblock: [2, 2], ublock: [2, 2]}}")

print("")

#################################
# PROGRAMS                      #
#################################

print('''programs:
  - run_back_to_back_epochs:
      - var: [$ptr, $c_loop_count, $c_input_count]
      - varinst: [$c_loop_count, set, 2]  # load loop count
      - varinst: [$c_input_count, set, 1] # load input count
      - loop: $c_loop_count''')


for epoch_id in range(num_epochs):
    # epoch_id_str = str(epoch_id).zfill(2)

    graph_name = f"unary_graph{epoch_id}"
    op_name = f"unary{epoch_id}"

    print(f"      - execute: {{graph_name: {graph_name}, queue_settings: {{")
    print(f"          in{epoch_id}: {{prologue: false, epilogue: false, zero: false, rd_ptr_local: $ptr, rd_ptr_global: $ptr}}}}}}")

print('''      - varinst: [$ptr, add, $ptr, $c_input_count] # incr ptr
      - endloop''')
