// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>

#include "model/tensor.hpp"
#include "verif_stimulus_types.hpp"
namespace verif::stimulus {

StimulusType get_stimulus_type(std::string input);
//! Will read only the default config from the yaml file
VerifStimulusConfig read_from_yaml_file(const std::string &filepath);
//! Will read default config + any overrides which are keyed off a tag
std::unordered_map<std::string, VerifStimulusConfig> read_configs_map_from_yaml_file(const std::string &filepath);
//! Will read default config + any overrides which are keyed off a tag, a defaults file can be used to fill in missing configs
std::unordered_map<std::string, VerifStimulusConfig> read_configs_map_with_defaults(const std::string &filepath, const std::string &default_filepath);
//! Returns a default config if key doesn't exist
VerifStimulusConfig get_config(
    const std::string key, const std::unordered_map<std::string, VerifStimulusConfig> &configs);
void generate_tensor(VerifStimulusConfig stimulus_config, tt_tensor &tensor);

}  // namespace verif::stimulus
