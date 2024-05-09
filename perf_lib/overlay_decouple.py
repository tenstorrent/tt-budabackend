#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os
from typing import List, Union, Tuple
from perf_test_base import *
from perf_analyzer_api import *
from perf_comparison import (
    RUNTIME_FILE_NAME,
    EpochProp,
    MAX_NUM_INPUTS_PERF,
    AnalyzerOpPerf,
    CoreCoord,
    CorePerf,
    GraphPerf,
    AppendMode,
)
from perf_analysis_base import (
    AnalysisConfig,
    run_single_test,
    write_to_json_file,
)

from perf_analyzer_summary import (
    add_entries_to_summary_report,
)

from logger_utils import (
    logger,
    ASSERT,
)
from perf_graph import (
    Graph,
    AllGraphs,
)
from perf_report import PerfReport
from postprocess_api import *
import yaml
import shutil

GENERATE_OP_REPORT = True

class ReportType(Enum):
    Kernel      = 1
    Pipe        = 2
    KernelPipe  = 3
    Summary     = 4

def get_overlay_decouple_analysis_csv_labels(report_type: ReportType, generate_op_report: bool) -> List[str]:
    label = (analyzer_global_epoch_ids_key, analyzer_program_names_key, analyzer_graph_name_key, warnings_key,)
    if not generate_op_report:
        label += (analyzer_core_coord_key,)
    label += (analyzer_op_name_key, analyzer_first_last_input_key, analyzer_grid_size_key,)

    kernel_model_runtime_keys = (analyzer_kernel_runtime_key, analyzer_kernel_runtime_per_input_key, analyzer_model_runtime_per_input_key,)
    kernel_model_math_util_keys = (analyzer_kernel_math_util_key, analyzer_model_math_util_key,)
    if report_type == ReportType.Kernel:
        label += kernel_model_runtime_keys
        label += kernel_model_math_util_keys
    elif report_type == ReportType.Pipe:
        label += (analyzer_operand_key, analyzer_src_dest_key, analyzer_pipe_type_key, analyzer_required_kernel_bw_key, analyzer_pipe_bw_key, analyzer_percent_diff_key,)
    elif report_type == ReportType.KernelPipe:
        bw_bound_keys = (analyzer_bw_limited_factor_key, analyzer_slowest_operand_key, analyzer_bw_bound_runtime_key,
                         analyzer_bw_bound_runtime_per_input_key, analyzer_bw_bound_math_util_key,)
        label += kernel_model_runtime_keys
        label += kernel_model_math_util_keys
        label += bw_bound_keys
        label += (analyzer_output_bw_key, analyzer_required_output_bw_key,)
        for i in range (MAX_NUM_INPUTS_PERF):
            label += (get_input_operand_bw_key(i), get_input_operand_required_bw_key(i),)
    elif report_type == ReportType.Summary:
        label = (analyzer_graph_name_key, )

        if not generate_op_report:
            label += (analyzer_core_coord_key, )

        label += (
                analyzer_op_name_key,
                analyzer_first_last_input_key,
                analyzer_grid_size_key,
                analyzer_bottleneck_key,
                analyzer_bw_limited_factor_key,
                analyzer_measured_pipe_bw_key,
                analyzer_required_pipe_bw_key,
                analyzer_kernel_math_util_key,
                analyzer_bw_bound_math_util_key,
                analyzer_summary_excel_chart_key,
                analyzer_kernel_runtime_key,
                analyzer_kernel_runtime_per_input_key,
                analyzer_bw_bound_runtime_key,
                analyzer_bw_bound_runtime_per_input_key,
            )
    else:
        raise Exception(f"Invalid report type {report_type}")

    return list(label)

def get_overlay_decouple_analysis_excel_configs(report_type: ReportType):
    # Always freeze the first row consisting of labels
    report_config = {
        "freeze_pane": "A2",
    }
    if report_type == ReportType.Kernel:
        labels_to_separate = {
            analyzer_kernel_runtime_key: [1]
        }
        labels_to_bold = [
            analyzer_kernel_runtime_key,
            analyzer_kernel_math_util_key
        ]
        labels_to_highlight_graded_color = [
            analyzer_kernel_math_util_key
        ]
        report_config["labels_to_separate"] = labels_to_separate
        report_config["labels_to_bold"] = labels_to_bold
        report_config["labels_to_highlight_graded_color"] = labels_to_highlight_graded_color
    elif report_type == ReportType.Pipe:
        labels_to_separate = {
            analyzer_operand_key: [1],
            analyzer_required_kernel_bw_key: [1],
        }
        labels_to_bold = [
            analyzer_required_kernel_bw_key,
            analyzer_pipe_bw_key,
            analyzer_percent_diff_key,
        ]
        report_config["labels_to_separate"] = labels_to_separate
        report_config["labels_to_bold"] = labels_to_bold
    elif report_type == ReportType.KernelPipe:
        labels_to_separate = {
            analyzer_kernel_runtime_key: [1],
            analyzer_bw_limited_factor_key: [1],
            analyzer_output_bw_key: [1],
            get_input_operand_bw_key(0): [1],
        }
        labels_to_bold = [
            analyzer_bw_bound_runtime_key,
            analyzer_bw_bound_runtime_per_input_key,
            analyzer_bw_bound_math_util_key,
            analyzer_bw_limited_factor_key,
        ]

        labels_to_highlight_graded_color = [
            analyzer_bw_bound_math_util_key,
        ]

        report_config["labels_to_separate"] = labels_to_separate
        report_config["labels_to_bold"] = labels_to_bold
        report_config["labels_to_highlight_graded_color"] = labels_to_highlight_graded_color

    elif report_type == ReportType.Summary:
        larger_is_better_labels = [analyzer_kernel_math_util_key, analyzer_bw_bound_math_util_key]
        labels_to_bold = [analyzer_graph_name_key, analyzer_bottleneck_key]
        labels_to_merge_cells = [analyzer_graph_name_key]
        freeze_pane = "C2"
        excel_chart_label = analyzer_summary_excel_chart_key
        report_config["larger_is_better_labels"] = larger_is_better_labels
        report_config["labels_to_bold"] = labels_to_bold
        report_config["labels_to_merge_cells"] = labels_to_merge_cells
        report_config["freeze_pane"] = freeze_pane
        report_config["excel_chart_label"] = excel_chart_label
    else:
        raise Exception(f"Invalid report type {report_type}")

    return report_config

def get_perf_info(perf_output_dir: str):
    perf_info_path = f"{perf_output_dir}/{PERF_INFO_FILE_NAME}"
    ASSERT(
        os.path.exists(perf_info_path),
        f"perf_info output file does not exist under {perf_info_path}. This might be because the test itself failed or it was run without perf dump enabled. Please check the test log."
    )

    perf_info = None
    with open(perf_info_path) as perf_info_file:
        perf_info = yaml.load(perf_info_file, Loader=yaml.FullLoader)

    return perf_info


def run_overlay_decouple_test(analysis_config: AnalysisConfig, output_dir: str, log_path: str):

    logger.info("Running the overlay decouple test")
    cmd_current_test = analysis_config.test_cmd.copy()
    if analysis_config.test_build_dir is not None:
        cmd_current_test += [f"--outdir", analysis_config.test_build_dir]
    os.environ[BACKEND_PERF_ANALYZER_ENV_VAR] = '1'
    os.environ[PERF_OUTPUT_DIR_ENV_VAR] = output_dir
    run_single_test(cmd_current_test, log_path, None)
    del os.environ[PERF_OUTPUT_DIR_ENV_VAR]
    del os.environ[BACKEND_PERF_ANALYZER_ENV_VAR]

def get_all_perf(graph_perf:GraphPerf, graph: Graph, generate_op_report: bool):

    program_names = graph_perf.program_names
    graph_name = graph_perf.graph_name
    global_epoch_ids = graph_perf.global_epoch_ids
    all_cores_kernels = []
    all_cores_pipes = []
    all_cores_results = []

    core_op_perf: Union[Dict[str, AnalyzerOpPerf], Dict[CoreCoord, CorePerf]] = graph_perf.analyzer_op_perf if generate_op_report else graph_perf.all_cores
    for coord_label, core_perf in core_op_perf.items():
        op_name = coord_label if generate_op_report else coord_label.op_name
        ethernet_outputs = graph.ops[op_name].ethernet_output
        if (len(ethernet_outputs) > 0):
            core_perf.inaccuracy.append_ethernet_output(ethernet_outputs)
        if (len(graph.get_input_operands_for_op(op_name)) > MAX_NUM_INPUTS_PERF):
            core_perf.inaccuracy.NotAllInputsRecorded = True
        warnings = core_perf.inaccuracy.str()
        coord = "-" if generate_op_report else coord_label.str(True)
        grid_size = str(graph.get_grid_size_for_op(op_name))
        all_input_trisc_bw = []
        all_input_brisc_bw = []

        kernel_temp = [global_epoch_ids, program_names, graph_name, warnings]
        if not generate_op_report:
            kernel_temp += [coord]
        kernel_temp += [
            op_name,
            f"{core_perf.first_input_recorded}->{core_perf.last_input_recorded}",
            grid_size,
            core_perf.total_runtime,
            ROUND(core_perf.runtime_per_input, 0),
            core_perf.model_num_cycles,
            ROUND(core_perf.average_math_utilization),
            ROUND(core_perf.model_math_utilization),
        ]
        all_cores_kernels.append(kernel_temp)

        inputs = graph.get_input_operands_for_op(op_name)
        for input_idx, input_name in enumerate(inputs):
            if input_idx >= MAX_NUM_INPUTS_PERF:
                logger.warning(f"Skipping input-idx {input_idx} for op {op_name}. Performance infra only supports up to {MAX_NUM_INPUTS_PERF} input-operands currently")
                continue
            is_queue = graph.is_queue(input_name)
            required_kernel_bw = ROUND(core_perf.get_trisc_input_bw(input_idx))
            all_input_trisc_bw.append(core_perf.get_trisc_input_bw(input_idx))
            actual_pipe_bw = ROUND(core_perf.get_brisc_input_bw(input_idx))
            all_input_brisc_bw.append(core_perf.get_brisc_input_bw(input_idx))
            percentage_of_difference = get_diff_percentage(required_kernel_bw, actual_pipe_bw)

            kernel_temp = [global_epoch_ids, program_names, graph_name, warnings]
            if not generate_op_report:
                kernel_temp += [coord]
            kernel_temp += [op_name, f"{core_perf.first_input_recorded}->{core_perf.last_input_recorded}", grid_size]
            kernel_temp += [
                f"input-{input_idx}",
                input_name,
                "input-queue-pipe" if is_queue else "input-op-pipe",
                required_kernel_bw,
                actual_pipe_bw,
                percentage_of_difference,
            ]

            all_cores_pipes.append(kernel_temp)

        outputs = graph.get_output_operands_for_op(op_name)
        output_name_all = ""
        any_queue = False
        any_op = False
        pipe_type = ""
        for output_idx, output_name in enumerate(outputs):
            if len(outputs) > 1:
                output_name_all += f"<{output_name}>"
            else:
                output_name_all += f"{output_name}"
            if graph.is_queue(output_name):
                any_queue = True
            else:
                any_op = True
        if any_queue and any_op:
            pipe_type += "output-queue/op-pipe"
        elif any_queue:
            pipe_type = "output-queue-pipe"
        elif any_op:
            pipe_type = "output-op-pipe"
        else:
            pipe_type = "no-op-output-pipe"

        required_output_bw = ROUND(core_perf.get_trisc_output_bw())
        actual_output_bw = ROUND(core_perf.get_output_decouple_bw())
        percentage_of_difference_output = ROUND(get_diff_percentage(required_output_bw, actual_output_bw))

        kernel_temp = [global_epoch_ids, program_names, graph_name, warnings]
        if not generate_op_report:
            kernel_temp += [coord]
        kernel_temp += [op_name, f"{core_perf.first_input_recorded}->{core_perf.last_input_recorded}", grid_size]
        kernel_temp += [
            f"output",
            output_name_all,
            pipe_type,
            required_output_bw,
            actual_output_bw,
            percentage_of_difference_output,
        ]

        all_cores_pipes.append(kernel_temp)

        core_results = [global_epoch_ids, program_names, graph_name, warnings]
        if not generate_op_report:
            core_results += [coord]
        core_results += [op_name, f"{core_perf.first_input_recorded}->{core_perf.last_input_recorded}", grid_size]

        core_results.append(ROUND(core_perf.total_runtime, 0))
        core_results.append(ROUND(core_perf.runtime_per_input, 0))
        core_results.append(core_perf.model_num_cycles)
        core_results.append(ROUND(core_perf.average_math_utilization))
        core_results.append(ROUND(core_perf.model_math_utilization))

        core_results.append(ROUND(core_perf.biggest_bw_factor))
        core_results.append(core_perf.biggest_bw_factor_operand)

        core_results.append(ROUND(core_perf.total_runtime_bw_scaled))
        core_results.append(ROUND(core_perf.runtime_per_input_bw_scaled, 0))
        core_results.append(ROUND(core_perf.average_math_utilization_bw_scaled))

        core_results.append(actual_output_bw)
        core_results.append(required_output_bw)
        for i in range(len(inputs)):
            if i >= MAX_NUM_INPUTS_PERF:
                logger.warning(f"Skipping input-idx {i} for op {op_name}. Performance infra only supports up to {MAX_NUM_INPUTS_PERF} input-operands currently")
                continue
            input_brisc_bw = ROUND(all_input_brisc_bw[i])
            core_results.append(input_brisc_bw)
            input_trisc_bw = ROUND(all_input_trisc_bw[i])
            core_results.append(input_trisc_bw)

        all_cores_results.append(core_results)

    return (all_cores_kernels, all_cores_pipes, all_cores_results)

def get_overlay_decouple_per_op_json(sorted_core_perf: List[List[str]], sorted_op_perf: List[List[str]]):
    attributes_key = "op-attributes"
    op_measurements_key = "op-measurements"
    core_measurements_key = "core-measurements"
    json_report = {}

    # Dump per-op measurements
    per_op_labels = get_overlay_decouple_analysis_csv_labels(ReportType.KernelPipe, True)

    op_metadata_key_index_map: Dict[str, int] = {
        analyzer_global_epoch_ids_key: per_op_labels.index(analyzer_global_epoch_ids_key),
        analyzer_program_names_key: per_op_labels.index(analyzer_program_names_key),
        analyzer_graph_name_key: per_op_labels.index(analyzer_graph_name_key),
        analyzer_first_last_input_key: per_op_labels.index(analyzer_first_last_input_key),
        analyzer_grid_size_key: per_op_labels.index(analyzer_grid_size_key),

    }

    op_name_index = per_op_labels.index(analyzer_op_name_key)
    for op_entry in sorted_op_perf:
        op_name = op_entry[op_name_index]
        if op_name not in json_report:
            json_report[op_name] = {}
            json_report[op_name][attributes_key] = {}
            json_report[op_name][op_measurements_key] = {}

        # These are properties of the op and can be recorded once per op
        for metadata_key, metadata_key_index in op_metadata_key_index_map.items():
            json_report[op_name][attributes_key][metadata_key] = op_entry[metadata_key_index]

        for i, measurement in enumerate(op_entry):
            measurement_key = per_op_labels[i]
            if measurement_key != analyzer_op_name_key and measurement_key not in op_metadata_key_index_map:
                json_report[op_name][op_measurements_key][measurement_key] = measurement

    # Dump per-core measurements
    per_core_labels = get_overlay_decouple_analysis_csv_labels(ReportType.KernelPipe, False)
    core_coord_index = per_core_labels.index(analyzer_core_coord_key)
    op_name_index = per_core_labels.index(analyzer_op_name_key)

    for core_entry in sorted_core_perf:
        core_coord = "-".join(core_entry[core_coord_index].split(","))
        op_name = core_entry[op_name_index]
        ASSERT(op_name in json_report, "Op name should be recorded in report")

        if core_measurements_key not in json_report[op_name]:
            json_report[op_name][core_measurements_key] = {}

        json_report[op_name][core_measurements_key][core_coord] = {}
        for i, measurement in enumerate(core_entry):
            measurement_key = per_core_labels[i]
            if measurement_key != analyzer_op_name_key and measurement_key != analyzer_core_coord_key and measurement_key not in op_metadata_key_index_map:
                json_report[op_name][core_measurements_key][core_coord][measurement_key] = measurement


    return json_report

def run_overlay_decouple(analysis_config: AnalysisConfig):
    logger.info("Starting overlay decouple analysis")
    if not analysis_config.skip_run:
        decoupled_dir = analysis_config.output_dir + "decoupled_test/"
        if not os.path.exists(decoupled_dir):
            os.makedirs(decoupled_dir)
        decoupled_log_path = decoupled_dir + "test_decoupled.log"
        run_overlay_decouple_test(analysis_config, decoupled_dir, decoupled_log_path)
        perf_results_dir = decoupled_dir + "/perf_results/"
        reports_output_dir = analysis_config.output_dir
        logger.info("Finished running the decoupled test")
    else:
        perf_results_dir = analysis_config.input_perf_results_dir
        reports_output_dir = perf_results_dir + "/analyzer_results/"
        if os.path.exists(reports_output_dir):
            shutil.rmtree(reports_output_dir)
        os.mkdir(reports_output_dir)
        logger.info(f"Skipping test run since --skip-run is set. Reading perf results from {perf_results_dir}")

    ASSERT(os.path.exists(perf_results_dir + "/graph_descriptor/"), f"graph_descriptor directory does not exist under {perf_results_dir}")
    perf_info_decoupled = get_perf_info(perf_results_dir)
    all_graphs = AllGraphs(perf_results_dir).all_graphs

    graph_name_to_perf: Dict[str, Tuple[Graph, GraphPerf]] = {}
    summary_labels = get_overlay_decouple_analysis_csv_labels(report_type=ReportType.Summary, generate_op_report=GENERATE_OP_REPORT)
    summary_report_configs = get_overlay_decouple_analysis_excel_configs(report_type=ReportType.Summary)
    summary_report = PerfReport(labels=summary_labels, name="overlay_decouple_summary_report")
    summary_report.set_configs(summary_report_configs)
    for program in perf_info_decoupled.keys():
        if program == "global-events":
            continue
        logger.info(f"program_name {program}")
        assert "-" in program, "Format of program label in perf-info must be #-PROGRAM_NAME"

        program_label = program[program.find("-")+1:]
        epochs = perf_info_decoupled[program]
        for epoch in epochs:
            assert "-" in epoch, "Format of program label in perf-info must be #-GRAPH_NAME"
            epoch_label = epoch[epoch.find("-")+1:]
            epoch_data = perf_info_decoupled[program][epoch]
            epoch_perf_output_dir = epoch_data["output_directory"]

            decoupled_perf = EpochProp(epoch_perf_output_dir + RUNTIME_FILE_NAME)
            graph_perf = decoupled_perf.graph_perf
            ASSERT(epoch_label in all_graphs, f"graph with name {epoch_label} is not generated")
            graph = all_graphs[epoch_label]
            if graph_perf.graph_name in graph_name_to_perf:
                graph_name_to_perf[graph_perf.graph_name][1].append_graph(graph_perf, AppendMode.Average)
            else:
                graph_name_to_perf[graph_perf.graph_name] = (graph, graph_perf)

    for graph_name, (graph, graph_perf) in graph_name_to_perf.items():

        graph_dir_name = reports_output_dir + graph_name + "/"
        ASSERT(not os.path.exists(graph_dir_name), f"Analyzer results for graph {graph_name} exist under dir {graph_dir_name}")
        # logger.info(f"Generating the report for graph {graph_name} under path {graph_dir_name}")
        os.mkdir(graph_dir_name)

        graph_perf.populate_analyzer_op_perf()
        (all_kernel_perf_epoch_per_op, all_pipe_perf_epoch_per_op, all_perf_epoch_per_op) = get_all_perf(graph_perf, graph, True)
        (all_kernel_perf_epoch_per_core, all_pipe_perf_epoch_per_core, all_perf_epoch_per_core) = get_all_perf(graph_perf, graph, False)

        ##### Kernel report #####
        kernel_csv_path, kernel_excel_path, kernel_txt_path = [
            graph_dir_name + KERNEL_REPORT_FILE_NAME_BASE + ".csv",
            graph_dir_name + KERNEL_REPORT_FILE_NAME_BASE + ".xlsx",
            graph_dir_name + KERNEL_REPORT_FILE_NAME_BASE + ".txt",
        ]
        kernel_labels = get_overlay_decouple_analysis_csv_labels(ReportType.Kernel, GENERATE_OP_REPORT)
        all_kernel_perf_epoch = all_kernel_perf_epoch_per_op if GENERATE_OP_REPORT else all_kernel_perf_epoch_per_core
        sorted_perf = sorted(all_kernel_perf_epoch, key=lambda x: (isinstance(x[kernel_labels.index("kernel_total_runtime")], (float, int)), x[kernel_labels.index("kernel_total_runtime")]), reverse=True)
        kernel_report_configs = get_overlay_decouple_analysis_excel_configs(ReportType.Kernel)
        kernel_report = PerfReport(labels=kernel_labels, initial_entries=sorted_perf, name="overlay_decouple_kernel_report")
        kernel_report.set_configs(kernel_report_configs)
        kernel_report.generate_csv(kernel_csv_path)
        kernel_report.write_table_str_to_file(kernel_txt_path)

        ##### Pipe report #####
        pipe_csv_path, pipe_excel_path, pipe_txt_path = [
            graph_dir_name + PIPE_REPORT_FILE_NAME_BASE + ".csv",
            graph_dir_name + PIPE_REPORT_FILE_NAME_BASE + ".xlsx",
            graph_dir_name + PIPE_REPORT_FILE_NAME_BASE + ".txt",
        ]
        pipe_labels = get_overlay_decouple_analysis_csv_labels(ReportType.Pipe, GENERATE_OP_REPORT)
        all_pipe_perf_epoch = all_pipe_perf_epoch_per_op if GENERATE_OP_REPORT else all_pipe_perf_epoch_per_core
        sorted_perf = sorted(all_pipe_perf_epoch, key=lambda x: (isinstance(x[pipe_labels.index("percentage_of_difference")], float), x[pipe_labels.index("percentage_of_difference")]))
        pipe_report_configs = get_overlay_decouple_analysis_excel_configs(ReportType.Pipe)
        pipe_report = PerfReport(labels=pipe_labels, initial_entries=sorted_perf, name="overlay_decouple_pipe_report")
        pipe_report.set_configs(pipe_report_configs)
        pipe_report.generate_csv(pipe_csv_path)
        pipe_report.write_table_str_to_file(pipe_txt_path)

        ##### Kernel+Pipe report #####
        all_csv_path, all_excel_path, all_txt_path, all_op_json_path = [
            graph_dir_name + ALL_REPORT_FILE_NAME_BASE + ".csv",
            graph_dir_name + ALL_REPORT_FILE_NAME_BASE + ".xlsx",
            graph_dir_name + ALL_REPORT_FILE_NAME_BASE + ".txt",
            graph_dir_name + ALL_REPORT_PER_OP_JSON_NAME_BASE + ".json",
        ]
        all_labels = get_overlay_decouple_analysis_csv_labels(ReportType.KernelPipe, GENERATE_OP_REPORT)
        sorted_op_perf = sorted(all_perf_epoch_per_op, key=lambda x: (isinstance(x[all_labels.index("bw_bound_total_runtime")], (float, int)), x[all_labels.index("bw_bound_total_runtime")]), reverse=True)
        sorted_core_perf = sorted(all_perf_epoch_per_core, key=lambda x: (isinstance(x[all_labels.index("bw_bound_total_runtime")], (float, int)), x[all_labels.index("bw_bound_total_runtime")]), reverse=True)
        sorted_perf = sorted_op_perf if GENERATE_OP_REPORT else sorted_core_perf
        all_report_configs = get_overlay_decouple_analysis_excel_configs(ReportType.KernelPipe)
        all_report = PerfReport(labels=all_labels, initial_entries=sorted_perf, name="overlay_decouple_graph_report")
        all_report.set_configs(all_report_configs)
        all_report.generate_csv(all_csv_path)
        all_report.write_table_str_to_file(all_txt_path)
        # all_report.create_excel_worksheet(all_excel_path)
        graph_per_op_json = get_overlay_decouple_per_op_json(sorted_core_perf, sorted_op_perf)

        write_to_json_file(all_op_json_path, graph_per_op_json)

        add_entries_to_summary_report(
            summary_report=summary_report,
            graph_perf_label=get_overlay_decouple_analysis_csv_labels(ReportType.KernelPipe, GENERATE_OP_REPORT),
            per_core_graph_perf_label=get_overlay_decouple_analysis_csv_labels(ReportType.KernelPipe, False),
            sorted_perf=sorted_perf,
            sorted_core_perf=sorted_core_perf,
            generate_op_report=GENERATE_OP_REPORT,
        )

    ##### Summary report #####
    all_summary_csv_path, all_summary_xlsx_path, all_summary_txt_path = [
        reports_output_dir + ALL_REPORT_SUMMARY_FILE_NAME_BASE + ".csv",
        reports_output_dir + ALL_REPORT_SUMMARY_FILE_NAME_BASE + ".xlsx",
        reports_output_dir + ALL_REPORT_SUMMARY_FILE_NAME_BASE + ".txt",
    ]
    summary_report.generate_csv(all_summary_csv_path)
    summary_report.write_table_str_to_file(all_summary_txt_path)
    # summary_report.create_excel_worksheet(all_summary_xlsx_path)
