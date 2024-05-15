# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

KERNEL_REPORT_FILE_NAME_BASE = "kernel_perf_report"
PIPE_REPORT_FILE_NAME_BASE = "pipe_perf_report"
ALL_REPORT_FILE_NAME_BASE = "graph_perf_report"
ALL_REPORT_PER_OP_JSON_NAME_BASE = "graph_perf_report_per_op"
ALL_REPORT_SUMMARY_FILE_NAME_BASE = "analyzer_summary"
FORK_JOIN_REPORT_BASE = "fork_join_report"

analyzer_global_epoch_ids_key = "global_epoch_ids"
analyzer_program_names_key = "program_names"
analyzer_graph_name_key = "graph_name"
warnings_key = "warnings"
analyzer_core_coord_key = "core_coord"
analyzer_op_name_key = "op_name"
analyzer_grid_size_key = "grid_size"
analyzer_first_last_input_key = "first_to_last_input"

analyzer_operand_key = "operand"
analyzer_src_dest_key = "source/dest"
analyzer_pipe_type_key = "pipe_type"
analyzer_required_kernel_bw_key = "required_kernel_bw"
analyzer_pipe_bw_key = "pipe_bw"
analyzer_percent_diff_key = "percentage_of_difference"

analyzer_kernel_runtime_key = "kernel_total_runtime"
analyzer_kernel_runtime_per_input_key = "kernel_runtime_per_input"
analyzer_kernel_math_util_key = "kernel_math_utilization"

analyzer_model_runtime_per_input_key = "model_runtime_per_input"
analyzer_model_math_util_key = "model_math_utilization"

analyzer_bw_bound_runtime_key = "bw_bound_total_runtime"
analyzer_bw_bound_runtime_per_input_key = "bw_bound_runtime_per_input"
analyzer_bw_bound_math_util_key = "bw_bound_math_utilization"
analyzer_bw_limited_factor_key = "bw_limited_factor"
analyzer_slowest_operand_key = "slowest_operand"

analyzer_output_bw_key = "output_pipe_bw_0"
analyzer_required_output_bw_key = "required_output_pipe_bw_0"

analyzer_input_bw_prefix = "input_pipe_bw_"
analyzer_required_input_bw_prefix = "required_input_bw_"

analyzer_configs_key = "analyzer_configs"

analyzer_bottleneck_key = "bottleneck"
analyzer_required_pipe_bw_key = "required_pipe_bw"
analyzer_measured_pipe_bw_key = "measured_pipe_bw"
analyzer_summary_excel_chart_key = "bounded_runtime_plot_per_core"

def get_input_operand_bw_key(operand_idx: int):
    return analyzer_input_bw_prefix + str(operand_idx)

def get_input_operand_required_bw_key(operand_idx: int):
    return analyzer_required_input_bw_prefix + str(operand_idx)