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
    GraphPerf,
)
from perf_analysis_base import (
    AnalysisConfig,
    run_original_test,
    run_single_test,
)

from logger_utils import (
    logger,
    ASSERT,
    COLORS,
    redirect_log_to_file,
    disable_log_file,
)
from perf_graph import (
    AllGraphs,
    ForkNode,
    get_non_overlap_fork_joins,
)
from perf_report import PerfReport
from postprocess_api import *
import yaml
import shutil
import time
from tqdm import tqdm

def get_performance_for_test(perf_info, all_graphs: AllGraphs) -> Dict[str, GraphPerf]:

    graph_perf_results: Dict[str, GraphPerf] = {}
    for program in perf_info.keys():
        if program == "global-events":
            continue
        logger.info(f"program_name {program}")
        assert "-" in program, "Format of program label in perf-info must be #-PROGRAM_NAME"
        
        program_label = program[program.find("-")+1:]
        epochs = perf_info[program]
        for epoch in epochs:
            assert "-" in epoch, "Format of program label in perf-info must be #-GRAPH_NAME"
            epoch_label = epoch[epoch.find("-")+1:]
            epoch_data = perf_info[program][epoch]
            epoch_perf_output_dir = epoch_data["output_directory"]
            
            decoupled_perf = EpochProp(epoch_perf_output_dir + RUNTIME_FILE_NAME)
            graph_perf = decoupled_perf.graph_perf
            graph_perf.populate_analyzer_op_perf()
            ASSERT(epoch_label in all_graphs.all_graphs, f"graph with name {epoch_label} is not generated")
            graph_perf_results[graph_perf.graph_name] = graph_perf
    
    return graph_perf_results

def get_overlay_decouple_config_for_fork_group_initial_run(fork_join_group: List[ForkNode]) -> List[str]:
    decouplings = []
    for node in fork_join_group:
        decouplings.append(f"{node.fork_op_name}:Input")
        decouplings.append(f"{node.join_op_name}:Output")
        for op_after in node.ops_after_join_name:
            decouplings.append(f"{op_after}:Input")
    
    return decouplings

def get_overlay_decouple_config_for_fork_group_second_run(fork_join_group: List[ForkNode], all_graphs: AllGraphs) -> List[str]:
    decouplings = get_overlay_decouple_config_for_fork_group_initial_run(fork_join_group)
    for node in fork_join_group:
        graph_name = node.graph_name
        shorter_branch = node.first_fork_path if len(node.first_fork_path) < len(node.second_fork_path) else node.second_fork_path
        if not len(shorter_branch):
            feeder = node.fork_op_name
        else:
            join_inputs = all_graphs.all_graphs[graph_name].get_input_operands_for_op(node.join_op_name)
            feeder_set = set(join_inputs).intersection(shorter_branch)
            ASSERT(len(feeder_set) == 1, f"join op {node.join_op_name} must have exactly one input inside the shorted fork-join-path {shorter_branch}. fork_op: {node.fork_op_name}")
            iterator = iter(feeder_set)
            feeder = next(iterator)
        
        operand_idx = all_graphs.all_graphs[graph_name].get_input_operand_idx_for_op(node.join_op_name, feeder)
        decouplings.append(f"{node.join_op_name}:Input{operand_idx}")
    
    return decouplings

def run_overlay_decoupled_test(analysis_config: AnalysisConfig, fork_join_group: List[ForkNode], is_initial_test: bool, all_graphs: AllGraphs, output_dir: str, log_path: str):
    cmd_current_test = analysis_config.test_cmd.copy()
    os.environ[PERF_OUTPUT_DIR_ENV_VAR] = output_dir
    if analysis_config.test_build_dir is not None:
        cmd_current_test += [f"--outdir", analysis_config.test_build_dir]
    
    if is_initial_test:
        decouplings = get_overlay_decouple_config_for_fork_group_initial_run(fork_join_group)
    else:
        decouplings = get_overlay_decouple_config_for_fork_group_second_run(fork_join_group, all_graphs)
    
    write_list_to_file(output_dir, "overlay_decouplings_desc.txt", decouplings)

    os.environ[BACKEND_OVERLAY_DECOUPLE_DESC_PATH_ENV_VAR] = output_dir + "/" + "overlay_decouplings_desc.txt"
    
    run_single_test(cmd_current_test, log_path, None)
    
    perf_info_path = f"{output_dir}/perf_results/{PERF_INFO_FILE_NAME}"
    ASSERT(
        os.path.exists(perf_info_path),
        f"perf_info output file does not exist under {perf_info_path}. This might be because the test itself failed or it was run without perf dump enabled. Please check the test log."
    )
    perf_info = None
    with open(perf_info_path) as perf_info_file:
        perf_info = yaml.load(perf_info_file, Loader=yaml.FullLoader)

    del os.environ[PERF_OUTPUT_DIR_ENV_VAR]
    del os.environ[BACKEND_OVERLAY_DECOUPLE_DESC_PATH_ENV_VAR]
    return perf_info

def run_fork_join_analysis(analysis_config: AnalysisConfig):
    logger.info("Starting fork-join performance analysis")
    original_runtime = None
    os.environ[BACKEND_FORK_JOIN_ANALYZER_ENV_VAR] = "1"
    if not analysis_config.skip_run:
        original_dir = analysis_config.output_dir + "original_test/"
        if not os.path.exists(original_dir):
            os.makedirs(original_dir)
        log_path = original_dir + "test.log"
        logger.info("Running the original test")
        start_time = time.time()
        _ = run_original_test(analysis_config, original_dir, log_path)
        end_time = time.time()
        original_runtime = end_time - start_time
        perf_results_dir = original_dir + "perf_results/"
    else:
        perf_results_dir = analysis_config.input_perf_results_dir + "original_test/perf_results/"
    
    all_graphs_ds = AllGraphs(perf_results_dir)
    all_graphs = all_graphs_ds.all_graphs

    # 2d list of ForkNode's
    # All ForkNodes within each sublist can be processed in parallel and have no mutual nodes
    all_forks = all_graphs_ds.get_all_fork_joins()
    fork_join_groups: List[List[ForkNode]] = get_non_overlap_fork_joins(all_forks)

    total_num_forks = len(all_forks)
    logger.info(f"Total number of Fork/Joins = {total_num_forks}")
    logger.info(f"Number of fork groups = {len(fork_join_groups)}")
    total_num_tests = 2 * len(fork_join_groups)
    logger.info(f"Total number of iterations required = {total_num_tests}")
    if original_runtime:
        logger.info(f"Estimated total time of analysis = {round(total_num_tests * original_runtime, 2)}s")
    for idx, group in enumerate(fork_join_groups):
        logger.debug(f"Number of fork/joins in group {idx} = {len(group)}")
    
    redirect_log_to_file(analysis_config.output_dir + 'fork_join.log')

    with tqdm(
        total=total_num_tests,
        desc=f"{COLORS['GREEN']}Fork/Join Performance Analysis{COLORS['RESET']}",
        ascii=True,
        ncols=150,
    ) as progress_bar:
        # if original_runtime:
        #     progress_bar.set_postfix_str(f"Estimated time remaining: {round(original_runtime * remaining_tests, 2)}s")
        for idx, group in enumerate(fork_join_groups):
            output_dir = analysis_config.output_dir + f"group_{idx}_initial_run/"
            log_path = output_dir + f"log_group{idx}_initial_run.log"
            logger.info(f"Running the initial test with each fork/join path under group {idx} decoupled from the rest of the graph. Output dir: {output_dir}, log file path: {log_path}")
            # print_progress_bar(idx, total_num_tests)
            if os.path.exists(output_dir):
                shutil.rmtree(output_dir)
            os.makedirs(output_dir)

            if not analysis_config.skip_run:
                perf_info_initial_test = run_overlay_decoupled_test(
                    analysis_config=analysis_config,
                    fork_join_group=group,
                    is_initial_test=True,
                    all_graphs=all_graphs_ds,
                    output_dir=output_dir,
                    log_path=log_path,
                )
            else:
                output_dir = analysis_config.input_perf_results_dir + f"group_{idx}_initial_run/"
                perf_info_path = f"{output_dir}/perf_results/{PERF_INFO_FILE_NAME}"
                ASSERT(
                    os.path.exists(perf_info_path),
                    f"perf_info output file does not exist under {perf_info_path}. This might be because the test itself failed or it was run without perf dump enabled. Please check the test log."
                )
                perf_info_initial_test = None
                with open(perf_info_path) as perf_info_file:
                    perf_info_initial_test = yaml.load(perf_info_file, Loader=yaml.FullLoader)


            progress_bar.update(1)
            # remaining_tests -= 1
            # if original_runtime:
            #     progress_bar.set_postfix_str(f"Estimated time remaining: {round(original_runtime * remaining_tests, 2)}s")
            initial_graph_perf_results = get_performance_for_test(perf_info_initial_test, all_graphs_ds)

            for node in group:
                graph_name = node.graph_name
                ASSERT(graph_name in initial_graph_perf_results, f"Performance results for graph {graph_name} not found.")
                graph_perf: GraphPerf = initial_graph_perf_results[graph_name]
                ASSERT(node.fork_op_name in graph_perf.analyzer_op_perf, f"Op performance results does not exist for op name {node.fork_op_name}")

                fork_output_bw = graph_perf.analyzer_op_perf[node.fork_op_name].get_trisc_output_bw()
                node.fork_op_output_bw_original = fork_output_bw

            output_dir = analysis_config.output_dir + f"group_{idx}_decoupled_run/"
            log_path = output_dir + f"log_group{idx}_decoupled_run.log"
            logger.info(
                f"Running the decoupled test with each fork/join path under group {idx} decoupled from the rest of the graph, and the short path decoupled from the join-op-input Output dir: {output_dir}, log file path: {log_path}")
            # print_progress_bar(idx + 1, total_num_tests)
            if os.path.exists(output_dir):
                shutil.rmtree(output_dir)
            os.makedirs(output_dir)
            
            if not analysis_config.skip_run:
                perf_info_decoupled_test = run_overlay_decoupled_test(
                    analysis_config=analysis_config,
                    fork_join_group=group,
                    is_initial_test=False,
                    all_graphs=all_graphs_ds,
                    output_dir = output_dir,
                    log_path=log_path,
                )
            else:
                output_dir = analysis_config.input_perf_results_dir + f"group_{idx}_decoupled_run/"
                perf_info_path = f"{output_dir}/perf_results/{PERF_INFO_FILE_NAME}"
                ASSERT(
                    os.path.exists(perf_info_path),
                    f"perf_info output file does not exist under {perf_info_path}. This might be because the test itself failed or it was run without perf dump enabled. Please check the test log."
                )
                perf_info_decoupled_test = None
                with open(perf_info_path) as perf_info_file:
                    perf_info_decoupled_test = yaml.load(perf_info_file, Loader=yaml.FullLoader)

            progress_bar.update(1)
            # remaining_tests -= 1
            # if original_runtime:
            #     progress_bar.set_postfix_str(f"Estimated time remaining: {round(original_runtime * remaining_tests, 2)}s")
            decoupled_graph_perf_results = get_performance_for_test(perf_info_decoupled_test, all_graphs_ds)

            for node in group:
                graph_name = node.graph_name
                ASSERT(graph_name in decoupled_graph_perf_results, f"Performance results for graph {graph_name} not found.")
                graph_perf: GraphPerf = decoupled_graph_perf_results[graph_name]
                ASSERT(node.fork_op_name in graph_perf.analyzer_op_perf, f"Op performance results does not exist for op name {node.fork_op_name}")

                fork_output_bw = graph_perf.analyzer_op_perf[node.fork_op_name].get_trisc_output_bw()
                node.fork_op_output_bw_decoupled = fork_output_bw

        progress_bar.close()
        print()
    
    # print_progress_bar(total_num_tests, total_num_tests)
    # logger.info("Finished the fork/join analysis successfully")
    labels = ["Fork Check", "Graph Name", "Fork Group", "Fork ID", "Fork Op Name", "Join Op Name", "Original Fork Output BW", "Decoupled Fork Output BW", "Improvement After Decouple (%)"]
    perf_report = PerfReport(labels)
    perf_report_failed = PerfReport(labels)
    for idx, group in enumerate(fork_join_groups):
        for node in group:
            initial_bw = node.fork_op_output_bw_original
            decocupled_bw = node.fork_op_output_bw_decoupled
            diff_percent = get_diff_percentage(initial_bw, decocupled_bw)
            color = COLORS["RESET"]
            if diff_percent != "N/A":
                if diff_percent < 3:
                    check = "PASSED"
                    color = COLORS["GREEN"]
                elif diff_percent >= 3 and diff_percent <= 5:
                    check = "CHECK"
                    color = COLORS["YELLOW"]
                else:
                    check = "FAILED"
                    color = COLORS["RED"]
            else:
                check = "N/A"
                color = COLORS["YELLOW"]
            
            entry = [check, node.graph_name, idx, node.local_id, node.fork_op_name, node.join_op_name, ROUND(initial_bw), ROUND(decocupled_bw), ROUND(diff_percent)]
            
            if diff_percent >= 1:
                perf_report_failed.add_entry(entry, color)
            
            perf_report.add_entry(entry, color)
    
    
    print(f"Generating the report under {analysis_config.output_dir + FORK_JOIN_REPORT_BASE + '_all.csv'}")
    print(f"Performance results for all Fork/Joins in the graph:")
    perf_report.print()
    perf_report.generate_csv(analysis_config.output_dir + FORK_JOIN_REPORT_BASE + "_all.csv")
    perf_report_failed.generate_csv(analysis_config.output_dir + FORK_JOIN_REPORT_BASE + "_failed.csv")
    
    disable_log_file()
    
    del os.environ[BACKEND_FORK_JOIN_ANALYZER_ENV_VAR]
    logger.info("Finished Fork/Join performance analysis successfully")
