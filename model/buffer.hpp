// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"

#include <cstdint>
#include <vector>
#include <deque>

#include "common/base.hpp"
#include "tile.hpp"
#include "common/model/tensor_hierarchy_metadata.hpp"
#include "utils/thread_safe_queue.hpp"
#include "utils/simple_queue.hpp"

using std::array;
using std::deque;
using std::uint32_t;
using std::vector;

namespace tt
{

// A buffer is a flat vector of tile pointers
// it has some quirks:
// - It deallocate its tiles in destructor
// - it can be a double buffer, this is for use as an operation output buffer
//   it made sense to implement the double buffering within the same class
//   since it greatly cuts down on code and book-keeping
class tt_buffer : public tt_dram_resident
{
    public:
    tt_buffer_metadata metadata;
    bool is_initialized = false;
    bool untilized_output = false;

    tt::UniqueId unique_id;
    tt_core *creating_core = nullptr;
    int epoch_tiles; // tiles that go through buffer in an epoch
    vector<int> epoch_tiles_per_phase;
    bool double_buf;
    bool wr_phase;
    bool rd_phase;
    vector<int> all_consumer_cnt;
    vector<int> remain_consumer_cnt;
    bool infinite_lifetime;
    bool dram_buf_streaming;
    bool epilog_write_back;
    bool core_input_buf;


    deque <tt_tile *> t_ptr_vec;
    deque <tt_tile *> t_ptr_vec_dbl;
    virtual bool is_tile_buf();
    void flip_wr_phase();
    virtual void flip_rd_phase();
    void set_core_ptr(tt_core *ptr);
    virtual DataFormat get_data_format() const;
    void set_data_format(DataFormat dt);
    tt_buffer();
    tt_buffer(tt_buffer_metadata metadata, bool dbl);
    tt_buffer(tt_buffer_metadata metadata, tt_core* creating_core, bool dbl);
    // tt_buffer(DataFormat data_format, unsigned int id, int insize, bool dbl);
    // tt_buffer(DataFormat data_format, int rti, int cti, int zti, int wti, unsigned int corer, unsigned int corec, unsigned int id, tt_core * core_ptr, int insize, bool dbl);
    // tt_buffer(DataFormat data_format, int rti, int cti, int zti, int wti, unsigned int id, int insize, bool dbl);
    // Copy constructor
    tt_buffer(const tt_buffer &other) = delete;
    virtual ~tt_buffer();
    void propagate_dram_allocation();
    void pack_data();
    void unpack_data();
    void dump_buf();
    void dump_buf_packed();
    void clear_packed_data();
    void set_input_buf();
    bool is_input_buf();
    virtual int size_bytes() const;
    virtual int get_l1_allocated_size_in_tiles() const;
    virtual int get_buffer_num_tiles();
    void set_infinite();
    virtual bool is_infinite_life();
    bool is_dram_buf_streaming();
    bool is_epilog_written();
    void set_epilog_written_flag(bool flag);
    uint32_t get_id() const;
    tt_core_coord get_core_coord() const;
    virtual uint32_t get_rt() const;
    virtual uint32_t get_ct() const;
    virtual uint32_t get_zt() const;
    virtual uint32_t get_wt() const;
    virtual tt_buffer_shape get_buffer_shape() const;
    virtual BufferType get_type() const;
    void set_buffer_metadata(const tt_buffer_metadata& buffer_metadata);
    void set_id(uint32_t id);
    void set_core_coord(tt_core_coord core_coord);
    void set_buffer_shape(tt_buffer_shape buffer_shape);
    virtual void set_l1_allocated_size_in_tiles(int size);
    // Inserts tile at pos
    void add_tile(const tt_tile & tile, int pos);
    // Appends the tile
    void add_tile(const tt_tile & tile);
    int tiles_in_buf();
    // This actually deletes tiles
    void pop_tiles(int num_tiles);
    void fill_with_data(const tt_buffer& buffer);
    virtual tt_core *get_core_ptr();
    tt_tile &get_tile(int index) const;
    tt_tile *get_tile_ptr(int index);
    bool is_having_untilized_output();
    void set_untilized_output(bool flag);
    virtual void add_phase();
    virtual void incr_consumer_cnt(int phase);
    virtual void decr_rem_consumer_cnt(int phase);
    virtual int get_rem_consumer_cnt(int phase);
    virtual void reset_consumer_cnt(int phase);
    virtual void zero_consumer_cnt(int phase);
    virtual bool is_double_buffered();
    virtual bool is_valid(bool use_shadow_rd_ptr = false);
    virtual bool is_ready_to_write();
    virtual int occupancy();
    virtual void clear_buf();
    virtual void stream_append(tt_buffer *ttb);
    virtual void stream_copy(tt_buffer *ttb);
    // BELOW IS A DIRTY HACK
    // PAY GOOD ATTENTION OR ASK LJUBISA ABOUT IT
    virtual void push(tt_buffer * ptr);// helper for buffer queue function
    virtual bool is_totally_empty();
    virtual bool is_bufq();
    virtual int get_num_slots();
    virtual tt_buffer *get_buf_ptr(bool use_shadow_rd_ptr = false);
    bool operator==(const tt_buffer &rhs);
    bool operator!=(const tt_buffer &rhs);

    void print(std::ostream & os) const;
};   // tt_buffer

inline std::ostream & operator<<(std::ostream & os, const tt_buffer & buf)
{
    buf.print(os);
    return os;
}

inline std::ostream & operator<<(std::ostream & os, tt_buffer * buf)
{
    buf->print(os);
    return os;
}

// Queue of buffers
// but also mascarades as a buffer
// so that it can connect to places that buffers connect to
// seemlessly
// Buffer queue owns its buffers and deletes them before popping the pointer
class tt_buffer_queue : public tt_buffer
{
    SimpleQueue<tt_buffer*> bufq;
    int slots;
    tt_buffer_shape buffer_shape;
    tt_bufq_ptr dram_ptrs;
    bool is_consumed = false;
    bool allocated_to_dram;
    //allocated_to_host is true if this queue lives in a DMABuffer
    bool assigned_to_dram_channel;

    public:
    tt_core *associated_core;
    tt_dram_io* parent_dram_io; // useful for debug, to know which dram this queue belongs to
    tt_buffer_queue();

    // NOTE: we should be able to make a tt_buffer_queue w/o associating it with a core (mapping is not 1-to-1)
    tt_buffer_queue(int islots, tt_buffer_shape buffer_shape, DataFormat data_formati);
    tt_buffer_queue(int islots, tt_buffer_shape buffer_shape, DataFormat data_formati, tt_core *icore_ptr);
    tt_buffer_queue(const tt_buffer_queue &buffer_queue);
    virtual int size_bytes() const override;
    virtual int get_l1_allocated_size_in_tiles() const override;
    virtual int get_buffer_num_tiles() override;
    uint32_t get_rt() const;
    uint32_t get_ct() const;
    uint32_t get_zt() const;
    uint32_t get_wt() const;
    bool is_tile_buf();
    virtual tt_buffer_shape get_buffer_shape() const override;
    int occupancy();
    void push(tt_buffer * buf_ptr);
    void bufq_pop();
    void set_num_slots(int islots);
    int get_num_slots();
    void assign_to_dram_channel(uint32_t dram_channel, uint32_t subchannel);
    bool is_assigned_to_dram_channel() const;
    bool is_allocated_to_dram();
    void set_allocated_to_dram(bool flag);
    void set_host_resident(bool flag);

    /////////////////
    // This set of methods are re-implementations
    // of buffer functions in bufq
    // generally they just redirect the call to the tt_buffer
    // that is at the top of the queue
    bool is_double_buffered();
    bool is_valid(bool use_shadow_rd_ptr = false);
    bool bufq_totally_empty();
    bool is_totally_empty();
    bool is_bufq();
    bool is_ready_to_write();
    void clear_buf();
    void clear_and_pop_slot(int num_pops);
    void clear_consumed_flag();
    void stream_append(tt_buffer *ttb);
    void stream_copy(tt_buffer *ttb);
    tt_buffer *get_buf_ptr(bool use_shadow_rd_ptr = false);
    tt_buffer *get_buf_ptr(int off);
    tt_buffer *get_first_inserted_buf_ptr();
    tt_buffer *get_last_inserted_buf_ptr();
    tt_core *get_core_ptr();
    void set_rd_ptr(int ptr, bool use_shadow_rd_ptr = false);
    void set_q_offset(int ptr);
    int get_q_offset();
    int get_rd_ptr(bool use_shadow_rd_ptr = false);
    void increment_offset();
    void reset_offset();
    int get_offset();
    bool is_param_bufq();
    ~tt_buffer_queue();
};

bool is_tt_buffer_queue(tt_buffer* buf);


class tt_tile_buffer : public tt_buffer
{
    public:
    bool type;
    tt_buffer *parent_buf;
    int tile_index; // max
    tt_buffer *tmp_buf;

    virtual bool is_tile_buf() { return true; }
    tt_buffer *get_parent_ptr()
    {
        return parent_buf;
    }
    tt_tile_buffer(tt_buffer *parent, int index) : parent_buf(parent), tile_index(index)
    {
        tmp_buf = new tt_buffer;
        tmp_buf->set_l1_allocated_size_in_tiles(1);
        tmp_buf->double_buf = false;
        tmp_buf->set_data_format(parent_buf->get_data_format());
        this->set_data_format(parent_buf->get_data_format());
        this->creating_core = parent->creating_core;
    }

    ~tt_tile_buffer()
    {
        delete tmp_buf;
    }

    tt_buffer *get_buf_ptr(bool use_shadow_rd_ptr)
    {
        
        // If local one-tile buffer is empty
        // get the appropriate tile from the parent buffer
        // make a deep copy of it
        // put it in the local one-tile buffer
        tt_buffer *read_buf;
        if(parent_buf->is_bufq())
        {
            read_buf = parent_buf->get_buf_ptr(use_shadow_rd_ptr);
        }
        else
        {
            read_buf = parent_buf;
        }
        if(tmp_buf->occupancy() == 0)
        {
            tt_tile tmp_tile(read_buf->get_tile(tile_index));
            tmp_buf->add_tile(tmp_tile);
        }
        // otherwise, refresh local data
        // in the local one-tile buffer
        else
        {
            log_assert(tmp_buf->occupancy() == 1, "Expected one element in buf");
            tmp_buf->pop_tiles(1);
            tt_tile tmp_tile(read_buf->get_tile(tile_index)); 
            //tmp_tile.data_format = parent_buf->get_data_format();
            tmp_buf->add_tile(tmp_tile);
        }

        return tmp_buf;
    }

    void clear_buf()
    {
        if(tmp_buf->occupancy() == 0)
        {
            // do nothing
        }
        else
        {
            log_assert(tmp_buf->occupancy() == 1, "Expected one element in buf");
            tmp_buf->pop_tiles(1);
        }
        parent_buf->clear_buf();
    }

    // We expect flip_rd phase to be called once a buffers total reference count for a phase falls to zero
    // whichever is the last tile to be accessed will get the call, and should pass it on to its parent
    void flip_rd_phase()                          {parent_buf->flip_rd_phase();                        };

    // There is one tile in these buffers
    virtual int get_buffer_num_tiles() override   {return 1;}
    virtual int get_l1_allocated_size_in_tiles() const override          {log_fatal("Should not be called"); return(-1);                       } // disallow this for now, need to sort bufq behavior later

    // Change buffer type to a new one (TileBuffer)
    // hopefully this will throw any inappropriate use
    BufferType get_type() const  {return(BufferType::TileBuffer);}

    // For occupancy vs buffer volume check in tt_pipe::tx()
    int occupancy() {return parent_buf->occupancy();}
    std::uint32_t get_rt() const {return parent_buf->get_rt();}
    std::uint32_t get_ct() const {return parent_buf->get_ct();}
    std::uint32_t get_zt() const {return parent_buf->get_zt();}
    std::uint32_t get_wt() const {return parent_buf->get_wt();}

    void incr_consumer_cnt(int phase)             {parent_buf->incr_consumer_cnt(phase);               }
    void decr_rem_consumer_cnt(int phase)         {parent_buf->decr_rem_consumer_cnt(phase);           }
    int get_rem_consumer_cnt(int phase)           {return(parent_buf->get_rem_consumer_cnt(phase));    } // should never be called
    void reset_consumer_cnt(int phase)            {parent_buf->reset_consumer_cnt(phase);              }
    void zero_consumer_cnt(int phase)             {parent_buf->zero_consumer_cnt(phase);               }
    bool is_double_buffered()                     {return false;                                       }
    bool is_valid(bool use_shadow_rd_ptr = false) {return parent_buf->is_valid(use_shadow_rd_ptr);     }
    bool is_totally_empty()                       {
        return parent_buf->is_totally_empty();
                      }
    virtual DataFormat get_data_format() const override                 {return parent_buf->get_data_format();               }
    virtual tt_buffer_shape get_buffer_shape() const override            {return parent_buf->get_buffer_shape();              }
    bool is_infinite_life()                       {return parent_buf->is_infinite_life();              }

    virtual int size_bytes() const override       {log_fatal("Should not be called"); return(-1);                       }
    bool is_bufq()                                {return parent_buf->is_bufq();                       }
    int get_num_slots()                           {log_fatal("Should not be called"); return(-1);                       }
    bool is_ready_to_write()                      {log_fatal("Should not be called"); return(false);                    } // should never be called
    void stream_append(tt_buffer *ttb)            {log_fatal("Should not be called");                                   } // should never be called
    void stream_copy(tt_buffer *ttb)              {log_fatal("Should not be called");                                   } // should never be called

    // - member of parent buffer: first_input_buffer->creating_core -> get from parent buffer and set in derived, or just pass by public inheritance

};  // tt_tile_buffer


class tt_sub_buffer : public tt_buffer
{
    public:
    bool type;
    tt_buffer *parent_buf;
    int tile_index; // max
    int size;
    tt_buffer *tmp_buf;

    virtual bool is_tile_buf() { return true; }
    tt_buffer *get_parent_ptr()
    {
        return parent_buf;
    }
    tt_sub_buffer(tt_buffer *parent, int index, int size) : parent_buf(parent), tile_index(index), size(size)
    {
        // check that sub-buffers evenly divide the buffer
        log_assert(parent->get_l1_allocated_size_in_tiles() % size == 0, "Sub bufs do not evenly divide bufs");
        log_assert(parent->get_buffer_num_tiles() % size == 0, "Sub bufs do not evenly divide bufs");

        tmp_buf = new tt_buffer;
        tmp_buf->set_l1_allocated_size_in_tiles(size);
        tmp_buf->double_buf = false;
        tmp_buf->set_data_format(parent_buf->get_data_format());
        this->set_data_format(parent_buf->get_data_format());
        this->creating_core = parent->creating_core;
    }

    ~tt_sub_buffer()
    {
        delete tmp_buf;
    }

    tt_buffer *get_buf_ptr(bool use_shadow_rd_ptr)
    {
        
        // If local one-tile buffer is empty
        // get the appropriate tile from the parent buffer
        // make a deep copy of it
        // put it in the local one-tile buffer
        tt_buffer *read_buf;
        if(parent_buf->is_bufq())
        {
            read_buf = parent_buf->get_buf_ptr(use_shadow_rd_ptr);
        }
        else
        {
            read_buf = parent_buf;
        }
        if(tmp_buf->occupancy() == 0)
        {
            for(int i=0;i<size;++i)
            {
                tt_tile tmp_tile(read_buf->get_tile(tile_index*size + i));
                tmp_buf->add_tile(tmp_tile);
            }
        }
        // otherwise, refresh local data
        // in the local one-tile buffer
        else
        {
            log_assert(tmp_buf->occupancy() == size, "unexpected occupancy");
            for(int i=0;i<size;++i) tmp_buf->pop_tiles(1);

            for(int i=0;i<size;++i)
            {
                tt_tile tmp_tile(read_buf->get_tile(tile_index*size + i)); 
                //tmp_tile.data_format = parent_buf->get_data_format();
                tmp_buf->add_tile(tmp_tile);
            }
        }

        return tmp_buf;
    }

    void clear_buf()
    {
        if(tmp_buf->occupancy() == 0)
        {
            // do nothing
        }
        else
        {
            log_assert(tmp_buf->occupancy() == size, "unexpected occupancy");
            for(int i=0;i<size;++i) tmp_buf->pop_tiles(1);
        }
        parent_buf->clear_buf();
    }

    // We expect flip_rd phase to be called once a buffers total reference count for a phase falls to zero
    // whichever is the last tile to be accessed will get the call, and should pass it on to its parent
    void flip_rd_phase()                          {parent_buf->flip_rd_phase();                        };

    // There is one tile in these buffers
    virtual int get_buffer_num_tiles() override  {return size;}

    // Change buffer type to a new one (TileBuffer)
    // hopefully this will throw any inappropriate use
    BufferType get_type() const  {return(BufferType::TileBuffer);}

    // For occupancy vs buffer volume check in tt_pipe::tx()
    int occupancy() {return parent_buf->occupancy();}
    std::uint32_t get_rt() const {log_fatal("Should not be called"); return 0;}
    std::uint32_t get_ct() const {log_fatal("Should not be called"); return 0;}
    std::uint32_t get_zt() const {log_fatal("Should not be called"); return 0;}
    std::uint32_t get_wt() const {log_fatal("Should not be called"); return 0;}

    void incr_consumer_cnt(int phase)             {parent_buf->incr_consumer_cnt(phase);               }
    void decr_rem_consumer_cnt(int phase)         {parent_buf->decr_rem_consumer_cnt(phase);           }
    int get_rem_consumer_cnt(int phase)           {return(parent_buf->get_rem_consumer_cnt(phase));    } // should never be called
    void reset_consumer_cnt(int phase)            {parent_buf->reset_consumer_cnt(phase);              }
    void zero_consumer_cnt(int phase)             {parent_buf->zero_consumer_cnt(phase);               }
    bool is_double_buffered()                     {return false;                                       }
    bool is_valid(bool use_shadow_rd_ptr = false) {return parent_buf->is_valid(use_shadow_rd_ptr);     }
    bool is_totally_empty()                       {
        return parent_buf->is_totally_empty();
                      }
    virtual DataFormat get_data_format() const override                  {return parent_buf->get_data_format();               }
    virtual tt_buffer_shape get_buffer_shape() const override            {return parent_buf->get_buffer_shape();              }
    bool is_infinite_life()                       {return parent_buf->is_infinite_life();              }

    virtual int size_bytes() const override       {log_fatal("Should not be called"); return(-1);                       }
    bool is_bufq()                                {return parent_buf->is_bufq();                       }
    int get_num_slots()                           {log_fatal("Should not be called"); return(-1);                       }
    bool is_ready_to_write()                      {log_fatal("Should not be called"); return(false);                    } // should never be called
    void stream_append(tt_buffer *ttb)            {log_fatal("Should not be called");                                   } // should never be called
    void stream_copy(tt_buffer *ttb)              {log_fatal("Should not be called");                                   } // should never be called

    // - member of parent buffer: first_input_buffer->creating_core -> get from parent buffer and set in derived, or just pass by public inheritance

};  // tt_sub_buffer

} // end namespace tt

#pragma clang diagnostic pop
