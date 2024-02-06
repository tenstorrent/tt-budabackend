// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "verif_stimulus.hpp"

#include <cmath>

#include "utils/logger.hpp"
#include "verif/verif_stimulus_types.hpp"
#include "verif_rand_utils.hpp"
#include "verif_sparse.hpp"
#include "verif_test_config.hpp"
#include "yaml-cpp/yaml.h"

namespace verif::stimulus {

StimulusType get_stimulus_type(string input) {
     std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return std::tolower(c); });
    if (input == "uniform") {
        return StimulusType::Uniform;
    } else if (input == "normal") {
        return StimulusType::Normal;
    } else if (input == "constant") {
        return StimulusType::Constant;
    } else if (input == "debugtileid") {
        return StimulusType::DebugTileId;
    } else if (input == "sparse") {
        return StimulusType::Sparse;
    } else if (input == "sparse_encoding") {
        return StimulusType::SparseEncoding;
    } else if (input == "sparse_encoding_nz_counts") {
        return StimulusType::SparseEncodingNonzeroCounts;
    } else if (input == "step") {
        return StimulusType::Step;
    } else {
        throw std::runtime_error("Incorrect stimulus type selected");
    }
}

VerifStimulusConfig read_from_yaml_node(const YAML::Node &node) {
    if (not node.IsMap()) {
        log_fatal("VerifStimulusConfig expected yaml node have to be a map");
    }
    VerifStimulusConfig config;
    for (auto it : node) {
        std::string key = it.first.as<std::string>();
        if (key == "type") {
            config.type = get_stimulus_type(it.second.as<std::string>());
        } else if (key == "uniform_lower_bound") {
            config.uniform_lower_bound = it.second.as<double>();
        } else if (key == "uniform_upper_bound") {
            config.uniform_upper_bound = it.second.as<double>();
        } else if (key == "normal_stddev") {
            config.normal_stddev = it.second.as<double>();
        } else if (key == "normal_mean") {
            config.normal_mean = it.second.as<double>();
        } else if (key == "constant_value") {
            config.constant_value = it.second.as<double>();
        } else if (key == "normal_mean") {
            config.normal_mean = it.second.as<double>();
        } else if (key == "debug_tile_id_base") {
            config.debug_tile_id_base = it.second.as<double>();
        } else if (key == "debug_tile_id_step") {
            config.debug_tile_id_step = it.second.as<double>();
        } else if (key == "set_tile_rows") {
            const vector<int> row_range = it.second.as<std::vector<int>>();
            log_assert(row_range.size() == 2 && row_range[0] >= 1 && row_range[0] <= row_range[1] &&
                       row_range[1] <= tt::constants::TILE_HEIGHT,
                       "Stimuls set_tile_rows has to be in format [r1, r2] (1-based inclusive range)");
            config.set_tile_rows_range = {row_range[0] - 1, row_range[1] - 1};
        } else if (key == "set_tile_cols") {
            const vector<int> col_range = it.second.as<std::vector<int>>();
            log_assert(col_range.size() == 2 && col_range[0] >= 1 && col_range[0] <= col_range[1] &&
                       col_range[1] <= tt::constants::TILE_WIDTH,
                       "Stimuls set_tile_cols has to be in format [c1, c2] (1-based inclusive range)");
            config.set_tile_cols_range = {col_range[0] - 1, col_range[1] - 1};
        } else if (key == "zero_strip_freq") {
            config.sparse_encoding_attrs.zero_strip_freq = it.second.as<double>();
        } else if (key == "zero_ublock_freq") {
            config.sparse_encoding_attrs.zero_ublock_freq = it.second.as<double>();
        } else if (key == "zero_tile_freq") {
            config.sparse_encoding_attrs.zero_tile_freq = it.second.as<double>();
        } else if (key == "nz_strips") {
            config.sparse_encoding_attrs.nz_strips = it.second.as<int>();
        } else if (key == "nz_ublocks") {
            config.sparse_encoding_attrs.nz_ublocks = it.second.as<int>();
        } else if (key == "nz_tiles") {
            config.sparse_encoding_attrs.nz_tiles = it.second.as<int>();
        } else if (key == "enc_tile_byte_size") {
            config.sparse_encoding_attrs.enc_tile_byte_size = it.second.as<int>();
        } else if (key == "matmul_ident_name") {
            config.sparse_encoding_attrs.matmul_ident_name = it.second.as<string>();
        } else if (key == "step_start") {
            config.step_start = it.second.as<float>();
        } else if (key == "step_increment") {
            config.step_increment = it.second.as<float>();
        } else if (key == "overrides") {
        } else {
            log_fatal("Unsupported key in config parse: {}", key);
        }
    }
    return config;
}

VerifStimulusConfig read_from_yaml_file(const std::string &filepath) {
    return verif::test_config::read_config_from_yaml_file<VerifStimulusConfig>(filepath, "stimulus-config", verif::stimulus::read_from_yaml_node);
}

std::unordered_map<std::string, VerifStimulusConfig> read_configs_map_from_yaml_file(const std::string &filepath) {
    return verif::test_config::read_configs_map_from_yaml_file<VerifStimulusConfig>(filepath, "stimulus-config", verif::stimulus::read_from_yaml_node);
}

std::unordered_map<std::string, VerifStimulusConfig> read_configs_map_with_defaults(const std::string &filepath, const std::string &default_filepath) {
    return verif::test_config::read_configs_map_with_defaults<VerifStimulusConfig>(filepath, default_filepath, "stimulus-config",
        verif::stimulus::read_from_yaml_node);
}

//! Returns a default config if key doesn't exist
VerifStimulusConfig get_config(const std::string key, const std::unordered_map<std::string, VerifStimulusConfig> &configs) {
    return verif::test_config::get_config_from_map<VerifStimulusConfig>(key, configs);
}

void generate_tensor(VerifStimulusConfig stimulus_config, tt_tensor &tensor) {
    if (stimulus_config.type == StimulusType::Normal) {
        log_assert(
            !std::isnan(stimulus_config.normal_mean) and !std::isnan(stimulus_config.normal_stddev),
            "normal_stdev and normal_mean must be set for normal type");
        verif::random::log_assert_seed_uninitialized(__func__);
        if (tt::is_integer_format(tensor.get_data_format())) {
            if (tensor.get_data_format() == DataFormat::Int8) {
                verif::random::randomize_normal<int>(
                    tensor,
                    (int)stimulus_config.normal_mean,
                    (int)stimulus_config.normal_stddev,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            } else if (tensor.get_data_format() == DataFormat::Int32) {
                verif::random::randomize_normal<int32_t>(
                    tensor,
                    (int32_t)stimulus_config.normal_mean,
                    (int32_t)stimulus_config.normal_stddev,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            } else {
                verif::random::randomize_normal<uint32_t>(
                    tensor,
                    (uint32_t)stimulus_config.normal_mean,
                    (uint32_t)stimulus_config.normal_stddev,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            }
        } else {
            verif::random::randomize_normal(
                tensor,
                stimulus_config.normal_mean,
                stimulus_config.normal_stddev,
                stimulus_config.set_tile_rows_range,
                stimulus_config.set_tile_cols_range);
        }
    } else if (stimulus_config.type == StimulusType::Uniform) {
        log_assert(
            !std::isnan(stimulus_config.uniform_lower_bound) and !std::isnan(stimulus_config.uniform_upper_bound),
            "uniform_lower_bound and uniform_upper_bound must be set for uniform type");
        verif::random::log_assert_seed_uninitialized(__func__);
        if (tt::is_integer_format(tensor.get_data_format())) {
            if (tensor.get_data_format() == DataFormat::Int8) {
                verif::random::randomize_uniform<int>(
                    tensor,
                    (int)stimulus_config.uniform_lower_bound,
                    (int)stimulus_config.uniform_upper_bound,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            } else if (tensor.get_data_format() == DataFormat::Int32) {
                verif::random::randomize_uniform<int32_t>(
                    tensor,
                    (int32_t)stimulus_config.uniform_lower_bound,
                    (int32_t)stimulus_config.uniform_upper_bound,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            } else {
                verif::random::randomize_uniform<uint32_t>(
                    tensor,
                    (uint32_t)stimulus_config.uniform_lower_bound,
                    (uint32_t)stimulus_config.uniform_upper_bound,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            }
        } else {
            verif::random::randomize_uniform(
                tensor,
                (float)stimulus_config.uniform_lower_bound,
                (float)stimulus_config.uniform_upper_bound,
                stimulus_config.set_tile_rows_range,
                stimulus_config.set_tile_cols_range);
        }
    } else if (stimulus_config.type == StimulusType::DebugTileId) {
        log_assert(
            !std::isnan(stimulus_config.debug_tile_id_base) and !std::isnan(stimulus_config.debug_tile_id_step),
            "debug_tile_id_base and debug_tile_id_step must be set for DebugTileId type");
        verif::random::init_to_tile_id(tensor, stimulus_config.debug_tile_id_step, stimulus_config.debug_tile_id_base);
    } else if (stimulus_config.type == StimulusType::Constant) {
        log_assert(!std::isnan(stimulus_config.constant_value),  "constant_value must be set for constant type");
        if (tt::is_integer_format(tensor.get_data_format())) {
            if (tensor.get_data_format() == DataFormat::Int8) {
                verif::random::set_number<int>(
                    tensor,
                    (int)stimulus_config.constant_value,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            } else if (tensor.get_data_format() == DataFormat::Int32) {
                verif::random::set_number<int32_t>(
                    tensor,
                    (int32_t)stimulus_config.constant_value,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            } else {
                verif::random::set_number<uint32_t>(
                    tensor,
                    (uint32_t)stimulus_config.constant_value,
                    stimulus_config.set_tile_rows_range,
                    stimulus_config.set_tile_cols_range);
            }
        } else {
            verif::random::set_number(
                tensor,
                stimulus_config.constant_value,
                stimulus_config.set_tile_rows_range,
                stimulus_config.set_tile_cols_range);
        }
    } else if (stimulus_config.type == StimulusType::Sparse) {
        sparse::fill_sparse_tensor(tensor, stimulus_config);
    } else if (
        stimulus_config.type == StimulusType::SparseEncoding ||
        stimulus_config.type == StimulusType::SparseEncodingNonzeroCounts) {
        sparse::fill_sparse_encoding_tensor(tensor, stimulus_config);
    } else if (stimulus_config.type == StimulusType::Step) {
        verif::random::generate_with_step(tensor, stimulus_config.step_start, stimulus_config.step_increment);
    } else {
        log_error("Unsupported type in generate_tensor");
        log_fatal("Unsupported key in generate_tensor config");
    }
}
}  // namespace verif::stimulus