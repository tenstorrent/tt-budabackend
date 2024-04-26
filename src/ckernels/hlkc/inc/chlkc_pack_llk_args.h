// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
const llk_pack_params_t llk_args = {
	.pack_output = (std::uint32_t) 16, 
        .relu_config = {.f = {.ApplyRelu=0, .Threshold=0}}
};
