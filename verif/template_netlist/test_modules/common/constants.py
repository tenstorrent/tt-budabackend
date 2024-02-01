# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from test_modules.common.enums import TMS
from util import get_git_root

TEMPLATE_VAR_PREFIX = "$TEMPLATE_"
TEMPLATE_DIR_PATH = f"{get_git_root()}/verif/template_netlist/templates"

SIZE_R_VAR_NAME = "size_r"
SIZE_C_VAR_NAME = "size_c"
ARCH_VAR_NAME = "arch_name"

GLOBAL_T_DIM_KEY = "global_t_dim"
DATA_FORMAT_KEY = "data_format"
INPUT_COUNT_KEY = "input_count"
DEFAULT_BROADCAST_KEY = "brcast_var"
DEFAULT_PROLOGUE_KEY = "prologue"
INPUT_COUNT_KEY = "input_count"

# TMs that are valid candidates for TM randomization in the multi tm tests.
MULTI_TM_TESTS_VALID_TMS = [
    TMS.c_broadcast,
    TMS.r_broadcast,
    TMS.t_broadcast,
    TMS.transpose,
    TMS.hslice,
    TMS.vslice,
    TMS.hstack,
    TMS.vstack,
]

# TMs which should be swept in multi TM tests (pretty much always should be the same as the list of
# valid TMs, apart from scenarios where we want to debug something).
MULTI_TM_TESTS_SWEEP_TMS = MULTI_TM_TESTS_VALID_TMS
