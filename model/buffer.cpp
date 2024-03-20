// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "buffer.hpp"
#include "dram.hpp"
#include "size_lib.hpp"

using std::cout;
using std::endl;
using std::uint64_t;

namespace tt
{

// A buffer is a flat vector of tile pointers
// it has some quirks:
// - It deallocate its tiles in destructor
// - it can be a double buffer, this is for use as an operation output buffer
//   it made sense to implement the double buffering within the same class
//   since it greatly cuts down on code and book-keeping
    void tt_buffer::flip_wr_phase()
    {
        if(double_buf) wr_phase = !wr_phase;
        else wr_phase = false;
    }
    void tt_buffer::flip_rd_phase()
    {
        if(double_buf) rd_phase = !rd_phase;
        else rd_phase = false;
    }
    void tt_buffer::set_core_ptr(tt_core *ptr)
    {
        creating_core = ptr;
    }
    DataFormat tt_buffer::get_data_format() const
    {
        return metadata.data_format;
    }

    void tt_buffer::set_data_format(DataFormat dt)
    {
        metadata.data_format = dt;
    }

    tt_buffer::tt_buffer() :
        is_initialized(false),
        untilized_output(false),
        metadata(tt_buffer_metadata{}),
        creating_core(nullptr),
        double_buf(false),
        wr_phase(false),
        rd_phase(false),
        infinite_lifetime(0),
        dram_buf_streaming(0),
        epoch_tiles(0),
        core_input_buf(false),
        epilog_write_back(false)
    {
    }

    tt_buffer::tt_buffer(tt_buffer_metadata metadata, bool dbl) :
        is_initialized(true),
        untilized_output(false),
        metadata(metadata),
        creating_core(nullptr),
        double_buf(dbl),
        wr_phase(false),
        rd_phase(false),
        infinite_lifetime(0),
        dram_buf_streaming(0),
        epoch_tiles(0),
        core_input_buf(false),
        epilog_write_back(false)
    {
    }

    tt_buffer::tt_buffer(tt_buffer_metadata metadata, tt_core* creating_core, bool dbl):
        is_initialized(true),
        untilized_output(false),
        metadata(metadata),
        creating_core(creating_core),
        double_buf(dbl),
        wr_phase(false),
        rd_phase(false),
        infinite_lifetime(0),
        dram_buf_streaming(0),
        epoch_tiles(0),
        core_input_buf(false),
        epilog_write_back(false)
    {
    }

    tt_buffer::~tt_buffer()
    {
        // Buffer sometimes owns tiles within it and needs to delete them when it dies
        deque<tt_tile *>::iterator it;

        for(it=t_ptr_vec.begin();it!=t_ptr_vec.end();++it)
        {
            delete (*it);
        }
        for(it=t_ptr_vec_dbl.begin();it!=t_ptr_vec_dbl.end();++it)
        {
            delete (*it);
        }
    }

    void tt_buffer::propagate_dram_allocation()
    {
        TT_ASSERT(double_buf == false);

        uint64_t tile_size = size::get_tile_size_in_bytes(this->get_data_format(), true);
        uint64_t base = d_addr;

        deque<tt_tile *>::iterator it;

        for(it=t_ptr_vec.begin();it!=t_ptr_vec.end();++it)
        {
            (*it)->set_dram_addr(base);
            (*it)->set_dram_chan(d_chan);
            (*it)->set_dram_subchannel(d_subchannel);
            (*it)->set_chip_id(d_chip_id);
            base += tile_size;
        }
    }

    void tt_buffer::pack_data()
    {
        // Iterate through all the tiles in the buffer and perform packing
        deque<tt_tile *>::iterator it;
        for(it=t_ptr_vec.begin();it!=t_ptr_vec.end();++it)
        {
            (*it)->pack_data();
        }
    }

    void tt_buffer::unpack_data()
    {
        // Iterate through all the tiles in the buffer and perform unpack from packed data into float numbers
        deque<tt_tile *>::iterator it;
        for(it=t_ptr_vec.begin();it!=t_ptr_vec.end();++it)
        {
            (*it)->packed_data_to_tile();
        }
    }

    void tt_buffer::dump_buf()
    {
        deque<tt_tile*>::iterator it_a;
        int tile_id = 0;
        for (it_a=t_ptr_vec.begin(); it_a!=t_ptr_vec.end(); ++it_a) {
            cout << "Tile " << tile_id << endl;
            (*it_a)->dump();
        }
    }

    void tt_buffer::dump_buf_packed()
    {
        deque<tt_tile*>::iterator it_a;
        int tile_id = 0;
        for (it_a=t_ptr_vec.begin(); it_a!=t_ptr_vec.end(); ++it_a) {
            cout << "Tile " << tile_id << endl;
            (*it_a)->dump_packed();
        }
    }

    void tt_buffer::clear_packed_data()
    {
        // Iterate through all the tiles in the buffer and perform unpack from packed data into float numbers
        deque<tt_tile *>::iterator it;
        for(it=t_ptr_vec.begin();it!=t_ptr_vec.end();++it)
        {
            (*it)->clear_packed_data();
        }
    }

    void tt_buffer::set_input_buf()
    {
        core_input_buf = true;
    }
    bool tt_buffer::is_input_buf()
    {
        return core_input_buf;
    }

    int tt_buffer::size_bytes() const
    {
        int dbl_mul = double_buf ?  2 : 1;
        int lsize = dbl_mul * this->get_l1_allocated_size_in_tiles() * size::get_tile_size_in_bytes(this->get_data_format(), true);
        TT_ASSERT(lsize % 16 == 0);
        TT_ASSERT(lsize % 32 == 0);
        return(lsize);
    }

    int tt_buffer::get_l1_allocated_size_in_tiles() const
    {
        return metadata.L1_allocated_size_num_tiles;
    }

    int tt_buffer::get_buffer_num_tiles()
    {
        return metadata.buffer_shape.volume();
    }

    void tt_buffer::set_infinite()
    {
        infinite_lifetime = true;
    }

    bool tt_buffer::is_infinite_life()
    {
        return infinite_lifetime;
    }

    bool tt_buffer::is_dram_buf_streaming()
    {
        return dram_buf_streaming;
    }

    bool tt_buffer::is_epilog_written()
    {
        return epilog_write_back;
    }

    void tt_buffer::set_epilog_written_flag(bool flag)
    {
        epilog_write_back = flag;
    }

    uint32_t tt_buffer::get_id() const { return this->metadata.id; }

    tt_core_coord tt_buffer::get_core_coord() const { return this->metadata.core_coord; }

    uint32_t tt_buffer::get_rt() const { return this->metadata.buffer_shape.block_shape.r; }
    uint32_t tt_buffer::get_ct() const { return this->metadata.buffer_shape.block_shape.c; }
    uint32_t tt_buffer::get_zt() const { return this->metadata.buffer_shape.z * this->metadata.buffer_shape.num_blocks_r * this->metadata.buffer_shape.num_blocks_c; }
    uint32_t tt_buffer::get_wt() const { return this->metadata.buffer_shape.w; }

    tt_buffer_shape tt_buffer::get_buffer_shape() const
    {
        return this->metadata.buffer_shape;
    }

    BufferType tt_buffer::get_type() const
    {
        return this->metadata.type;
    }

    void tt_buffer::set_id(uint32_t id)
    {
        this->metadata.id = id;
    }

    void tt_buffer::set_core_coord(tt_core_coord core_coord)
    {
        this->metadata.core_coord = core_coord;
    }

    void tt_buffer::set_buffer_shape(tt_buffer_shape buffer_shape)
    {
        this->metadata.buffer_shape = buffer_shape;
    }

    void tt_buffer::set_l1_allocated_size_in_tiles(int size)
    {
        this->metadata.L1_allocated_size_num_tiles = size;
    }

    void tt_buffer::set_buffer_metadata(const tt_buffer_metadata& buffer_metadata) {
        metadata = buffer_metadata;
    }

    // Inserts tile at pos
    void tt_buffer::add_tile(const tt_tile & tile, int pos)
    {
        if(pos < 0)
        {
            bool dbl = (!double_buf) ? false : wr_phase;

            TT_ASSERT((this->get_l1_allocated_size_in_tiles() > 0) ,  "Cannot add tiles to a zero-sized buffer!");
            if(dbl)
            {
                t_ptr_vec_dbl.push_back(new tt_tile(tile, this->get_data_format()));
            }
            else
            {
                t_ptr_vec.push_back(new tt_tile(tile, this->get_data_format()));
            }
            TT_ASSERT((t_ptr_vec_dbl.size() == 0 or t_ptr_vec.size() == 0) ,  "Cannot use both t_ptr_vec_dbl and t_ptr_vec");
        }
        else
        {
            bool dbl = (!double_buf) ? false : wr_phase;

            deque <tt_tile *> & tile_deque = dbl ? t_ptr_vec_dbl : t_ptr_vec;

            if ((uint32_t) pos >= tile_deque.size())
            {
                tile_deque.resize(pos+1);
            }
            tile_deque[pos] = new tt_tile(tile, this->get_data_format());
        }
    }

    // Appends the tile
    void tt_buffer::add_tile(const tt_tile & tile)
    {
        add_tile(tile, -1);
    }

    int tt_buffer::tiles_in_buf()
    {
        // popping tiles out of double buffers is
        // currently not allowed
        TT_ASSERT(double_buf == false);
        return t_ptr_vec.size();
    }

    // This actually deletes tiles
    void tt_buffer::pop_tiles(int num_tiles)
    {
        // popping tiles out of double buffers is
        // currently not allowed
        TT_ASSERT(double_buf == false);

        TT_ASSERT(t_ptr_vec.size() >= (uint32_t) num_tiles);
        for(int i=0;i<num_tiles;++i)
        {
            delete t_ptr_vec[0];
            t_ptr_vec.pop_front();
        }
    }


    tt_core *tt_buffer::get_core_ptr()
    {
        return(creating_core);
    }

    tt_tile &tt_buffer::get_tile(int index) const
    {
        bool dbl = (!double_buf) ? false : rd_phase;
        if(dbl)
        {
            TT_ASSERT(t_ptr_vec_dbl.size() > (uint32_t) index);
            return(*t_ptr_vec_dbl[index]);
        }
        else
        {
            TT_ASSERT(t_ptr_vec.size() > (uint32_t) index);
            return(*t_ptr_vec[index]);
        }
    }

    tt_tile *tt_buffer::get_tile_ptr(int index)
    {
        // This currently only supports
        // single (non double) buffers
        TT_ASSERT(double_buf == false);

        TT_ASSERT(t_ptr_vec.size() > (uint32_t) index);
        return t_ptr_vec[index];
    }

    void tt_buffer::add_phase()
    {
        all_consumer_cnt.push_back(0);
        remain_consumer_cnt.push_back(0);
        epoch_tiles_per_phase.push_back(0);
    }
    void tt_buffer::incr_consumer_cnt(int phase)
    {
        // add phases as needed
        while(all_consumer_cnt.size() <= (uint32_t) phase)
        {
            add_phase();
        }
        TT_ASSERT(all_consumer_cnt.size() == (uint32_t) (phase + 1));
        TT_ASSERT(remain_consumer_cnt.size() == (uint32_t) (phase +1));
        TT_ASSERT(epoch_tiles_per_phase.size() == (uint32_t) (phase +1));
        all_consumer_cnt[phase]++;
        remain_consumer_cnt[phase]++;
    }
    void tt_buffer::decr_rem_consumer_cnt(int phase)
    {
        TT_ASSERT(remain_consumer_cnt.size() > (uint32_t) phase);
        remain_consumer_cnt[phase]--;
    }
    int tt_buffer::get_rem_consumer_cnt(int phase)
    {
        TT_ASSERT(remain_consumer_cnt.size() > (uint32_t) phase);
        return remain_consumer_cnt[phase];
    }
    void tt_buffer::reset_consumer_cnt(int phase)
    {
        TT_ASSERT(all_consumer_cnt.size() > (uint32_t) phase);
        TT_ASSERT(remain_consumer_cnt.size() > (uint32_t) phase);
        remain_consumer_cnt[phase] = all_consumer_cnt[phase];
    }
    void tt_buffer::zero_consumer_cnt(int phase)
    {
        // add phases as needed
        while(all_consumer_cnt.size() <= (uint32_t) phase)
        {
            add_phase();
        }
        TT_ASSERT(all_consumer_cnt.size() > (uint32_t) phase);
        TT_ASSERT(remain_consumer_cnt.size() > (uint32_t) phase);
        remain_consumer_cnt[phase] = 0;
        all_consumer_cnt[phase] = 0;
    }
    bool tt_buffer::is_double_buffered() { return double_buf; }

    bool tt_buffer::is_valid(bool use_shadow_rd_ptr)
    {
        bool vld=false;
        if(double_buf)
        {
            if(rd_phase)
            {
                if(t_ptr_vec_dbl.size() > 0) vld = true;
            }
            else
            {
                if(t_ptr_vec.size() > 0) vld = true;
            }
        }
        else
        {
            if(t_ptr_vec.size() > 0) vld = true;
        }
        return vld;
    }

    bool tt_buffer::is_ready_to_write()
    {
        bool rdy=false;
        if(double_buf)
        {
            if(wr_phase)
            {
                if(t_ptr_vec_dbl.size() == 0) rdy = true;
            }
            else
            {
                if(t_ptr_vec.size() == 0) rdy = true;
            }
        }
        else
        {
            if(t_ptr_vec.size() == 0) rdy = true;
        }
        return rdy;
    }

    int tt_buffer::occupancy()
    {
        int occupancy;
        if((!double_buf) || (!rd_phase)) // clear buffer 0 ('main' buffer)
        {
            occupancy = t_ptr_vec.size();
        }
        else // clear buffer 1 ('double' buffer)
        {
            occupancy = t_ptr_vec_dbl.size();
        }
        return occupancy;
    }
    void tt_buffer::clear_buf()
    {
        if((!double_buf) || (!rd_phase)) // clear buffer 0 ('main' buffer)
        {
            int tile_pointers = t_ptr_vec.size();

            for(int i=0;i<tile_pointers;++i)
            {
                delete t_ptr_vec[0];
                t_ptr_vec.pop_front();
            }
        }
        else // clear buffer 1 ('double' buffer)
        {
            int tile_pointers = t_ptr_vec_dbl.size();

            for(int i=0;i<tile_pointers;++i)
            {
                delete t_ptr_vec_dbl[0];
                t_ptr_vec_dbl.pop_front();
            }
        }
    }

    void tt_buffer::stream_append(tt_buffer *ttb)
    {

        // TT_ASSERT no double buffering in stream copy target;
        TT_ASSERT(double_buf == false);

        // step through input buffer and append it to this one
        std::deque<tt_tile *>::iterator it;

        // The phase pointer will be pointing to the next (free) buffer half
        // so it should be flipped to point to the previously written half
        bool dbl = (!ttb->double_buf) ? false : ttb->rd_phase;
        TT_ASSERT((this->get_l1_allocated_size_in_tiles() > 0) ,  "Cannot push to a zero-sized buffer!");
        if(dbl)
        {
            for(it = ttb->t_ptr_vec_dbl.begin(); it != ttb->t_ptr_vec_dbl.end(); ++it)
            {
                TT_ASSERT((*it) != NULL ,  "Trying to copy a NULL tile");
                t_ptr_vec.push_back(new tt_tile(*(*it)));
            }
        }
        else
        {
            for(it = ttb->t_ptr_vec.begin(); it != ttb->t_ptr_vec.end(); ++it)
            {
                TT_ASSERT((*it) != NULL ,  "Trying to copy a NULL tile");
                t_ptr_vec.push_back(new tt_tile(*(*it)));
            }
        }
    }

    void tt_buffer::stream_copy(tt_buffer *ttb)
    {
        // check that buffer is empty
        // then append incoming stream
        TT_ASSERT(t_ptr_vec.size() == 0);
        stream_append(ttb);
    }

    // BELOW IS A DIRTY HACK
    // PAY GOOD ATTENTION OR ASK LJUBISA ABOUT IT
    void tt_buffer::push(tt_buffer * ptr) // helper for buffer queue function
    {
        // in a regular buffer, just delete the pointer
        delete ptr;
    }

    bool tt_buffer::is_totally_empty()
    {
        if(t_ptr_vec.empty() && t_ptr_vec_dbl.empty()) return true;
        else return false;
    }
    bool tt_buffer::is_bufq()
    {
        return false;
    }
    bool tt_buffer::is_having_untilized_output()
    {
        return untilized_output;
    }
    void tt_buffer::set_untilized_output(bool flag)
    {
        untilized_output = flag;
    }
    int tt_buffer::get_num_slots()
    {
        return -1;
    }
    tt_buffer *tt_buffer::get_buf_ptr(bool use_shadow_rd_ptr)
    {
        return(this);
    }

    bool tt_buffer::operator==(const tt_buffer &rhs) {
        bool match = true;
        TT_ASSERT(double_buf == rhs.double_buf);
        std::deque <tt_tile*>::iterator ita;
        std::deque <tt_tile*>::const_iterator itb;
        if (double_buf) {
            TT_ASSERT(t_ptr_vec_dbl.size() == rhs.t_ptr_vec_dbl.size());
            ita = t_ptr_vec_dbl.begin();
            itb = rhs.t_ptr_vec_dbl.begin();
        } else {
            TT_ASSERT(t_ptr_vec.size() == rhs.t_ptr_vec.size());
            ita = t_ptr_vec.begin();
            itb = rhs.t_ptr_vec.begin();
        }

        // Run data check for each tile
        while (ita != t_ptr_vec.end()) {
            if (*(*ita) != *(*itb)) {
                match = false;
            }
            ++ita; ++itb;
        }
        return match;
    };

    bool tt_buffer::operator!=(const tt_buffer &rhs) {
        return !operator==(rhs);
    }

    void tt_buffer::print(std::ostream & os) const
    {
        os << "tt_buffer{";
        os << ".metadata=" << metadata;
        os << " .is_initialized=" << is_initialized;
        os << " .untilized_output=" << untilized_output;
        os << " .unique_id=" << unique_id;
        os << " .epoch_tiles=" << epoch_tiles;
        os << " .double_buf=" << double_buf;
        os << " .wr_phase=" << wr_phase;
        os << " .rd_phase=" << rd_phase;
        os << " .infinite_lifetime=" << infinite_lifetime;
        os << " .dram_buf_streaming=" << dram_buf_streaming;
        os << " .epilog_write_back=" << epilog_write_back;
        os << "}";
    }

    deque<tt_tile*> tt_tile::deepcopy_tile_queue_on_heap(const deque<tt_tile*>& tile_queue)
    {
        deque<tt_tile*> result;
        for (tt_tile* tile: tile_queue)
        {
            result.push_back(new tt_tile(*tile));
        }
        return result;
    }

    void tt_buffer::fill_with_data(const tt_buffer& buffer) {
        TT_ASSERT(double_buf == buffer.double_buf);
        if (double_buf) {
            TT_ASSERT(t_ptr_vec_dbl.size() == 0 ,  "Trying to fill a buffer that already has data in it's t_ptr_vec_dbl");
            t_ptr_vec_dbl = tt_tile::deepcopy_tile_queue_on_heap(buffer.t_ptr_vec_dbl);
        } else {
            TT_ASSERT(t_ptr_vec.size() == 0 ,  "Trying to fill a buffer that already has data in it's t_ptr_vec");
            t_ptr_vec = tt_tile::deepcopy_tile_queue_on_heap(buffer.t_ptr_vec);

        }
    }

    bool tt_buffer::is_tile_buf()
    {
        return false;
    }

    tt_buffer_queue::tt_buffer_queue() : dram_ptrs(0xabadface)
    {
        slots = 0xabadface;
        buffer_shape = tt_buffer_shape {
            .w               = 0xabadface,
            .z               = 0xabadface,
            .num_blocks_r    = 0xabadface,
            .num_blocks_c    = 0xabadface,
        };
        bufq.clear();
        is_consumed = false;
        allocated_to_dram = false;
        is_initialized = false;
        untilized_output = false;
        this->assigned_to_dram_channel = false;
    }

    // NOTE: we should be able to make a tt_buffer_queue w/o associating it with a core (mapping is not 1-to-1)
    tt_buffer_queue::tt_buffer_queue(int islots, tt_buffer_shape buffer_shape, DataFormat data_formati) : dram_ptrs(islots)
    {
        this->associated_core = nullptr;
        this->slots = islots;
        this->buffer_shape = buffer_shape;
        this->metadata.data_format = data_formati;
        this->bufq.clear();
        this->is_consumed = false;
        this->allocated_to_dram = false;
        this->is_initialized = true;
        this->untilized_output = false;
        this->assigned_to_dram_channel = false;
    }

    tt_buffer_queue::tt_buffer_queue(int islots, tt_buffer_shape buffer_shape, DataFormat data_formati, tt_core *icore_ptr) : dram_ptrs(islots)
    {
        this->associated_core = icore_ptr;
        this->slots = islots;
        this->buffer_shape = buffer_shape;
        this->metadata.data_format = data_formati;
        this->bufq.clear();
        this->is_consumed = false;
        this->allocated_to_dram = false;
        this->is_initialized = true;
        this->untilized_output = false;
        this->assigned_to_dram_channel = false;
    }

    tt_buffer_queue::tt_buffer_queue(const tt_buffer_queue &buffer_queue) 
        : dram_ptrs(buffer_queue.slots),
          tt_buffer()
    {
        this->associated_core = buffer_queue.associated_core;
        this->slots = buffer_queue.slots;
        this->buffer_shape = buffer_queue.buffer_shape;
        this->metadata.data_format = buffer_queue.get_data_format();
        this->d_addr = buffer_queue.d_addr;
        this->d_chan = buffer_queue.d_chan;
        this->d_record_vld = buffer_queue.d_record_vld;
        bufq.clear();
        this->is_consumed = false;
        this->allocated_to_dram = buffer_queue.allocated_to_dram;
        this->is_initialized = buffer_queue.is_initialized;
        this->untilized_output = buffer_queue.untilized_output;
        this->assigned_to_dram_channel = buffer_queue.assigned_to_dram_channel;
    }

    int tt_buffer_queue::size_bytes() const
    {
        TT_ASSERT(is_initialized);
        TT_ASSERT(buffer_shape.w != 0xabadface);
        TT_ASSERT(buffer_shape.z != 0xabadface);
        TT_ASSERT(buffer_shape.num_blocks_r != 0xabadface);
        TT_ASSERT(buffer_shape.num_blocks_c != 0xabadface);
        TT_ASSERT((uint32_t) slots != 0xabadface);

        int size;
        if (untilized_output) {
            // Untilized output will have 3D tensor in one slot, i.e. [minibatch, r, c]
            bool include_tile_header = false;
            size = slots * epoch_tiles * size::get_tile_size_in_bytes(this->get_data_format(), include_tile_header) + 32;
        } else {
            int tiles = buffer_shape.volume();
            // Pad size by 32 bytes for pointers and other metadata
            size = slots * tiles * size::get_tile_size_in_bytes(this->get_data_format(), true) + 32;
        }
        TT_ASSERT(size % 16 == 0);
        TT_ASSERT(size % 32 == 0);

        return(size);
    }

    int tt_buffer_queue::get_l1_allocated_size_in_tiles() const
    {
        TT_ASSERT(is_initialized);
        TT_ASSERT(buffer_shape.w != 0xabadface);
        TT_ASSERT(buffer_shape.z != 0xabadface);
        TT_ASSERT(buffer_shape.num_blocks_r != 0xabadface);
        TT_ASSERT(buffer_shape.num_blocks_c != 0xabadface);
        TT_ASSERT((uint32_t) slots != 0xabadface);

        return buffer_shape.volume();
    }

    uint32_t tt_buffer_queue::get_rt() const { return this->buffer_shape.block_shape.r; }
    uint32_t tt_buffer_queue::get_ct() const { return this->buffer_shape.block_shape.c; }
    uint32_t tt_buffer_queue::get_zt() const { return this->buffer_shape.z * this->buffer_shape.num_blocks_r * this->buffer_shape.num_blocks_c; }
    uint32_t tt_buffer_queue::get_wt() const { return this->buffer_shape.w; }

    tt_buffer_shape tt_buffer_queue::get_buffer_shape() const
    {
        return this->buffer_shape;
    }

    int tt_buffer_queue::get_buffer_num_tiles()
    {
        // Assuming all buffer sizes are the same
        TT_ASSERT(is_initialized);
        TT_ASSERT(buffer_shape.w != 0xabadface);
        TT_ASSERT(buffer_shape.z != 0xabadface);
        TT_ASSERT(buffer_shape.num_blocks_r != 0xabadface);
        TT_ASSERT(buffer_shape.num_blocks_c != 0xabadface);
        TT_ASSERT((uint32_t) slots != 0xabadface);

        return buffer_shape.volume();
    }

    int tt_buffer_queue::occupancy()
    {
        return bufq.size();
    }

    void tt_buffer_queue::push(tt_buffer * buf_ptr)
    {
        TT_ASSERT(this->buffer_shape == buf_ptr->get_buffer_shape(), this->buffer_shape, buf_ptr->get_buffer_shape());
        TT_ASSERT(this->get_data_format() == buf_ptr->get_data_format());

        bufq.push_blocking(buf_ptr);
    }

    void tt_buffer_queue::bufq_pop()
    {
        TT_ASSERT(this->bufq.size() != 0);

        tt_buffer* tmp;
        this->bufq.pop_blocking_by_ref(tmp);
        delete tmp;
    }

    void tt_buffer_queue::set_num_slots(int islots) {
        cout << "Setting num slots:  dram_io: " << this->parent_dram_io->name << ", bufq unique_id: " << this->unique_id.get() << ", num_slots: " << islots << endl;
        slots = islots;
        dram_ptrs.slots = islots;
    }

    int tt_buffer_queue::get_num_slots()
    {
        return slots;
    }

    bool tt_buffer_queue::is_allocated_to_dram()
    {
        return allocated_to_dram || host_resident;
    }

    void tt_buffer_queue::set_allocated_to_dram(bool flag)
    {
        allocated_to_dram = flag;
    }

    void tt_buffer_queue::set_host_resident(bool flag)
    {
        host_resident = flag;
    }


    void tt_buffer_queue::assign_to_dram_channel(uint32_t dram_channel, uint32_t subchannel) {
        this->assigned_to_dram_channel = true;
        this->set_dram_chan(dram_channel);
        this->set_dram_subchannel(subchannel);
    }

    bool tt_buffer_queue::is_assigned_to_dram_channel() const {
        return this->assigned_to_dram_channel;
    }

    bool tt_buffer_queue::is_tile_buf()
    {
        return false;
    }
    /////////////////
    // This set of methods are re-implementations
    // of buffer functions in bufq
    // generally they just redirect the call to the tt_buffer
    // that is at the top of the queue

    bool tt_buffer_queue::is_double_buffered()
    {
        // double_buf pre-requisite: not empty and not consumed (same as valid)
        if(bufq.size() == 0 || is_consumed) return false;
        else return bufq.front()->is_double_buffered();
    }

    bool tt_buffer_queue::is_valid(bool use_shadow_rd_ptr)
    {
        // valid pre-requisite: not empty and not consumed
        if (this->bufq.size() > 0 and not is_consumed) {
            return this->get_buf_ptr(use_shadow_rd_ptr)->is_valid(use_shadow_rd_ptr);
        }
        return false;
    }

    bool tt_buffer_queue::bufq_totally_empty()
    {
        // for tt_buffer_queue (DRAM), is_consumed flag is also means empty
        return (is_consumed || bufq.empty());
    }

    bool tt_buffer_queue::is_totally_empty()
    {
        return bufq_totally_empty();
    }

    bool tt_buffer_queue::is_bufq()
    {
        return true;
    }

    bool tt_buffer_queue::is_param_bufq()
    {
        // FIXME: Certain conditions where buffer type is wrong, so I can't check for 
        // tensor type parameter in this function. 
        return bufq.size() == 1;
    }

    bool tt_buffer_queue::is_ready_to_write()
    {
        // In a queue, there's always space
        // to write since a new value
        // will be pushed each pipe.tx()
        return true;
    }

    void tt_buffer_queue::clear_buf()
    {
        is_consumed = true;

        // TODO: at the moment we don't clear buffer_queues to allow consumers from multiple epochs, 
        // next step is to add modeling last_consuming_epoch, which will clear the buffer         
        //bufq[0]->clear_buf();
        //this->bufq_pop();
        // incr_rd_ptr();
    }

    void tt_buffer_queue::clear_and_pop_slot(int num_pops)
    {
        if (this->bufq.front()->is_infinite_life()) {
            return;
        }

        num_pops = (this->parent_dram_io->get_tensor_type() == tt::TensorType::Activation) ? num_pops: 1;

        for (int i = 0; i < num_pops; i++) {
            this->bufq.front()->clear_buf();
            this->bufq_pop();
        }
    }

    void tt_buffer_queue::clear_consumed_flag()
    {
        this->is_consumed = false;
    }

    void tt_buffer_queue::stream_append(tt_buffer *ttb)
    {
        TT_ASSERT(this->bufq.size() != 0);

        this->bufq.back()->stream_append(ttb);
    }

    void tt_buffer_queue::stream_copy(tt_buffer *ttb)
    {
        TT_ASSERT(this->bufq.size() != 0);
        this->bufq.back()->stream_copy(ttb);
    }

    tt_buffer *tt_buffer_queue::get_buf_ptr(bool use_shadow_rd_ptr)
    {
        return this->bufq.read(use_shadow_rd_ptr);
    }

    tt_buffer *tt_buffer_queue::get_buf_ptr(int off)
    {
        return this->bufq.read(off);
    }

    void tt_buffer_queue::set_rd_ptr(int ptr, bool use_shadow_rd_ptr)
    {
        this->bufq.set_ptr(ptr, use_shadow_rd_ptr);
    }

    void tt_buffer_queue::set_q_offset(int ptr)
    {
        this->bufq.set_offset(ptr);
    }

    int tt_buffer_queue::get_q_offset()
    {
        return this->bufq.get_offset();
    }

    int tt_buffer_queue::get_rd_ptr(bool use_shadow_rd_ptr)
    {
        return this->bufq.get_ptr(use_shadow_rd_ptr);
    }

    void tt_buffer_queue::increment_offset()
    {

        this->bufq.increment_offset();
    }

    void tt_buffer_queue::reset_offset()
    {
        this->bufq.reset_offset();
    }

    int tt_buffer_queue::get_offset() 
    {
        return this->bufq.get_offset();
    }


    tt_buffer *tt_buffer_queue::get_first_inserted_buf_ptr()
    {
        TT_ASSERT(this->bufq.size() != 0);

        return this->bufq.front();
    }

    tt_buffer *tt_buffer_queue::get_last_inserted_buf_ptr()
    {
        TT_ASSERT(this->bufq.size() != 0);

        return this->bufq.back();
    }

    tt_core *tt_buffer_queue::get_core_ptr()
    {
        return this->associated_core;
    }

    tt_buffer_queue::~tt_buffer_queue()
    {
        while (not bufq.empty())
        {
            tt_buffer* buf_ptr;
            bufq.pop_blocking_by_ref(buf_ptr);
            delete buf_ptr;
        }
    }

    bool is_tt_buffer_queue(tt_buffer* buf) {
        return (dynamic_cast<tt_buffer_queue*>(buf) != nullptr);
    }

} // end namespace tt
