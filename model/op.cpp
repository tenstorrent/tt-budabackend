// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "model/op.hpp"

#include "model/model.hpp"
#include "model/utils.hpp"

// Ops needed for set_in_block_shape function

namespace tt
{

    tt_op::tt_op(string type, string name, tt_grid_shape grid_shape, int max_num_dst_tiles) : type(type), name(name), grid_shape(grid_shape), max_num_dst_tiles(max_num_dst_tiles)
    {
        // Create and populate descriptor
        tt_hlk_desc desc;

        // make cores

        // loop through rows
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            vector <tt_core *> row_of_cores;
            // Create rows (loop through columns)
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                desc.core_rc[0] = i;
                desc.core_rc[1] = j;
                tt_core * core = new tt_core(desc, this, max_num_dst_tiles);
                row_of_cores.push_back(core);
            }
            // Push row back
            cores.push_back(row_of_cores);
        }
    }

    tt_op::tt_op(string type, string name, tt_grid_shape grid_shape, tt_grid_shape grid_loc, bool grid_transpose, int max_num_dst_tiles) : type(type), name(name), grid_shape(grid_shape), max_num_dst_tiles(max_num_dst_tiles)
    {
        // Create and populate descriptor
        tt_hlk_desc desc;

        // make cores

        // loop through rows
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            vector <tt_core *> row_of_cores;
            // Create rows (loop through columns)
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                desc.core_rc[0] = i;
                desc.core_rc[1] = j;
                tt_core * core = new tt_core(desc, this, max_num_dst_tiles);
                if (grid_transpose) {
                  core->core_coords.logical_coords.absolute.row = grid_loc.r + j;
                  core->core_coords.logical_coords.absolute.col = grid_loc.c + i;
                } else {
                  core->core_coords.logical_coords.absolute.row = grid_loc.r + i;
                  core->core_coords.logical_coords.absolute.col = grid_loc.c + j;
                }
                row_of_cores.push_back(core);
            }
            // Push row back
            cores.push_back(row_of_cores);
        }
    }

    tt_op::tt_op(string type, string name, tt_grid_slice grid_slice, int max_num_dst_tiles) :
        type(type),
        name(name),
        grid_shape(
            tt_grid_shape{.r = (grid_slice.end.r - grid_slice.start.r), .c = (grid_slice.end.c - grid_slice.start.c)}),
        max_num_dst_tiles(max_num_dst_tiles) {
        // Create and populate descriptor
        tt_hlk_desc desc;

        // make cores

        // loop through rows
        for (std::uint32_t i = grid_slice.start.r; i < grid_slice.end.r; ++i) {
            vector <tt_core *> row_of_cores;
            // Create rows (loop through columns)
            for (std::uint32_t j = grid_slice.start.c; j < grid_slice.end.c; ++j) {
                desc.core_rc[0] = i;
                desc.core_rc[1] = j;
                tt_core * core = new tt_core(desc, this, max_num_dst_tiles);
                row_of_cores.push_back(core);
            }
            // Push row back
            cores.push_back(row_of_cores);
        }
    }

    void tt_op::op_prolog()
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                cores[i][j]->hlk_prolog();
            }
        }
    }

    void tt_op::set_core_buffers_for_op_epilog(int id, tt_dram* dram, int epoch)
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                cores[i][j]->set_buffer_for_op_epilog(id, dram, epoch);
            }
        }
    }

    tt_grid_shape tt_op::get_grid_shape() const { return grid_shape; }

    pair<int,int> tt_op::get_max_core_id()
    {
        // return cores[grid_shape[0]-1][0]->get_logical_absolute_row_id();
        pair<int,int> max_core_id = {0,0};
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                if (cores[i][j]->get_logical_absolute_row_id() > (uint32_t) max_core_id.first) {
                    max_core_id.first = cores[i][j]->get_logical_absolute_row_id();
                }
                if (cores[i][j]->get_logical_absolute_col_id() > (uint32_t) max_core_id.second) {
                    max_core_id.second = cores[i][j]->get_logical_absolute_col_id();
                }
            }
        }
        return max_core_id;
    }

    tt_op::~tt_op()
    {
        // Delete the cores
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                delete cores[i][j];
            }
        }

    }

    void tt_op::set_buffer_metadata_all_cores(int stream, const tt_buffer_metadata& buffer_metadata)
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                cores[i][j]->get_buf_ptr(stream)->set_buffer_metadata(buffer_metadata);
            }
        }
    }

    void tt_op::set_hlk_cpp_file_name_all_cores(const string& file_name)
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                cores[i][j]->local_desc.set_hlk_cpp_file_name(file_name);
            }
        }
    }

    void tt_op::set_hlk_math_fidelity_all_cores(MathFidelity math_fidelity)
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                cores[i][j]->local_desc.set_hlk_math_fidelity(math_fidelity);
            }
        }
    }

    void tt_op::set_hlk_math_approx_mode_all_cores(bool approx_mode)
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                cores[i][j]->local_desc.set_hlk_math_approx_mode(approx_mode);
            }
        }
    }

    void tt_op::set_hlk_operand_dataformat_all_cores(HlkOperand op_id, DataFormat data_format)
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                if (op_id <= HlkOperand::in7) {
                    cores[i][j]->local_desc.set_input_buf_dataformat((int)op_id, data_format);
                } else if (op_id <= HlkOperand::param7) {
                    cores[i][j]->local_desc.set_param_buf_dataformat((int)op_id - ((int)HlkOperand::in7+1), data_format);
                } else if (op_id <= HlkOperand::out7) {
                    cores[i][j]->local_desc.set_output_buf_dataformat((int)op_id - ((int)HlkOperand::param7+1), data_format);
                } else if (op_id <= HlkOperand::intermed7) {
                    cores[i][j]->local_desc.set_intermediate_buf_dataformat((int)op_id - ((int)HlkOperand::out7+1), data_format);
                } else {
                    log_fatal("Error: incorrect operand identifier");
                }
            }
        }
    }

    void tt_op::set_hlk_bufsize_all_cores(HlkOperand op_id, int num_tiles)
    {
        for(uint32_t i=0;i<grid_shape[0];++i)
        {
            for(uint32_t j=0;j<grid_shape[1];++j)
            {
                if (op_id <= HlkOperand::in7) {
                    cores[i][j]->local_desc.set_input_buf_size((int)op_id, num_tiles);
                } else if (op_id <= HlkOperand::param7) {
                    cores[i][j]->local_desc.set_param_buf_size((int)op_id - ((int)HlkOperand::in7+1), num_tiles);
                } else if (op_id <= HlkOperand::out7) {
                    cores[i][j]->local_desc.set_output_buf_size((int)op_id - ((int)HlkOperand::param7+1), num_tiles);
                } else if (op_id <= HlkOperand::intermed7) {
                    cores[i][j]->local_desc.set_intermediate_buf_size((int)op_id - ((int)HlkOperand::out7+1), num_tiles);
                } else {
                    cout << "Error: incorrect operand identifier" << endl;
                    log_fatal("Error: incorrect operand identifier");
                }
            }
        }
    }

    void tt_op::set_hlk_allow_unpacked_dst_all_cores(bool allow_unpacked_dst) {
        for (uint32_t i = 0; i < grid_shape[0]; ++i) {
            for (uint32_t j = 0; j < grid_shape[1]; ++j) {
                cores[i][j]->local_desc.set_hlk_allow_unpacked_dst(allow_unpacked_dst);
            }
        }
    }

    tt_buffer* tt_op::create_buffer(tt_buffer_metadata metadata, bool is_param, bool to_intermediate, bool is_temporary_buffer) {

        bool double_buffered = false;
        tt_buffer* buffer = new tt_buffer(metadata, double_buffered);

        int i = metadata.core_coord.row;
        int j = metadata.core_coord.col;

        if (is_param)
        {
            if (is_temporary_buffer)
            {
                buffer->set_core_ptr(cores[i][j]);
            }
            else
            {
                int new_bufid = to_intermediate ? cores[i][j]->get_next_intermediate_bufid() : cores[i][j]->get_next_param_bufid();
                buffer->set_id(new_bufid);
                buffer->set_core_ptr(cores[i][j]);
            }
            buffer->set_infinite();
        }

        return buffer;
    }

    /* virtual */ tt_buffer_grid* tt_op::get_input_buffer_grid(uint32_t index) {

        vector<vector<tt_buffer*>> buffer_grid_vector(this->grid_shape.r, vector<tt_buffer*>(this->grid_shape.c, nullptr));

        for (uint32_t row_index = 0; row_index < this->grid_shape.r; ++row_index) {
            for (uint32_t column_index = 0; column_index < this->grid_shape.c; ++column_index) {
                buffer_grid_vector[row_index][column_index] = this->cores[row_index][column_index]->get_in_buf_ptr(index);
            }
        }

        return new tt_custom_buffer_grid(buffer_grid_vector);
    }

    /* virtual */ tt_buffer_grid* tt_op::get_output_buffer_grid(uint32_t index) {

        vector<vector<tt_buffer*>> buffer_grid_vector(this->grid_shape.r, vector<tt_buffer*>(this->grid_shape.c, nullptr));

        for (uint32_t row_index = 0; row_index < this->grid_shape.r; ++row_index) {
            for (uint32_t column_index = 0; column_index < this->grid_shape.c; ++column_index) {
                buffer_grid_vector[row_index][column_index] = this->cores[row_index][column_index]->get_out_buf_ptr(index);
            }
        }

        return new tt_custom_buffer_grid(buffer_grid_vector);
    }

    tt_buffer_grid* tt_op::get_parameter_buffer_grid(uint32_t index)
    {
        vector<vector<tt_buffer*>> buffer_grid_vector(this->grid_shape.r, vector<tt_buffer*>(this->grid_shape.c, nullptr));

        for (uint32_t row_index = 0; row_index < this->grid_shape.r; ++row_index) {
            for (uint32_t column_index = 0; column_index < this->grid_shape.c; ++column_index) {
                buffer_grid_vector[row_index][column_index] = this->cores[row_index][column_index]->get_param_buf_ptr(index);
            }
        }
        return new tt_custom_buffer_grid(buffer_grid_vector);
    }

} // end namespace tt
