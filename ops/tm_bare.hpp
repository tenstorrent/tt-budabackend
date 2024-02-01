// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "common/base.hpp"
#include "model/model.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_tm_config {
    TmOp op = TmOp::Invalid;
    std::vector<int> args = {};
    bool full_broadcast = false; // This is meant to store if we do full broadcast, we implicitly do this in matmul_with_bias for the bias only
};
//! TM Op mainly for golden and modelling of behaviour that is needed and implemented by pipes
class tt_tm_bare_op : public tt_op {
   public:
    tt_tm_bare_op(string name, string type, tt_grid_shape grid_shape, TmOp tm_op, std::vector<int> args);
    ~tt_tm_bare_op() {}

    static std::string type_to_str();

    //! Model Implementation
    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    tt_tm_config m_config;
    string tm_op_to_string(TmOp tm_op);
};

namespace tt_tm::utils {
string tm_op_to_string(TmOp tm_op);
void golden_model(const tt_tm_config &config, vector<tt_tensor *> &inputs, tt_tensor *out);
}  // namespace tt_tm::utils
