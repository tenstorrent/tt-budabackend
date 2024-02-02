# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
sys.path.append('./perf_lib')

import argparse
import json
import os
import shutil
import subprocess
import yaml

from logger_utils import logger
from performance_model import parse_perf_sweep_results, run_eltwise_perf_regression, run_matmul_perf_regression, run_matmulV2_perf_regression
from perf_sweep import get_op_type_from_arg
from perf_test_base import OpType 
from op_model.scripts.sweep_configurations import DEFAULT_CONFIGURATIONS


PERF_SWEEP_SCRIPT                      = "./perf_lib/perf_sweep.py"
DEFAULT_PERF_SWEEP_OUTPUT_ROOT_DIR     = "./build/test/perf_lib/op_model/data"
DEFAULT_ESTIMATE_PARAMS_OUTPUTROOT_DIR = "./build/test/perf_lib/op_model"
DEFAULT_PERF_SWEEP_OUTPUT_FILE         = "perf_sweep_summary.csv"
DEFAULT_PERF_SWEEP_CONFIG_FILE         = "test_configs.yaml"
DEFAULT_CONFIG_DIR                     = ""
DEFAULT_RAW_DATA_OUTPUT_DIR            = "./build/test/perf_lib/op_model/perf_data/raw/data"
DEFAULT_DATA_OUTPUT_DIR                = "./perf_lib/op_model/scripts/perf_data"
DEFAULT_OPERATIONS_FILE                = "./perf_lib/op_model/scripts/default_operations.json"

OP_TYPE_TO_MODEL_GS = {
    OpType.Binary: run_eltwise_perf_regression,
    OpType.Unary: run_eltwise_perf_regression,
    OpType.ReduceMax: run_eltwise_perf_regression,
    OpType.Splice: run_eltwise_perf_regression,
    OpType.Matmul: run_matmul_perf_regression,
}

OP_TYPE_TO_MODEL_WH_B0 = {
    OpType.Binary: run_eltwise_perf_regression,
    OpType.Unary: run_eltwise_perf_regression,
    OpType.ReduceMax: run_eltwise_perf_regression,
    OpType.Splice: run_eltwise_perf_regression,
    OpType.Matmul: run_matmulV2_perf_regression,
    OpType.Quant: run_eltwise_perf_regression
}

class ProgramConfig:
    def __init__(self):
        # temporary directory to store intermediate files generated by perf_sweep.py
        self.perf_sweep_output_dir = DEFAULT_PERF_SWEEP_OUTPUT_ROOT_DIR
        self.raw_data_output_dir = DEFAULT_RAW_DATA_OUTPUT_DIR
        self.data_output_dir = DEFAULT_DATA_OUTPUT_DIR
        self.config_dir = ""
        self.operations = []
        self.force_clean = False
        self.continue_run = False
        self.skip_perf_sweep = False
        self.arch_name = ""
        self.skip_performance_model = False
        self.estimate_params_output_file = ""

    def validate(self):
        if not self.skip_perf_sweep:
            assert not(self.force_clean and self.continue_run), "cannot create perf sweep output dir and update existing data at the same time"
            assert not os.path.exists(self.perf_sweep_output_dir) or self.force_clean or self.continue_run, "perf sweep output dir exists but flag --update or --force-clean is not set"
            assert os.path.exists(self.perf_sweep_output_dir) or not self.continue_run, "perf sweep output dir does not exist - cannot update"
        if self.config_dir != "":
            assert os.path.exists(self.config_dir), f"config dir does not exist {self.config_dir}"
        if self.raw_data_output_dir != "":
            assert os.path.exists(self.raw_data_output_dir), "raw data output dir does not exist"

    def is_continue(self):
        return self.continue_run

    def set_arch_name(self, arch_name):
        assert self.arch_name == "", "arch name already set"
        self.arch_name = arch_name.lower()
        if self.config_dir:
            self.config_dir = f"{self.config_dir}/{arch_name}"
        self.raw_data_output_dir = f"{self.raw_data_output_dir}/{arch_name}"

        if not os.path.exists(self.raw_data_output_dir):
            logger.info(f"Creating directory {self.raw_data_output_dir}")
            os.makedirs(self.raw_data_output_dir)

        self.data_output_dir = f"{self.data_output_dir}/{arch_name}"
        self.estimate_params_output_file = f"{DEFAULT_ESTIMATE_PARAMS_OUTPUTROOT_DIR}/{arch_name}_params.yaml"

    def get_op_config_file(self, op):
        if 'op_config_file' not in op:
            if self.continue_run:
                return self.get_perf_sweep_op_config_file(op)
            else:
                return ""
        else:
            return f"{self.config_dir}/{op['op_config_file']}"

    def get_config_overrides(self, op):
        if 'config_overrides' not in op:
            return ""
        
        return json.dumps(op['config_overrides'])

    def get_additional_arguments(self, op):
        if 'additional_arguments' not in op:
            return ""
        else:
            if 'default' not in op['additional_arguments']:
                return ""
            return op['additional_arguments']['default']

    def get_op_raw_data_file(self, op):
        return f"{self.raw_data_output_dir}/{op['data_file']}"

    def get_op_data_file(self, op):
        return f"{self.data_output_dir}/{op['data_file']}"

    def get_perf_sweep_op_output_dir(self, op, start_index = 0):
        if (start_index > 0):
            return f"{self.perf_sweep_output_dir}/{op['op_name']}_index_{start_index}"
        else:
            return f"{self.perf_sweep_output_dir}/{op['op_name']}"

    def get_perf_sweep_op_output_file(self, op):
        return f"{self.get_perf_sweep_op_output_dir(op)}/{DEFAULT_PERF_SWEEP_OUTPUT_FILE}"

    def get_perf_sweep_op_config_file(self, op):
        return f"{self.get_perf_sweep_op_output_dir(op)}/{DEFAULT_PERF_SWEEP_CONFIG_FILE}"

    def get_op_name(self, op):
        return op['op_name']

    def get_op_type(self, op):
        return op['op_type']

# Executes perf_sweep.py for each operation.
# If some of templates from config file has been already executed,
# then perf_sweep.py is executed only for remaining templates
class PerfSweepCommand:
    def __init__(self, op_type, op_name, config_file, perf_sweep_op_output_dir, perf_sweep_op_output_file, config_overrides, additional_arguments):
        self.op_type = op_type
        self.op_name = op_name
        self.op_config_file = config_file
        self.perf_sweep_op_output_dir = perf_sweep_op_output_dir
        self.perf_sweep_op_output_file = perf_sweep_op_output_file
        self.config_overrides = config_overrides
        self.additional_arguments = additional_arguments

        self.start_index = self.get_currently_generated_count()
        if not os.path.exists(self.op_config_file):
            self.op_config_file = ""
            self.start_index = 0
            self.config_count = 0
        else:
            self.config_count = self.get_total_count()

    def get_currently_generated_count(self):
        if os.path.exists(self.perf_sweep_op_output_file):
            with open(self.perf_sweep_op_output_file) as f:
                return sum(1 for line in f) - 1
        else:
            return 0

    def get_total_count(self):
        assert os.path.exists(self.op_config_file), f"config file does not exist {self.op_config_file}"
        with open(self.op_config_file, 'r') as file:
            configs_dict = yaml.safe_load(file)
            return len(configs_dict)

    def get_command(self):
        #construct command
        if self.start_index > 0:
            temp_perf_sweep_out_dir = f"{self.perf_sweep_op_output_dir}_index_{self.start_index}"
        else:
            temp_perf_sweep_out_dir = self.perf_sweep_op_output_dir

        cmd = f"python3 {PERF_SWEEP_SCRIPT} --output-dir {temp_perf_sweep_out_dir} --op-type {self.op_type} --reuse-fw-bin"

        if self.op_config_file != "":
            cmd += f" --config-file {self.op_config_file}"

        if self.config_overrides != "":
            cmd += f" --config-overrides '{self.config_overrides}'"

        if self.additional_arguments != "":
            cmd += f" {self.additional_arguments}"

        if self.start_index > 0:
            cmd += f" --start-from-index {self.start_index}"
            cmd += f" --update-existing-csv-path {self.perf_sweep_op_output_file}"

        return cmd

    # execute command to generate performance data
    # if start_index is 0, then generate all configurations
    # if start_index is > 0, then generate only configurations with index >= start_index
    def execute_command(self):
        logger.info(f"op_name: {self.op_name}, start_index: {self.start_index}, number of configurations: {self.config_count}")

        if self.config_count != 0:
            if self.start_index == self.config_count:
                logger.info(f"Skipping {self.op_name} - already generated {self.start_index} configurations")
                return

            if self.start_index < self.config_count:
                logger.info(f"Skipping first {self.start_index} configurations for {self.op_name}")

            logger.info(f"Generating {self.config_count - self.start_index} configurations for {self.op_name}")

        else:
            assert self.start_index == 0, f"start_index should be 0 when config_count = 0, but it is {self.start_index}"

        cmd = self.get_command()
        logger.info(f"Executing command: {cmd}")
        subprocess.call(cmd, shell=True)

        generated = self.get_currently_generated_count()

        if self.config_count != 0:
            assert generated == self.config_count, f"generated {generated} rows, but expected {self.config_count} rows"

def get_arch_name():
    arch_name = "ARCH_NAME"
    assert arch_name in os.environ, f"environment variable {arch_name} not set"
    assert os.environ[arch_name] in ["grayskull", "wormhole_b0"]
    return os.environ[arch_name]

def get_program_config():
    parser = argparse.ArgumentParser(description="Generates performance results for specified ops")

    parser.add_argument("--skip-perf-sweep", action="store_true", default=False, help="skip perf sweep")
    parser.add_argument("--skip-performance-model", action="store_true", default=False, help="skip running performance model to generate weights for estimates")
    parser.add_argument("--force-clean", action="store_true", default=False, help="delete previous temp output")
    parser.add_argument("--perf-sweep-output-dir", required=False,  type=str, default=DEFAULT_PERF_SWEEP_OUTPUT_ROOT_DIR, help="path to perf sweep csv")
    parser.add_argument("--config-dir", required=False,  type=str, default=DEFAULT_CONFIG_DIR, help="path to perf sweep csv")
    parser.add_argument("--raw-data-output-dir", required=False,  type=str, default=DEFAULT_RAW_DATA_OUTPUT_DIR, help="path to perf sweep csv")
    parser.add_argument("--continue-run", action="store_true", default=False, help="continue with generating data")
    parser.add_argument("--operations-cfg", required=False,  type=str, default=None, help="path to operations config file")
    parser.add_argument("--op-name", required=False,  type=str, default="", help="name of operation to generate")
    parser.add_argument("--op-type", required=False,  type=str, default="", help="type of operation to generate")

    state = parser.parse_args()

    program_config = ProgramConfig()
    program_config.perf_sweep_output_dir = state.perf_sweep_output_dir
    program_config.force_clean = state.force_clean
    program_config.continue_run = state.continue_run
    program_config.skip_perf_sweep = state.skip_perf_sweep
    program_config.skip_performance_model = state.skip_performance_model

    if state.operations_cfg is not None:
        assert os.path.exists(state.operations_cfg), f"operations config file {state.operations_cfg} does not exist"
        with open(state.operations_cfg, 'r') as file:
            program_config.operations = json.load(file)
    else:
        program_config.operations = DEFAULT_CONFIGURATIONS

    if state.op_name != "":
        program_config.operations = [op for op in program_config.operations if op["op_name"] == state.op_name]
    if state.op_type != "":
        program_config.operations = [op for op in program_config.operations if op["op_type"] == state.op_type]

    program_config.set_arch_name(get_arch_name())
    program_config.validate()
    return program_config

def create_command(program_config: ProgramConfig, op: dict):
    op_type = program_config.get_op_type(op)
    op_name = program_config.get_op_name(op)
    perf_sweep_output_dir = program_config.get_perf_sweep_op_output_dir(op)
    output_file = program_config.get_perf_sweep_op_output_file(op)
    op_config_file = program_config.get_op_config_file(op)
    config_overrides = program_config.get_config_overrides(op)
    additional_arguments = program_config.get_additional_arguments(op)

    command = PerfSweepCommand(op_type, op_name, op_config_file, perf_sweep_output_dir, output_file, config_overrides, additional_arguments)
    return command

def execute_perf_sweep(program_config: ProgramConfig):
    for op in program_config.operations:
        command = create_command(program_config, op)
        command.execute_command()

def move_perf_sweep_file_to_raw_data_output_dir(program_config: ProgramConfig):
    for op in program_config.operations:
        temp_file = program_config.get_perf_sweep_op_output_file(op)
        output_file = program_config.get_op_raw_data_file(op)
        logger.info(f"Copying {temp_file} to {output_file}")
        shutil.copy(temp_file, output_file)

def create_temp_dirs_if_needed(program_config: ProgramConfig):
    if not program_config.is_continue():
        if os.path.exists(program_config.perf_sweep_output_dir):
            logger.info(f"Deleting temp dir: {program_config.perf_sweep_output_dir}")
            shutil.rmtree(program_config.perf_sweep_output_dir)
        logger.info(f"Creating temp dir: {program_config.perf_sweep_output_dir}")
        os.makedirs(program_config.perf_sweep_output_dir)

def generate_raw_data(program_config: ProgramConfig):
    create_temp_dirs_if_needed(program_config)
    execute_perf_sweep(program_config)
    move_perf_sweep_file_to_raw_data_output_dir(program_config)

def filter_results_and_run_performance_model(op, model, filter={}):
    input_file = program_config.get_op_raw_data_file(op)
    perf_sweep_results = parse_perf_sweep_results(input_file, filter)
    params_for_op = model(perf_sweep_results, print_results=False)

    # In estimate params file we round obtained weights on 8 decimals
    return {param_name: round(param_value, 8) for param_name, param_value in params_for_op.items()}

def run_performance_model(program_config: ProgramConfig):
     # logger.info(f"Running performance model for ops...")

    if program_config.arch_name == "grayskull":
        op_type_to_model = OP_TYPE_TO_MODEL_GS
    else:
        op_type_to_model = OP_TYPE_TO_MODEL_WH_B0
    
    for op in program_config.operations:
        op_type = get_op_type_from_arg(program_config.get_op_type(op), OpType)
        op_name = program_config.get_op_name(op)
        if op_type in op_type_to_model and op_name != "all":
            if op_type == OpType.Unary and not op_name.startswith("nop"):
                params_for_op = filter_results_and_run_performance_model(op, op_type_to_model[op_type], {"Vector-Mode": "rc"})

                params_for_op_r = filter_results_and_run_performance_model(op, op_type_to_model[op_type], {"Vector-Mode": "r"})
                params_for_op.update({f"{param_name}_vector_r": param_value for param_name, param_value in params_for_op_r.items()})

                params_for_op_c = filter_results_and_run_performance_model(op, op_type_to_model[op_type], {"Vector-Mode": "c"})
                params_for_op.update({f"{param_name}_vector_c": param_value for param_name, param_value in params_for_op_c.items()})

                print(yaml.dump({op_name: params_for_op}, sort_keys=False, default_flow_style=False, indent=2))
            else:
                params_for_op = filter_results_and_run_performance_model(op, op_type_to_model[op_type])
                print(yaml.dump({op_name: params_for_op}, sort_keys=False, default_flow_style=False, indent=2))
        else:
            logger.warning(f"Performance model run not supported for op type {op_type.name} and op name {op_name}. Skipping..")

if __name__ == "__main__":
    program_config = get_program_config()

    if program_config.skip_perf_sweep:
        logger.info("Skipping perf sweep")
    else:
        generate_raw_data(program_config)
    
    if program_config.skip_performance_model:
        logger.info("Skipping performance model")
    else:
        run_performance_model(program_config)