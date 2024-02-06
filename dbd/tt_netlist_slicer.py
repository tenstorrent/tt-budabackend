#!/usr/bin/env python3
import argparse, tapout_sweep, yaml
import traceback
import os
from netlist_common.netlist import Netlist, NetlistConsts
from netlist_common.netlistbuilder import BuildNetlist, NetlistBuilderHelper

def get_graph_name_from_list(nl, operations):
    s = set()

    for op in operations:
        s.add(nl.get_graphs().get_graph_name(op))

    if len(s) > 1:
        raise Exception(f"Selected operations are in more than one graph. [{s}]" )

    return s.pop()

def combine_inputs(nl, graph_name, in_ops, in_op_inputs):
    result = in_ops
    for in_op in in_op_inputs:
        in_names = nl.get_operation(graph_name, in_op).get_input_names()
        for name in in_names:
            if name not in nl.get_queue_names():
                result.append(name)

    result = list(set(result))
    return result

def slice(netlist_filename, in_ops, in_op_inputs, out_ops):
    nl = Netlist(NetlistBuilderHelper.build_netlist_from_file(netlist_filename))
    graph_name = get_graph_name_from_list(nl, [*in_ops, *in_op_inputs, *out_ops])
    builder = BuildNetlist(nl).build_single_graph(graph_name)

    # if there is single op in out_ops remove all existing out queues
    # and add new outputs
    if len(out_ops) > 0:
        builder.remove_output_queues_for_graph(graph_name)
        for op_name in out_ops:
            builder.add_outputs_to_operation(graph_name, op_name)

    inputs = combine_inputs(builder.get_netlist(), graph_name, in_ops, in_op_inputs)

    # add input queues and replace inputs with new op
    for in_op in inputs:
        new_op_name = builder.add_input_queue_for_operation(graph_name, in_op)
        builder.replace_input_names(graph_name, in_op, new_op_name)

    builder.remove_operations_without_output(graph_name)

    builder.add_programs(graph_name)
    builder.allocate_dram()

    return builder.get_netlist()

def main():
    parser = argparse.ArgumentParser(description=__doc__ )
    parser.add_argument('--netlist', type=str, required=True, help='Netlist filename')
    parser.add_argument('--in_ops', type=str, required=False, help='List of operations that will be replaced with input queues')
    parser.add_argument('--in_op_inputs', type=str, required=False, help='List of operations which inputs will be replaced with input queues')
    parser.add_argument('--out_ops', type=str, required=False, help='List of operations whose outputs will be replaced with ouput queues')
    parser.add_argument('--out_file', type=str, required=False, help='Output filename')
    args = parser.parse_args()

    if not os.path.exists(args.netlist):
        print(f"Netlist filename: {args.netlist}, does not exist")
        exit(1)

    if not args.in_ops and not args.out_ops and not args.in_op_inputs:
        print(f"At least one in_ops, in_op_inputs or out_ops should be supplied")
        exit(1)

    try:
        sliced_netlist = slice(
            args.netlist,
            args.in_ops.split() if args.in_ops else [],
            args.in_op_inputs.split() if args.in_op_inputs else [],
            args.out_ops.split() if args.out_ops else [])
    except Exception as e:
        print(e)
        exit(1)

    if args.out_file:
        with open(args.out_file, "w") as f:
            f.write(sliced_netlist)
    else:
        print(sliced_netlist)

if __name__ == "__main__":
    main()