// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>

#include "tt_core.hpp"
#include "netlist/tt_backend_api_types.hpp"
#include "model/tile.hpp"
#include "model/buffer.hpp"
#include "model/dram.hpp"
#include "model/op.hpp"

void tt_core::init_b_arr_num_free_and_packed_tiles() {
        for (int buf_index = 0; buf_index < MAX_BUFFER_COUNT; buf_index++) {
            b_arr_num_free_tiles[buf_index] = 0;
            b_arr_num_packed_tiles[buf_index] = 0;
        }
    }

    tt_core::tt_core(tt::tt_hlk_desc desc, tt::tt_op *op_ptr, int max_num_dst_tiles) : local_desc(desc), next_bufid(0), next_param_bufid(8), next_intermediate_bufid(24), next_in_bufid(0), dst_valid(false), my_op_ptr(op_ptr)
    {
        core_coords.logical_coords.relative.row = desc.core_rc[0];
        core_coords.logical_coords.relative.col = desc.core_rc[1];
        core_coords.logical_coords.absolute.row = desc.core_rc[0];
        core_coords.logical_coords.absolute.col = desc.core_rc[1];

        dst_acquired = false;
        dst_offset = -1;
        pack_executed = false;
        dst_acquired_prev = false;
        dst_offset_prev = -1;
        scout_mode = false;
        wrote_intermediate_buffer = false;

        // initialize buf ptrs to nullptr, so that we can catch illegal use (before they were allocated)
        for (int buf_index = 0; buf_index < MAX_BUFFER_COUNT; buf_index++) {
            b_arr[buf_index] = nullptr;
        }

        init_b_arr_num_free_and_packed_tiles();

        DstSize dst_size = op_ptr->get_dst_size();
        switch(dst_size) {
            case DstSize::FullSize:
                dst_mode = DstMode::Full;
                break;
            case DstSize::HalfSize:
                dst_mode = DstMode::Half;
                break;
            case DstSize::TileSize:
                dst_mode = DstMode::Tile;
                break;
            default:
                log_fatal("Invalid dst_size: {}.", dst_size);
        }

        dst_mode_size[(int)Full] = max_num_dst_tiles;
        dst_mode_size[(int)Half] = max_num_dst_tiles/2;
        dst_mode_size[(int)Tile] = 1;

        dst.reserve(max_num_dst_tiles);

        for(int i = 0; i < max_num_dst_tiles; i++){dst.emplace_back(tt::tt_tile(tt::DataFormat::Float32));}

    }

    unsigned int tt_core::get_logical_absolute_row_id() const { return core_coords.logical_coords.absolute.row; }
    unsigned int tt_core::get_logical_absolute_col_id() const { return core_coords.logical_coords.absolute.col; }
    unsigned int tt_core::get_logical_relative_row_id() const { return core_coords.logical_coords.relative.row; }
    unsigned int tt_core::get_logical_relative_col_id() const { return core_coords.logical_coords.relative.col; }
    unsigned int tt_core::get_physical_row_id() const { return core_coords.physical_absolute_coords.row; }
    unsigned int tt_core::get_physical_col_id() const { return core_coords.physical_absolute_coords.col; }

    vector<int> tt_core::get_allocated_buffer_ids_for_range(int start_id, int end_id) {
        vector<int> ids;
        for (int i=start_id ; i<=end_id ;++i)
        {
            tt::tt_buffer* buf = get_buf_ptr(i);
            if (buf) ids.push_back(i);
        }
        return ids;
    }

    vector<int> tt_core::get_allocated_input_buffer_ids() {
        return get_allocated_buffer_ids_for_range(0,7);
    }

    vector<int> tt_core::get_allocated_param_buffer_ids() {
        return get_allocated_buffer_ids_for_range(8,15);
    }

    vector<int> tt_core::get_allocated_output_buffer_ids() {
        return get_allocated_buffer_ids_for_range(16,23);
    }

    vector<int> tt_core::get_allocated_intermediate_buffer_ids() {
        return get_allocated_buffer_ids_for_range(24,31);
    }

    int tt_core::get_buffer_id_list_L1_usage_bytes(const vector<int>& buf_id_list) {
        int L1_usage_bytes = 0;
        for (int buf_id : buf_id_list)
        {
            tt::tt_buffer* buf = b_arr[buf_id];
            log_assert(buf ,  "Error: buffer is not allocated.");
            L1_usage_bytes += buf->metadata.get_L1_allocated_size_bytes();
        }
        return L1_usage_bytes;
    }

    int tt_core::get_allocated_input_buffers_L1_usage_bytes() {
        return get_buffer_id_list_L1_usage_bytes(get_allocated_input_buffer_ids());
    }

    int tt_core::get_allocated_output_buffers_L1_usage_bytes() {
        return get_buffer_id_list_L1_usage_bytes(get_allocated_output_buffer_ids());
    }

    int tt_core::get_allocated_param_buffers_L1_usage_bytes() {
        return get_buffer_id_list_L1_usage_bytes(get_allocated_param_buffer_ids());
    }

    int tt_core::get_allocated_intermediate_buffers_L1_usage_bytes() {
        return get_buffer_id_list_L1_usage_bytes(get_allocated_intermediate_buffer_ids());
    }

    int tt_core::get_non_buffer_reserved_L1_usage_bytes() {
        static constexpr int32_t PIPEGEN_INTERMEDIATES = 256 * 1024;
        return dram_mem::address_map::FW_DRAM_BLOCK_SIZE() + PIPEGEN_INTERMEDIATES;
    }

    int tt_core::get_allocated_all_buffers_L1_usage_bytes() {
        return get_allocated_input_buffers_L1_usage_bytes() + get_allocated_output_buffers_L1_usage_bytes() +
                get_allocated_param_buffers_L1_usage_bytes() + get_allocated_intermediate_buffers_L1_usage_bytes();
    }

    int tt_core::get_total_allocated_L1_usage_bytes() {
        return get_non_buffer_reserved_L1_usage_bytes() + get_allocated_all_buffers_L1_usage_bytes();
    }

    void tt_core::epoch_reset()
    {
        dst_valid = false;

        dst_acquired = false;
        dst_offset = -1;
        pack_executed = false;

        init_b_arr_num_free_and_packed_tiles();
    }

    unsigned int tt_core::get_next_param_bufid()
    {
        log_assert(next_param_bufid < 16 ,  "Error: exceeded the number of param buffers per core (8)");
        unsigned int cur = next_param_bufid;
        log_assert(cur < MAX_BUFFER_COUNT, "cur index exceeds MAX_BUFFER_COUNT");
        next_param_bufid++;
        return(cur);
    }

    unsigned int tt_core::get_next_intermediate_bufid()
    {
        log_assert(next_intermediate_bufid < 32 ,  "Error: exceeded the number of param buffers per core (8)");
        unsigned int cur = next_intermediate_bufid;
        log_assert(cur < MAX_BUFFER_COUNT, "cur index exceeds MAX_BUFFER_COUNT");
        next_intermediate_bufid++;
        return(cur);
    }

    unsigned int tt_core::get_num_param_bufs()
    {
        return next_param_bufid-8;
    }

    unsigned int tt_core::get_next_in_bufid()
    {
        unsigned int cur = next_in_bufid;
        log_assert(cur < MAX_BUFFER_COUNT, "cur index exceeds MAX_BUFFER_COUNT");
        next_in_bufid++;
        return(cur);
    }

    tt::tt_buffer *tt_core::get_buf_ptr(unsigned int b)
    {
        log_assert(b < MAX_BUFFER_COUNT, "buf_ptr exceeds MAX_BUFFER_COUNT");
        return(b_arr[b]);
    }

    tt::tt_buffer *tt_core::get_in_buf_ptr(unsigned int b)
    {
        log_assert(b < MAX_BUFFER_COUNT, "buf_ptr exceeds MAX_BUFFER_COUNT");
        log_assert(b_arr[b] != NULL, "expected b_arr to be populated");
        return(b_arr[b]);
    }

    tt::tt_buffer *tt_core::get_param_buf_ptr(unsigned int b)
    {
        log_assert(b + 8 < MAX_BUFFER_COUNT, "buf_ptr exceeds MAX_BUFFER_COUNT");
        log_assert(b_arr[b + 8] != NULL, "expected b_arr to be populated");
        return(b_arr[b+8]);
    }

    tt::tt_buffer *tt_core::get_out_buf_ptr(unsigned int b)
    {
        log_assert(b + 16 < MAX_BUFFER_COUNT, "buf_ptr exceeds MAX_BUFFER_COUNT");
        log_assert(b_arr[b + 16] != NULL, "expected b_arr to be populated");
        return(b_arr[b+16]);
    }

    tt::tt_buffer *tt_core::get_int_buf_ptr(unsigned int b)
    {
        log_assert(b + 24 < MAX_BUFFER_COUNT, "buf_ptr exceeds MAX_BUFFER_COUNT");
        log_assert(b_arr[b + 24] != NULL, "expected b_arr to be populated");
        return(b_arr[b+24]);
    }

    bool tt_core::input_valid(int index)
    {
        log_assert(index < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        return b_arr[index]->is_valid();
    }

    bool tt_core::buffer_ready_to_write (int index)
    {
        log_assert(index < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        return b_arr[index]->is_ready_to_write();
    }

    void tt_core::clear_buffer(int index)
    {
        log_assert(index < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        b_arr[index]->clear_buf();
    }

    bool tt_core::any_input_ready()
    {
        bool ready = false;
        for(int i=0;i<8;++i)
        {
            if(b_arr[i]->is_valid()) ready = true;
        }
        return ready;
    }

    void tt_core::flip_wr_phase(int index)
    {
        log_assert(index < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        b_arr[index]->flip_wr_phase();
    }

    // To be used only for intermediate buffers!!!!
    // others read phase is flipped by pipe.tx()
    void tt_core::flip_rd_phase(int index)
    {
        log_assert(index < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        b_arr[index]->flip_rd_phase();
    }

    void tt_core::hlk_prolog()
    {
        // Allocate input buffers
        for(int i=0;i<8;++i)
        {
            // in model implementation the entire buffer
            // is allocated at input
            // in real implementation it's specified size in descriptor
            create_core_buffers(true,false,false);
        }
        // Allocate parameter buffers
        for(int i=0;i<8;++i)
        {
            create_core_buffers(false,false,true);
        }
        // Allocate output buffers
        // double buffered - so double the space
        for(int i=0;i<8;++i)
        {
            create_core_buffers(true,true,false);
        }
        // Allocate intermediate buffers
        // Not double-buffered
        for(int i=0;i<8;++i)
        {
            create_core_buffers(true, false, false);
        }
    }

    void tt_core::hlk_epilog(vector<tt::tt_dram_io*> dram_io_list, int epoch)
    {
        for (tt::tt_dram_io* dram_io: dram_io_list) {
            if (dram_io->is_epilogue_write(epoch)) {
                for (uint32_t j = 0; j < dram_io->bufqs.size(); j++) {
                    tt::tt_buffer_queue* bufq = dram_io->get_q_ptr(j);
                    tt::tt_buffer* buffer = bufq->get_first_inserted_buf_ptr();
                    log_assert(!buffer->is_totally_empty(), "Buffer is empty");
                }
            }
        }
    }

    void tt_core::copy_dram_buf(int id, tt::tt_buffer *buf)
    {
        log_assert(id < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        b_arr[id] = buf;
    }

    void tt_core::copy_param_buf(int id, tt::tt_buffer *buf)
    {
        log_assert(id + 8 < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        copy_dram_buf(id+8,buf);
    }

    void tt_core::set_buffer_for_op_epilog(int id, tt::tt_dram* dram, int epoch)
    {
        log_assert(id < MAX_BUFFER_COUNT, "index exceeds MAX_BUFFER_COUNT");
        dram->add_epilog_written_buffer(b_arr[id], epoch);

    }

    // No longer called by model.  model calls model_digraph.cpp process_op_buffers() instead
    void tt_core::create_core_buffers(bool owned, bool dbl, bool books_only)
    {
        log_assert(next_bufid < 32, "next_bufid exceeds range");

        tt::tt_buffer *buf_ptr;

        int desc_size = 0;
        tt::DataFormat desc_data_format = tt::DataFormat::Invalid;
        bool is_input_buf = false;
        if(next_bufid < 8)
        {
            desc_size = this->local_desc.get_input_buf_size(next_bufid);
            desc_data_format = this->local_desc.get_input_buf_dataformat(next_bufid);
            is_input_buf = true;
        }
        else if(next_bufid < 16)
        {
            // books only for the dram owned param buffers
            // these bufs will just be tied to pointers that are in dram
        }
        else if(next_bufid < 24)
        {
            desc_size = local_desc.get_output_buf_size(next_bufid-16);
            desc_data_format = local_desc.get_output_buf_dataformat(next_bufid-16);
        }
        else if(next_bufid < 32)
        {
            desc_size = local_desc.get_intermediate_buf_size(next_bufid-24);
            desc_data_format = local_desc.get_intermediate_buf_dataformat(next_bufid-24);
        }
        else
        {
            log_fatal( "next_bufid exceeds range");
            std::cout << "invalid buffer index" << std::endl;
        }

        if(desc_size != 0)
        {
            if(!books_only)
            {
                if(dbl)
                {
                    log_assert(desc_data_format != tt::DataFormat::Invalid, "Invalid data format");

                    buf_ptr = new tt::tt_buffer(tt::tt_buffer_metadata {.id = next_bufid, .L1_allocated_size_num_tiles = desc_size, .data_format = desc_data_format, .core_coord = {.row = get_logical_absolute_row_id(), .col = get_logical_absolute_col_id()}}, true);
                    buf_ptr->set_core_ptr(this);
                    if (is_input_buf) buf_ptr->set_input_buf();
                }
                else
                {
                    log_assert(desc_data_format != tt::DataFormat::Invalid,  "next_bufid exceeds range");

                    buf_ptr = new tt::tt_buffer(tt::tt_buffer_metadata {.id = next_bufid, .L1_allocated_size_num_tiles = desc_size, .data_format = desc_data_format, .core_coord = {.row = get_logical_absolute_row_id(), .col = get_logical_absolute_col_id()}}, false);
                    buf_ptr->set_core_ptr(this);
                    if (is_input_buf) buf_ptr->set_input_buf();
                }
                if(owned)
                {
                    owned_buf_vec.push_back(buf_ptr);
                }
                log_assert(next_bufid < MAX_BUFFER_COUNT,  "next_bufid exceeds range");
                b_arr[next_bufid] = buf_ptr;
            }
        }
        next_bufid++;
    }

    void tt_core::hlk_clear_dst()
    {
        for(int i=0;i<16;++i)
        {
            dst[i] = 0.0;
        }
    }

    void tt_core::hlk_clear_dst(int offset, int size)
    {
        for(int i=offset; i<offset+size ; ++i)
        {
            dst[i] = 0.0;
        }
    }

    void tt_core::rollback_acquire_dst()
    {
        dst_acquired = dst_acquired_prev;
        dst_offset = dst_offset_prev;
    }

    void tt_core::hlk_acquire_dst() {
        log_assert(
            !dst_acquired,
            "Error: dst already acquired, two back-to-back calls to hlk_acquire_dst are not allowed.",
            *this);
        log_assert(
            !pack_executed,
            "Error: a call to acquire without a preceding release, i.e., the last pack (the one before the next "
            "unpack) was executed but hlk_release_dst was not called after the last pack.",
            *this);

        // For coremodel, save previous value in case we need to rollback
        dst_acquired_prev = dst_acquired;
        dst_offset_prev = dst_offset;

        dst_acquired = true;

        // update dst_offset to what we're acquring
        if (dst_offset == -1) { // first acquire in an epoch
            dst_offset = 0;
        } else {
            dst_offset += dst_mode_size[(int)dst_mode];
            dst_offset %= dst_size_full;
        }

        // clear the region we just acquired
        hlk_clear_dst(dst_offset, dst_mode_size[(int)dst_mode]);
    }

    void tt_core::hlk_release_dst() {
        // pack sets pack_executed and clears dst_acquired
        log_assert(
            local_desc.allow_unpacked_dst ||
                (pack_executed && !dst_acquired), 
                 "Error: call to hlk_release_dst is not allowed unless a pack call was executed.",
            *this);

        pack_executed = false; // reset the pack flag
        dst_acquired = false;

        // clear the region before we release it
        hlk_clear_dst(dst_offset, dst_mode_size[(int)dst_mode]);
    }

    void tt_core::write_tile_to_dst(int dstindex, const tt::tt_tile& tile) {
        log_assert(
            dst_acquired,
            "Error: cannot write tile to dst if dst hasn't been acquired previously (read the docs about the correct "
            "placement of hlk_acquired_dst/hlk_release_dst calls).",
            *this);
        log_assert(dstindex < dst_mode_size[(int)dst_mode], "Error: dst_index out-of-bounds.", *this);

        dst[dst_offset + dstindex] = tile;
    }

    tt::tt_tile& tt_core::read_tile_from_dst(int dstindex) {
        log_assert(dst_acquired, "Error: cannot read tile from dst if dst hasn't been acquired previously.", *this);
        log_assert(dstindex < dst_mode_size[(int)dst_mode], "Error: dst_index out-of-bounds.", *this);

        return dst[dst_offset + dstindex];
    }

    void tt_core::hlk_add_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose)
    {
        tt::tt_tile *lptr;
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        lptr = b_arr[lstream]->get_tile_ptr(lindex);
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        tt::tt_tile result = *lptr + *rptr;
        log_trace(tt::LogModel, "hlk_add_tile lptr={} rptr={} result={}", *lptr, *rptr, result);

        write_tile_to_dst(dstindex, result);
    }

    void tt_core::hlk_subtract_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose)
    {
        tt::tt_tile *lptr;
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        lptr = b_arr[lstream]->get_tile_ptr(lindex);
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex, *lptr - *rptr);
    }

    void tt_core::hlk_multiply_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose)
    {
        tt::tt_tile *lptr;
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        lptr = b_arr[lstream]->get_tile_ptr(lindex);
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex, *lptr * *rptr);
    }

    void tt_core::hlk_add_tile_bcast(Dim dim, int lstream, int rstream, int lindex, int rindex, int dstindex)
    {
        // Operand 1 is implicitly broadcasted
        tt::tt_tile *lptr;
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        lptr = b_arr[lstream]->get_tile_ptr(lindex);
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex, lptr->eltwise_binary_with_broadcast(*rptr, BinaryOp::Add, dim));
    }

    void tt_core::hlk_multiply_tile_bcast(Dim dim, int lstream, int rstream, int lindex, int rindex, int dstindex)
    {
        tt::tt_tile *lptr;
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        lptr = b_arr[lstream]->get_tile_ptr(lindex);
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex,  lptr->eltwise_binary_with_broadcast(*rptr, BinaryOp::Multiply, dim));
    }

    void tt_core::hlk_subtract_tile_bcast(Dim dim, int lstream, int rstream, int lindex, int rindex, int dstindex)
    {
        tt::tt_tile *lptr;
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        lptr = b_arr[lstream]->get_tile_ptr(lindex);
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex,  lptr->eltwise_binary_with_broadcast(*rptr, BinaryOp::Subtract, dim));
    }

    void tt_core::hlk_add_tile_from_dst(int rstream, int rindex, int dstindex)
    {
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex, read_tile_from_dst(dstindex) + *rptr);
    }

    void tt_core::hlk_subtract_tile_from_dst(int rstream, int rindex, int dstindex)
    {
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex, read_tile_from_dst(dstindex) - *rptr);
    }

    void tt_core::hlk_multiply_tile_from_dst(int rstream, int rindex, int dstindex)
    {
        tt::tt_tile *rptr;

        // Check that input buffers are valid
        // which they won't be if this pipe stage is not yet flushed
        rptr = b_arr[rstream]->get_tile_ptr(rindex);

        write_tile_to_dst(dstindex, read_tile_from_dst(dstindex) * (*rptr));
    }

    // TODO: rename the rest of APIs to hlk_*
    void tt_core::hlk_copy_tile_to_dst(int stream, int index, int dstindex, int transpose)
    {
        tt::tt_tile *lptr;

        lptr = b_arr[stream]->get_tile_ptr(index);

#if 0
        if(stream == 24)
        {
            log_info(tt::LogModel, "hlk_copy_tile_to_dst intermediate index {} dstindex {} tile {}",
                index, dstindex, *lptr);
        }
#endif

        write_tile_to_dst(dstindex, transpose ? lptr->transpose_xy() : *lptr);
    }

    void tt_core::hlk_load_tile_to_dst(int stream, int index, int dstindex, int transpose)
    {
        tt::tt_tile *lptr;

        lptr = b_arr[stream]->get_tile_ptr(index);

#if 0
        if(stream == 24)
        {
            log_info(tt::LogModel, "hlk_copy_tile_to_dst intermediate index {} dstindex {} tile {}",
                index, dstindex, *lptr);
        }
#endif

        write_tile_to_dst(dstindex, transpose ? lptr->transpose_xy() : *lptr);
    }

    void tt_core::hlk_transpose_yz_tile(int stream, int index, int dstindex, int dim, int phase)
    {
        //TODO: implement yz transpose

    }

    void tt_core::hlk_mm_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim)
    {
        // todo: do something with in0_tile_dim and in1_tile_dim

        tt::tt_tile *lptr;
        tt::tt_tile *rptr;

        for (int in_c=0; in_c<inner_c_dim; in_c++) {
            lptr = b_arr[lstream]->get_tile_ptr(lindex);
            rptr = b_arr[rstream]->get_tile_ptr(rindex+in_c);

            const tt::tt_tile res = lptr->matmul(transpose ? rptr->transpose_xy() : *rptr);
            // op1 is the accumulated result from previous matmul
            const tt::tt_tile op1 = read_tile_from_dst(dstindex+in_c);
            const tt::tt_tile final_result = op1 + res;

            //log_info(tt::LogModel, "hlk_mm_tile dstindex {} lindex {} rindex {} transpose {} lptr {} rptr {} \n op1{} \n final_result {}\n",
            //    dstindex, lindex, rindex, transpose,
            //    *lptr, *rptr, op1, final_result);

            //dst[dstindex] = dst[dstindex] + res; use below instead for all the acquire/out-of-bounds checks
            write_tile_to_dst(dstindex+in_c, final_result);
        }    
    }

    void tt_core::hlk_mm_block(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim)
    {
        // todo: do something with in0_tile_dim and in1_tile_dim

        tt::tt_tile *lptr;
        tt::tt_tile *rptr;
        for (int in_r = 0; in_r < inner_r_dim; in_r++) {
            int _rindex_ = rindex + in_r*inner_d_dim;
            int _lindex_ = lindex;
            for (int in_c=0; in_c<inner_c_dim; in_c++) {
                lptr = b_arr[lstream]->get_tile_ptr(_lindex_);
                rptr = b_arr[rstream]->get_tile_ptr(_rindex_+in_c);

                const tt::tt_tile res = lptr->matmul(transpose ? rptr->transpose_xy() : *rptr);
                // op1 is the accumulated result from previous matmul
                const tt::tt_tile op1 = read_tile_from_dst(dstindex+in_c);
                const tt::tt_tile final_result = op1 + res;

                //log_info(tt::LogModel, "hlk_mm_tile dstindex {} lindex {} rindex {} transpose {} lptr {} rptr {} \n op1{} \n final_result {}\n",
                //    dstindex, lindex, rindex, transpose,
                //    *lptr, *rptr, op1, final_result);

                //dst[dstindex] = dst[dstindex] + res; use below instead for all the acquire/out-of-bounds checks
                write_tile_to_dst(dstindex+in_c, final_result);
            }    
            dstindex+=inner_c_dim;    
        }    
         
    }

    void tt_core::hlk_reduce_tile(ReduceFunc reduce_func, Dim dim, int lstream, int lindex, int dstindex, float coefficient)
    {
        tt::tt_tile *lptr = b_arr[lstream]->get_tile_ptr(lindex);

        tt::tt_tile input_tile = read_tile_from_dst(dstindex);
        write_tile_to_dst(dstindex, input_tile + lptr->reduce(reduce_func, dim, coefficient));
    }

    void tt_core::hlk_tilize(int rstream, int rindex, int dstindex)
    {
        //FIXME: TODO 
    }

    void tt_core::hlk_sfpu_op_requant_int32(int dstindex_a, int dstindex_b)
    {
        //TODO: implement requant int32
    }

    void tt_core::hlk_sfpu_op_dequant_int32(int dstindex_a, int dstindex_b)
    {
        //TODO: implement dequant int32
    }

    void tt_core::hlk_pop_tiles(int stream, int num_tiles)
    {
        log_assert(b_arr[stream]->tiles_in_buf() >= num_tiles, "tiles_in_buf exceeds num_tiles", *this);
        log_assert(b_arr[stream]->get_l1_allocated_size_in_tiles() % num_tiles == 0, "expected l1 size in tiles to be divisible by num_tiles", *this);
        b_arr[stream]->pop_tiles(num_tiles);
    }

    void tt_core::hlk_wait_tiles(int stream, int num_tiles)
    {
        // in model, tiles should always be there
        // so just assert , *thisso
        log_assert(b_arr[stream]->tiles_in_buf() >= num_tiles, 
            "stream {} has tiles_in_buf={} wait_tiles={} {}", stream, b_arr[stream]->tiles_in_buf(), num_tiles,
            *this);

        // TODO: don't check in until confirmed that this not a requirement
        // TT_ASSERT(b_arr[stream]->size % num_tiles == 0, *this);
    }

    void tt_core::hlk_wait_for_free_tiles(int stream, int num_tiles)
    {
        log_assert(num_tiles > 0, "Error: num_tiles must be > 0.", *this);
        log_assert(
            num_tiles <= b_arr[stream]->get_l1_allocated_size_in_tiles(),
            "Error: num_tiles must be < buffer size.",
            *this);
        log_assert(
            b_arr_num_free_tiles[stream] == 0,
            "Error: hlk_wait_for_free_tiles cannot be called if free tile slots were already reserved.",
            *this);
        log_assert(
            b_arr_num_packed_tiles[stream] == 0,
            "Error: calling hlk_wait_for_free but packed tiles were not pushed to output. Did you forget to call "
            "hlk_push_tiles?",
            *this);

        b_arr_num_free_tiles[stream] = num_tiles;
    }

    void tt_core::hlk_push_tiles(int stream, int num_tiles)
    {
        log_assert(num_tiles > 0, "Error: num_tiles must be > 0.", *this);
        log_assert(
            num_tiles <= b_arr[stream]->get_l1_allocated_size_in_tiles(),
            "Error: num_tiles must be < buffer size.",
            *this);
        log_assert(
            b_arr_num_packed_tiles[stream] == num_tiles,
            "Error: the number of tiles being pushed to the output operand doesn't match the number of tiles that were "
            "packed to that operand.",
            *this);

        // b_arr_num_free_tiles[stream] == 0 --> this double checks hlk_pack_tile has been called num_tiles, i.e., all reserved slots were filled up
        log_assert(
            b_arr_num_free_tiles[stream] == 0,
            "Error: pushing less tiles than resevered via hlk_wait_for_free_tiles is not allowed.",
            *this);

        b_arr_num_packed_tiles[stream] = 0;
    }

    // Common code for all hlk_pack_*tile* functions.
    void tt_core::_pack_tile_setup(int stream)
    {
        // pack rules:
        // - for the first pack, dst_acquired must be set
        // - for the subsequent packs we disable dst_acquired (because we know it was acquired for the first pack)
        // - this ensures that that pack leaves dst in released sate and thus hlk_acquire_dst must be called before the next unpack
        if (!pack_executed) {
            log_assert(dst_acquired, "Error: attempting to pack but dst was not acquired.", *this);
        }

        pack_executed = true;
        log_assert(
            b_arr_num_free_tiles[stream] > 0,
            "Error: cannot pack to buffer, there is no space for the tile (i.e., there are no free tile slots). Did "
            "you forget to use hlk_wait_for_free_tiles?",
            *this);
        b_arr_num_free_tiles[stream]--;
        b_arr_num_packed_tiles[stream]++;

        dst_acquired = true; // we set true here so that we're allowed to read_tile_from_dst (it does all the checking)
    }

    void tt_core::hlk_pack_tile_to_stream(int dst_index, int stream, int tile_index, bool relu)
    {
        _pack_tile_setup(stream);
        tt::tt_tile t = read_tile_from_dst(dst_index);
        if (relu)
            t = tt::math::relu(t);

        if(tile_index == -1) // Append mode
        {

#if 0
            if(stream == 24)
            {
                //log_info(tt::LogModel, "hlk_pack_tile_to_stream intermediate dst_index {} tile {}", 
                //    dst_index, t);
            }
            else if(stream == 16)
            {
                log_info(tt::LogModel, "hlk_pack_tile_to_stream output dst_index {} tile {}", 
                    dst_index, t);
            }
#endif

            b_arr[stream]->add_tile(t);
        }
        else
        {
            b_arr[stream]->add_tile(t, tile_index);
        }
        dst_acquired = false; // pack leaves dst in released state (for the reasons described above)
    }

    void tt_core::hlk_pack_tile_to_stream(int dst_index, int stream) {
        // todo: do something with tile dims
        hlk_pack_tile_to_stream(dst_index, stream, -1);
    }

    void tt_core::hlk_pack_tile_to_lbuf(int dst_index, int lbuf, int buf_tile_index)
    {
        tt::tt_tile bla(dst[dst_index]);
        b_arr[lbuf]->add_tile(bla);
    }

    tt_core::~tt_core()
    {
        // Delete owned buffers
        vector<tt::tt_buffer *>::iterator it;
        for(it=owned_buf_vec.begin();it!=owned_buf_vec.end();++it)
        {
            delete *it;
        }
    }

    void tt_core::clear_wait_state()
    {
        wait_stream_id = -1;
        wait_stream_tiles = 0;
        wait_for_free_tiles_stream_id = -1;
        wait_for_free_tiles_stream_tiles = 0;
    }

    void tt_core::print(std::ostream & os) const
    {
        os << "Core(" << get_logical_relative_row_id() << "," << get_logical_relative_col_id() << ") - Grid(" << my_op_ptr->grid_shape.r << ","
            << my_op_ptr->grid_shape.c << ")" << std::endl;
        os << "  Op: " << my_op_ptr->name << " - " << my_op_ptr->type << std::endl;

        static std::pair<char const *, std::pair<int, int>> buffer_ranges[] = {
            {"Input", {0, 8}},
            {"Parameter", {8, 16}},
            {"Output", {16, 24}},
            {"Intermediate", {24, 32}},
        };
        for (auto [name, range] : buffer_ranges) {
            os << "  " << name << " buffers:" << std::endl;
            for (int i = range.first; i < range.second; ++i) {
                if (b_arr[i]) {
                    os << "    [" << (i - range.first) << "] " << b_arr[i]->metadata << " uniqueId=" << b_arr[i]->unique_id 
                    << " tiles_in_buf=" << ((!b_arr[i]->is_double_buffered()) ? b_arr[i]->tiles_in_buf() : 0) << std::endl;
                }
            }
        }

    }

// hlk api impl currently here, possibly move into a seperate file
void hlk_wait_tiles(tt_core* core_ptr, int stream, int num_tiles) {

    // If we are trying to execute a loop iteration and we don't have sufficient input tiles, undo the acquire_dst and go into scout mode
    if(!core_ptr->scout_mode &&
        core_ptr->b_arr[stream]->tiles_in_buf() < num_tiles)
    {
        log_debug(tt::LogModel, "core {} stream {} has insufficient tiles tiles_in_buf {} num_tiles {}", 
            core_ptr->core_coords,
            stream, core_ptr->b_arr[stream]->tiles_in_buf(), num_tiles);

        // wait until main simulation thread has produced sufficient tiles
        std::unique_lock<boost::fibers::mutex> lock(core_ptr->stream_mutex);
        core_ptr->wait_stream_id = stream;
        core_ptr->wait_stream_tiles = num_tiles;

        // remember this count for later on when returning credits
        log_assert((core_ptr->stream_wait_tiles.find(stream) == core_ptr->stream_wait_tiles.end()) ||
                    (core_ptr->stream_wait_tiles[stream] == num_tiles), "invalid wait tiles in stream" );
        core_ptr->stream_wait_tiles[stream] = num_tiles;

        while(core_ptr->b_arr[stream]->tiles_in_buf() < num_tiles)
        {
            log_debug(tt::LogModel, "core {} stream {} waiting on num_tiles {} tiles_in_buf {}",
                core_ptr->core_coords, stream, num_tiles, core_ptr->b_arr[stream]->tiles_in_buf());
            // wakeup main thread
            core_ptr->stream_cond.notify_all();
            core_ptr->stream_cond.wait(lock);
        }
        log_debug(tt::LogModel, "after hlk_wait_tiles satisfied for core {}", core_ptr->core_coords);
        // Reset the pop_tiles count until next hlk_pop_tiles call is seen
        //core_ptr->stream_pop_tiles[stream] = 0;


        //core_ptr->rollback_acquire_dst();
        //core_ptr->scout_mode = true;
    }

    log_debug(tt::LogModel, "hlk_wait_tiles on core {} stream {} num_tiles {} tiles_in_buf {} scout_mode {}", 
        core_ptr->core_coords, stream, num_tiles, core_ptr->b_arr[stream]->tiles_in_buf(), core_ptr->scout_mode);

    if(core_ptr->scout_mode)
    {
        log_assert((core_ptr->stream_wait_tiles.find(stream) == core_ptr->stream_wait_tiles.end()) ||
                    (core_ptr->stream_wait_tiles[stream] == num_tiles), "invalid wait tiles in stream"  );
        core_ptr->stream_wait_tiles[stream] = num_tiles;
        //log_debug(tt::LogModel, "stream {} has num_tiles {}", stream, num_tiles);
    }
    else
        core_ptr->hlk_wait_tiles(stream, num_tiles);
}
void hlk_wait_for_free_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    log_debug(tt::LogModel, "hlk_wait_for_free_tiles on core {} stream {} num_tiles {} scout_mode {}", 
        core_ptr->core_coords, stream, num_tiles, core_ptr->scout_mode);

    // If we are trying to execute the HLK and don't have sufficient free tile space, wait for this condition
    const int32_t num_free_tiles = (!core_ptr->b_arr[stream]->is_double_buffered()) ?
        core_ptr->b_arr[stream]->metadata.L1_allocated_size_num_tiles - core_ptr->b_arr[stream]->tiles_in_buf() :
        num_tiles;

    if(!core_ptr->scout_mode &&
        (num_free_tiles < num_tiles))
    {
        log_debug(tt::LogModel, "stream {} has insufficient free tiles num_free_tiles {} wait_free_tiles {}", 
            stream, num_free_tiles, num_tiles);

        // wait until main simulation thread has produced sufficient tiles
        std::unique_lock<boost::fibers::mutex> lock(core_ptr->stream_mutex);
        core_ptr->wait_for_free_tiles_stream_id = stream;
        core_ptr->wait_for_free_tiles_stream_tiles = num_tiles;
        while((core_ptr->b_arr[stream]->metadata.L1_allocated_size_num_tiles - core_ptr->b_arr[stream]->tiles_in_buf()) < num_tiles)
        {
            log_debug(tt::LogModel, "stream {} waiting on free_tiles {} num_free_tiles {}",
                stream, num_tiles, (core_ptr->b_arr[stream]->metadata.L1_allocated_size_num_tiles - core_ptr->b_arr[stream]->tiles_in_buf()));
            // wakeup main thread
            core_ptr->stream_cond.notify_all();
            core_ptr->stream_cond.wait(lock);
        }
        log_debug(tt::LogModel, "after hlk_wait_for_free_tiles satisfied for core {}", core_ptr->core_coords);
    }

    if(core_ptr->scout_mode)
    {
        log_assert((core_ptr->stream_wait_for_tiles.find(stream) == core_ptr->stream_wait_for_tiles.end()) ||
                    (core_ptr->stream_wait_for_tiles[stream] == num_tiles), "invalid wait tiles in stream"  );
        core_ptr->stream_wait_for_tiles[stream] = num_tiles;
        //log_debug(tt::LogModel, "stream {} has num_tiles {}", stream, num_tiles);
    }
    else
        core_ptr->hlk_wait_for_free_tiles(stream, num_tiles);
}

void hlk_get_tile(tt_core *core_ptr, int stream, int index, volatile void* p_tile)
{  
    //TODO: return pointer to the tile      
}

void hlk_release_tile(tt_core* core_ptr, int stream)
{  
    //in model, there is no need to release the tile
}

void hlk_debug_dump(tt_core *core_ptr, unsigned char *data, int size)
{  
    //Nothing to do in the model    
}

void hlk_debug_dump_seek(tt_core *core_ptr, int offset)
{  
    //Nothing to do in the model      
}

void hlk_ttlog(tt_core* core_ptr, const char* fmt, ...)
{
    // Nothing to do in the model
}

void hlk_ttlog_assert(tt_core* core_ptr, bool condition, const char* fmt, ...)
{
    // Nothing to do in the model
}

void hlk_ttdump_assert(tt_core* core_ptr, bool condition, const char* fmt, ...)
{
    // Nothing to do in the model
}

void hlk_ttdump_log(tt_core* core_ptr, const char* fmt, ...)
{
    // Nothing to do in the model
}

void hlk_flush_tiles(tt_core *core_ptr, int stream, int num_tiles)
{  
    //TODO: flush tiles from buffer
}

// This version is for matmul_l1_acc
void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream, int pos, bool pack_l1_acc) {
    //log_info(tt::LogModel, "hlk_pack_tile_to_stream on core {} dst_index {} stream {}", core_ptr->core_coords, dst_index, stream);
    if(!core_ptr->scout_mode)
    {
        if((stream >= tt::constants::MIN_INTERMEDIATE_STREAMID) && (stream <= tt::constants::MAX_INTERMEDIATE_STREAMID))
            core_ptr->wrote_intermediate_buffer = true;
        core_ptr->hlk_pack_tile_to_stream(dst_index, stream, pos);
    }
}

// This version is for matmul_u
void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream) {
    if(!core_ptr->scout_mode)
    {
        if((stream >= tt::constants::MIN_INTERMEDIATE_STREAMID) && (stream <= tt::constants::MAX_INTERMEDIATE_STREAMID))
            core_ptr->wrote_intermediate_buffer = true;
        core_ptr->hlk_pack_tile_to_stream(dst_index, stream);
    }
}

void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream, int pos) {
    //log_debug(tt::LogModel, "hlk_pack_tile_to_stream on core {} dst_index {} stream {} pos {}", core_ptr->core_coords, dst_index, stream, pos);
    if(!core_ptr->scout_mode)
    {
        if((stream >= tt::constants::MIN_INTERMEDIATE_STREAMID) && (stream <= tt::constants::MAX_INTERMEDIATE_STREAMID))
            core_ptr->wrote_intermediate_buffer = true;
        core_ptr->hlk_pack_tile_to_stream(dst_index, stream, pos);
    }
}

template<int pop_blocks = 0>
void hlk_pop_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    //log_info(tt::LogModel, "hlk_pop_tiles on core {} stream {} num_tiles {} tiles_in_buf {}", 
    //    core_ptr->core_coords, stream, num_tiles, core_ptr->b_arr[stream]->tiles_in_buf());
    if(!core_ptr->scout_mode)
    {
        // remember how many tiles were popped
        core_ptr->stream_pop_tiles[stream] += num_tiles;

        core_ptr->hlk_pop_tiles(stream, num_tiles);
        //log_info(tt::LogModel, "after hlk_pop_tiles on core {} stream {} num_tiles {} tiles_in_buf {} stream_pop_tiles {}", 
        //    core_ptr->core_coords, stream, num_tiles, 
        //    core_ptr->b_arr[stream]->tiles_in_buf(),
        //    core_ptr->stream_pop_tiles[stream]);
    }
}
void hlk_copy_tile_to_dst(tt_core* core_ptr, int stream, int index, int dstindex, int transpose) {
    //log_debug(tt::LogModel, "hlk_copy_tile_to_dst on core {} stream {} index {} dstindex {}", core_ptr->core_coords, stream, index, dstindex);
    if(!core_ptr->scout_mode)
        core_ptr->hlk_copy_tile_to_dst(stream, index, dstindex, transpose);
}
void hlk_load_tile_to_dst(tt_core* core_ptr, int stream, int index, int dstindex, int transpose) {
    //log_debug(tt::LogModel, "hlk_load_tile_to_dst on core {} stream {} index {} dstindex {}", core_ptr->core_coords, stream, index, dstindex);
    if(!core_ptr->scout_mode)
        core_ptr->hlk_load_tile_to_dst(stream, index, dstindex, transpose);
}

void hlk_transpose_yz_tile(tt_core* core_ptr, int stream, int index, int dstindex, int dim, int phase) {
    //log_debug(tt::LogModel, "hlk_transpose_yz_tile on core {} stream {} index {} dstindex {}", core_ptr->core_coords, stream, index, dstindex);
    //TBD
}

void hlk_mm_tile(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim) {
    //log_debug(tt::LogModel, "hlk_mm_tile on core {} lstream {} rstream {} lindex {} rindex {} dstindex {} transpose {} ", 
    //    core_ptr->core_coords, lstream, rstream, lindex, rindex, dstindex, transpose);
    if(!core_ptr->scout_mode)
        core_ptr->hlk_mm_tile(lstream, rstream, lindex, rindex, dstindex, transpose, inner_c_dim);    
}

void hlk_mm_block(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim) {
    //log_debug(tt::LogModel, "hlk_mm_block on core {} lstream {} rstream {} lindex {} rindex {} transpose {} inner_r_dim {} inner_c_dim {} inner_d_dim ", 
    //    core_ptr->core_coords, lstream, rstream, lindex, rindex, dstindex, transpose);
    if(!core_ptr->scout_mode)
        core_ptr->hlk_mm_block(lstream, rstream, lindex, rindex, dstindex, transpose, inner_c_dim, inner_r_dim, inner_d_dim);    
}
void hlk_push_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    //log_info(tt::LogModel, "hlk_push_tiles on core {} stream {} num_tiles {}", core_ptr->core_coords, stream, num_tiles);
    if(!core_ptr->scout_mode)
    {
        core_ptr->hlk_push_tiles(stream, num_tiles);
        //log_info(tt::LogModel, "after hlk_push_tiles on core {} stream {} tiles_in_buf {}", core_ptr->core_coords, stream, core_ptr->b_arr[stream]->tiles_in_buf());
    }
}

void hlk_sfpu_requant_int32(tt_core* core_ptr, int stream, int dstindex_a, int dstindex_b, int dim) {
    //log_debug(tt::LogModel, "hlk_sfpu_requant_int32 on core {} dstindex_a {} dstindex_b {}", core_ptr->core_coords, dstindex);
    if(!core_ptr->scout_mode)
        core_ptr->hlk_sfpu_op_requant_int32(dstindex_a, dstindex_b);
}

void hlk_sfpu_dequant_int32(tt_core* core_ptr, int stream, int dstindex_a, int dstindex_b, int dim) {
    //log_debug(tt::LogModel, "hlk_sfpu_dequant_int32 on core {} dstindex_a {} dstindex_b {}", core_ptr->core_coords, dstindex);
    if(!core_ptr->scout_mode)
        core_ptr->hlk_sfpu_op_dequant_int32(dstindex_a, dstindex_b);
}

template<BinaryOp op_type, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dst_index, int transpose) {
    switch(op_type) {
        case BinaryOp::Add:
            if (broadcast_dim == Dim::None) {
                core_ptr->hlk_add_tile(lstream, rstream, lindex, rindex, dst_index, transpose);
            } else {
                core_ptr->hlk_add_tile_bcast(broadcast_dim, lstream, rstream, lindex, rindex, dst_index);
            }
            break;
        case BinaryOp::Subtract:
            if (broadcast_dim == Dim::None) {
                core_ptr->hlk_subtract_tile(lstream, rstream, lindex, rindex, dst_index, transpose);
            } else {
                core_ptr->hlk_subtract_tile_bcast(broadcast_dim, lstream, rstream, lindex, rindex, dst_index);
            }
            break;
        case BinaryOp::Multiply:
            if (broadcast_dim == Dim::None) {
                core_ptr->hlk_multiply_tile(lstream, rstream, lindex, rindex, dst_index, transpose);
            } else {
                core_ptr->hlk_multiply_tile_bcast(broadcast_dim, lstream, rstream, lindex, rindex, dst_index);
            }
            break;
    }
}

template<BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim = Dim::None>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_reuse_dest(tt_core* core_ptr, int rstream, int rindex, int dst_index, int clear_fp_32_dst_acc) {
    switch(op_type) {
        case BinaryOp::Add:
            if (dst_reuse == BinaryOpDstReuse::DstToSrcA) {
                if (broadcast_dim == Dim::None) {
                    core_ptr->hlk_add_tile_from_dst(rstream, rindex, dst_index);
                } else {
                    // Model function missing
                }
            } else {
                // Model function missing
            }
            break;
        case BinaryOp::Subtract:
            if (dst_reuse == BinaryOpDstReuse::DstToSrcA) {
                if (broadcast_dim == Dim::None) {
                    core_ptr->hlk_subtract_tile_from_dst(rstream, rindex, dst_index);
                } else {
                    // Model function missing
                }
            } else {
                // Model function missing
            }
            break;
        case BinaryOp::Multiply:
            if (dst_reuse == BinaryOpDstReuse::DstToSrcA) {
                if (broadcast_dim == Dim::None) {
                    core_ptr->hlk_multiply_tile_from_dst(rstream, rindex, dst_index);
                } else {
                    // Model function missing
                }
            } else {
                // Model function missing
            }
            break;
    }
}

void hlk_acquire_dst(tt_core* core_ptr) {
    if(!core_ptr->scout_mode)
        core_ptr->hlk_acquire_dst();
}
void hlk_release_dst(tt_core *core_ptr) {
    if(!core_ptr->scout_mode)
        core_ptr->hlk_release_dst();
}

// Placeholder init functions so that model will link correctly with HLKs
void hlk_mm_tile_init_short(tt_core *core_ptr, int lstream, int rstream, int transpose) {
    //log_debug(tt::LogModel, "hlk_mm_tile_init_short on core {} transpose {}", 
    //    core_ptr->core_coords, transpose );
    // nothing to be done
}

void hlk_mm_block_init_short(tt_core *core_ptr, int lstream, int rstream, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim) {
    //log_debug(tt::LogModel, "hlk_mm_block_init_short on core {} transpose {} inner_c_dim {} inner_r_dim {} inner_d_dim {}", 
    //    core_ptr->core_coords, transpose );
    // nothing to be done
}

void hlk_copy_tile_to_dst_init_short(tt_core *core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    //log_debug(tt::LogModel, "hlk_copy_tile_to_dst_init_short on core {}", 
    //    core_ptr->core_coords, transpose );
    // nothing to be done
}

void hlk_load_tile_to_dst_init_short(tt_core *core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    //log_debug(tt::LogModel, "hlk_load_tile_to_dst_init_short on core {}", 
    //    core_ptr->core_coords, transpose );
    // nothing to be done
}

// todo: transpose_of_faces
void hlk_transpose_xy_tile_init_short(tt_core *core_ptr, int transpose_of_faces, int within_face_16x16_transpose) {
    //log_debug(tt::LogModel, "hlk_transpose_xy_tile_init_short on core {}", 
    //    core_ptr->core_coords, transpose );
    // nothing to be done
}
void hlk_transpose_yz_tile_init_short(tt_core *core_ptr) {
    //log_debug(tt::LogModel, "hlk_transpose_yz_tile_init_short on core {}", 
    //    core_ptr->core_coords, transpose );
    // nothing to be done
}

void hlk_reconfig_unpacker_df(tt_core *core_ptr,  int new_lstream, int new_rstream) {
    log_debug(tt::LogModel, "hlk_reconfig_unpacker_df on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_reconfig_unpacker_df_srca(tt_core *core_ptr,  int old_lstream, int new_lstream) {
    log_debug(tt::LogModel, "hlk_reconfig_unpacker_df_srca on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_reconfig_unpacker_df_srca(tt_core *core_ptr, int new_lstream) {
    log_debug(tt::LogModel, "hlk_reconfig_unpacker_df_srca on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_reconfig_unpacker_df_srcb(tt_core *core_ptr,  int old_rstream, int new_rstream) {
    log_debug(tt::LogModel, "hlk_reconfig_unpacker_df_srcb on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_reconfig_unpacker_df_srcb(tt_core *core_ptr, int new_rstream) {
    log_debug(tt::LogModel, "hlk_reconfig_unpacker_df_srcb on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_reconfig_packer_l1_acc(tt_core *core_ptr, int config) {
    log_debug(tt::LogModel, "hlk_reconfig_packer_l1_acc on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_sfpu_requant_int32_init(tt_core *core_ptr, int stream, int zero_point) {
    // log_debug(tt::LogModel, "hlk_sfpu_requant_int32_init on core {}, stream {}, zero_point {}", 
    //     core_ptr->core_coords);
    // nothing to be done
}

void hlk_sfpu_dequant_int32_init(tt_core *core_ptr, int stream, int zero_point) {
    // log_debug(tt::LogModel, "hlk_sfpu_dequant_int32_init on core {}, stream {}, zero_point {}", 
    //     core_ptr->core_coords);
    // nothing to be done
}

void hlk_tilize_init_short(tt_core *core_ptr) {
    //log_debug(tt::LogModel, "hlk_tilize_init_short on core {}", 
    //    core_ptr->core_coords);
    // nothing to be done
}

void hlk_hw_config_single_operand(tt_core* core_ptr, int stream, int transpose) {
    //log_debug(tt::LogModel, "hlk_hw_config_single_operand on core {}",
    //     core_ptr->core_coords);
    // nothing to be done
};

void hlk_hw_config_two_operands(tt_core* core_ptr, int stream_a, int stream_b, int transpose) {
    //log_debug(tt::LogModel, "hlk_hw_config_two_operands on core {}",
    //     core_ptr->core_coords);
    // nothing to be done
};

void hlk_hw_config_tilize(tt_core* core_ptr, int stream, int ct_dim) {
    //log_debug(tt::LogModel, "hlk_hw_config_tilize on core {}",
    //     core_ptr->core_coords);
    // nothing to be done
};

template<ReduceFunc reduce_func, Dim reduce_dim>
void hlk_hw_config_reduce(tt_core* core_ptr, int stream, float coefficient) {
    //log_debug(tt::LogModel, "hlk_hw_config_reduce on core {}",
    //     core_ptr->core_coords);
    // nothing to be done
};

void hlk_hw_config_matmul(tt_core* core_ptr, int stream_a, int stream_b, int transpose) {
    //log_debug(tt::LogModel, "hlk_hw_config_matmul on core {}",
    //     core_ptr->core_coords);
    // nothing to be done
};


void hlk_reconfig_packer_df(tt_core *core_ptr, int old_stream, int new_stream) {
    log_debug(tt::LogModel, "hlk_reconfig_packer_df on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_reconfig_packer_df(tt_core *core_ptr, int new_stream) {
    log_debug(tt::LogModel, "hlk_reconfig_packer_df on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_reconfig_unpacker_df(tt_core *core_ptr,  int old_lstream, int new_lstream, int old_rstream, int new_rstream) {
    log_debug(tt::LogModel, "hlk_reconfig_unpacker_df on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_relu_config(tt_core *core_ptr, int config) {
    log_debug(tt::LogModel, "hlk_relu_config on core {}", 
       core_ptr->core_coords);
    // nothing to be done
}

void hlk_copy_tile_to_dst_init(tt_core *core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    log_debug(tt::LogModel, "hlk_copy_tile_to_dst_init on core {} ", 
        core_ptr->core_coords );
    // nothing to be done
}

void hlk_load_tile_to_dst_init(tt_core *core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    log_debug(tt::LogModel, "hlk_load_tile_to_dst_init on core {} ", 
        core_ptr->core_coords );
    // nothing to be done
}

void hlk_transpose_xy_tile_init(tt_core *core_ptr, int transpose_of_faces, int within_face_16x16_transpose) {
    log_debug(tt::LogModel, "hlk_transpose_xy_tile_init on core {} ", 
        core_ptr->core_coords );
    // nothing to be done
}

void hlk_transpose_yz_tile_init(tt_core *core_ptr) {
    log_debug(tt::LogModel, "hlk_transpose_yz_tile_init on core {} ", 
        core_ptr->core_coords );
    // nothing to be done
}

void hlk_mm_tile_init(tt_core *core_ptr, int lstream, int rstream, int transpose) {
    log_debug(tt::LogModel, "hlk_mm_tile_init on core {} lstream {} rstream {} transpose {}", 
        core_ptr->core_coords, lstream, rstream, transpose);
    // nothing to be done
}

void hlk_mm_block_init(tt_core *core_ptr, int lstream, int rstream, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim) {
    log_debug(tt::LogModel, "hlk_mm_block_init on core {} lstream {} rstream {} transpose {} inner_c_dim {} inner_r_dim {} inner_d_dim {}", 
        core_ptr->core_coords, lstream, rstream, transpose, inner_c_dim, inner_r_dim, inner_d_dim);
    // nothing to be done
}

template<BinaryOp op_type, Dim broadcast_dim>
void hlk_binary_op_init(tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest) {
    // do nothing
}

template<BinaryOp op_type, Dim broadcast_dim>
void hlk_binary_op_init_short(tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest) {
    // do nothing
}

template<BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim>
void hlk_binary_op_reuse_dest_init(tt_core* core_ptr) {
    // do nothing
}

template<BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim>
void hlk_binary_op_reuse_dest_init_short(tt_core* core_ptr) {
    // do nothing
}

void hlk_tilize_init(tt_core *core_ptr) {
    log_debug(tt::LogModel, "hlk_tilize_init on core {}", 
        core_ptr->core_coords);
    // nothing to be done
}

template<ReduceFunc reduce_func, Dim dim>
void hlk_reduce_tile_init_short(tt_core* core_ptr, int within_face_16x16_transpose) {
    log_debug(tt::LogModel, "hlk_reduce_tile_init_short on core {}",
        core_ptr->core_coords);
    // nothing to be done
}

template<ReduceFunc reduce_func, Dim dim>
void hlk_reduce_tile_init(tt_core* core_ptr, int within_face_16x16_transpose) {
    log_debug(tt::LogModel, "hlk_reduce_tile_init on core {}", 
        core_ptr->core_coords);
    // nothing to be done
}

/////////////////////////////////////////////////////////
// These are Deprecated API calls leftover from spatial1
/////////////////////////////////////////////////////////

template <SfpuOp sfpu_op>
void hlk_unary_sfpu_init(tt_core *core_ptr, int stream, unsigned int sfpu_specific_parameter) {

}

template<SfpuOp sfpu_op>
void hlk_unary_sfpu_op(tt_core* core_ptr, int stream, int dst_index, int dim, unsigned int sfpu_specific_param_0, unsigned int sfpu_specific_param_1) {
    core_ptr->hlk_unary_sfpu_op<sfpu_op>(dst_index, sfpu_specific_param_0, sfpu_specific_param_1);
}

template<ReduceFunc reduce_func, Dim dim>
void hlk_reduce_tile(tt_core* core_ptr, int lstream, int lindex, int dstindex, float coefficient) {
    log_debug(tt::LogModel, "hlk_reduce_tile on core {} reduce_func {} dim {} lstream {} lindex {} dstindex {} coeff {}", 
        core_ptr->core_coords, reduce_func, dim, lstream, lindex, dstindex, coefficient);
    core_ptr->hlk_reduce_tile((ReduceFunc)reduce_func, (Dim)dim, lstream, lindex, dstindex, coefficient);
}


void hlk_tilize(tt_core* core_ptr, int rstream, int rindex, int dstindex, int ct_dim) {
    log_debug(tt::LogModel, "hlk_tilize on core {} rstream {} rindex {} dstindex {} ct_dim {}", 
        core_ptr->core_coords, rstream, rindex, dstindex, ct_dim);
    core_ptr->hlk_tilize(rstream, rindex, dstindex);
}

void hlk_pre_input_processing(tt_core* core_ptr, const int input_index) {

}

void hlk_post_input_processing(tt_core* core_ptr) {

}

// Template function instantiations...


// Reduce tile
#define REDUCE_TILE(Func, Dim) \
    template void hlk_reduce_tile<ReduceFunc::Func, Dim>(tt_core *core_ptr, int lstream, int lindex, int dstindex, float coefficient);

#define REDUCE_TILE_FUNC(Func) \
    REDUCE_TILE(Func, Dim::C) \
    REDUCE_TILE(Func, Dim::R) \
    REDUCE_TILE(Func, Dim::RC) \

REDUCE_TILE_FUNC(Avg);
REDUCE_TILE_FUNC(Max);
REDUCE_TILE_FUNC(Sum);

// Reduce inits
#define REDUCE_TILE_INIT(Func, RFunc, Dim) \
    template void Func<ReduceFunc::RFunc, Dim>(tt_core *core_ptr, int within_face_16x16_transpose); \

#define REDUCE_TILE_INIT_DIM(Func, RFunc) \
    REDUCE_TILE_INIT(Func, RFunc, Dim::C) \
    REDUCE_TILE_INIT(Func, RFunc, Dim::R) \
    REDUCE_TILE_INIT(Func, RFunc, Dim::RC) \

#define REDUCE_TILE_INIT_RFUNC(Func) \
    REDUCE_TILE_INIT_DIM(Func, Avg) \
    REDUCE_TILE_INIT_DIM(Func, Max) \
    REDUCE_TILE_INIT_DIM(Func, Sum) \

REDUCE_TILE_INIT_RFUNC(hlk_reduce_tile_init)
REDUCE_TILE_INIT_RFUNC(hlk_reduce_tile_init_short)

// Reduce hw config
#define REDUCE_HW_CONFIG_REDUCE(Func, Dim) \
    template void hlk_hw_config_reduce<ReduceFunc::Func, Dim>(tt_core *core_ptr, int stream, float coefficient);

#define REDUCE_HW_CONFIG_REDUCE_FUNC(Func) \
    REDUCE_HW_CONFIG_REDUCE(Func, Dim::C) \
    REDUCE_HW_CONFIG_REDUCE(Func, Dim::R) \
    REDUCE_HW_CONFIG_REDUCE(Func, Dim::RC) \

REDUCE_HW_CONFIG_REDUCE_FUNC(Avg);
REDUCE_HW_CONFIG_REDUCE_FUNC(Max);
REDUCE_HW_CONFIG_REDUCE_FUNC(Sum);

// Pop tiles
template void hlk_pop_tiles<0>(tt_core *core_ptr, int stream, int num_tiles);
template void hlk_pop_tiles<1>(tt_core *core_ptr, int stream, int num_tiles);

#define SFPU_OP_FUNC_AND_INIT(sfpu_op) \
    template void hlk_unary_sfpu_op<sfpu_op>(tt_core *core_ptr, int stream, int dst_index, int dim, unsigned int sfpu_specific_param_0, unsigned int sfpu_specific_param_1); \
    template void hlk_unary_sfpu_init<sfpu_op>(tt_core *core_ptr, int stream, unsigned int sfpu_specific_parameter); \

#define BINARY_OP_FUNC_AND_INIT(binary_op, broadcast_dim) \
    template void hlk_binary_op_init<binary_op, broadcast_dim>(tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest); \
    template void hlk_binary_op_init_short<binary_op, broadcast_dim>(tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest); \
    template void hlk_binary_op<binary_op, broadcast_dim>(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dst_index, int transpose); \

#define BINARY_OP_DST_REUSE_FUNC_AND_INIT(binary_op, dst_reuse, broadcast_dim) \
    template void hlk_binary_op_reuse_dest_init<binary_op, dst_reuse, broadcast_dim>(tt_core* core_ptr); \
    template void hlk_binary_op_reuse_dest_init_short<binary_op, dst_reuse, broadcast_dim>(tt_core* core_ptr); \
    template void hlk_binary_op_reuse_dest<binary_op, dst_reuse, broadcast_dim>(tt_core* core_ptr, int rstream, int rindex, int dst_index, int clear_fp_32_dst_acc); \

SFPU_OP_FUNC_AND_INIT(SfpuOp::Exp);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Log);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Sqrt);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Gelu);
SFPU_OP_FUNC_AND_INIT(SfpuOp::GeluDerivative);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Reciprocal);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Sigmoid);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Dropout);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Tanh);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Square);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Power);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Sine);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Cosine);
SFPU_OP_FUNC_AND_INIT(SfpuOp::ReluMax);
SFPU_OP_FUNC_AND_INIT(SfpuOp::ReluMin);
SFPU_OP_FUNC_AND_INIT(SfpuOp::LRelu);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Abs);
SFPU_OP_FUNC_AND_INIT(SfpuOp::Max);

BINARY_OP_FUNC_AND_INIT(BinaryOp::Add, Dim::None);
BINARY_OP_FUNC_AND_INIT(BinaryOp::Add, Dim::R);
BINARY_OP_FUNC_AND_INIT(BinaryOp::Add, Dim::C); // ?

BINARY_OP_FUNC_AND_INIT(BinaryOp::Subtract, Dim::None);
BINARY_OP_FUNC_AND_INIT(BinaryOp::Subtract, Dim::R);
BINARY_OP_FUNC_AND_INIT(BinaryOp::Subtract, Dim::C);

BINARY_OP_FUNC_AND_INIT(BinaryOp::Multiply, Dim::None);
BINARY_OP_FUNC_AND_INIT(BinaryOp::Multiply, Dim::R);
BINARY_OP_FUNC_AND_INIT(BinaryOp::Multiply, Dim::C);

BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Add, BinaryOpDstReuse::DstToSrcA, Dim::None);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Add, BinaryOpDstReuse::DstToSrcA, Dim::R);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Add, BinaryOpDstReuse::DstToSrcA, Dim::C);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Add, BinaryOpDstReuse::DstToSrcB, Dim::None); // No broadcast variants supported for dstB reuse atm

BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Subtract, BinaryOpDstReuse::DstToSrcA, Dim::None);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Subtract, BinaryOpDstReuse::DstToSrcA, Dim::R);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Subtract, BinaryOpDstReuse::DstToSrcA, Dim::C);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Subtract, BinaryOpDstReuse::DstToSrcB, Dim::None); // No broadcast variants supported for dstB reuse atm

BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Multiply, BinaryOpDstReuse::DstToSrcA, Dim::None);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Multiply, BinaryOpDstReuse::DstToSrcA, Dim::R);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Multiply, BinaryOpDstReuse::DstToSrcA, Dim::C);
BINARY_OP_DST_REUSE_FUNC_AND_INIT(BinaryOp::Multiply, BinaryOpDstReuse::DstToSrcB, Dim::None); // No broadcast variants supported for dstB reuse atm
