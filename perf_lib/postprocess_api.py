# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

RUNTIME_FILE_NAME       = "runtime_table.json"
CORES_TO_OPS_FILE_NAME  = "cores_to_ops.json"
PERF_INFO_FILE_NAME     = "perf_info_all_epochs.yaml"

PERF_OUTPUT_DIR_ENV_VAR                     = "TT_BACKEND_PERF_OUTPUT_DIR"
BACKEND_PERF_ANALYZER_ENV_VAR               = "TT_BACKEND_PERF_ANALYZER"
BACKEND_OVERLAY_DECOUPLE_DESC_PATH_ENV_VAR  = "TT_BACKEND_OVERLAY_DECOUPLE_DESC_PATH"
BACKEND_FORK_JOIN_ANALYZER_ENV_VAR          = "TT_BACKEND_FORK_JOIN_ANALYZER"

PERF_ANALYZER_ENV_VARS = [
    PERF_OUTPUT_DIR_ENV_VAR,
    BACKEND_PERF_ANALYZER_ENV_VAR,
    BACKEND_OVERLAY_DECOUPLE_DESC_PATH_ENV_VAR,
    BACKEND_FORK_JOIN_ANALYZER_ENV_VAR,
]

per_epoch_events_key    = "per-epoch-events"
global_epoch_id_key     = "epoch-global-id"
local_epoch_id_key      = "epoch-local-id"
graph_name_key          = "graph-name"
program_name_key        = "program-name"
program_id_key          = "program-id"
device_id_key           = "device-id"
aiclk_key               = "AICLK"

op_type_label           = "op"
queue_type_label        = "queue"

logical_coord_key       = "logical-core-id"
op_name_key             = "op-name"
op_type_key             = "op-type"
num_inputs_key          = "num-inputs"
num_outputs_key         = "num-outputs"

cores_to_ops_inputs_key           = "inputs"
cores_to_ops_outputs_key          = "outputs"
cores_to_ops_operand_name_key     = "name"
cores_to_ops_operand_type_key     = "type"
cores_to_ops_operand_index_key    = "index"

math_util_key           = "math-utilization-first-unpack-to-last-pack"
runtime_key             = "first-unpack-to-last-pack"
model_key               = "model-cycles-per-core"
wait_for_free_tile_key  = "total-wait-for-free-tile-after-first-unpack"
wait_for_tile_key       = "total-unpack-wait-for-tile-after-first-unpack"
average_utilization_key = "average-math-utilization"
total_runtime_key       = "total-runtime"
first_input_recorded_key= "first-input-recorded"
last_input_recorded_key = "last-input-recorded"
first_unpack_first_input_key = "first-unpack-first-input"
last_pack_last_input_key = "last-pack-last-input"

model_num_cycles        = "model-num-cycles"
model_math_utilization  = "model-math-utilization"

output_bw_key           = "trisc-bw-operand-output-0"
out_of_memory_key       = "out-of-memory"
trisc_output_tensor_size_key = "trisc-total-tensor-size-operand-output-0"

def input_bw_key(operand_idx: int):
    return "trisc-bw-operand-input-" + str(operand_idx)

def brisc_input_bw_key(operand_idx: int):
    return "brisc-bw-operand-input-" + str(operand_idx)

def trisc_input_tensor_size_key(operand_idx: int):
    return "trisc-total-tensor-size-operand-input-" + str(operand_idx)

brisc_output_bw_key                     = "brisc-bw-operand-output-0"
packer_overlay_decoupled_output_bw_key  = "packer-overlay-decoupled-output-bw"
