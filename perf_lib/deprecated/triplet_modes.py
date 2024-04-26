#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os
from typing import List, Union
from perf_test_base import *
from perf_comparison import (
    RUNTIME_FILE_NAME,
    EpochProp,
)
import csv
from logger_utils import logger, print_progress_bar
from perf_analysis_base import (
    AnalysisConfig,
    TEST_OUTPUT_DIR,
    run_original_test,
    get_all_ops_for_epoch,
    get_perf_output_dir_path,
    append_to_csv_file,
    append_to_csv_file_multiple_entries,
    analysis_enabled_for_graph,
    reset_silicon,
    run_single_test,
)

TRIPLET_MODE_CSV_REPORT = "all_triplet_modes.csv"
TRIPLET_MODE_CSV_REPORT_SORTED = "all_triplet_modes_sorted.csv"
ORIGINAL_CSV_REPORT = "original_report.csv"

def get_triplet_mode_csv_label():
    labels = [
        "op_name",
        "core_coord",
        "global_epoch_id",
        "program_name",
        "graph_name",
        "device_id",
        "aiclk",
        "num_cycles_per_tensor",
        "num_tensors_per_second",
        
        "first_and_last_inputs_recorded",
        "total_math_utilization",
        "total_runtime",
        "input0_bw",
        "input1_bw",
        "output_bw",
        
        "single_input_measurements_idx",
        "math_utilization_single_input",
        "execution_cycles_single_input",
    ]
    return labels
def get_perf_results_csv_for_op_name(epoch_prop: EpochProp, op_name: str):
    graph_perf = epoch_prop.graph_perf
    target_cores, all_perf_results = graph_perf.get_perf_results_for_target_op_last_input(op_name)
    assert len(target_cores) > 0, f"The op name {op_name} must exist in the perf results"
    results = []
    for perf_result in all_perf_results:
        single_result = (
            op_name,
            f"{perf_result.target_core}".replace('-', ','),
            graph_perf.global_epoch_id,
            epoch_prop.program_name,
            epoch_prop.graph_name,
            graph_perf.device_id,
            graph_perf.aiclk,
            graph_perf.num_cycles_per_tensor,
            graph_perf.num_tensors_per_second,
            
            f"{perf_result.first_input_recorded}->{perf_result.last_input_recorded}",
            perf_result.average_math_utilization,
            perf_result.total_runtime,
            perf_result.input0_bw,
            perf_result.input1_bw,
            perf_result.output_bw,
            
            f"{perf_result.target_input}",
            perf_result.math_utilization,
            perf_result.execution_cycles,
        )
        
        single_result_round = ()
        for val in single_result:
            single_result_round += (round(val, 2) if isinstance(val, float) else val, )
        
        results.append(list(single_result_round))
    return results

def run_all_triplets_for_epoch(analysis_config: AnalysisConfig, program, graph, all_ops, report_dir) -> EpochProp:

    previous_op_compiled = None
    for op_idx, op_name in enumerate(all_ops.keys()):
        print_progress_bar(op_idx, len(all_ops))
        if op_name in analysis_config.excluded_ops:
            logger.warning(f"Skipping triplet mode for op {op_name} since it was specified inside the exclude_op_list")
            continue
        
        current_dir = analysis_config.test_build_dir or (report_dir + op_name + "/")
        if not os.path.exists(current_dir):
            os.makedirs(current_dir)
        log_path = report_dir + f"test_{op_name}.log"
        cmd_current_test = analysis_config.test_cmd.copy()
        if analysis_config.test_build_dir is not None:
            cmd_current_test += [f"--run-only"]
        cmd_current_test += [f"--triplet-mode", op_name]
        cmd_current_test += [f"--outdir", current_dir]
        if previous_op_compiled is not None:
            cmd_current_test += [f"--reset-triplet-mode", previous_op_compiled]
        
        try:
            run_single_test(cmd_current_test, log_path, analysis_config.timeout)
        except Exception:
            logger.error("Test timeout. Possible hang")
            if analysis_config.reset_silicon_after_hangs:
                logger.warning("Resetting silicon because of a hang. Please ensure device has a reset dongle.")
                log_path_hang = report_dir + f"test_{op_name}_reset.log"
                reset_silicon(log_path_hang)
            else:
                raise Exception(
                    "Test timeout. Possible hang, and --reset-silicon-after-hangs option is not enabled. Please only use this option in case device has a reset dongle. Otherwise include this op name in --exclude-op-list list"
                )
        
        perf_output_dir = get_perf_output_dir_path(current_dir, program, graph)
        epoch_prop = EpochProp(perf_output_dir + RUNTIME_FILE_NAME)
        epoch_prop.graph_name = graph
        epoch_prop.program_name = program
        perf_results_csv = get_perf_results_csv_for_op_name(epoch_prop, op_name)
        triplet_modes_csv_path = report_dir + TRIPLET_MODE_CSV_REPORT
        append_to_csv_file_multiple_entries(triplet_modes_csv_path, perf_results_csv)
        
        previous_op_compiled = op_name
    
    print_progress_bar(len(all_ops), len(all_ops))

def run_triplet_analysis(analysis_config: AnalysisConfig):
    logger.info("Starting triplet mode analysis")
    original_dir = analysis_config.output_dir + "original_test/"
    if not os.path.exists(original_dir):
        os.makedirs(original_dir)
    log_path = original_dir + "test.log"
    perf_info_original = run_original_test(analysis_config, original_dir, log_path)

    report_global_id = 0
    for program in perf_info_original.keys():
        if program == "global-events":
            continue
        assert "-" in program, "Format of program label in perf-info must be #-PROGRAM_NAME"
        program_label = program[program.find("-")+1:]
        epochs = perf_info_original[program]
        for epoch in epochs:
            assert "-" in epoch, "Format of program label in perf-info must be #-GRAPH_NAME"
            epoch_label = epoch[epoch.find("-")+1:]
            if not analysis_enabled_for_graph(analysis_config, program_label, epoch_label):
                logger.warning(f"Skipping triplet mode for program {program_label} and graph {epoch_label}")
                continue
            
            logger.info(f"Running triplet mode for program {program_label} and graph {epoch_label}")
            epoch_data = perf_info_original[program][epoch]
            perf_output_dir = epoch_data["output_directory"]
            
            all_ops = get_all_ops_for_epoch(perf_output_dir)
            logger.debug(f"perf_output_dir = {perf_output_dir}")
            original_epoch_perf = EpochProp(perf_output_dir + RUNTIME_FILE_NAME)
            original_epoch_perf.graph_name = epoch
            original_epoch_perf.program_name = program

            logger.info(f"Total number of ops = {len(all_ops)}")
            report_dir = analysis_config.output_dir + f"output_id_{report_global_id}_program_{program_label}_graph_{epoch_label}/"
            assert not os.path.exists(report_dir)
            os.makedirs(report_dir)

            triplet_modes_csv_path = report_dir + TRIPLET_MODE_CSV_REPORT
            original_csv_path = report_dir + ORIGINAL_CSV_REPORT

            label_perf_csv = get_triplet_mode_csv_label()
            append_to_csv_file(triplet_modes_csv_path, label_perf_csv)
            append_to_csv_file(original_csv_path, label_perf_csv)
            for op_name in all_ops.keys():
                original_perf_op = get_perf_results_csv_for_op_name(original_epoch_perf, op_name)
                append_to_csv_file_multiple_entries(original_csv_path, original_perf_op)

            run_all_triplets_for_epoch(analysis_config, program, epoch, all_ops, report_dir)

            with open(triplet_modes_csv_path, 'r') as final_report:
                final_report_triplet_mode = csv.reader(final_report)
                rows = list(final_report_triplet_mode)
            
            total_runtime_index = list(rows[0]).index('total_runtime')
            sorted_rows = sorted(rows[1:], key=lambda x: int(x[total_runtime_index]), reverse=True)
            sorted_rows.insert(0, rows[0])
            sorted_csv_path = report_dir + TRIPLET_MODE_CSV_REPORT_SORTED
            append_to_csv_file_multiple_entries(sorted_csv_path, sorted_rows)
