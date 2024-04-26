# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0



def print_devices(arch):
    template = """
devices:
  arch: [{arch}]
"""
    print(template.format(arch=arch))

def print_queues(num_graphs):
  template = "  {name:<30}: {{type: queue, input: {input:<8}, entries: 80, grid_size: [1, 1], t: 1, mblock: [1, 1], ublock: [1, 1], df: Float16, target_device: {device}, loc: dram, dram: [[0, 0x{addr:08x}]]}}"""

  addr_base = 0x30000000
  print("\nqueues:\n")
  print(template.format(device=0, addr=addr_base, name="input", input="HOST"))

  for i in range(0, num_graphs-1):
    addr_base += 0x100000
    print(template.format(device=0, addr=addr_base, name=f"e2e_graph{i}_graph{i+1}", input=f"unary{i}"))

  print(template.format(device=1, addr=0x38000000, name="output", input=f"unary{num_graphs-1}"))


def print_graphs(num_graphs, device_id):
  print("\n\ngraphs:")
  template = """
  temporal_epoch_{index}_device_{device_id}:
    target_device: {device_id}
    input_count: 2
    unary{index}: {{type: nop, grid_loc: [0, 0], grid_size: [1, 1], inputs: [{input_queue}], in_df: [Float16], acc_df: Float16, out_df: Float16, intermed_df: Float16, ublock_order: r, buf_size_mb: 2, math_fidelity: HiFi3, untilize_output: false, t: 1, mblock: [1, 1], ublock: [1, 1]}}"""
  for i in range(num_graphs):
    input_queue = "input" if i == 0 else f"e2e_graph{i-1}_graph{i}"
    print(template.format(index=i, device_id=device_id, input_queue=input_queue))


def print_empty_graphs(num_graphs, device_id):
  template = """
  temporal_epoch_{index}_device_{device_id}:
    target_device: {device_id}
    input_count: 2"""
  for i in range(num_graphs):
    print(template.format(index=i, device_id=device_id))




def print_program(first_program, name, loop_count, num_graphs, num_chips):
  template_header = """  - {name}:
    - staticvar: {{$lptr0: 0, $gptr0: 0}}
    - var: {{$c_loop_count: {loop_count}, $c_input_count: 2, $c_input_wrap: 160}}
    - loop: $c_loop_count"""

  template_body = """
    - execute: {{graph_name: temporal_epoch_{index}_device_{device_id}, queue_settings: {{
        {queue_name:<30} {{prologue: false, epilogue: false, zero: false,  rd_ptr_local: $lptr0, rd_ptr_global: $gptr0}}}}}}"""

  template_body_other_chips = """    - execute: {{graph_name: temporal_epoch_{index}_device_{device_id}}}"""


  template_footer = """
    - varinst: [$lptr0, incwrap, $c_input_count, $c_input_wrap] # incr and wrap
    - varinst: [$gptr0, incwrap, $c_input_count, $c_input_wrap] # incr and wrap
    - endloop


test-config:
  comparison-config:
    type: AllCloseHw
    atol: 0.01
    rtol: 0.15
    check_pct: 0.50
    check_pcc: 0.92
    verbosity: Concise
  stimulus-config:
    type: Normal
    normal_mean: 0.0
    normal_stddev: 0.1
  io-config:
    inputs: [input]
    outputs: [output]


"""

  if (first_program):
    print("\n\nprograms:")

  print(template_header.format(name=name, loop_count=loop_count))

  for i in range(0, num_graphs):

    queue_name = "input" if i == 0 else f"e2e_graph{i-1}_graph{i}"
    queue_name += ":"

    print(template_body.format(index=i, device_id=0, queue_name=queue_name))

    if num_chips > 1:
      for device in range(1, num_chips):
        print(template_body_other_chips.format(index=i, device_id=device))

  print(template_footer)


if __name__ == '__main__':
    arch = str(input("Enger the target arch name: "))
    number_of_graphs = int(input("Enter the number of graphs: "))
    # num_chips = int(input("Enter the number of chips: "))

    # arch = "wormhole_b0"
    # number_of_graphs = 34
    num_chips = 2

    print_devices(arch)
    print_queues(number_of_graphs)
    print_graphs(number_of_graphs, 0)
    for i in range(1, num_chips):
      print_empty_graphs(number_of_graphs, i)

    print_program(True, "program0", 10, number_of_graphs, num_chips)