// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <set>
#include <vector>

#include "third_party/json/json.hpp"

using json = nlohmann::json;
using json_node = nlohmann::basic_json<>::value_type;

namespace tt {

struct tt_device_image {
    using tensor_meta =
        std::tuple<std::string, uint32_t, std::string, std::vector<uint32_t>, std::vector<uint32_t>, uint32_t>;
    // <z_dim, y_dim, x_dim, stride>
    using prestride_info = 
        std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>;
    std::string arch() const;
    std::string backend() const;

    tt_device_image(const std::string &tti_path, const ::std::string &output_path = "tt_build/tti");
    tt_device_image() = delete;
    tt_device_image(tt_device_image const &) = delete;
    void operator=(tt_device_image const &) = delete;

    std::string get_netlist_path() const;
    std::string get_model_path(const std::string &relative_path) const;

    std::vector<std::string> get_graph_input_names() const;
    std::vector<std::string> get_graph_output_names() const;
    std::vector<std::string> get_graph_constant_and_parameter_names() const;
    std::unordered_map<std::string, prestride_info> get_io_prestride_map() const;
    tensor_meta get_tensor_meta(const std::string &name) const;
    tensor_meta get_tensor_meta(const json_node &node) const;

   private:
    json metadata;
    std::string output_path = "";
    std::string model_path = "";

    std::set<std::string> constant_names = {};
    std::set<std::string> parameter_names = {};
    std::unordered_map<std::string, prestride_info> prestride_transform_per_input = {};

    void populate_io_prestride_map();
};

}  // namespace tt
