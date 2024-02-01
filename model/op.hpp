// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <ostream>
#include <numeric>

#include "utils/pointers.hpp"
#include "buffer.hpp"
#include "forward_declarations.hpp"
#include "model/tensor.hpp"
#include "common/model/tensor_hierarchy_metadata.hpp"
#include "model/base_defs.h"

// Utils
using std::uint32_t;
using std::function;

// Datastructures
using std::array;
using std::map;
using std::pair;
using std::string;
using std::unordered_map;
using std::vector;
using std::set;

class tt_buffer_grid;

namespace tt
{
    
using tt_core_ptr_grid = vector <vector <tt_core *> >;

// One spatial op
class tt_op
{
    public:
    tt_grid_shape grid_shape;
    tt_core_ptr_grid cores;
    string name;
    string type;

    bool untilize_output;

    bool pack_microblocks;

    bool fp32_dest_acc_en;

    bool relu_en;

    SfpuExecutionThread sfpu_execution_thread = SfpuExecutionThread::Default;

    std::uint32_t relu_threshold;
    ReluMode relu_mode;
    
    bool pack_l1_acc_en = false;

    int max_num_dst_tiles;

    tt::ARCH arch_name = tt::ARCH::Invalid;

    StochRndMode stoch_rnd_mode = StochRndMode::None;

    bool int_fpu_en = false;

    TileDimReconfigMode is_tile_dim_reconfig_mode = {0,0};

    std::array<std::uint32_t, 2> output_tile_dims = {32, 32};
    std::vector<std::array<std::uint32_t, 2>> input_tile_dims;
    std::vector<std::pair<int, bool>> kernel_broadcasts;

    void op_prolog();
    void set_core_buffers_for_op_epilog(int id, tt_dram* dram, int epoch);
    tt_grid_shape get_grid_shape() const;
    pair<int,int> get_max_core_id();

    virtual ~tt_op();
    void set_buffer_metadata_all_cores(int stream, const tt_buffer_metadata& buffer_metadata);
    void set_hlk_cpp_file_name_all_cores(const string& file_name);
    void set_hlk_math_fidelity_all_cores(MathFidelity math_fidelity);
    void set_hlk_math_approx_mode_all_cores(bool approx_mode);
    void set_hlk_operand_dataformat_all_cores(HlkOperand op_id, DataFormat data_format);
    void set_hlk_bufsize_all_cores(HlkOperand op_id, int num_tiles);
    void set_hlk_allow_unpacked_dst_all_cores(bool allow_unpacked_dst);

    tt_buffer* create_buffer(tt_buffer_metadata metadata, bool is_param, bool to_intermediate, bool is_temporary_buffer);

    virtual tt_buffer_grid* get_input_buffer_grid(uint32_t index);
    virtual tt_buffer_grid* get_output_buffer_grid(uint32_t index);
    virtual tt_buffer_grid* get_parameter_buffer_grid(uint32_t index);

    virtual void model(vector<tt_tensor*> &inputs, tt_tensor *out) {log_assert(false, "model function not implemented for op");}

    virtual DstSize get_dst_size() const { return DstSize::HalfSize; }
    
    //private:
    tt_op(string type, string name, tt_grid_shape grid_shape, int max_num_dst_tiles=16);
    tt_op(string type, string name, tt_grid_shape grid_shape, tt_grid_shape grid_loc, bool grid_transpose, int max_num_dst_tiles=16);
    tt_op(string type, string name, tt_grid_slice grid_slice, int max_num_dst_tiles=16);
};

// Base class for collections of tt_ops
struct tt_op_module {};

} // end namespace tt
