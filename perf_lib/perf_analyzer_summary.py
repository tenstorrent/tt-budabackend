#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

from typing import List, Union, Dict
from perf_report import (
    PerfReport,
    ExcelChartSeries,
    ExcelChart,
)

from perf_analyzer_api import *

# We have an excel chart object per graph
# Each excel chart contains multiple series where each series represents the bounded runtime of each core of each op in the graph
def create_bounded_runtime_per_core_chart(per_core_graph_perf_label: List[str], sorted_core_perf: List[List[Union[str, int, float]]]):
    chart_series = []
    graph_core_id = 0
    op_bounded_runtime_per_core: Dict[str, List] = {}

    graph_name = ""
    for core_op_perf in sorted_core_perf:
        if not graph_name:
            graph_name = core_op_perf[per_core_graph_perf_label.index(analyzer_graph_name_key)]
        assert graph_name == core_op_perf[per_core_graph_perf_label.index(analyzer_graph_name_key)]
        op_name = core_op_perf[per_core_graph_perf_label.index(analyzer_op_name_key)]
        bw_bound_runtime_index = per_core_graph_perf_label.index(analyzer_bw_bound_runtime_key)
        if bw_bound_runtime_index < len(core_op_perf) and core_op_perf[bw_bound_runtime_index] != "N/A":
            if op_name not in op_bounded_runtime_per_core:
                op_bounded_runtime_per_core[op_name] = []
            op_bounded_runtime_per_core[op_name].append(core_op_perf[bw_bound_runtime_index])
    
    for op_name in op_bounded_runtime_per_core:
        op_bounded_runtime_per_core[op_name].sort(reverse=True)
    
    op_bounded_runtime_per_core = dict(sorted(op_bounded_runtime_per_core.items(), key=lambda item: item[1], reverse=True))
    
    for op_name, all_cores_runtime in op_bounded_runtime_per_core.items():
        cores = list(range(graph_core_id, graph_core_id + len(all_cores_runtime)))
        graph_core_id += len(all_cores_runtime)
        chart_series.append(ExcelChartSeries(x_data=cores, y_data=all_cores_runtime, name=op_name))

    return ExcelChart(chart_series, title=graph_name, x_axis_name="Num Cores", y_axis_name="Bounded Runtime")

def add_entries_to_summary_report(summary_report: PerfReport, graph_perf_label: List[str], per_core_graph_perf_label: List[str], sorted_perf: List[List[Union[str, int, float]]], 
                                sorted_core_perf: List[List[Union[str, int, float]]], generate_op_report: bool, num_slowest_ops: int = 3):
    graph_name = ""
    analyzer_summary_label = summary_report.labels
    start_row_idx = len(summary_report)
    for op_id, op_perf in enumerate(sorted_perf):
        if op_id == num_slowest_ops:
            break
        entry = [""] * len(analyzer_summary_label)

        # all ops should belong to the same graph
        if not graph_name:
            graph_name = op_perf[graph_perf_label.index(analyzer_graph_name_key)]
        assert graph_name == op_perf[graph_perf_label.index(analyzer_graph_name_key)]

        if op_id == 0:
            entry[analyzer_summary_label.index(analyzer_graph_name_key)] = graph_name
        
        if not generate_op_report:
            core_coord = op_perf[graph_perf_label.index(analyzer_core_coord_key)]
            entry[analyzer_summary_label.index(analyzer_core_coord_key)] = core_coord
        
        op_name = op_perf[graph_perf_label.index(analyzer_op_name_key)]
        first_last_input = op_perf[graph_perf_label.index(analyzer_first_last_input_key)]
        grid_size = op_perf[graph_perf_label.index(analyzer_grid_size_key)]
        bw_limited_factor = op_perf[graph_perf_label.index(analyzer_bw_limited_factor_key)]
        if bw_limited_factor == 1 or bw_limited_factor == "N/A":
            bottleneck = "kernel"
            measured_bw = "N/A"
            required_bw = "N/A"
        elif bw_limited_factor > 1:
            slowest_operand = op_perf[graph_perf_label.index(analyzer_slowest_operand_key)]
            bottleneck = slowest_operand
            if slowest_operand.startswith("input-"):
                input_id = int(slowest_operand.split("-").pop())
                measured_bw = op_perf[graph_perf_label.index(get_input_operand_bw_key(input_id))]
                required_bw = op_perf[graph_perf_label.index(get_input_operand_required_bw_key(input_id))]
            elif slowest_operand == "output":
                measured_bw = op_perf[graph_perf_label.index(analyzer_output_bw_key)]
                required_bw = op_perf[graph_perf_label.index(analyzer_required_output_bw_key)]
            else:
                raise ValueError(f"Unexpected slowest operand {slowest_operand}")
        math_util = op_perf[graph_perf_label.index(analyzer_kernel_math_util_key)]
        bw_bound_math_util = op_perf[graph_perf_label.index(analyzer_bw_bound_math_util_key)]
        runtime = op_perf[graph_perf_label.index(analyzer_kernel_runtime_key)]
        runtime_per_input = op_perf[graph_perf_label.index(analyzer_kernel_runtime_per_input_key)]
        bw_bound_runtime = op_perf[graph_perf_label.index(analyzer_bw_bound_runtime_key)]
        bw_bound_runtime_per_input = op_perf[graph_perf_label.index(analyzer_bw_bound_runtime_per_input_key)]

        entry[analyzer_summary_label.index(analyzer_op_name_key)] = op_name
        entry[analyzer_summary_label.index(analyzer_first_last_input_key)] = first_last_input
        entry[analyzer_summary_label.index(analyzer_grid_size_key)] = grid_size
        entry[analyzer_summary_label.index(analyzer_bottleneck_key)] = bottleneck
        entry[analyzer_summary_label.index(analyzer_bw_limited_factor_key)] = bw_limited_factor
        entry[analyzer_summary_label.index(analyzer_measured_pipe_bw_key)] = measured_bw
        entry[analyzer_summary_label.index(analyzer_required_pipe_bw_key)] = required_bw
        entry[analyzer_summary_label.index(analyzer_kernel_math_util_key)] = math_util
        entry[analyzer_summary_label.index(analyzer_bw_bound_math_util_key)] = bw_bound_math_util
        entry[analyzer_summary_label.index(analyzer_kernel_runtime_key)] = runtime
        entry[analyzer_summary_label.index(analyzer_kernel_runtime_per_input_key)] = runtime_per_input
        entry[analyzer_summary_label.index(analyzer_bw_bound_runtime_key)] = bw_bound_runtime
        entry[analyzer_summary_label.index(analyzer_bw_bound_runtime_per_input_key)] = bw_bound_runtime_per_input
        summary_report.add_entry(entry)

    # The excel chart plots the bounded runtime of each core of each graph
    excel_chart = create_bounded_runtime_per_core_chart(per_core_graph_perf_label, sorted_core_perf)
    
    # The excel chart should cover all rows describing this graph
    row_range = (start_row_idx, start_row_idx + min(len(sorted_perf), num_slowest_ops) - 1)

    summary_report.add_excel_charts({row_range: excel_chart})
