#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
from perf_analysis_base import (
    AnalysisMode,
    AnalysisConfig,
)
from overlay_decouple import (
    run_overlay_decouple,
)
from fork_join import(
    run_fork_join_analysis,
)
from logger_utils import logger, ASSERT

if __name__ == "__main__":
    try:
        idx = sys.argv.index("--")
        script_args = sys.argv[1:idx]
        cmd_args = sys.argv[(idx + 1) :]
    except ValueError:
        script_args = sys.argv[1:]
        cmd_args = []
    
    logger.info("Started running performance analysis")
    logger.debug(f"script command arguments: {script_args}")
    logger.debug(f"test command arguments: {cmd_args}")

    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--analysis-mode", type=str, help=f"Type of graph analysis. Current options: overlay_decouple, fork_join")
    parser.add_argument("--timeout", type=int, default=120, help="timeout in seconds to wait for each siicon test to finish")
    parser.add_argument("--output-dir", type=str, default="perf_lib/analysis_output/", help="The output directory for the results")
    parser.add_argument("--target-programs", type=str, default="", help="target program to do the analysis for")
    parser.add_argument("--target-graphs", type=str, default="", help="target graph to do the analysis for")
    parser.add_argument("--reset-silicon-after-hang", action="store_true", default=False, help="Reset device in case of a hang (timeout)")
    parser.add_argument("--exclude-op-list", type=str, default="", help="List of op-names to exclude from running during analysis. Use ',' between op names")

    parser.add_argument("--delay-start", type=int, default=20000, help="In op_delay analysis mode, sets the initial delay value")
    parser.add_argument("--delay-step", type=int, default=10000, help="In op_delay analysis mode, sets the value to increment the delay by at every step")
    parser.add_argument("--delay-num-steps", type=int, default=2, help="In op_delay analysis mode, sets the total number of times to increment the delay for each op")

    parser.add_argument("--host-analysis", action="store_true", default=False, help=f"Store all test output build directories. If this is not enabled, all output directories will overwrite each other")
    
    parser.add_argument("--keep-all-build-directories", action="store_true", default=False, help=f"Store all test output build directories. If this is not enabled, all output directories will overwrite each other")

    parser.add_argument("--skip-test-run", action="store_true", default=False, help=f"If set, skip running the test and read the results from directory provided by --input-test-perf-results. Only functional for overlay-decouple analysis")
    parser.add_argument("--input-test-perf-results", type=str, default="", help="Directory path for test perf_results if --skip-test-run is set")
    
    state = parser.parse_args(script_args)
    state.cmd = cmd_args
    state.skipped_ops = {}

    analysis_config = AnalysisConfig(state)
    if analysis_config.mode == AnalysisMode.OverlayDecouple:
        run_overlay_decouple(analysis_config)
    elif analysis_config.mode == AnalysisMode.ForkJoin:
        run_fork_join_analysis(analysis_config)
    else:
        raise ValueError(f"Invalid analysis mode {analysis_config.mode}")
    
    logger.info("Analysis finished successfully")
