// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "dram.hpp"

#include <algorithm>

#include "common/size_lib.hpp"
#include "epoch_q.h"
#include "model.hpp"
#include "utils.hpp"
#include "utils/logger.hpp"

namespace tt
{
    tt_dram_io::tt_dram_io() {}
    tt_dram_io::tt_dram_io(const string &name) : name(name) {}
    tt_dram_io::tt_dram_io(const string &name, bool epoch_to_epoch_connection) : name(name), epoch_to_epoch_connection(epoch_to_epoch_connection) {}

    int tt_dram_io::size_bytes()
    {
        int tiles;
        tiles = buffer_shape.volume();
        int size;
        // Pad size by 32 bytes for pointers and other metadata
        size = (grid_shape[0] * grid_shape[1] * (q_slots * tiles * size::get_tile_size_in_bytes(data_format, true) + 32));
        TT_ASSERT(size % 16 == 0);
        TT_ASSERT(size % 32 == 0);
        return(size);
    }

    vector<tt_buffer_queue *> *tt_dram_io::get_bufqs_ptr()
    {
        return(&bufqs);
    }

    int tt_dram_io::occupancy()
    {
        TT_ASSERT(bufqs.size() > 0);
        return(bufqs[0]->occupancy());
    }

    bool tt_dram_io::all_empty()
    {
        vector <tt_buffer_queue *>::iterator it;
        bool empty = true;
        if(bufqs.empty())
        {
            empty = true;
        }
        else
        {
            for(it=bufqs.begin();it!=bufqs.end();++it)
            {
                if(!(*it)->bufq_totally_empty()) empty = false;
            }
        }

        // if (empty) {
        //     cout << grid_shape[0] << ", " << grid_shape[1] << endl;
        //     TT_ASSERT(false);
        // }

        return(empty);
    }

    bool tt_dram_io::all_not_empty()
    {
        vector <tt_buffer_queue *>::iterator it;
        bool not_empty = true;
        if (!untilized_payloads.empty()) {
            return not_empty;
        }
        if(bufqs.empty())
        {
            not_empty = false;
        }
        else
        {
            for(it=bufqs.begin();it!=bufqs.end();++it)
            {
                if((*it)->bufq_totally_empty()) not_empty = false;
            }
        }
        return(not_empty);
    }

    void tt_dram_io::add_buffer_queue(int slots, const tt_buffer_shape& buffer_shape, DataFormat data_formati, bool allocated_to_dram = true) {
        q_slots = slots;

        // Shape of each buffer (in bufq)
        this->buffer_shape = buffer_shape;
        this->data_format = data_formati;


        tt_buffer_queue* buf_queue = new tt_buffer_queue(q_slots, buffer_shape, data_format);
        buf_queue->parent_dram_io = this;
        this->bufqs.push_back(buf_queue);
    }


    // creating an ioq without associating queues with specific cores (mapping is not 1-to-1)
    void tt_dram_io::init_ioq(int slots, const tt_buffer_shape& buffer_shape, DataFormat data_formati, tt_grid_shape grid_shape, bool untilized_output)
    {
        q_slots = slots;

        // Shape of each buffer (in bufq)
        this->buffer_shape = buffer_shape;
        this->data_format = data_formati;

        // Core grid R&C
        this->grid_shape = grid_shape;

        for(int i = 0; i < grid_shape[0]; ++i)
        {
            for(int j = 0; j < grid_shape[1]; ++j)
            {
                tt_buffer_queue* buf_queue = new tt_buffer_queue(q_slots, buffer_shape, data_format);
                buf_queue->parent_dram_io = this;
                this->bufqs.push_back(buf_queue);
            }
        }

        // TODO(arakhmati): Fix grid_shape spaghetti
//        if (this->get_tensor()->get_grid_shape() == tt_grid_shape{0, 0}) {
//            this->get_tensor()->set_grid_shape(grid_shape);
//        }
//
//        if (!untilized_output) {
//            TT_ASSERT (this->get_tensor()->get_grid_shape() == grid_shape);
//        }

    }

    void tt_dram_io::init_ioq(int slots, const tt_buffer_shape& buffer_shape, DataFormat data_formati, tt_grid_shape grid_shape, vector <vector <tt_core *> > &core_grid, bool untilized_output)
    {
        this->q_slots = slots;

        // Shape of each buffer (in bufq)
        this->buffer_shape = buffer_shape;
        this->data_format = data_formati;

        // Core grid R&C
        this->grid_shape = grid_shape;

        for(int i = 0; i < grid_shape[0]; ++i)
        {
            for(int j = 0; j < grid_shape[1]; ++j)
            {
                tt_buffer_queue* buf_queue = new tt_buffer_queue(q_slots, buffer_shape, data_format, core_grid[i][j]);
                buf_queue->parent_dram_io = this;
                this->bufqs.push_back(buf_queue);
            }
        }

        // TODO(arakhmati): Fix grid_shape spaghetti
        if (this->get_tensor()->get_grid_shape() == tt_grid_shape{0, 0}) {
            this->get_tensor()->set_grid_shape(grid_shape);
        }
        if (!untilized_output) {
            TT_ASSERT (this->get_tensor()->get_grid_shape() == grid_shape);
        }
    }

    bool tt_dram_io::is_initialized() {
        return bufqs.size() > 0;
    }

    void tt_dram_io::set_epoch_to_epoch_connection(bool _epoch_to_epoch_connection) { epoch_to_epoch_connection = _epoch_to_epoch_connection; }
    bool tt_dram_io::is_epoch_to_epoch_connection() const {return epoch_to_epoch_connection;}

    void tt_dram_io::set_untilized_output(bool _untilized_output) { 
        untilized_output = _untilized_output;
        TT_ASSERT(bufqs.size() > 0);
        
        vector<tt_buffer_queue *>::iterator it;
        for(it=bufqs.begin();it!=bufqs.end();++it)
        {
           (*it)->set_untilized_output(_untilized_output);
        }
    }
    void tt_dram_io::set_untilized_output_per_core_rc(int _rdim, int _cdim) {
        untilized_output_per_core_r = _rdim;
        untilized_output_per_core_c = _cdim;
    }
    int tt_dram_io::get_untilized_output_per_core_r() { return untilized_output_per_core_r; }
    int tt_dram_io::get_untilized_output_per_core_c() { return untilized_output_per_core_c; }

    void tt_dram_io::set_tensor(tt_tensor* _tensor) {tensor = _tensor;}
    tt_tensor* tt_dram_io::get_tensor() const { 
        TT_ASSERT(tensor != nullptr && "Tensor not set for tt_dram_io");
        return tensor;
    }
    TensorType tt_dram_io::get_tensor_type() {
        TT_ASSERT(tensor != nullptr && "Tensor not set for tt_dram_io");
        return tensor->get_tensor_type();
    }
    
    void tt_dram_io::set_prologue_read(int epoch) { prologue_read.insert(epoch); }
    bool tt_dram_io::is_prologue_read(int epoch) { return prologue_read.find(epoch) != prologue_read.end(); }
    bool tt_dram_io::is_prologue_read_in_any_epoch() {return (prologue_read.size() > 0);}

    void tt_dram_io::set_epilogue_write(int epoch) { epilogue_write.insert(epoch); }
    bool tt_dram_io::is_epilogue_write(int epoch) { return epilogue_write.find(epoch) != epilogue_write.end(); }
    bool tt_dram_io::is_epilogue_write_in_any_epoch() {return (epilogue_write.size() > 0);}

    void tt_dram_io::set_pipe_read(int epoch) { pipe_read.insert(epoch); }
    bool tt_dram_io::is_pipe_read(int epoch) { return pipe_read.find(epoch) != pipe_read.end(); }
    void tt_dram_io::set_pipe_write(int epoch) { pipe_write.insert(epoch); }
    bool tt_dram_io::is_pipe_write(int epoch) { return pipe_write.find(epoch) != pipe_write.end(); }
    
    void tt_dram_io::add_consumer_epoch(int _epoch) {
        consumer_epochs.push_back(_epoch);
    }
    void tt_dram_io::set_producer_epoch(int _epoch) {
        TT_ASSERT(producer_epochs.size() == 0 ,  "Setting producer epoch on a dram_io that already has a producer epoch assigned");
        producer_epochs.push_back(_epoch);
    }
    bool tt_dram_io::is_producer_epoch(int _epoch) {
        return (std::find(producer_epochs.begin(), producer_epochs.end(), _epoch) != producer_epochs.end());
    };
    bool tt_dram_io::is_consumer_epoch(int _epoch) {
        return (std::find(consumer_epochs.begin(), consumer_epochs.end(), _epoch) != consumer_epochs.end());
    };

    vector<int>& tt_dram_io::get_all_consumer_epochs() {
        return consumer_epochs;
    }

    bool tt_dram_io::all_consumer_epochs_are_prologue_read() {
        bool all_consumers_as_prologue = true;
        for (int ep : consumer_epochs) {
            if (!is_prologue_read(ep)) {
                all_consumers_as_prologue = false;
            }
        }
        return all_consumers_as_prologue;
    }

    int tt_dram_io::get_last_consuming_epoch() {
        return *max_element(consumer_epochs.begin(), consumer_epochs.end());
    }

    bool tt_dram_io::has_consuming_epochs() {
        return (consumer_epochs.size() > 0);
    }

    bool tt_dram_io::has_producer_epoch() {
        return (producer_epochs.size() > 0);
    }

    bool tt_dram_io::is_having_untilized_output() {
      return untilized_output;
    }
    void tt_dram_io::popall()
    {
        vector<tt_buffer_queue *>::iterator it;
        int i = 0;
        if (is_having_untilized_output()) {
          untilized_payloads.pop_front();
        } else {
            for(it=bufqs.begin();it!=bufqs.end();++it)
            {
               (*it)->bufq_pop();
               ++i;
            }
        }
    }

    void tt_dram_io::clear_consumed_flag()
    {
        for (tt_buffer_queue* buffer_queue: this->bufqs) {
            buffer_queue->clear_consumed_flag();
        }
    }

    void tt_dram_io::push(int index, tt_buffer *ptr)
    {
        // Check that dimensions and data type are
        // consistent
        TT_ASSERT(buffer_shape == ptr->get_buffer_shape());
        TT_ASSERT(data_format == ptr->get_data_format());

        // If pushing into a queue index that's still not existence
        // add queues all the way up to that index
        TT_ASSERT(index < bufqs.size());

        bufqs[index]->push(ptr);
    }

    void tt_dram_io::push(tt_tensor *in_tensor, bool vertical_mblock_scan, int grid_r, int grid_c, int ublock_rt, int ublock_ct, int ublock_id, int mblock_m, int mblock_n, int t)
    {
        TT_ASSERT(grid_r == grid_shape.r);
        TT_ASSERT(grid_c == grid_shape.c);
        TT_ASSERT(buffer_shape.block_shape.r == ublock_rt*mblock_m);
        TT_ASSERT(buffer_shape.block_shape.c == ublock_ct*mblock_n);
        TT_ASSERT(in_tensor->getz() == t);
        TT_ASSERT(in_tensor->getrt() == grid_r*ublock_rt*mblock_m);
        TT_ASSERT(in_tensor->getct() == grid_c*ublock_ct*mblock_n);


        tt_buffer_metadata metadata;
        metadata.id = 0;
        metadata.L1_allocated_size_num_tiles = ublock_rt*mblock_m*ublock_ct*mblock_n * 2;
        metadata.data_format = data_format;
        metadata.type = tt::BufferType::Input;
        metadata.buffer_shape = buffer_shape;

        // slice tensor into macro blocks and push them into queue

        for(int z=0; z<in_tensor->getz(); ++z)
        {
            for(int gr=0; gr<grid_r; ++gr)
            {
                for(int gc=0; gc<grid_c; ++gc)
                {
                    // one buffer per grid location
                    tt_buffer_metadata metadata;
                    metadata.id = 0;
                    metadata.L1_allocated_size_num_tiles = ublock_rt*mblock_m*ublock_ct*mblock_n;
                    metadata.data_format = data_format;
                    metadata.type = tt::BufferType::Output;
                    metadata.buffer_shape = buffer_shape;
                    tt_buffer* buffer = new tt_buffer(metadata, NULL, false);

                    if(vertical_mblock_scan)
                    {
                        for(int ubc=0; ubc<mblock_n; ++ubc)
                        {
                            for(int ubr=0; ubr<mblock_m; ++ubr)
                            {
                                for(int tr=0; tr<ublock_rt; ++tr)
                                {
                                    for(int tc=0; tc<ublock_ct; ++tc)
                                    {
                                        int rt_index = gr*ublock_rt*mblock_m + ubr*ublock_rt + tr;
                                        int ct_index = gc*ublock_ct*mblock_n + ubc*ublock_ct + tc;
                                        buffer->add_tile(in_tensor->get_tile(rt_index, ct_index,z,0));
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        for(int ubr=0; ubr<mblock_m; ++ubr)
                        {
                            for(int ubc=0; ubc<mblock_n; ++ubc)
                            {
                                for(int tr=0; tr<ublock_rt; ++tr)
                                {
                                    for(int tc=0; tc<ublock_ct; ++tc)
                                    {
                                        int rt_index = gr*ublock_rt*mblock_m + ubr*ublock_rt + tr;
                                        int ct_index = gc*ublock_ct*mblock_n + ubc*ublock_ct + tc;
                                        buffer->add_tile(in_tensor->get_tile(rt_index, ct_index,z,0));
                                    }
                                }
                            }
                        }
                    }

                    this->push(gr*grid_c + gc, buffer);
                }
            }
        }
    }

    tt_tensor tt_dram_io::get(int q_offset, int grid_r, int grid_c, int ublock_rt, int ublock_ct, int ublock_id, int mblock_m, int mblock_n, int t)
    {
        tt_shape shape;
        shape.rt = grid_r * mblock_m * ublock_rt;
        shape.ct = grid_c * mblock_n * ublock_ct;
        shape.z = t;
        shape.w = 1;
        tt_tensor new_tensor(shape, data_format);

        // re-assemble tensor from micro blocks and return from queue
        vector <vector <vector <tt_tile> > > tmp_z;
        for(int z=0; z<t; ++z)
        {
            vector <vector <tt_tile> > tmp_r;
            for(int gr=0; gr<grid_r; ++gr)
            {
                for(int ubr=0; ubr<mblock_m; ++ubr)
                {
                    for(int tr=0; tr<ublock_rt; ++tr)
                    {
                        vector <tt_tile> tmp_c;
                        for(int gc=0; gc<grid_c; ++gc)
                        {
                            for(int ubc=0; ubc<mblock_n; ++ubc)
                            {
                                for(int tc=0; tc<ublock_ct; ++tc)
                                {
                                    int buf_index = gr*grid_c + gc;
                                    int tile_index;
                                    tt_buffer *buffer;
                                    buffer = get_buf_ptr(buf_index,q_offset+z);

                                    tile_index = (ublock_rt*ublock_ct)*(ubr*mblock_n + ubc) + (tr*ublock_ct + tc);
                                    tmp_c.push_back(buffer->get_tile(tile_index));
                                }
                            }
                        }
                        tmp_r.push_back(tmp_c);
                    }
                }
            }
            tmp_z.push_back(tmp_r);
        }
        new_tensor.tile_tensor.push_back(tmp_z);
        return(new_tensor);
    }

    tt_buffer *tt_dram_io::get_buf_ptr(int index, int offset)
    {
        TT_ASSERT(index < bufqs.size());
        return(bufqs[index]->get_buf_ptr(offset));
    }

    tt_buffer *tt_dram_io::get_buf_ptr(int index)
    {
        TT_ASSERT(index < bufqs.size());
        return(bufqs[index]->get_buf_ptr());
    }

    tt_buffer_queue *tt_dram_io::get_q_ptr(int index)
    {
        TT_ASSERT(index < bufqs.size());

        return(bufqs[index]);
    }

    void tt_dram_io::set_local_rd_ptr(int rd_ptr)
    {
        for (tt_buffer_queue* bufq: this->bufqs)
        {
            bufq->set_q_offset(rd_ptr);
        }
    }

    void tt_dram_io::set_global_rd_ptr(int rd_ptr)
    {
        for (tt_buffer_queue* bufq: this->bufqs)
        {
            bufq->set_rd_ptr(rd_ptr, false);
        }
    }

    void tt_dram_io::reset_rd_ptrs(bool use_shadow_rd_ptr)
    {
        for (tt_buffer_queue* bufq: this->bufqs)
        {
            bufq->set_rd_ptr(0, false);
        }
    }

    void tt_dram_io::reset_rd_offset_ptrs()
    {
        for (tt_buffer_queue* bufq: this->bufqs)
        {
            bufq->reset_offset();
        }
    }

    void tt_dram_io::pop_dram_io_slot(int num_pops)
    {
        if (!can_loopback) {
            for (tt_buffer_queue* bufq: this->bufqs) {
                bufq->clear_and_pop_slot(num_pops);
            }
        }
    }

    // Called by passes.cpp
    void tt_dram_io::populate_dram_io_desc(tt_dram_io_desc& io_desc)
    {
        TT_ASSERT(bufqs.size() > 0 && "tt_dram_io descriptor can't be populated unless buffer queues are created!");
        io_desc.bufq_grid_dim_r = grid_shape[0];
        io_desc.bufq_grid_dim_c = grid_shape[1];
        io_desc.bufq_num_slots = q_slots;
        for (int rr=0; rr<grid_shape[0]; rr++) {
            for (int cc=0; cc<grid_shape[1]; cc++) {
                tt_buffer_queue* bufq = this->get_q_ptr(rr*grid_shape[1] + cc);
                TT_ASSERT(bufq->is_allocated_to_dram() && "tt_dram_io descriptor can't be populated unless buffer queues are allocated to DRAM!");
                // Temp disable
                // TT_ASSERT(bufq->d_chan == 0 && "");
                io_desc.bufq_start_addr_channel.push_back({bufq->d_addr, 0xFF});
                TT_ASSERT(tensor != nullptr && "Tensor not set for tt_dram_io");
                io_desc.bufq_target_format = tensor->get_data_format();
                io_desc.bufq_entry_size = bufq->epoch_tiles * size::get_tile_size_in_bytes(tensor->get_data_format(), false);
            }
        }
    }

    bool tt_dram_io::use_shadow_rd_ptr(int epoch) {
        return (
            this->use_shadow_rd_ptr_for_epochs.find(epoch) != this->use_shadow_rd_ptr_for_epochs.end()
        );
    }

    tt_dram_io::~tt_dram_io()
    {
        vector<tt_buffer_queue *>::iterator it;
        for(it=bufqs.begin();it!=bufqs.end();++it)
        {
            delete *it;
        }
    }

    tt_dram::tt_dram()
    {
    }

    void tt_dram::incr_epoch()
    {
        buffers.push_back(vector <tt_buffer *>());
        epilog_written_buffers.push_back(map<tt_core*, tt_buffer*>());
    }

    void tt_dram::reset_epochs()
    {
        buffers.clear();
        epilog_written_buffers.clear();
    }

    vector<tt_buffer*>& tt_dram::get_buf_vec_ptr(int epoch)
    {
        TT_ASSERT(epoch<buffers.size());
        return(buffers[epoch]);
    }
 
    map<tt_core*, tt_buffer*>& tt_dram::get_epilog_written_map_ptr(int epoch)
    {
        TT_ASSERT(epoch < epilog_written_buffers.size());
        return(epilog_written_buffers[epoch]);
    }



    void tt_dram::clear_buffers(int epoch)
    {
        // Clears the data inside all buffers for this epoch
        for(tt_buffer* buffer: buffers.at(epoch))
        {
            buffer->clear_buf();
        }
    }

    tt_dram::~tt_dram()
    {
        // DRAM owns all of it's buffers
        // it needs to delete them on destruction
        vector<tt_buffer *>::iterator it;
        for(int i=0;i<buffers.size();++i)
        {
            for(it=buffers[i].begin();it!=buffers[i].end();++it)
            {
                delete (*it);
            }
        }
    }

    void tt_dram::add_buffer(tt_buffer *buf_ptr, int epoch)
    {
        TT_ASSERT(epoch < buffers.size());
        // per_core_bufs[epoch][buf_ptr->get_core_ptr()->get_logical_absolute_row_id()][buf_ptr->get_core_ptr()->get_logical_absolute_col_id()].push_back(buf_ptr);
        buffers[epoch].push_back(buf_ptr);
    }

    void tt_dram::add_epilog_written_buffer(tt_buffer *buf_ptr, int epoch)
    {
        TT_ASSERT(epoch < epilog_written_buffers.size());
        buf_ptr->set_epilog_written_flag(true);
        epilog_written_buffers[epoch].insert({buf_ptr->creating_core, buf_ptr});
    }

    tt_buffer* tt_dram::get_epilog_written_buffer(tt_core* core, int epoch) {
        map<tt_core*, tt_buffer*>::iterator it;
        it = epilog_written_buffers[epoch].find(core);
        if (it != epilog_written_buffers[epoch].end()) {
            return (it->second);
        } else {
            return NULL;
        }
    }

    bool tt_dram::epilog_written_buffer_array_empty(int epoch) {
        return epilog_written_buffers[epoch].empty();
    }

    int tt_dram_chan_desc::get_chan_capacity() const
    {
        return chan_capacity;
    }


    tt_dram_chan_desc::tt_dram_chan_desc() :
	    chan_num(-1),
        subchannel(-1),
	    up_logical_core_coord{-1,-1},
        dn_logical_core_coord{-1,-1},
	    up_routing_core_coord{-1,-1},
        dn_routing_core_coord{-1,-1},
	    base_addr(~0ULL),
        chan_capacity(~0ULL)
    {}
    
    bool tt_dram_chan_desc::is_mapped_to_logical_core(int logical_r, int logical_c) const{
        return (logical_r >= this->up_logical_core_coord[0]) && (logical_r <= this->dn_logical_core_coord[0]) && (logical_c >= this->up_logical_core_coord[1]) && (logical_c <= this->dn_logical_core_coord[1]);
    }

    bool tt_dram_chan_desc::is_mapped_to_routing_core(int routing_r, int routing_c) const {
        return (routing_r >= this->up_routing_core_coord[0]) && (routing_r <= this->dn_routing_core_coord[0]) && (routing_c >= this->up_routing_core_coord[1]) && (routing_c <= this->dn_routing_core_coord[1]);
    }


    tt_chan_alloc_struct::tt_chan_alloc_struct(uint64_t tr, uint64_t tstart_table, uint64_t tkc, uint64_t tb, uint64_t tchan, uint64_t eqslot_size, uint64_t top_q_update)
    {
        top_of_reserved = tr;
        top_of_epoch0_start_table = tstart_table;
        top_of_kernel_cache = tkc;
        top_of_binaries = tb;
        top_of_chan = tchan;
        current_kernel_cache_ptr = top_of_epoch0_start_table;
        current_bin_ptr = top_of_kernel_cache;
        current_queue_ptr = top_of_chan;
        top_of_q_update_blobs = top_q_update;
        current_q_update_blob_ptr = top_of_binaries;
        current_buf_ptr = top_q_update;
        epoch_q_start = top_of_kernel_cache;
        epoch_q_slot_size = eqslot_size;
    }

    uint64_t tt_chan_alloc_struct::alloc_buf(uint64_t size_bytes)
    {
        uint64_t tmp = current_buf_ptr;
        TT_ASSERT((size_bytes & 0x3) == 0);
        current_buf_ptr += size_bytes;
        TT_ASSERT((current_buf_ptr & 0x3) == 0);
        TT_ASSERT(current_queue_ptr > current_buf_ptr);
        return tmp;
    }

    uint64_t tt_chan_alloc_struct::alloc_q(uint32_t channel_id, uint64_t size_bytes)
    {
        uint64_t tmp;
        TT_ASSERT((size_bytes & 0x3) == 0);
        tmp = current_queue_ptr - size_bytes;
        current_queue_ptr -= size_bytes;

        TT_ASSERT((current_queue_ptr & 0x3) == 0);
        TT_ASSERT(current_queue_ptr > current_buf_ptr);


        uint64_t channel_capacity = top_of_chan - current_buf_ptr;
        uint64_t used = top_of_chan - current_queue_ptr;
        log_debug(tt::LogModel, "Channel {}: {}/{}[{:.1f}%]", channel_id, utils::pretty_print_bytes(used), utils::pretty_print_bytes(channel_capacity), float(used)/float(channel_capacity)*100.0);

        return tmp;
    }

    uint64_t tt_chan_alloc_struct::alloc_bin(uint64_t size_bytes)
    {
        uint64_t tmp;
        TT_ASSERT((size_bytes & 0x3) == 0);
        tmp = current_bin_ptr;
        current_bin_ptr += size_bytes;
        TT_ASSERT((current_bin_ptr & 0x3) == 0);
        TT_ASSERT(current_bin_ptr <= top_of_binaries);
        return tmp;
    }

    uint64_t tt_chan_alloc_struct::alloc_kernel_cache_bin(uint64_t size_bytes)
    {
        uint64_t tmp;
        TT_ASSERT((size_bytes & 0x3) == 0);
        tmp = current_kernel_cache_ptr;
        current_kernel_cache_ptr += size_bytes;
        TT_ASSERT((current_kernel_cache_ptr & 0x3) == 0);
        TT_ASSERT(current_kernel_cache_ptr <= top_of_kernel_cache);
        return tmp;
    }

    uint64_t tt_chan_alloc_struct::alloc_q_update_blob(uint64_t size_bytes) {
        uint64_t tmp;
        TT_ASSERT((size_bytes & 0x3) == 0);
        tmp = current_q_update_blob_ptr;
        current_q_update_blob_ptr += size_bytes;
        TT_ASSERT((current_q_update_blob_ptr & 0x3) == 0);
        TT_ASSERT(current_q_update_blob_ptr <= top_of_q_update_blobs);
        return tmp;        
    }

    uint64_t tt_chan_alloc_struct::alloc_epoch0_start_table(uint64_t size_bytes)
    {
        uint64_t tmp;
        TT_ASSERT((size_bytes & 0x3) == 0);
        tmp = top_of_reserved;
        uint64_t tmp_end = top_of_reserved + size_bytes;
        TT_ASSERT((tmp_end & 0x3) == 0);
        TT_ASSERT(tmp_end <= top_of_epoch0_start_table);
        return tmp;
    }

    uint64_t tt_chan_alloc_struct::alloc_epoch_address_q(uint64_t size_bytes, tt_xy_pair core)
    {
        TT_ASSERT((size_bytes & 0x3) == 0);
        uint64_t tmp = top_of_reserved + (core.y*epoch_queue::GridSizeCol + core.x)*size_bytes;
        TT_ASSERT((tmp & 0x3) == 0);
        TT_ASSERT(tmp <= top_of_epoch0_start_table);
        return tmp;
    }

    uint64_t tt_chan_alloc_struct::current_buf_addr()
    {
        return current_buf_ptr;
    }

    uint64_t tt_chan_alloc_struct::current_q_addr()
    {
        return current_queue_ptr;
    }

    void tt_chan_alloc_struct::set_binary_address_to_q_slot_start(int epoch_q_slot) {
        current_bin_ptr = epoch_q_start + (uint64_t)epoch_q_slot*epoch_q_slot_size;
        // std::cout << __FUNCTION__ << std::hex << " set current_bin_ptr = " << epoch_q_start
        //     << " + " << epoch_q_slot << "*" << epoch_q_slot_size << " = " << current_bin_ptr << std::endl;
    }

    void tt_chan_alloc_struct::set_q_update_blob_address_to_q_slot_start(int epoch_q_slot, int worker_idx_within_channel) {
        uint64_t single_worker_size = (uint64_t)epoch_queue::get_epoch_io_queue_update_num_slots() * (uint64_t)epoch_queue::get_queue_update_blob_size_bytes();
        current_q_update_blob_ptr = top_of_binaries + (worker_idx_within_channel * single_worker_size) + ((uint64_t)epoch_q_slot * epoch_queue::get_queue_update_blob_size_bytes());
    }

    // Combined version of set-addr + allocate functions, return address.
    uint64_t tt_chan_alloc_struct::alloc_kernel_cache_bin_at_slot_idx(uint64_t size_bytes, int slot_idx) {
        current_kernel_cache_ptr = top_of_epoch0_start_table + ((uint64_t)slot_idx * size_bytes);
        return alloc_kernel_cache_bin(size_bytes);
    }

} // end namespace tt
