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
)
from logger_utils import (
    logger,
    print_progress_bar,
)

REPORT_CSV_FILE_NAME = "report.csv"

def run_host_analysis(analysis_config: AnalysisConfig):
    logger.info("Starting op delay analysis")
