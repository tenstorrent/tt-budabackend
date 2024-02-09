#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os
from typing import List, Union
from perf_test_base import *
from perf_comparison import (
    RUNTIME_FILE_NAME,
    GraphPerf,
)
from perf_analysis_base import (
    AnalysisMode,
    AnalysisConfig,
    run_original_test,
    analysis_enabled_for_graph,
    get_all_ops_for_epoch,
    append_to_csv_file,
    get_perf_output_dir_path,
    get_analysis_csv_labels,
    run_single_test,
)
from logger_utils import (
    logger,
    print_progress_bar,
)

REPORT_CSV_FILE_NAME = "report.csv"

def run_all_op_delays_for_epoch(analysis_config: AnalysisConfig, program, graph, all_ops, report_dir):

    previous_op_compiled = None
    csv_path = report_dir + f"report.csv"
    for op_idx, op_name in enumerate(all_ops.keys()):
        all_throughputs_current_op = (op_name,)
        for delay_iter in range(analysis_config.delay_config.num_steps):
            for i in range(2):
                print_progress_bar(
                    (op_idx * analysis_config.delay_config.num_steps + delay_iter) * 2 + i,
                    len(all_ops) * analysis_config.delay_config.num_steps * 2
                )
                if op_name in analysis_config.excluded_ops:
                    all_throughputs_current_op = ("N/A",)
                    logger.warning(f"Skipping triplet mode for op {op_name} since it was specified inside the exclude_op_list")
                    continue
                
                delay_in_out_str = "_in" if i == 0 else "_out"
                current_dir = analysis_config.test_build_dir or (report_dir + op_name + "_delay_iter_" + str(delay_iter) + delay_in_out_str + "/")
                if not os.path.exists(current_dir):
                    os.makedirs(current_dir)
                log_path = report_dir + "test_" + op_name + "_delay_iter_" + str(delay_iter) + delay_in_out_str + ".log"
                cmd_current_test = analysis_config.test_cmd.copy()
                
                if i == 0:
                    cmd_current_test += [f"--insert-kernel-delay", f"{op_name}:{analysis_config.delay_config.start + analysis_config.delay_config.step * delay_iter}-0-0"]
                else:
                    cmd_current_test += [f"--insert-kernel-delay", f"{op_name}:0-0-{analysis_config.delay_config.start + analysis_config.delay_config.step * delay_iter}"]
                if analysis_config.test_build_dir is not None:
                    cmd_current_test += [f"--run-only"]
                # if analysis_config.test_build_dir is not None:
                cmd_current_test += [f"--outdir", current_dir]
                if previous_op_compiled is not None:
                    cmd_current_test += [f"--reset-kernel-delay", previous_op_compiled]

                run_single_test(cmd_current_test, log_path, analysis_config.timeout)
                
                perf_output_dir = get_perf_output_dir_path(current_dir, program, graph)
                graph_perf = GraphPerf(perf_output_dir + RUNTIME_FILE_NAME)
                all_throughputs_current_op += (graph_perf.num_cycles_per_tensor,)

        append_to_csv_file(csv_path, list(all_throughputs_current_op))
        previous_op_compiled = op_name
    
    print_progress_bar(
        len(all_ops) * analysis_config.delay_config.num_steps * 2,
        len(all_ops) * analysis_config.delay_config.num_steps * 2
    )

def run_op_delay_analysis(analysis_config: AnalysisConfig):
    logger.info("Starting op delay analysis")
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
                logger.warning(f"WARNING - Skipping triplet mode for program {program_label} and graph {epoch_label}")
                continue
            logger.info(f"Running op delay analysis for program {program_label} and graph {epoch_label}")
            epoch_data = perf_info_original[program][epoch]
            perf_output_dir = epoch_data["output_directory"]
            
            all_ops = get_all_ops_for_epoch(perf_output_dir)
            original_graph_perf = GraphPerf(perf_output_dir + RUNTIME_FILE_NAME)
            
            logger.info(f"Total number of ops = {len(all_ops)}")
            
            report_dir = analysis_config.output_dir + f"output_id_{report_global_id}_program_{program_label}_graph_{epoch_label}/"
            assert not os.path.exists(report_dir)
            os.makedirs(report_dir)
            
            csv_path = report_dir + f"{REPORT_CSV_FILE_NAME}"
            if os.path.exists(csv_path):
                logger.warning(f"Cleaning the existing csv report file {csv_path}")
                os.remove(csv_path)
            logger.info(f"Generating the report for the current epoch udner {csv_path}")
            
            labels = get_analysis_csv_labels(analysis_config)
            append_to_csv_file(csv_path, labels)

            all_throughputs_original = ("-",)
            for delay_iter in range(analysis_config.delay_config.num_steps):
                all_throughputs_original += (original_graph_perf.num_cycles_per_tensor,)
            append_to_csv_file(csv_path, list(all_throughputs_original))
            run_all_op_delays_for_epoch(analysis_config, program, epoch, all_ops, report_dir)
    
            report_global_id += 1
