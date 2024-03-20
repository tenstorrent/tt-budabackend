// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/base.hpp"
#include "buffer.hpp"
#include "dram.hpp"
using namespace tt;

// interface to a grid of buffers
class tt_buffer_grid {
    public:
    virtual tt_grid_shape get_grid_shape() = 0;
    virtual std::vector<tt_buffer*> get_buffers(uint32_t row_index, uint32_t column_index) = 0;
    virtual string get_source_name() = 0;
    virtual ~tt_buffer_grid() = default;
};

class tt_buffer_grid_from_op : public tt_buffer_grid {
    private:
    tt_op* op;

    public:
    tt_buffer_grid_from_op(tt_op* op);

    virtual tt_grid_shape get_grid_shape() override;
    virtual std::vector<tt_buffer*> get_buffers(uint32_t row_index, uint32_t column_index) override;
    virtual string get_source_name() override;
};

class tt_buffer_grid_from_dram_io : public tt_buffer_grid {
    private:
    tt_dram_io* dram_queue;
    public:
    tt_buffer_grid_from_dram_io(tt_dram_io* dram_queue) : dram_queue(dram_queue) {
        log_assert(dram_queue->is_initialized(),  "Invalid tt_dram_io grid shape!");
        log_assert(dram_queue->grid_shape[0] > 0 && dram_queue->grid_shape[1] > 0 ,  "Invalid tt_dram_io grid shape!");
    }

    virtual tt_grid_shape get_grid_shape() override { return dram_queue->grid_shape; }
    virtual std::vector<tt_buffer*> get_buffers(uint32_t row_index, uint32_t column_index) override {
        return {dram_queue->get_q_ptr(row_index * dram_queue->grid_shape.c + column_index)};
    }
    virtual string get_source_name() override { return "DRAM IO"; }
};

class tt_custom_buffer_grid : public tt_buffer_grid {
   private:
    std::vector<std::vector<tt_buffer*>> buffer_grid;

   public:
    tt_custom_buffer_grid(const std::vector<std::vector<tt_buffer*>>& buffer_grid);

    virtual tt_grid_shape get_grid_shape() override;
    virtual std::vector<tt_buffer*> get_buffers(uint32_t row_index, uint32_t column_index) override;
    virtual string get_source_name() override;
};

tt_buffer_grid* create_buffer_grid(tt_dram_io *dram_queue, const tt_tensor& tensor);

