// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "netlist_parser.hpp"

namespace netlist_unittests {
    class netlist_parser_mock : public netlist_parser
    {
    public:
        netlist_parser_mock() = default;
        ~netlist_parser_mock() = default;

        // scheduled ops
        tt_scheduled_op_info parse_scheduled_op_fields(const std::string& op_name, const std::string& op_fields_str) {
            tt_scheduled_op_info scheduled_op_info;
            YAML::Node op_fields = YAML::Load(op_fields_str);
            netlist_parser::parse_scheduled_op_fields(op_fields, op_name, scheduled_op_info);
            return scheduled_op_info;
        }

        // regular ops
        tt_op_info parse_op_fields(const std::string& op_fields_str) {
            tt_op_info op_info;
            YAML::Node op_fields = YAML::Load(op_fields_str);
            netlist_parser::parse_op(op_fields, op_info);
            return op_info;
        }
    };
}