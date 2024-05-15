// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <tuple>
#include <unordered_set>
#include <fstream>
#include <cstring>
#include <string>
#include <sstream>
#include <thread>
#include <type_traits>

#include "base_types.hpp"
#include "device/cpuset_lib.hpp"
#include "model/tensor.hpp"
#include "size_lib.hpp"
#include "tensor_hierarchy_metadata.hpp"
#include "tile.hpp"
#include "utils/scoped_timer.hpp"
#include "common/tt_parallel_for.h"

// Utils
using std::cout;
using std::endl;
using std::begin;
using std::end;
using std::runtime_error;
using std::ofstream;

// Functions
using std::memcpy;

// Datastructures
using std::unordered_set;

namespace tt
{
    // rti and cti are tensor dimensions in tiles
    // zi and wi are regular scalar dimensions
    tt_tensor::tt_tensor(tt_tensor_metadata const &metadata) : metadata(metadata) {}

    tt_tensor::tt_tensor(tt_shape shape_, DataFormat data_format_, TensorType tensor_type_, bool requires_grad_, DataFormat gradient_data_format_, DataFormat optimizer_data_format_) :
        metadata({.shape= shape_, .data_format = data_format_, .tensor_type = tensor_type_, .is_tilized = true, .requires_grad = requires_grad_})
    {
        metadata.gradient_data_format = (gradient_data_format_ != DataFormat::Invalid) ? gradient_data_format_ : metadata.data_format;
        metadata.optimizer_data_format = (optimizer_data_format_ != DataFormat::Invalid) ? optimizer_data_format_ : metadata.gradient_data_format;
    }

    tt_tensor tt_tensor::make_untilized_tensor(tt_tensor_metadata metadata)
    {
        metadata.is_tilized = false;
        tt_tensor result (metadata);
        return result;
    }

    tt_tensor::tt_tensor(tt_shape shape_, const vector<tt_tile>& tiles, TensorType tensor_type_, bool requires_grad_) :
        metadata({.shape= shape_, .tensor_type = tensor_type_, .requires_grad = requires_grad_})
    {

        int tile_index = 0;
        for (int iw = 0; iw < getw(); ++iw)
        {
            vector <vector <vector <tt_tile> > > tmp_z;
            for(int iz=0;iz<getz();++iz)
            {
                vector <vector <tt_tile> > tmp_r;
                for(int ri=0;ri<getrt();++ri)
                {
                    vector <tt_tile> tmp_c;
                    for(int ci=0;ci<getct();++ci)
                    {
                        this->metadata.data_format = tiles.at(tile_index).data_format;
                        this->metadata.gradient_data_format = this->metadata.data_format;
                        tmp_c.push_back(tiles.at(tile_index));
                        tile_index += 1;
                    }
                    tmp_r.push_back(tmp_c);
                }
                tmp_z.push_back(tmp_r);
            }
            tile_tensor.push_back(tmp_z);
        }
    }

    int tt_tensor::total_tiles() const
    {
        return metadata.shape.volume();
    }

    tt_tensor::tile_data tt_tensor::extract_tiles_from_range(tt_dims range_start, tt_dims range_end) const
    {
        uint32_t w0 = range_start.w;
        uint32_t z0 = range_start.z;
        uint32_t r0 = range_start.rt;
        uint32_t c0 = range_start.ct;
        uint32_t w1 = range_end.w;
        uint32_t z1 = range_end.z;
        uint32_t r1 = range_end.rt;
        uint32_t c1 = range_end.ct;

        log_assert(w1<=this->getw(), "w index out of range");
        log_assert(z1<=this->getz(), "z index out of range");
        log_assert(r1<=this->getrt(), "x index out of range");
        log_assert(c1<=this->getct(), "y index out of range");

        tt_tensor::tile_data result;

        for(int wi=w0; wi<w1; ++wi)
        {
            vector <vector <vector <tt_tile> > > tmp_z;
            for(int zi=z0; zi<z1; ++zi)
            {
                vector <vector <tt_tile> > tmp_r;
                for(int ri=r0; ri<r1; ++ri)
                {
                    vector <tt_tile> tmp_c;
                    for(int ci=c0; ci<c1; ++ci)
                    {
                        tt_tile tile = this->tile_tensor[wi][zi][ri][ci];
                        tmp_c.push_back(tile);
                    }
                    tmp_r.push_back(tmp_c);
                }
                tmp_z.push_back(tmp_r);
            }
            result.push_back(tmp_z);
        }

        tt_shape result_shape = tt_shape {.rt = (r1-r0), .ct = (c1-c0), .z = (z1-z0), .w = (w1-w0)};
        log_assert(result_shape == get_tile_tensor_shape(result) ,  "Extract shape mismatches expectation");

        return result;
    }

    tt_tensor tt_tensor::extract_range_to_tensor(tt_dims range_start, tt_dims range_end, bool shape_only) const
    {
        uint32_t w0 = range_start.w;
        uint32_t z0 = range_start.z;
        uint32_t r0 = range_start.rt;
        uint32_t c0 = range_start.ct;
        uint32_t w1 = range_end.w;
        uint32_t z1 = range_end.z;
        uint32_t r1 = range_end.rt;
        uint32_t c1 = range_end.ct;
        log_assert(w1<=this->getw(), "w index out of range");
        log_assert(z1<=this->getz(), "z index out of range");
        log_assert(r1<=this->getrt(), "x index out of range");
        log_assert(c1<=this->getct(), "y index out of range");

        tt_tensor result(tt_shape {.rt = (r1-r0), .ct = (c1-c0), .z = (z1-z0), .w = (w1-w0)}, this->get_data_format());

        if (not shape_only)
        {
            tt_tensor::tile_data tile_tensor = this->extract_tiles_from_range(range_start, range_end);
            result.fill_with_data(tile_tensor);
        }

        return result;
    }

    // this method makes and returns a deep copy of this tensor
    tt_tensor tt_tensor::copy(bool shape_only) const
    {
        tt_tensor result(this->metadata);
        result.tile_tensor.clear();

        if (this->is_tilized() and this->get_num_stored_tiles() <= 0)
        {
            return result;
        }
        else if (not this->is_tilized() and this->flat_tensor_data.size()<= 0)
        {
            return result;
        }

        result.tile_tensor = this->tile_tensor;
        result.flat_tensor_data = this->flat_tensor_data;
        return result;
    }

    // This method creates a copy of this tensor on the heap.
    // Remember to free this memory to prevent leaks!
    tt_tensor *tt_tensor::allocate_on_heap(tt_tensor const &&tensor) { return tensor.copy_on_heap(); }

    // This method creates a copy of this tensor on the heap.
    // Remember to free this memory to prevent leaks!
    tt_tensor* tt_tensor::copy_on_heap() const
    {
        // TODO: add allocation tracking here
        tt_tensor *result = new tt_tensor(metadata);
        *result = *this;
        return result;
    }

    tt_tensor tt_tensor::reshape(tt_shape new_shape, bool shape_only) const
    {
        tt_tensor ret(metadata);
        // set new shape
        ret.metadata.shape = new_shape;

        if (shape_only) {
            return ret;
        }

        // clear current storage
        ret.tile_tensor.clear();

        // read out rows into a flat array
        typedef float t_row[32];
        vector<const t_row *> rows;
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for (int line=0; line < 32; line++)
                    {
                        for(int ci=0;ci<getct();++ci)
                        {
                            rows.push_back(&tile_tensor[wi][zi][ri][ci].t[line]);
                        }
                    }
                }
            }
        }

        // write back out into new shape
        int index = 0;
        for(int wi=0;wi<new_shape.w;++wi)
        {
            vector <vector <vector <tt_tile> > > tmp_z;
            for(int zi=0;zi<new_shape.z;++zi)
            {
                vector <vector <tt_tile> > tmp_r;
                for(int ri=0;ri<new_shape.rt;++ri)
                {
                    vector <tt_tile> tmp_c;
                    for (int line=0; line < 32; line++)
                    {
                        for(int ci=0;ci<new_shape.ct;++ci)
                        {
                            if (line == 0)
                                tmp_c.push_back(tt_tile(tile_tensor[0][0][0][0].data_format));

                            auto &row = *rows[index++];
                            std::copy(begin(row), end(row), begin(tmp_c[ci].t[line]));
                        }
                    }
                    tmp_r.push_back(tmp_c);
                }
                tmp_z.push_back(tmp_r);
            }
            ret.tile_tensor.push_back(tmp_z);
        }
        return ret;
    }

    tt_tensor::tt_tensor(const tt_tensor& rhs) {
        *this = rhs;
    }

    tt_tensor::tt_tensor(tt_tensor &&rhs) {
        *this = std::move(rhs);
    }

    tt_tensor &tt_tensor::operator=(tt_tensor &&rhs) {
        if (this != &rhs) {
            metadata = rhs.metadata;
            tile_tensor = std::move(rhs.tile_tensor);
            flat_tensor_data = std::move(rhs.flat_tensor_data);
            flat_tensor_data_half = std::move(rhs.flat_tensor_data_half);
            flat_untilized_tensor_data = std::move(rhs.flat_untilized_tensor_data);
            owns_tile_buffers = rhs.owns_tile_buffers;
            rhs.owns_tile_buffers = false;
            tile_map = std::move(rhs.tile_map);
            tile_level_mappings = rhs.tile_level_mappings;
        }

        return *this;
    }

    tt_tensor& tt_tensor::operator = (const tt_tensor& rhs)
    {
        // self-assignment check
        if (this == &rhs) {
            return *this;
        }

        this->metadata = rhs.metadata;
        this->set_grid_shape({0, 0});

        // clear current storage
        this->tile_tensor.clear();

        // Only makes a copy of the tile_tensor
        // if the rhs has a tile_tensor
        if ((!rhs.is_shape_only()) && (rhs.get_num_stored_tiles() > 0)) {
            this->tile_tensor = rhs.tile_tensor;
        }

        this->flat_tensor_data = rhs.flat_tensor_data;

        return *this;
    }

    void tt_tensor::operator = (float val)
    {
        this->reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        tile_tensor[wi][zi][ri][ci] = val;
                    }
                }
            }
        }
    }

    // Debug for tracking tiles
    void tt_tensor::init_to_xyz_values()
    {
        this->reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        tile_tensor[wi][zi][ri][ci].init_to_xyz_values(wi * getz() + zi);
                    }
                }
            }
        }
    }

    // Debug for tracking tiles
    void tt_tensor::init_to_tile_id(float step_size, float base_val)
    {
        this->reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            int w_offset = wi * getz() * getrt() * getrt();
            for(int zi=0;zi<getz();++zi)
            {
                int z_offset = zi * getrt() * getrt();
                for(int ri=0;ri<getrt();++ri)
                {
                    int r_offset = ri * getrt();
                    for(int ci=0;ci<getct();++ci)
                    {
                        int unique_tile_id = w_offset + z_offset + r_offset + ci;
                        tile_tensor[wi][zi][ri][ci] = unique_tile_id * step_size + base_val;
                    }
                }
            }
        }
    }

    void tt_tensor::init_to_input_value(float value)
    {
        this->reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        tile_tensor[wi][zi][ri][ci] = value;
                    }
                }
            }
        }
    }

    int tt_tensor::size_bytes(bool include_header_padding)
    {
        int total_tensor_size = 0;
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        total_tensor_size += tile_tensor[wi][zi][ri][ci].size_bytes(include_header_padding);
                    }
                }
            }
        }
        return total_tensor_size;
    }
    
    int tt_tensor::size_bytes(DataFormat data_format, bool include_header_padding)
    {
        int total_tensor_size = 0;
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        total_tensor_size += size::get_tile_size_in_bytes(data_format, include_header_padding);
                    }
                }
            }
        }
        return total_tensor_size;
    }

    float &tt_tensor::at(int w_index, int z_index, int rt_index, int ct_index, int r_index, int c_index) {
        return tile_tensor[w_index][z_index][rt_index][ct_index].t[r_index][c_index];
    }
    float tt_tensor::at(int w_index, int z_index, int rt_index, int ct_index, int r_index, int c_index) const {
        return tile_tensor[w_index][z_index][rt_index][ct_index].t[r_index][c_index];
    }

    tt_tile tt_tensor::get_tile(int rti, int cti, int zi, int wi) const
    {
        log_assert(rti < tile_tensor[0][0].size(), "y index out of range");
        log_assert(cti < tile_tensor[0][0][0].size(), "x index out of range");
        log_assert( zi < tile_tensor[0].size(), "z index out of range");
        log_assert( wi < tile_tensor.size(), "w index out of range"    );

        return tile_tensor[wi][zi][rti][cti];
    }

    tt_tile* tt_tensor::get_tile_ptr(int rti, int cti, int zi, int wi, uint32_t height, uint32_t width) const
    {
        log_assert(rti < tile_tensor[0][0].size(), "y index out of range");
        log_assert(cti < tile_tensor[0][0][0].size(), "x index out of range");
        log_assert( zi < tile_tensor[0].size(), "z index out of range");
        log_assert( wi < tile_tensor.size(), "w index out of range"    );
        tt_tile* tile = const_cast<tt_tile*>(&tile_tensor[wi][zi][rti][cti]);
        tile -> tile_height = height;
        tile -> tile_width = width;
        return tile;
    }

    tt_tile tt_tensor::get_tile_from_flat_index(int index, Dim tile_order_dim) const
    {
        log_assert(tile_order_dim == Dim::R, "invalid tile_order_dim"); // Only support row order for now. 
        int r_stride = getct();
        int z_stride = r_stride * getrt();
        int w_stride = z_stride * getz(); 
        int remaining_index = index;
        int cti = index % r_stride;
        int rti = (index % z_stride) / r_stride;
        int zi = (index % w_stride) / z_stride;
        int wi = index / w_stride;
        log_assert (rti < tile_tensor[0][0].size(), "calculated rti={} < tile_tensor[0][0].size()={}", rti, tile_tensor[0][0].size());
        log_assert (cti < tile_tensor[0][0][0].size(), "calculated cti={} < tile_tensor[0][0][0].size()={}", cti, tile_tensor[0][0][0].size());
        log_assert ( zi < tile_tensor[0].size(), "calculated zi={} < tile_tensor[0].size()={}", zi, tile_tensor[0].size());
        log_assert ( wi < tile_tensor.size(), "calculated wi={} < tile_tensor.size()={}", wi, tile_tensor.size());
        return tile_tensor[wi][zi][rti][cti];
    }
    tt_tile* tt_tensor::get_tile_ptr_from_flat_index(int index, Dim tile_order_dim) const
    {
        log_assert(tile_order_dim == Dim::R, "invalid tile_order_dim"); // Only support row order for now. 
        int r_stride = getct();
        int z_stride = r_stride * getrt();
        int w_stride = z_stride * getz(); 
        int remaining_index = index;
        int cti = index % r_stride;
        int rti = (index % z_stride) / r_stride;
        int zi = (index % w_stride) / z_stride;
        int wi = index / w_stride;
        log_assert (rti < tile_tensor[0][0].size(),  "calculated rti={} < tile_tensor[0][0].size()={}", rti, tile_tensor[0][0].size());
        log_assert (cti < tile_tensor[0][0][0].size(),  "calculated cti={} < tile_tensor[0][0][0].size()={}", cti, tile_tensor[0][0][0].size());
        log_assert ( zi < tile_tensor[0].size(),  "calculated zi={} < tile_tensor[0].size()={}", zi, tile_tensor[0].size());
        log_assert ( wi < tile_tensor.size(), "calculated wi={} < tile_tensor.size()={}", wi, tile_tensor.size());
        return const_cast<tt_tile*>(&tile_tensor[wi][zi][rti][cti]);
    }

    DataFormat tt_tensor::get_data_format() const
    {
        return metadata.data_format;
    }

    tt_shape tt_tensor::get_shape() const
    {
        return metadata.shape;
    }

    void tt_tensor::set_shape(const tt_shape& shape)
    {
        metadata.shape = shape;
    }

    tt_block_shape tt_tensor::get_block_shape() const
    {
        return metadata.block_shape;
    }

    void tt_tensor::set_block_shape(const tt_block_shape& block_shape)
    {
        metadata.block_shape = block_shape;
    }

    tt_grid_shape tt_tensor::get_grid_shape() const
    {
        return metadata.grid_shape;
    }

    void tt_tensor::set_grid_shape(const tt_grid_shape& grid_shape)
    {
        metadata.grid_shape = grid_shape;
    }

    TensorType tt_tensor::get_tensor_type() const
    {
        return metadata.tensor_type;
    }

    void tt_tensor::set_tensor_type(const TensorType& tensor_type) {
        metadata.tensor_type = tensor_type;
    }

    tt_op* tt_tensor::get_producer_op_ptr() const
    {
        return metadata.producer_op_ptr;
    }

    std::uint32_t tt_tensor::get_producer_op_output_index() const 
    {
        return metadata.producer_op_output_index;
    }

    void tt_tensor::set_producer_op_ptr(tt_op* producer_op_ptr) {
        metadata.producer_op_ptr = producer_op_ptr;
    }

    void tt_tensor::set_producer_op_output_index(std::uint32_t producer_op_output_index) {
        metadata.producer_op_output_index = producer_op_output_index;
    }

    bool tt_tensor::same_shape(const tt_tensor &other) const
    {
      return this->metadata.shape == other.metadata.shape;
    }

    bool tt_tensor::get_requires_grad() const
    {
        return metadata.requires_grad;
    }

    void tt_tensor::set_requires_grad(bool requires_grad)
    {
        metadata.requires_grad = requires_grad;
    }

    DataFormat tt_tensor::get_gradient_data_format() const
    {
        return metadata.gradient_data_format;
    }

    DataFormat tt_tensor::get_optimizer_data_format() const
    {
        return metadata.optimizer_data_format;
    }

    void tt_tensor::set_gradient_data_format(DataFormat data_format_)
    {
        metadata.gradient_data_format = data_format_;
    }

    void tt_tensor::set_optimizer_data_format(DataFormat data_format_)
    {
        metadata.optimizer_data_format = data_format_;
    }

    bool tt_tensor::is_tilized() const
    {
        return metadata.is_tilized;
    }

    void tt_tensor::set_is_tilized(bool is_tilized)
    {
        metadata.is_tilized = is_tilized;
    }

    void tt_tensor::set_epilogue_dram_io(tt_dram_io* dram_io)
    {
        metadata.dram_io = dram_io;
    }

    tt_dram_io* tt_tensor::get_epilogue_dram_io() 
    {
        return metadata.dram_io;
    }

    void tt_tensor::tilize_inplace(bool is_c_contiguous, bool shape_only, bool force_megarow) {
        // No-op if tensor already tilized
        if (is_tilized()) {
            if (not shape_only) {
                log_assert(tile_tensor.size() > 0, "Expected tensor to be tilized, but tile_tensor is empty");
            }
            return ;
        }

        if (shape_only) {
            set_is_tilized(true);
        } else {
            log_assert(flat_tensor_data.size() > 0, "Expected tensor to be un-tilized, but flat_tensor_data is empty");
        }


        using tt::constants::TILE_HEIGHT;
        using tt::constants::TILE_WIDTH;

        tile_tensor.resize(getw());
        for (size_t iw = 0; iw < tile_tensor.size(); iw++) {
            tile_tensor[iw].resize(getz());
            for (size_t iz = 0; iz < tile_tensor[iw].size(); iz++) {
                tile_tensor[iw][iz].resize(getrt());
                for (size_t ir = 0; ir < tile_tensor[iw][iz].size(); ir++) {
                    tile_tensor[iw][iz][ir].resize(getct());
                }
            }
        }
        size_t num_datums = 0;
        size_t src_datum_index = 0;
        bool is_megarow = this->get_data_format() == DataFormat::RawUInt8 or
                          this->get_data_format() == DataFormat::RawUInt16 or
                          this->get_data_format() == DataFormat::RawUInt32 or force_megarow;

        const size_t tile_height = get_shape().tile_height;
        const size_t tile_width = get_shape().tile_width;
        const size_t rfull = getrfull();
        const size_t cfull = getcfull();
        const size_t rcfull = rfull * cfull;
        for (size_t iw = 0; iw < getw(); iw++) {
            size_t i_src_w = iw * getz() * rcfull;
            for (size_t iz = 0; iz < getz(); iz++) {
                size_t i_src_z = iz * rcfull;
                size_t i_src_w_and_i_src_z = i_src_w + i_src_z;
                for (size_t irt = 0; irt < getrt(); irt++) {
                    for (size_t ict = 0; ict < getct(); ict++) {
                        tt_tile& tile = tile_tensor[iw][iz][irt][ict];
                        tile.data_format = get_data_format();

                        for (size_t i_tile_row = 0; i_tile_row < tile_height; i_tile_row++) {
                            size_t ir = irt * tile_height + i_tile_row;
                            for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
                                if (is_megarow) {
                                    // Organize datums in the same order as original tensor layout
                                    src_datum_index = num_datums;
                                } else {
                                    size_t ic = ict * tile_width + i_tile_col;

                                    size_t i_src_outer;
                                    size_t i_src_inner;
                                    if (is_c_contiguous) {
                                        i_src_outer = ir * cfull;
                                        i_src_inner = ic;
                                    } else {
                                        i_src_outer = ic * cfull;
                                        i_src_inner = ir;
                                    }
                                    src_datum_index = i_src_w_and_i_src_z + i_src_outer + i_src_inner;
                                }


                                // at this point we only perform step 1 of 'tilize' operation, step 2 is done in 'pack_data'
                                // step 1 (here). tile-level data shuffling, direct float-to-float copy within a tile
                                // step 2 (pack). format conversion, insert tile header, shared exp, compression meta
                                tile.t[i_tile_row][i_tile_col] = flat_tensor_data[src_datum_index];
                                num_datums++;
                            }
                        }
                    }
                }
            }
        }
    }

    void tt_tensor::hstack(tt_tensor& in_tensor, bool shape_only) {
        // TODO: Fix this function to return a new tensor.

        // Check that current tensor is not empty
        // if it is, just create a copy of input
        if (this->total_tiles() == 0) {
            *this = in_tensor;
        } else {
            // Check all dimensions except rows are equal
            log_assert(getrt() == in_tensor.getrt(), "Invalid tensor y dim");
            log_assert(getw() == in_tensor.getw(), "Invalid tensor w dim");
            log_assert(getz() == in_tensor.getz(), "Invalid tensor z dim");

            if (shape_only) {
                metadata.shape.ct += in_tensor.metadata.shape.ct;
                return;
            }

            for (int wi = 0; wi < getw(); ++wi) {
                for (int zi = 0; zi < getz(); ++zi) {
                    for (int ri = 0; ri < getrt(); ++ri) {
                        // Append the in tensor rows to the existing ones, row by
                        tile_tensor[wi][zi][ri].insert(
                            tile_tensor[wi][zi][ri].end(),
                            in_tensor.tile_tensor[wi][zi][ri].begin(),
                            in_tensor.tile_tensor[wi][zi][ri].end());
                    }
                }
            }
            // Update dimension
            metadata.shape.ct += in_tensor.metadata.shape.ct;
        }
    }

    void tt_tensor::vstack(tt_tensor& in_tensor, bool shape_only) {
        // TODO: Fix this function to return a new tensor.
        // Check that current tensor is not empty
        // if it is, just create a copy of input

        if (shape_only) {
            metadata.shape.rt += in_tensor.getrt();
            return;
        }

        if (tile_tensor.size() == 0) {
            *this = in_tensor;
        } else {
            // Check all dimensions columns are equal
            log_assert(metadata.shape.ct == in_tensor.metadata.shape.ct, "Shape mismatch for vstack");
            log_assert(metadata.shape.w == in_tensor.metadata.shape.w, "Shape mismatch for vstack");
            log_assert(metadata.shape.z == in_tensor.metadata.shape.z, "Shape mismatch for vstack");

            for (int wi = 0; wi < getw(); ++wi) {
                for (int zi = 0; zi < getz(); ++zi) {
                    // Append the in tensor rows to the existing ones, row by
                    tile_tensor[wi][zi].insert(
                        tile_tensor[wi][zi].end(),
                        in_tensor.tile_tensor[wi][zi].begin(),
                        in_tensor.tile_tensor[wi][zi].end());
                }
            }
            // Update dimension
            metadata.shape.rt += in_tensor.getrt();
        }
    }

    void tt_tensor::zstack(tt_tensor &in_tensor, bool shape_only)
    {
            // Check that current tensor is not empty
        // if it is, just create a copy of input
        if(this->get_shape().volume() == 0)
        {
            *this = in_tensor;
        }
        else
        {
            // Check all dimensions columns are equal
            log_assert(getct() == in_tensor.getct(), "Shape mismatch for zstack");
            log_assert(getrt() == in_tensor.getrt(), "Shape mismatch for zstack");
            log_assert(getw() == in_tensor.getw(), "Shape mismatch for zstack");

            if (shape_only)
            {
                metadata.shape.z += in_tensor.getz();
                return;
            }

            for(int wi=0;wi<getw();++wi)
            {
                // Append the in tensor rows to the existing ones, row by
                tile_tensor[wi].insert(tile_tensor[wi].end(), in_tensor.tile_tensor[wi].begin(), in_tensor.tile_tensor[wi].end());
            }

            // Update dimension
            metadata.shape.z += in_tensor.getz();
        }
    }

    void tt_tensor::wstack(tt_tensor &in_tensor, bool shape_only)
    {
        if (shape_only)
        {
            metadata.shape.w += in_tensor.getw();
            return;
        }
        // Check that current tensor is not empty
        // if it is, just create a copy of input
        if(tile_tensor.size() == 0)
        {
            *this = in_tensor;
        }
        else
        {
            // Check all dimensions columns are equal
            log_assert(getct() == in_tensor.getct(), "Shape mismatch for wstack");
            log_assert(getrt() == in_tensor.getrt(), "Shape mismatch for wstack");
            log_assert(getz() == in_tensor.getz(), "Shape mismatch for wstack");

            // Append the in tensor rows to the existing ones, row by
            tile_tensor.insert(tile_tensor.end(), in_tensor.tile_tensor.begin(), in_tensor.tile_tensor.end());

            // Update dimension
            metadata.shape.w += in_tensor.getw();
        }
    }


    tt_tensor tt_tensor::wstack(vector<tt_tensor> &in_tensors) {
        log_assert(in_tensors.size() > 0, "Expected a positive number of tensors for wstack");
        tt_tensor t = in_tensors.at(0);

        if (in_tensors.size() > 1)
        {
            for (int i=1; i<in_tensors.size(); i++)
            {
                t.wstack(in_tensors.at(i), false);
            }
        }
        return t;
    }

    tt_tensor tt_tensor::hmerge(const vector<tt_tensor>& inputs, bool shape_only) const
    {
        log_assert (
            inputs.size() > 0, 
            "Must try to merge 1 or more inputs -- input_size={}", 
            inputs.size()
        );

        unsigned int input_w = inputs.at(0).getw();
        unsigned int input_z = inputs.at(0).getz();
        unsigned int input_rt = inputs.at(0).getrt();
        unsigned int input_ct = inputs.at(0).getct();
        unsigned int input_tile_height = inputs.at(0).get_tile_height();
        unsigned int input_tile_width = inputs.at(0).get_tile_width();
        unsigned int final_ct = inputs.size() * input_ct;
        tt_tensor result(
            tt_shape{
                .rt = input_rt,
                .ct = final_ct,
                .z = input_z,
                .w = input_w,
                .tile_height = input_tile_height,
                .tile_width = input_tile_width},
            inputs.at(0).get_data_format());
        if (shape_only) {
            return result;
        }

        result.reserve_tile_tensor();
        for (int tensor_idx=0; tensor_idx < inputs.size(); tensor_idx++) {
            const auto& input = inputs.at(tensor_idx);
            log_assert(
                (input.getw() == input_w) and
                (input.getw() == input_w) and
                (input.getw() == input_w) and
                (input.getw() == input_w), 
                "hmerge inputs must have same size across whole vector. tensor{} in inputs has different size", 
                tensor_idx
            );
            for (int w=0; w < input_w; w++) {
                for (int z=0; z < input_z; z++) {
                    for (int r=0; r < input_rt; r++) {
                        for (int c=0; c < input_ct; c++) {
                            result.tile_tensor[w][z][r][tensor_idx*input_ct + c] = input.tile_tensor[w][z][r][c];
                        }
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::vmerge(const vector<tt_tensor>& inputs, bool shape_only) const
    {
        log_assert (
            inputs.size() > 0,
            "Must try to merge 1 or more inputs -- input_size={}", 
            inputs.size()
        );

        unsigned int input_w  = inputs.at(0).getw();
        unsigned int input_z  = inputs.at(0).getz();
        unsigned int input_rt = inputs.at(0).getrt();
        unsigned int input_ct = inputs.at(0).getct();
        unsigned int input_tile_height = inputs.at(0).get_tile_height();
        unsigned int input_tile_width = inputs.at(0).get_tile_width();
        unsigned int final_rt = inputs.size() * input_rt;
        tt_tensor result(
            tt_shape{
                .rt = final_rt,
                .ct = input_ct,
                .z = input_z,
                .w = input_w,
                .tile_height = input_tile_height,
                .tile_width = input_tile_width},
            inputs.at(0).get_data_format());
        if (shape_only) {
            return result;
        }

        result.reserve_tile_tensor();
        for (int tensor_idx=0; tensor_idx < inputs.size(); tensor_idx++) {
            const auto& input = inputs.at(tensor_idx);
            log_assert(
                (input.getw() == input_w) and
                (input.getw() == input_w) and
                (input.getw() == input_w) and
                (input.getw() == input_w), 
                "vmerge inputs must have same size across whole vector. tensor{} in inputs has different size", 
                tensor_idx
            );
            for (int w=0; w < input_w; w++) {
                for (int z=0; z < input_z; z++) {
                    for (int r=0; r < input_rt; r++) {
                        for (int c=0; c < input_ct; c++) {
                            result.tile_tensor[w][z][tensor_idx*input_rt + r][c] = input.tile_tensor[w][z][r][c];
                        }
                    }
                }
            }
        }
        return result;
    }

    vector<tt_tensor> tt_tensor::hsplit(int split_factor, bool shape_only) const
    {
        log_assert(
            (getct() % split_factor) == 0, 
            "split_factor={} must cleanly divide out the ct={}", 
            split_factor,
            getct()
        );

        unsigned int final_ct = getct() / split_factor;
        tt_shape new_tensor_shape{
            .rt = this->getrt(),
            .ct = final_ct,
            .z = getz(),
            .w = getw(),
            .tile_height = get_tile_height(),
            .tile_width = get_tile_width()};

        vector<tt_tensor> result;
        if (shape_only)
        {
            for (int tensor_idx=0; tensor_idx < split_factor; tensor_idx++)
            {
                tt_tensor t(new_tensor_shape, this->get_data_format());
                result.push_back(t);
            }
            return result;
        }

        for (int tensor_idx=0; tensor_idx < split_factor; tensor_idx++) {
            tt_tensor t(new_tensor_shape, this->get_data_format());
            t.reserve_tile_tensor();
            for (int w=0; w < this->getw(); w++) {
                for (int z=0; z < this->getz(); z++) {
                    for (int r=0; r < this->getrt(); r++) {
                        for (int c_new=0; c_new < final_ct; c_new++) {
                            t.tile_tensor[w][z][r][c_new] = tile_tensor[w][z][r][c_new + tensor_idx*final_ct];
                        }
                    }
                }
            }
            result.push_back(t);
        }
        return result;
    }

    vector<tt_tensor> tt_tensor::vsplit(int split_factor, bool shape_only) const
    {
        log_assert(
            (getrt() % split_factor) == 0, 
            "split_factor={} must cleanly divide out the ct={}", 
            split_factor,
            getrt()
        );

        unsigned int final_rt = getrt() / split_factor;
        tt_shape new_tensor_shape = {
            .rt = final_rt,
            .ct = this->getct(),
            .z = getz(),
            .w = getw(),
            .tile_height = get_tile_height(),
            .tile_width = get_tile_width()};

        vector<tt_tensor> result;
        if (shape_only)
        {
            for (int tensor_idx=0; tensor_idx < split_factor; tensor_idx++)
            {
                tt_tensor t(new_tensor_shape, this->get_data_format());
                result.push_back(t);
            }
            return result;
        }

        for (int tensor_idx=0; tensor_idx < split_factor; tensor_idx++) {
            tt_tensor t(new_tensor_shape, this->get_data_format());
            t.reserve_tile_tensor();
            for (int w=0; w < this->getw(); w++) {
                for (int z=0; z < this->getz(); z++) {
                    for (int r_new=0; r_new < final_rt; r_new++) {
                        for (int c=0; c < this->getct(); c++) {
                            t.tile_tensor[w][z][r_new][c] = tile_tensor[w][z][r_new + tensor_idx*final_rt][c];
                        }
                    }
                }
            }
            result.push_back(t);
        }
        return result;
    }
    vector<tt_tensor> tt_tensor::zsplit(bool shape_only) const
    {
        log_assert(getw() == 1 ,  "Can't Z-split tensor with W > 1");

        tt_shape new_tensor_shape = {
            .rt = this->getrt(),
            .ct = this->getct(),
            .z = 1,
            .w = 1,
            .tile_height = get_tile_height(),
            .tile_width = get_tile_width()};

        vector<tt_tensor> result;
        if (shape_only)
        {
            for (int z=0; z < this->getz(); z++)
            {
                tt_tensor t(new_tensor_shape, this->get_data_format());
                result.push_back(t);
            }
            return result;
        }

        for (int z=0; z < this->getz(); z++)
        {
            tt_tensor t(new_tensor_shape, this->get_data_format());
            t.reserve_tile_tensor();
            t.tile_tensor[0][0] = tile_tensor[0][z];
            result.push_back(t);
        }
        return result;
    }

    vector<tt_tensor> tt_tensor::wsplit(bool shape_only) const {
        vector<tt_tensor> result;
        tt_shape new_tensor_shape = {
            .rt = this->getrt(),
            .ct = this->getct(),
            .z = this->getz(),
            .w = 1,
            .tile_height = this->get_tile_height(),
            .tile_width = this->get_tile_width()};

        if (shape_only)
        {
            for (int w=0; w < this->getw(); w++)
            {
                tt_tensor t(new_tensor_shape, this->get_data_format());
                result.emplace_back(t);
            }
            return result;
        }

        int flat_vector_size = this->getrt() * this->getct() * this->getz() * this->get_tile_height() * this->get_tile_width();
        for (int w=0; w < this->getw(); w++)
        {
            tt_tensor t(new_tensor_shape, this->get_data_format());
            t.reserve_tile_tensor();
            t.tile_tensor[0] = tile_tensor[w];

            if (flat_tensor_data.size() > 0) {
                auto it = flat_tensor_data.begin() + w * flat_vector_size;
                t.fill_with_data(std::vector<float>(it, it + flat_vector_size));
            }  
            t.metadata.is_tilized = this->metadata.is_tilized;

            result.emplace_back(t);
        }
        return result;
    }

    uint32_t tt_tensor::getrt() const { return(metadata.shape.rt); }
    uint32_t tt_tensor::getct() const { return(metadata.shape.ct); }
    uint32_t tt_tensor::getrfull() const { return(metadata.shape.getrfull()); }
    uint32_t tt_tensor::getcfull() const { return(metadata.shape.getcfull()); }
    uint32_t tt_tensor::getz() const { return(metadata.shape.z); }
    uint32_t tt_tensor::getw() const { return(metadata.shape.w); }
    uint32_t tt_tensor::get_tile_height() const { return(metadata.shape.tile_height); }
    uint32_t tt_tensor::get_tile_width() const { return(metadata.shape.tile_width); }

    bool tt_tensor::is_shape_only() const { return this->tile_tensor.empty(); }

    int tt_tensor::get_num_stored_tiles() const
    {
        int result = 0;

        for (const auto& ztiles: this->tile_tensor)
        {
            for (const auto& rtiles: ztiles)
            {
                for (const auto& ctiles: rtiles)
                {
                    result += ctiles.size();
                }
            }
        }
        return result;
    }

    void tt_tensor::reserve_tile_tensor()
    {
        this->tile_tensor.resize(this->getw());
        for (size_t iw = 0; iw < this->tile_tensor.size(); iw++) {
            this->tile_tensor[iw].resize(this->getz());
            for (size_t iz = 0; iz < this->tile_tensor[iw].size(); iz++) {
                this->tile_tensor[iw][iz].resize(this->getrt());
                for (size_t ir = 0; ir < this->tile_tensor[iw][iz].size(); ir++) {
                    this->tile_tensor[iw][iz][ir].resize(this->getct());
                    for (size_t ic = 0; ic < this->tile_tensor[iw][iz][ir].size(); ic++) {
                        this->tile_tensor[iw][iz][ir][ic].set_data_format(this->metadata.data_format);
                        this->tile_tensor[iw][iz][ir][ic].tile_height = this->metadata.shape.tile_height;
                        this->tile_tensor[iw][iz][ir][ic].tile_width = this->metadata.shape.tile_width;
                    }
                }
            }
        }
    }

    tt_tensor::tile_data tt_tensor::get_tensor_tile_data_from_vector(
        tt_shape shape, DataFormat data_format, vector<vector<vector<vector<int>>>> &data_vector) {
        
        tt_tensor::tile_data result;
        
        using tt::constants::TILE_HEIGHT;
        using tt::constants::TILE_WIDTH;

        for(int iw=0;iw<shape.w;++iw)
        {
            vector <vector <vector <tt_tile> > > tmp_z;

            vector <vector <vector <int> > > data_vector_z = data_vector.at(iw);
            for(int iz=0;iz<shape.z;++iz)
            {
                vector <vector <tt_tile> > tmp_r;
                vector <vector <int> > data = data_vector_z.at(iz);
                for(int ri=0;ri<shape.rt;++ri)
                {
                    vector <tt_tile> tmp_c;
                    int tensor_row_start = ri * TILE_HEIGHT;
                    for(int ci=0;ci<shape.ct;++ci)
                    {
                        tt_tile tmp_tile(data_format);
                        int tensor_col_start = ci * TILE_WIDTH;
                        for (int row_pos = 0; row_pos < TILE_HEIGHT; row_pos++) {
                            for (int col_pos = 0; col_pos < TILE_WIDTH; col_pos++) {
                                tmp_tile.t[row_pos][col_pos] = 
                                    data.at(tensor_row_start + row_pos).at(tensor_col_start + col_pos);
                            }
                        }

                        tmp_tile.adjust_tile_for_accuracy();
                        tmp_c.push_back(tmp_tile);
                    }
                    tmp_r.push_back(tmp_c);
                }
                tmp_z.push_back(tmp_r);
            }
            result.push_back(tmp_z);
        }
        return result;
    }

    template <typename T, typename>
    tt_tensor::tile_data tt_tensor::get_tensor_tile_data_from_distribution(
        tt_shape shape, DataFormat data_format, std::function<void(tt_tile&)> tile_distribution_function) {
        tt_tensor::tile_data result;

        for(int iw=0;iw<shape.w;++iw)
        {
            vector <vector <vector <tt_tile> > > tmp_z;
            for(int iz=0;iz<shape.z;++iz)
            {
                vector <vector <tt_tile> > tmp_r;
                for(int ri=0;ri<shape.rt;++ri)
                {
                    vector <tt_tile> tmp_c;
                    for(int ci=0;ci<shape.ct;++ci)
                    {
                        tt_tile tmp_tile(data_format);
                        tile_distribution_function(tmp_tile);
                        tmp_tile.adjust_tile_for_accuracy();
                        tmp_c.push_back(tmp_tile);
                    }
                    tmp_r.push_back(tmp_c);
                }
                tmp_z.push_back(tmp_r);
            }
            result.push_back(tmp_z);
        }
        return result;
    }

    tt_tensor::tile_data tt_tensor::get_randomized_tensor_tile_data(tt_shape shape, DataFormat data_format, float mean, float stddev)
    {
        return get_tensor_tile_data_from_distribution<float>(
            shape, 
            data_format,
            [mean, stddev](tt_tile& tile) {
                tile.randomize(mean, stddev);
            }
        );
    }

    tt_tensor::tile_data tt_tensor::get_randomized_manual_float_tile_data(tt_shape shape, DataFormat data_format, int spread, int man_variance_bits)
    {
        return get_tensor_tile_data_from_distribution<int>(
            shape, 
            data_format,
            [spread, man_variance_bits](tt_tile& tile) {
                tile.randomize_manual_float(spread, man_variance_bits);
            }
        );
    }

    tt_tensor::tile_data tt_tensor::get_arange_tensor_tile_data(tt_shape shape, DataFormat data_format, float start)
    {
        float number = start;
        return get_tensor_tile_data_from_distribution<float>(
            shape,
            data_format,
            [&number](tt_tile& tile) {
                tile.set_number(number++);
            }
        );
    }

    tt_tensor::tile_data tt_tensor::get_randomized_uniform_float_tile_data(tt_shape shape, DataFormat data_format, float lower_bound, float upper_bound) {
        return get_tensor_tile_data_from_distribution<float>(
            shape,
            data_format,
            [lower_bound, upper_bound](tt_tile& tile) {
                tile.randomize_uniform(lower_bound, upper_bound);
            }
        );
    }

    tt_shape tt_tensor::get_tile_tensor_shape(const tt_tensor::tile_data& tile_tensor)
    {
        tt_shape result;
        result.w = tile_tensor.size();
        result.z = 0;
        result.rt = 0;
        result.ct = 0;

        if (result.w == 0) {
            return result;
        }

        unordered_set<size_t> zs;
        unordered_set<size_t> rts;
        unordered_set<size_t> cts;

        for (size_t iw = 0; iw < tile_tensor.size(); iw++) {
            zs.insert(tile_tensor[iw].size());
            for (size_t iz = 0; iz < tile_tensor[iw].size(); iz++) {
                rts.insert(tile_tensor[iw][iz].size());
                for (size_t ir = 0; ir < tile_tensor[iw][iz].size(); ir++) {
                    cts.insert(tile_tensor[iw][iz][ir].size());
                }
            }
        }

        log_assert(zs.size() == 1 ,  "Non-uniform z shape in tile_tensor");
        log_assert(rts.size() == 1 ,  "Non-uniform rt shape in tile_tensor");
        log_assert(cts.size() == 1 ,  "Non-uniform ct shape in tile_tensor");

        result.z = *begin(zs);
        result.rt = *begin(rts);
        result.ct = *begin(cts);

        return result;
    }

    void tt_tensor::fill_with_data(const tt_tensor& source_tensor, const tt_dims offset) {
        log_assert(
            this->metadata.shape.volume() >= source_tensor.metadata.shape.volume(),
            "Can only fill if the tensor is larger than or equal to source tensor");
        tt_shape final_bounds = {
            .rt = source_tensor.getrt() + offset.rt,
            .ct = source_tensor.getct() + offset.ct,
            .z = source_tensor.getz() + offset.z,
            .w = source_tensor.getw() + offset.w,
        };
        log_assert(
            (this->metadata.shape.w >= final_bounds.w) and 
            (this->metadata.shape.z >= final_bounds.z) and 
            (this->metadata.shape.rt >= final_bounds.rt) and 
            (this->metadata.shape.ct >= final_bounds.ct),
            "offset + source tensor cannot be out of bounds");

        for(int wi=0;wi<source_tensor.getw();++wi)
        {
            for(int zi=0;zi<source_tensor.getz();++zi)
            {
                for(int ri=0;ri<source_tensor.getrt();++ri)
                {
                    for(int ci=0;ci<source_tensor.getct();++ci)
                    {
                        this->tile_tensor[wi+offset.w][zi+offset.z][ri+offset.rt][ci+offset.ct] = source_tensor.tile_tensor[wi][zi][ri][ci];
                    }
                }
            }
        }
    }
    void tt_tensor::fill_with_data(const tt_tensor::tile_data& source_tile_tensor)
    {
        log_assert(
            this->metadata.shape == get_tile_tensor_shape(source_tile_tensor),
            "Can only fill with tensor that matches shape");

        // clear current storage
        this->tile_tensor.clear();

        for(int wi=0;wi<this->getw();++wi)
        {
            vector <vector <vector <tt_tile> > > tmp_z;
            for(int zi=0;zi<this->getz();++zi)
            {
                vector <vector <tt_tile> > tmp_r;
                for(int ri=0;ri<this->getrt();++ri)
                {
                    vector <tt_tile> tmp_c;
                    for(int ci=0;ci<this->getct();++ci)
                    {
                        tmp_c.push_back(source_tile_tensor[wi][zi][ri][ci]);
                    }
                    tmp_r.push_back(tmp_c);
                }
                tmp_z.push_back(tmp_r);
            }
            this->tile_tensor.push_back(tmp_z);
        }
    }

    void tt_tensor::fill_with_data(const vector<float>& source_data)
    {
        int shape_volume_full = this->metadata.shape.volume_full();
        int source_data_size = source_data.size();
        log_assert(
            shape_volume_full == source_data_size,
            "Can only fill with buffer with same volume as shape. Shape volume = {} Buffer volume = {}", std::to_string(shape_volume_full),  std::to_string(source_data_size));

        this->flat_tensor_data = source_data;
    }

    void tt_tensor::apply(
        function<void(tt_tile&)> tile_operation
    ) {
        // iterate through all tiles and invoke some tile operation
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        tile_operation(tile_tensor[wi][zi][ri][ci]);
                    }
                }
            }
        }
    }

    void tt_tensor::apply_parallel(function<void(tt_tile&)> tile_operation) {
        const int num_tiles = getw() * getz() * getrt() * getct();

        tt::parallel_for(
            0,
            num_tiles,
            [&](int tile_index) {
                const int wi = tile_index / (getz() * getrt() * getct());
                const int zi = (tile_index / (getrt() * getct())) % getz();
                const int ri = (tile_index / getct()) % getrt();
                const int ci = tile_index % getct();
                auto& tile = tile_tensor[wi][zi][ri][ci];
                tile_operation(tile);
            },
            tt::cpuset::get_allowed_num_threads());
    }

    void tt_tensor::pack_data(int tile_height, int tile_width) {
        // iterate through all tiles and invoke pack
        apply([tile_height, tile_width](tt_tile& tile) {
            tile.tile_height = tile_height;
            tile.tile_width = tile_width;
            tile.pack_data();
        });
    }

    void tt_tensor::clear_packed_data() {
        apply([](tt_tile& tile) {
            tile.clear_packed_data();
        });
    }

    void tt_tensor::clear_tile_values(int tile_dim_r, int tile_dim_c) {
        
        // potentialy early out
        if (tile_dim_r == tt::constants::TILE_HEIGHT && tile_dim_c == tt::constants::TILE_WIDTH) {
            return;
        }

        // Check the tensor is a matrix
        log_assert(getw() == 1, "Expected 3d tensor");

        for (int z = 0; z < getz(); ++z) {
            for (int r = 0; r < getrt(); ++r) {
                for (int c = 0; c < getct(); ++c) {
                    tile_tensor[0][z][r][c].clear_values(tile_dim_r, tile_dim_c);
                }
            }
        }
    }

    void tt_tensor::set_tile_dims(int tile_dim_r, int tile_dim_c) {
        metadata.shape.tile_height = tile_dim_r;
        metadata.shape.tile_width = tile_dim_c;

        if (tile_tensor[0][0][0][0].tile_width != tile_dim_r || tile_tensor[0][0][0][0].tile_height != tile_dim_c) {
            apply([&](tt_tile& tile) {
                tile.tile_height = tile_dim_r;
                tile.tile_width = tile_dim_c;
            });
        }
    }

    void tt_tensor::unpack_data() {
        // iterate through all tiles and invoke unpack
        apply([](tt_tile& tile) {
            tile.packed_data_to_tile();
        });
    }

    bool tt_tensor::packed_data_present() {
        // iterate through all tiles and invoke some tile operation
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        if (tile_tensor[wi][zi][ri][ci].packed_data_present()) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    /*
        Expected to be mainly used for kernel/unit-test level verification
        
        Ideally we would also have a way to do intermediate computation
        at specified precision, but this should be able to capture input/output
        conversion loss
    */
    void tt_tensor::adjust_tensor_for_accuracy(bool truncate_bfp_mantissa) {
        // this assumes tt_tile holds data using float
        if(DataFormat::Float32 == this->metadata.data_format){return;}
        // iterate through all tiles and perform target data conversion and back

        apply_parallel([truncate_bfp_mantissa](tt_tile& tile) {
            tile.adjust_tile_for_accuracy(truncate_bfp_mantissa);
        });
    }

    bool tt_tensor::check_for_max_abs_value(float max_value) {
        bool max_value_exceeded = false;
        apply([&max_value_exceeded, max_value](tt_tile& tile) {
            for (int i = 0; i < tt::constants::TILE_HEIGHT; i++) {
                for (int j = 0; j < tt::constants::TILE_WIDTH; j++) {
                    if (std::abs(tile.t[i][j]) > max_value) {
                        max_value_exceeded = true;
                    }
                }
            }
        });
        return max_value_exceeded;
    }

    void tt_tensor::adjust_man_prec(uint32_t man_prec) {
        apply([man_prec](tt_tile& tile) {
            tile.adjust_man_precision(man_prec);
        });
    }

    void tt_tensor::randomize_per_col_mask(
        int spread, int man_variance_bits, vector<vector<vector<bool>>> &tensor_col_masks) {
        log_assert(tensor_col_masks.size() == getrt(), "Invalid col masks size");
        this->reserve_tile_tensor();

        // Not using 'apply' method here for readability 
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    log_assert(tensor_col_masks[ri].size() == getct(), "Invalid col masks size");
                    for(int ci=0;ci<getct();++ci)
                    {
                        tile_tensor[wi][zi][ri][ci].randomize_per_col_mask(
                            spread, man_variance_bits, tensor_col_masks[ri][ci]);
                    }
                }
            }
        }
    }

    void tt_tensor::randomize(float mean, float stddev) {
        this->reserve_tile_tensor();

        apply([mean, stddev](tt_tile& tile) {
            tile.randomize(mean, stddev);
        });
    }

    void tt_tensor::randomize_manual_float(int spread, int man_variance_bits) {
        this->reserve_tile_tensor();

        apply([spread, man_variance_bits](tt_tile& tile) {
            tile.randomize_manual_float(spread, man_variance_bits);
        });
    }

    void tt_tensor::randomize_uniform(float lower_bound, float upper_bound) {
        this->reserve_tile_tensor();

        apply([lower_bound, upper_bound](tt_tile& tile) {
            tile.randomize_uniform(lower_bound, upper_bound);
        });
    }

    void tt_tensor::randomize_diagonal(float mean, float stddev) {
        this->reserve_tile_tensor();

        apply([mean, stddev](tt_tile& tile) {
           tile.randomize_diagonal(mean, stddev);
        });
    }

    void tt_tensor::set_number(float num) {
        this->reserve_tile_tensor();

        apply([num](tt_tile& tile) {
            tile.set_number(num);
        });
    }

    void tt_tensor::set_data_format(DataFormat data_format_)
    {
        if (data_format_ == DataFormat::Invalid) {
            return;
        }

        this->metadata.data_format = data_format_;
        if (tile_tensor.size() == 0) { return; }
 
        apply([data_format_](tt_tile& tile) {
            tile.set_data_format(data_format_);
        });
        return;
    }

    void tt_tensor::set_gradient(tt_tensor* gradient) {
        this->metadata.gradient = gradient;
    }

    tt_tensor* tt_tensor::get_gradient() {
        return this->metadata.gradient;
    }

    void tt_tensor::set_transposed_parameter_copy(tt_tensor* transposed_copy) {
        this->metadata.transposed_parameter_copy = transposed_copy;
    }

    tt_tensor* tt_tensor::get_transposed_parameter_copy() {
        return this->metadata.transposed_parameter_copy;
    }

    tt_tensor tt_tensor::matmul(const tt_tensor &rhs, bool shape_only, bool fast) const
    {
        // Check the tensor is a matrix
        // don't have broadcast semantics for now
        // log_assert(rhs.getz() == 1);
        log_assert(rhs.getw() == 1, "Expected w dim to be 1 for rhs");
        // log_assert(getz() == 1);
        log_assert(getw() == 1, "Expected w dim to be 1 for rhs");
        // Check dimensions are compatible
        log_assert(getct() == rhs.getrt(), "Expected dim r for rhs to match dim c for lhs");

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = rhs.getct(),
                .z = getz(),
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = rhs.get_tile_width()},
            this->get_data_format());

        if (shape_only) {
            return result;
        }

        result.reserve_tile_tensor();
        int z, i, j, k;

        for (z = 0; z < getz(); ++z)
        {
            for (i = 0; i < getrt(); ++i)
            {
                int j, k;
                for (j = 0; j < rhs.getct(); ++j)
                {
                    result.tile_tensor[0][z][i][j] = 0.0;
                    for (k = 0; k < getct(); ++k) {
                        if (fast) {
                            tile_tensor[0][z][i][k].matmul_with_partial(rhs.tile_tensor[0][z][k][j], result.tile_tensor[0][z][i][j]);
                        } else {
                            result.tile_tensor[0][z][i][j] += tile_tensor[0][z][i][k].matmul(rhs.tile_tensor[0][z][k][j]);
                        }
                    }
                }
            }
        }
        return(result);
    }

    tt_tensor tt_tensor::batched_matmul(tt_tensor &rhs, bool shape_only) const
    {
        // Check the tensor is a matrix
        log_assert(rhs.getw() == 1, "Expected w dim to be 1 for rhs");
        log_assert(getw() == 1, "Expected w dim to be 1 for lhs");

        // Check dimensions are compatible
        log_assert(getct() == rhs.getrt(),  "Expected dim r for rhs to match dim c for lhs");
        log_assert(getz() == rhs.getz(), "Expected z dims to match");

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = rhs.getct(),
                .z = getz(),
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = rhs.get_tile_width()},
            this->get_data_format());

        if (shape_only) {
            return result;
        }

        result.reserve_tile_tensor();

        int input_inner_dim = getct();

        for (int b = 0; b < getz(); ++b)
        {
            for (int i = 0; i < getrt(); ++i)
            {
                for (int j = 0; j < rhs.getct(); ++j)
                {
                    result.tile_tensor[0][b][i][j] = 0.0;
                    for (int k = 0; k < getct(); ++k)
                    {
                        result.tile_tensor[0][b][i][j] += tile_tensor[0][b][i][k].matmul(rhs.tile_tensor[0][b][k][j]);
                    }
                }
            }
        }
        return result;
    }

    // Standard matmul, except weights can have more than one Z, and activations are reused
    // on each set of Z weights to produce Z outputs.
    // Typical use for this is a decomposed NxN convolution, where some number of 1x1 convolutions
    // can be performed together on the same core. Activations are common, but weights are coming from
    // different factors of the original NxN kernel.
    tt_tensor tt_tensor::conv1x1_matmul(tt_tensor &rhs, bool shape_only) const
    {
        // Check dimensions are compatible
        log_assert(rhs.getw() == 1, "expected rhs w = 1");
        log_assert(getw() == 1, "expected lhs w = 1");
        log_assert(getct() == rhs.getrt(), "expected lhs c to match rhs r");
        log_assert(getz() == 1, "expected lhs z = 1");

        int input_inner_dim = getct();
        cout << "conv1x1_matmul: c, input_inner_dim = " << getct() << ", " << input_inner_dim << ", batch=" << rhs.getz() << endl;

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = rhs.getct(),
                .z = rhs.getz(),
                .w = 1,
                .tile_height = get_tile_height(),
                .tile_width = rhs.get_tile_width()},
            this->get_data_format());

        if (shape_only) {
            return result;
        }

        result.reserve_tile_tensor();

        tt_tile acc(this->get_data_format());

        for (int b = 0; b < rhs.getz(); ++b)
        {
            for (int i = 0; i < getrt(); ++i)
            {
                for (int j = 0; j < rhs.getct(); ++j)
                {
                    acc = 0.0;
                    for (int k = 0; k < getct(); ++k)
                    {
                        acc += tile_tensor[0][0][i][k].matmul(rhs.tile_tensor[0][b][k][j]);
                    }
                    result.tile_tensor[0][b][i][j] = acc;
                }
            }
        }
        /*cout << "tt_tensor math:" << endl;
        cout << "activations:" << endl;
        dump();
        cout << "weights:" << endl;
        rhs.dump();
        cout << "tt_tensor:" << endl;
        tensor.dump();*/
        return result;
    }

    // Similar parameters to the conv1x1 above, except weights are only 1 row high (all others are zeros and discarded offline). 
    // If activations have more than one column (which would be common), the inner loop is one iteration and picks 
    // activations[weight_column] to multiply with. 
    tt_tensor tt_tensor::depthwise1x1_matmul(tt_tensor &rhs, bool shape_only) const
    {
        // Check dimensions are compatible
        log_assert(rhs.getw() == 1, "Expected w dim to be 1 for rhs");
        log_assert(getw() == 1, "Expected w dim to be 1 for lhs");
        log_assert(rhs.getrt() == 1, "Expected r dim to be 1 for rhs");
        log_assert(getz() == 1, "Expected z dim to be 1 for lhs");

        cout << "depthwise1x1_matmul: c=" << getct() << ", " << ", batch=" << rhs.getz() << endl;

        tt_tile acc(this->get_data_format());
        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = rhs.getct(),
                .z = rhs.getz(),
                .w = 1,
                .tile_height = get_tile_height(),
                .tile_width = rhs.get_tile_width()},
            this->get_data_format());

        if (shape_only)
            return result;

        result.reserve_tile_tensor();

        for (int b = 0; b < rhs.getz(); ++b)
        {
            for (int i = 0; i < getrt(); ++i)
            {
                for (int j = 0; j < rhs.getct(); ++j)
                {
                    result.tile_tensor[0][b][i][j] = tile_tensor[0][0][i][j].matmul(rhs.tile_tensor[0][b][0][j]);
                }
            }
        }
        /*std::cout << "tt_tensor depthwise math:" << std::endl;
        std::cout << "activations:" << std::endl;
        dump();
        std::cout << "weights:" << std::endl;
        rhs.dump();
        std::cout << "tt_tensor:" << std::endl;
        tensor.dump();*/
        return result;
    }

    // change an hstacked layout to z-dim layout
    tt_tensor tt_tensor::reshape_c_dim_into_z_dim_and_c_dim(uint32_t z_scaler, bool shape_only) const
    {
        // Check the tensor is a matrix
        //log_assert(getz() == 1);
        log_assert(getw() == 1, "Expected w dim to be 1");

        // compute input inner dim
        log_assert(getct() % z_scaler == 0, "Expected c dim to be divisible by z");
        uint32_t target_c = getct() / z_scaler;
        uint32_t target_z = getz() * z_scaler;

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = target_c,
                .z = target_z,
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            this->get_data_format());

        if (shape_only) {
            return result;
        }

       result.reserve_tile_tensor();

       for(int zi=0; zi<getz(); ++zi)
       {
           for(int zs=0; zs<z_scaler; ++zs)
           {
               for(int i=0;i<getrt();++i)
               {
                   for(int j=0; j<target_c;++j)
                   {
                       result.tile_tensor[0][zs+zi*z_scaler][i][j] = tile_tensor[0][zi][i][j+zs*target_c];
                   }
               }
           }
        }     
        return(result);
    }

    // change an hstacked layout to z-dim layout
    tt_tensor tt_tensor::reshape_r_dim_into_z_dim_and_r_dim(uint32_t z_scaler, bool shape_only) const
    {
        // Check the tensor is a matrix
        log_assert(getw() == 1, "Expected w dim to be 1");

        // compute input inner dim
        log_assert(getrt() % z_scaler == 0, "Expected r dim to be divisible by z");
        uint32_t target_r = getrt() / z_scaler;
        uint32_t target_z = getz() * z_scaler;

        tt_tensor result(
            tt_shape{
                .rt = target_r,
                .ct = getct(),
                .z = target_z,
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            this->get_data_format());

        if (shape_only) {
            return result;
        }

       result.reserve_tile_tensor();

       for(int zi=0; zi<getz(); ++zi)
       {
           for(int zs=0; zs<z_scaler; ++zs)
           {
               for(int i=0;i<target_r;++i)
               {
                   for(int j=0; j<getct();++j)
                   {
                       result.tile_tensor[0][zs+zi*z_scaler][i][j] = tile_tensor[0][zi][i+zs*target_r][j];
                   }
               }
           }
        }    
        return result;
    }

    // Take z-dim of the tensor and flatten by hstacking, i.e., concatente in the horizontal direction (concat 2D tensors along col dim, side-by-side)
    tt_tensor tt_tensor::reshape_z_dim_into_c_dim(uint32_t z_scaler, bool shape_only) const
    {
        if (z_scaler>0) {
           log_assert(getz() % z_scaler == 0, "Expected z dim to be divisible by z");
        }

        uint32_t target_z = z_scaler==0 ? 1 : getz()/z_scaler;
        if (z_scaler == 0) z_scaler=getz();
        uint32_t target_c = getct()*z_scaler;

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = target_c,
                .z = target_z,
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            this->get_data_format());

        if (shape_only) {
            return result;
        }

        result.reserve_tile_tensor();

        for (int wi = 0; wi < getw(); ++wi)
        {
            for (int zo = 0; zo < target_z; ++zo)
            {
                for (int i = 0; i < getrt(); ++i)
                {
                    for (int zs = 0; zs < z_scaler; ++zs)
                    {
                        for (int j = 0; j < getct(); ++j)
                        {
                            result.tile_tensor[wi][zo][i][j + zs * getct()] = tile_tensor[wi][zs + zo * z_scaler][i][j];
                        }
                    }
                }
            }
        }
        return(result);

    }

    tt_tensor tt_tensor::reshape_z_dim_into_r_dim(uint32_t z_scaler, bool shape_only) const
    {
        // compute input inner dim
        log_assert(getz() % z_scaler == 0, "Expected z dim to be divisible by z");
        uint32_t target_r = getrt() * z_scaler;
        uint32_t target_z = getz() / z_scaler;

        tt_tensor result(
            tt_shape{
                .rt = target_r,
                .ct = getct(),
                .z = target_z,
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            this->get_data_format());

        if (shape_only) {
            return result;
        }

        result.reserve_tile_tensor();

        for (int wi = 0; wi < getw(); ++wi)
        {
            for (int zo = 0; zo < target_z; ++zo)
            {
                for (int j = 0; j < getct(); ++j)
                {
                    for (int zs = 0; zs < z_scaler; ++zs)
                    {
                        for (int i = 0; i < getrt(); ++i)
                        {
                            result.tile_tensor[wi][zo][i + zs * getrt()][j] = tile_tensor[wi][zs + zo * z_scaler][i][j];
                        }
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::reduce(ReduceFunc reduce_func, Dim dim, bool shape_only, uint32_t z_dim) const
    {

        log_assert(getw() == 1 ,  "Error: w!=1 is not supported");

        float coefficient;
        float init_value;
        if (reduce_func == ReduceFunc::Avg) {
            init_value = 0.0f;
            if (dim == Dim::C) {
                coefficient = 1.0f / this->getcfull();
            } else if (dim == Dim::R) {
                coefficient = 1.0f / this->getrfull();
            } else if (dim == Dim::RC) {
                coefficient = 1.0f / (this->getrfull() * this->getcfull());
            } else if (dim == Dim::ZR) {
                coefficient = 1.0f / (this->getz() * this->getrfull());
            } else {
                log_assert(false ,  "Unsupported ReduceDim");
            }
        } else if (reduce_func == ReduceFunc::Sum) {
            init_value = 0.0f;
            coefficient = 1.0f;
        } else if (reduce_func == ReduceFunc::Max) {
            init_value = -std::numeric_limits<float>::infinity();
            coefficient = 1.0f;
        } else {
            throw runtime_error("Unrecognized ReduceFunc");
        }

        const auto reduce_func_call = [reduce_func](const tt_tile& acc, const tt_tile& other)
        {
            if (reduce_func == ReduceFunc::Avg or reduce_func == ReduceFunc::Sum)
            {
                return acc + other;
            }
            else if (reduce_func == ReduceFunc::Max)
            {
                return acc.max(other);
            }
            else
            {
                log_fatal("Unsupported Reduce func");
            }
        };

        int batch_size = getz();

        if (dim == Dim::C) {
            uint32_t out_rt = getrt();
            uint32_t out_ct = 1; // we reduce along the row, so there is only a single tile in the column dim
            uint32_t out_z = batch_size;
            uint32_t out_w = getw();

            tt_tensor result(
                tt_shape{
                    .rt = out_rt,
                    .ct = out_ct,
                    .z = out_z,
                    .w = out_w,
                    .tile_height = get_tile_height(),
                    .tile_width = get_tile_width()},
                this->get_data_format());

            if (shape_only)
            {
                return result;
            }

            result.reserve_tile_tensor();

            tt_tile acc(this->get_data_format());

            for(int b = 0; b < batch_size; ++b)
            {
                for(int i = 0; i < getrt(); ++i)
                {
                    acc = init_value;
                    for(int j = 0; j < getct(); ++j)
                    {
                        acc = reduce_func_call(acc, tile_tensor[0][b][i][j].reduce(reduce_func, dim, coefficient));
                    }
                    result.tile_tensor[0][b][i][0] = acc;
                }
            }
            return result;
        } else if (dim == Dim::R) {
            uint32_t out_rt = 1;
            uint32_t out_ct = getct(); // we reduce along the row, so there is only a single tile in the column dim
            uint32_t out_z = batch_size;
            uint32_t out_w = getw();

            tt_tensor result(
                tt_shape{
                    .rt = out_rt,
                    .ct = out_ct,
                    .z = out_z,
                    .w = out_w,
                    .tile_height = get_tile_height(),
                    .tile_width = get_tile_width()},
                this->get_data_format());

            if (shape_only)
            {
                return result;
            }

            result.reserve_tile_tensor();

            tt_tile acc(this->get_data_format());

            for(int b = 0; b < batch_size; ++b)
            {
                for(int j = 0; j < getct(); ++j)
                {
                    acc = init_value;
                    for(int i = 0; i < getrt(); ++i)
                    {
                        acc = reduce_func_call(acc, tile_tensor[0][b][i][j].reduce(reduce_func, dim, coefficient));
                    }
                    result.tile_tensor[0][b][0][j] = acc;
                }
            }
            return result;
        } else if (dim == Dim::RC) {
            uint32_t out_rt = 1;
            uint32_t out_ct = 1; // we reduce along the row, so there is only a single tile in the column dim
            uint32_t out_z = batch_size;
            uint32_t out_w = getw();

            tt_tensor result(
                tt_shape{
                    .rt = out_rt,
                    .ct = out_ct,
                    .z = out_z,
                    .w = out_w,
                    .tile_height = get_tile_height(),
                    .tile_width = get_tile_width()},
                this->get_data_format());

            if (shape_only)
            {
                return result;
            }

            result.reserve_tile_tensor();

            tt_tile acc(this->get_data_format());

            for(int b = 0; b < batch_size; ++b)
            {
                acc = init_value;
                for(int i = 0; i < getrt(); ++i)
                {
                    for(int j = 0; j < getct(); ++j)
                    {
                        acc = reduce_func_call(acc, tile_tensor[0][b][i][j].reduce(reduce_func, dim, coefficient));
                    }
                }
                result.tile_tensor[0][b][0][0] = acc;
            }
            return result;
        } else if (dim == Dim::ZR) {
            uint32_t out_rt = 1;
            uint32_t out_ct = getct(); // we reduce along the row, so there is only a single tile in the column dim
            uint32_t out_z = 1;
            uint32_t out_w = getw();

            tt_tensor result(
                tt_shape{
                    .rt = out_rt,
                    .ct = out_ct,
                    .z = out_z,
                    .w = out_w,
                    .tile_height = get_tile_height(),
                    .tile_width = get_tile_width()},
                this->get_data_format());

            if (shape_only)
            {
                return result;
            }

            result.reserve_tile_tensor();

            tt_tile acc(this->get_data_format());

            for(int b = 0; b < batch_size; ++b)
            {
                for(int j = 0; j < getct(); ++j)
                {
                    acc = init_value;
                    for(int i = 0; i < getrt(); ++i)
                    {
                        acc = reduce_func_call(acc, tile_tensor[0][b][i][j].reduce(reduce_func, Dim::R, coefficient));
                    }
                    result.tile_tensor[0][0][0][j] += acc;
                    result.tile_tensor[0][0][0][j].data_format = acc.data_format;
                }
            }
            return result;
        } else if (dim == Dim::Z) {
            uint32_t out_rt = getrt();
            uint32_t out_ct = getct(); 
            log_assert(getz() % z_dim == 0, "Expected z dim to be divisible by z");
            uint32_t out_z = getz()/z_dim;
            uint32_t out_w = getw();
            batch_size = out_z;

            tt_tensor result(
                tt_shape{
                    .rt = out_rt,
                    .ct = out_ct,
                    .z = out_z,
                    .w = out_w,
                    .tile_height = get_tile_height(),
                    .tile_width = get_tile_width()},
                this->get_data_format());

            if (shape_only)
            {
                return result;
            }

            result.reserve_tile_tensor();

            tt_tile acc(this->get_data_format());

            for(int b = 0; b < batch_size; ++b) 
            {
               for(int i = 0; i < getrt(); ++i)
               {
                   for(int j = 0; j < getct(); ++j)
                   {
                       for(int z = 0; z < z_dim; ++z)
                       {
                           if (0 == z) {
                               acc = tile_tensor[0][b*z_dim+z][i][j];
                           } else {
                                if (reduce_func == ReduceFunc::Max) {
                                    acc = tile_tensor[0][b*z_dim+z][i][j].max(acc);
                                } else {
                                    acc += tile_tensor[0][b*z_dim+z][i][j];
                                }
                           }
                       }
                       result.tile_tensor[0][b][i][j] = acc;
                   }
               }
            }   
            return result;
        }
        throw runtime_error("Unsupported dim");
    }

    tt_tensor tt_tensor::subtract(const tt_tensor &rhs, bool shape_only) const
    {
        // Check dimensions are compatible
        log_assert(this->get_shape() == rhs.get_shape(), "Expected shaoes to match");

        tt_tensor result(get_shape(), this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri][ci] - rhs.tile_tensor[wi][zi][ri][ci];
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::operator - (const tt_tensor &rhs) const
    {
        bool shape_only = false;
        return subtract(rhs, shape_only);
    }

    // tranpose_xy entire xy plane of the tensor (i.e., global)
    tt_tensor tt_tensor::transpose_xy(bool shape_only, bool tiles_only, bool tile_only) const
    {
        bool requires_grad = false;
        // swap (r, c) --> (c, r)
        tt_tensor result(
            tt_shape{
                .rt = tile_only ? getrt() : getct(),
                .ct = tile_only ? getct() : getrt(),
                .z = getz(),
                .w = getw(),
                .tile_height = tile_only ? get_tile_height() : get_tile_width(),
                .tile_width = tile_only ? get_tile_width() : get_tile_height()},
            this->get_data_format(),
            this->metadata.tensor_type,
            requires_grad);

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        // traverse the original tensor
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        if (tiles_only) {
                           // new(c,r) = original(r,c)
                           result.tile_tensor[wi][zi][ci][ri] = tile_tensor[wi][zi][ri][ci]; // Transpose tiles only
                        } else if (tile_only) {
                           // new(r,c) = original(r,c).transpose_xy();
                           result.tile_tensor[wi][zi][ri][ci] = tile_tensor[wi][zi][ri][ci].transpose_xy(); // Transpose tile only
                        } else {
                           // new(c,r) = original(r,c).transpose_xy()
                           result.tile_tensor[wi][zi][ci][ri] = tile_tensor[wi][zi][ri][ci].transpose_xy();
                        }   
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::transpose_yz(bool shape_only) const
    {
        // swap (r, z) --> (z, r)
        tt_tensor result(
            tt_shape{
                .rt = (uint32_t)ceil((float)getz() / tt::constants::TILE_HEIGHT),
                .ct = getct(),
                .z = (uint32_t)(getrt() * tt::constants::TILE_HEIGHT),
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        // traverse the original tensor
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt() * tt::constants::TILE_HEIGHT;++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        // new(z,r) = original(r,z)
                        const int zo_tile = int(zi / tt::constants::TILE_HEIGHT);
                        const int ro = zi % tt::constants::TILE_HEIGHT;
                        const int ri_tile = int(ri / tt::constants::TILE_HEIGHT);
                        const int row = ri % tt::constants::TILE_HEIGHT;
                        memcpy(
                            result.tile_tensor[wi][ri][zo_tile][ci].t[ro],
                            tile_tensor[wi][zi][ri_tile][ci].t[row],
                            sizeof(result.tile_tensor[wi][ri][zo_tile][ci].t[row]));
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::shift_y(int amount, bool shape_only) const
    {
        if (amount == 0)
            return this->copy(shape_only);

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = getct(),
                .z = getz(),
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        // traverse the original tensor
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        const tt_tile &tile0 = (amount > 0) ?
                            (ri == 0) ? tt_tile::zero_tile(get_data_format()) : tile_tensor[wi][zi][ri-1][ci] :
                            tile_tensor[wi][zi][ri][ci];
                        const tt_tile &tile1 = (amount < 0) ?
                            (ri == getrt()-1) ? tt_tile::zero_tile(get_data_format()) : tile_tensor[wi][zi][ri+1][ci] :
                            tile_tensor[wi][zi][ri][ci];

                        tt_tile t;
                        t.set_data_format(this->get_data_format());
                        int offset = (amount > 0) ? tt::constants::TILE_HEIGHT - amount : -amount;

                        for (int i=0; i < tt::constants::TILE_HEIGHT; i++)
                        {
                            const tt_tile &src_tile = ((i + offset) >= tt::constants::TILE_HEIGHT) ? tile1 : tile0;
                            memcpy(t.t[i], src_tile.t[ (i+offset) % tt::constants::TILE_HEIGHT ], sizeof(t.t[i]));
                        }
                        result.tile_tensor[wi][zi][ri][ci] = t;
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::shift_z(int amount, bool shape_only) const
    {
        if (amount == 0)
            return *this;

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = getct(),
                .z = getz(),
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        // traverse the original tensor
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        int dest_z = zi + amount;
                        if (dest_z < 0) {
                            result.tile_tensor[wi][dest_z + getz()][ri][ci] = tt_tile::zero_tile(this->get_data_format());
                        }
                        else if (dest_z >= getz()) {
                            result.tile_tensor[wi][dest_z - getz()][ri][ci] = tt_tile::zero_tile(this->get_data_format());
                        }
                        else  {
                            result.tile_tensor[wi][dest_z][ri][ci] = tile_tensor[wi][zi][ri][ci];
                        }
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::stride_y(int stride, int stride_offset, bool shape_only) const
    {
        if (stride == 1)
            return *this;

        uint32_t new_rows = ceil((float)getrt() * tt::constants::TILE_HEIGHT / stride);
        uint32_t new_rt = ceil((float)new_rows / tt::constants::TILE_HEIGHT);

        tt_tensor result(
            tt_shape{
                .rt = new_rt,
                .ct = getct(),
                .z = getz(),
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            get_data_format());

        if (shape_only)
        {
            cout << "------------------------------- in stride y, shapeonly";
            cout << ", result shape = " << result.get_shape();
            cout << "\n";
            return result;
        }

        result.reserve_tile_tensor();

        result.set_number(0.0); // padding

        // traverse the original tensor
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                int wrptr_tile = 0;
                int wrptr_row = 0;
                for(int ri=stride_offset; ri<getrt() * tt::constants::TILE_HEIGHT; ri += stride)
                {
                    int rdptr_tile = ri / tt::constants::TILE_HEIGHT;
                    int rdptr_row = ri % tt::constants::TILE_HEIGHT;

                    for(int ci=0;ci<getct();++ci)
                    {
                        memcpy(
                                result.tile_tensor[wi][zi][wrptr_tile][ci].t[wrptr_row],
                                tile_tensor[wi][zi][rdptr_tile][ci].t[rdptr_row],
                                sizeof(tile_tensor[0][0][0][0].t[0]));
                    }
                    wrptr_row++;
                    if (wrptr_row >= tt::constants::TILE_HEIGHT)
                    {
                        wrptr_row = 0;
                        wrptr_tile ++;
                    }
                }
            }
        }
        cout << "------------------------------- in stride y";
        cout << ", result shape = " << result.get_shape();
        cout << "\n";
        return result;
    }

    tt_tensor tt_tensor::stride_z(int stride, int stride_offset, bool shape_only) const
    {
        if (stride == 1)
            return *this;

        uint32_t new_z = ceil((float)getz() / stride);

        tt_tensor result(
            tt_shape{
                .rt = getrt(),
                .ct = getct(),
                .z = new_z,
                .w = getw(),
                .tile_height = get_tile_height(),
                .tile_width = get_tile_width()},
            get_data_format());

        if (shape_only)
        {
            cout << "------------------------------- in stride z, shapeonly";
            cout << ", result shape = " << result.get_shape();
            cout << "\n";
            return result;
        }

        result.reserve_tile_tensor();
        result.set_number(0.0); // padding

        // traverse the original tensor
        for(int wi=0;wi<getw();++wi)
        {
            int wrptr = 0;
            for(int zi=stride_offset;zi<getz(); zi += stride)
            {
                result.tile_tensor[wi][wrptr++] = tile_tensor[wi][zi];
            }
        }

        cout << "------------------------------- in stride z";
        cout << ", result shape = " << result.get_shape();
        cout << "\n";
        return result;
    }

    tt_tensor tt_tensor::add(const tt_tensor &rhs, bool shape_only) const
    {
        // Check dimensions are compatible
        log_assert(this->get_shape() == rhs.get_shape(),  "Expected shaoes to match");

        tt_tensor result(get_shape(), this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri][ci] + rhs.tile_tensor[wi][zi][ri][ci];
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::operator + (const tt_tensor &rhs) const
    {
        bool shape_only = false;
        return add(rhs, shape_only);
    }

    tt_tensor tt_tensor::multiply(const tt_tensor &rhs, bool shape_only) const
    {
        // Check dimensions are compatible
        log_assert(this->get_shape() == rhs.get_shape(),  "Expected shaoes to match");

        tt_tensor result(get_shape(), this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri][ci] * rhs.tile_tensor[wi][zi][ri][ci];
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::operator * (const tt_tensor &rhs) const
    {
        bool shape_only = false;
        return multiply(rhs, shape_only);
    }


    tt_tensor tt_tensor::unary(function<tt_tile(const tt_tile&)> func, bool shape_only) const
    {
        tt_tensor result(get_shape(), this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();


        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = func(tile_tensor[wi][zi][ri][ci]);
                    }
                }
            }
        }
        return result;
    }

    tt_tensor tt_tensor::exp(bool shape_only) const
    {
        return this->unary(tt::math::exp, shape_only);
    }

    tt_tensor tt_tensor::sine(bool shape_only) const
    {
        return this->unary(tt::math::sine, shape_only);
    }

    tt_tensor tt_tensor::cosine(bool shape_only) const
    {
        return this->unary(tt::math::cosine, shape_only);
    }

    tt_tensor tt_tensor::log(bool shape_only) const
    {
        return this->unary(tt::math::log, shape_only);
    }

    tt_tensor tt_tensor::sqrt(bool shape_only) const
    {
        return this->unary(tt::math::sqrt, shape_only);
    }

    tt_tensor tt_tensor::abs(bool shape_only) const
    {
        return this->unary(tt::math::abs, shape_only);
    }

    tt_tensor tt_tensor::sigmoid(bool shape_only) const
    {
        return this->unary(tt::math::sigmoid, shape_only);
    }

    tt_tensor tt_tensor::gelu(bool shape_only) const
    {
        return this->unary(tt::math::gelu, shape_only);
    }

    tt_tensor tt_tensor::relu(bool shape_only) const
    {
        return this->unary(tt::math::relu, shape_only);
    }

    tt_tensor tt_tensor::lrelu(bool shape_only, float slope) const
    {
        return this->unary([slope](tt_tile tile) { return tt::math::lrelu(tile, slope); }, shape_only);
    }

    tt_tensor tt_tensor::relu_with_threshold(bool shape_only, float threshold, ReluMode mode) const
    {
        return this->unary([threshold, mode](tt_tile tile) { return tt::math::relu_with_threshold(tile, threshold, mode); }, shape_only);
    }

    tt_tensor tt_tensor::gelu_derivative(bool shape_only) const
    {
        return this->unary(tt::math::gelu_derivative, shape_only);
    }

    tt_tensor tt_tensor::reciprocal(bool shape_only) const
    {
        return this->unary(tt::math::reciprocal, shape_only);
    }

    tt_tensor tt_tensor::tanh(bool shape_only) const
    {
        return this->unary(tt::math::tanh, shape_only);
    }

    tt_tensor tt_tensor::dropout(bool shape_only, float prob) const
    {
        // Check dimensions are compatible
        tt_tensor result(get_shape(), this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = tt::math::dropout(this->tile_tensor[wi][zi][ri][ci], prob);
                    }
                }
            }
        }
        return result;
    }

    void tt_tensor::dump() const
    {
        bool is_raw_format = this->get_data_format() == DataFormat::RawUInt8 or
                             this->get_data_format() == DataFormat::RawUInt16 or
                             this->get_data_format() == DataFormat::RawUInt32;
        cout << "Tensor with shape " << get_shape() << endl;

        int i,j,k;
        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrt();++ri)
                {
                    for(int ci=0;ci<getct();++ci)
                    {
                        cout << "r=" << ri << ", c=" << ci <<", z=" << zi << ", w=" << wi << endl;
                        tile_tensor[wi][zi][ri][ci].dump(is_raw_format);
                        cout << endl;
                    }
                }
            }
        }
    }

    void tt_tensor::dump(const string &file_name) const
    {

        ofstream file (file_name);
        log_assert (file.is_open(), "Error opening tensor dump file");

        file << getw() << " " << getz() << " " << getrfull() << " " << getcfull() << endl;

        for(int wi=0;wi<getw();++wi)
        {
            for(int zi=0;zi<getz();++zi)
            {
                for(int ri=0;ri<getrfull();++ri)
                {
                    for(int ci=0;ci<getcfull() ;++ci)
                    {
                        file << tile_tensor[wi][zi][ri / tt::constants::TILE_HEIGHT][ci / tt::constants::TILE_WIDTH].t[ri % tt::constants::TILE_HEIGHT][ci % tt::constants::TILE_WIDTH] << " ";
                    }
                }
            }
        }
        file << endl;
        file.close();
    }

    string tt_tensor::get_string() const
    {

        std::stringstream ss;

        if (std::getenv("TT_ENABLE_GOLDEN_FULL_TENSOR_TRACE") != nullptr) {
            ss << "Tensor Details: " << endl;
            ss << "[w=" << getw() << ", z=" << getz() << ", r=" << getrfull() << ", c=" << getcfull() << "]" << endl;
            for(int wi=0;wi<getw();++wi)
            {
                for(int zi=0;zi<getz();++zi)
                {
                    for(int ri=0;ri<getrt();++ri)
                    {
                        for(int ci=0;ci<getct();++ci)
                        {
                            ss << "r=" << ri << ", c=" << ci <<", z=" << zi << ", w=" << wi << endl;
                            ss << tile_tensor[wi][zi][ri][ci].get_string();
                            ss << endl;
                        }
                    }
                }
            }
        } else {
            ss << "Tensor Brief Summary: " << endl;
            ss << "[w=" << getw() << ", z=" << getz() << ", r=" << getrfull() << ", c=" << getcfull() << "]" << endl;
        }
        return ss.str();
    }

    tt_tensor tt_tensor::broadcast(const tt_shape& shape, Dim dim, bool shape_only) const
    {
    // Check dimensions are compatible
    log_assert(getw() == shape.w or getw() == 1, "Expected w dim to be 1 or w dims to match");
    //log_assert(getz() == shape.z or getz() == 1);

    tt_tensor result(shape, this->get_data_format());

    if (shape_only)
    {
        return result;
    }

    result.reserve_tile_tensor();

    if (dim == Dim::ZR) {
        log_assert(getrt() == 1, "Expected r dim  = 1");
        log_assert(getct() == shape.ct, "Expected c dim to match");

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri  <shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi % this->get_shape().w][zi % this->get_shape().z][ri % this->get_shape().rt][ci].broadcast(Dim::R);
                    }
                }
            }
        }
    } else if (dim == Dim::R) {
        //log_assert(getrt() == 1);
        log_assert(getct() == shape.ct, "Expected c dim to match");

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri % this->get_shape().rt][ci].broadcast(dim);
                    }
                }
            }
        }
    } else if (dim == Dim::C) {
        log_assert(getrt() == shape.rt, "Expected r dim to match");
        //log_assert(getct() == 1);

        int ct = getct();

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        tt_tile input_tile = this->tile_tensor[wi][zi][ri][ci % this->get_shape().ct];
                        tt_tile output_tile = input_tile.broadcast(dim);
                        result.tile_tensor[wi][zi][ri][ci] = output_tile;
                    }
                }
            }
        }
    }  else if (dim == Dim::RC) {
        log_assert(getrt() == 1, "Expected r dim to match");
        log_assert(getct() == 1, "Expected c dim to match");

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi % this->get_shape().w][zi % this->get_shape().z][ri % this->get_shape().rt][ci % this->get_shape().ct].broadcast(dim);
                    }
                }
            }
        }
    }  else if (dim == Dim::Z) {
        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi % this->get_shape().z][ri][ci];
                    }
                }
            }
        }
    } else {
        throw runtime_error("Bcast operation not implemented yet");
    }
    return result;
    }

    tt_tensor tt_tensor::broadcast_tiles(const tt_shape& shape, Dim dim, bool shape_only) const
    {
    // Check dimensions are compatible
    log_assert(getw() == shape.w or getw() == 1, "Expected w dim to match or w dim = 1");
    //log_assert(getz() == shape.z or getz() == 1);

    tt_tensor result(shape, this->get_data_format());

    if (shape_only)
    {
        return result;
    }

    result.reserve_tile_tensor();

    if (dim == Dim::ZR) {
        log_assert(getct() == shape.ct, "Expected c dim to match");

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri  <shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi % this->get_shape().w][zi % this->get_shape().z][ri % this->get_shape().rt][ci];
                    }
                }
            }
        }
    } else if (dim == Dim::R) {
        log_assert(getct() == shape.ct, "Expected c dim to match");

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri % this->get_shape().rt][ci];
                    }
                }
            }
        }
    } else if (dim == Dim::C) {
        log_assert(getrt() == shape.rt, "Expected r dim to match");

        int ct = getct();

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri][ci % this->get_shape().ct];
                    }
                }
            }
        }
    }  else if (dim == Dim::RC) {

        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi % this->get_shape().w][zi % this->get_shape().z][ri % this->get_shape().rt][ci % this->get_shape().ct];
                    }
                }
            }
        }
    }  else if (dim == Dim::Z) {
        for(int wi = 0; wi < shape.w; ++wi)
        {
            for(int zi = 0; zi < shape.z; ++zi)
            {
                for(int ri = 0; ri < shape.rt; ++ri)
                {
                    for(int ci = 0; ci < shape.ct; ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi % this->get_shape().z][ri][ci];
                    }
                }
            }
        }
    } else {
        throw runtime_error("Bcast operation not implemented yet");
    }
    return result;
    }

    // Broadcasts all tiles within tensor in the dim specified, does not change the overall tile tensor shape
    tt_tensor tt_tensor::broadcast_within_tiles(Dim dim, bool shape_only) const
    {
        tt_tensor result(this->get_shape(), this->get_data_format());
        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        for(int wi = 0; wi < this->getw(); ++wi)
        {
            for(int zi = 0; zi < this->getz(); ++zi)
            {
                for(int ri = 0; ri  <this->getrt(); ++ri)
                {
                    for(int ci = 0; ci < this->getct(); ++ci)
                    {
                        result.tile_tensor[wi][zi][ri][ci] =  this->tile_tensor[wi][zi][ri][ci].broadcast(dim);
                    }
                }
            }
        }
        return result;
    }
    tt_tensor tt_tensor::eltwise_binary_with_broadcast(const tt_tensor &rhs, BinaryOp binary_op, Dim dim, bool shape_only) const
    {
        // Check dimensions are compatible
        log_assert(rhs.getw() == getw() or rhs.getw() == 1, "Expected w dim to match or rhs w dim = 1");
        log_assert(rhs.getz() == getz() or rhs.getz() == 1, "Expected z dim to match or rhs z dim = 1");

        tt_tensor result(get_shape(), this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        if (dim == Dim::ZR) {
            log_assert(rhs.getrt() == 1, "Expected rhs r dim = 1");
            log_assert(rhs.getct() == getct(), "Expected c dim to match");

            for(int wi=0;wi<getw();++wi)
            {
                for(int zi=0;zi<getz();++zi)
                {
                    for(int ri=0;ri<getrt();++ri)
                    {
                        for(int ci=0;ci<getct();++ci)
                        {
                            result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri][ci].eltwise_binary_with_broadcast(rhs.tile_tensor[0][0][0][ci], binary_op, Dim::R);
                        }
                    }
                }
            }
        } else if (dim == Dim::R) {
            log_assert(rhs.getct() == getct(), "Expected c dim to match");

            for(int wi=0;wi<getw();++wi)
            {
                for(int zi=0;zi<getz();++zi)
                {
                    for(int ri=0;ri<getrt();++ri)
                    {
                        for(int ci=0;ci<getct();++ci)
                        {
                            result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri][ci].eltwise_binary_with_broadcast(rhs.tile_tensor[wi][zi][0][ci], binary_op, dim);
                        }
                    }
                }
            }
        } else if (dim == Dim::C) {
            log_assert(rhs.getrt() == getrt(), "Expected r dim to match");
            log_assert(rhs.getct() == 1, "Expected rhs c dim = 1");

            int ct = getct();

            for(int wi = 0; wi < getw(); ++wi)
            {
                for(int zi = 0; zi < getz(); ++zi)
                {
                    for(int ri = 0; ri < getrt(); ++ri)
                    {
                        for(int ci = 0; ci < ct; ++ci)
                        {
                            tt_tile tile_a = this->tile_tensor[wi][zi][ri][ci];
                            tt_tile tile_b = rhs.tile_tensor[wi][zi][ri][0];
                            tt_tile output_tile = tile_a.eltwise_binary_with_broadcast(tile_b, binary_op, dim);
                            result.tile_tensor[wi][zi][ri][ci] = output_tile;
                        }
                    }
                }
            }
        }  else if (dim == Dim::RC) {
            log_assert(rhs.getrt() == 1, "Expected rhs r dim = 1");
            log_assert(rhs.getct() == 1, "Expected rhs c dim = 1");

            for(int wi=0;wi<getw();++wi)
            {
                for(int zi=0;zi<getz();++zi)
                {
                    for(int ri=0;ri<getrt();++ri)
                    {
                        for(int ci=0;ci<getct();++ci)
                        {
                            result.tile_tensor[wi][zi][ri][ci] = this->tile_tensor[wi][zi][ri][ci].eltwise_binary_with_broadcast(rhs.tile_tensor[0][0][0][0], binary_op, dim);
                        }
                    }
                }
            }
        } else {
            throw runtime_error("Bcast operation not implemented yet");
        }

        return result;
    }

    tt_tile_buffer *tt_tensor::get_tile_buf_ptr(int w, int z, int rt, int ct)
    {
        log_assert(tile_map.size() > w, "w out ouf bounds");
        log_assert(tile_map[0].size() > z, "z out ouf bounds");
        log_assert(tile_map[0][0].size() > rt, "r out ouf bounds");
        log_assert(tile_map[0][0][0].size() > ct, "c out ouf bounds");

        return tile_map[w][z][rt][ct];
    }

    tt_tensor tt_tensor::concatenate(const tt_tensor& rhs, Dim dim, bool shape_only) const
    {
        // Check dimensions are compatible
        tt_shape shape = this->get_shape();

        if (dim == Dim::C) {
            shape.ct += rhs.getct();
        }
        else {
            log_fatal("Unimplemented!");
        }

        tt_tensor result(shape, this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        if (dim == Dim::C) {
            log_assert(this->getw() == rhs.getw(), "w dim mismatch");
            log_assert(this->getz() == rhs.getz(), "z dim mismatch");
            log_assert(this->getrt() == rhs.getrt(), "r dim mismatch");

            for (uint32_t w = 0; w < this->getw(); w++)
            {
                for (uint32_t z = 0; z < this->getz(); z++)
                {
                    for (uint32_t r = 0; r < this->getrt(); r++)
                    {
                        uint32_t output_c = 0;
                        for (uint32_t c = 0; c < this->getct(); c++)
                        {
                            result.tile_tensor[w][z][r][output_c] = this->tile_tensor[w][z][r][c];
                            output_c++;
                        }
                        for (uint32_t c = 0; c < rhs.getct(); c++)
                        {
                            result.tile_tensor[w][z][r][output_c] = rhs.tile_tensor[w][z][r][c];
                            output_c++;
                        }
                    }
                }
            }
        }

        return result;
    }



    tt_tensor tt_tensor::slice(Dim dim, uint32_t start, uint32_t end, bool shape_only) const
    {
        // Check dimensions are compatible
        tt_shape shape = this->get_shape();

        if (dim == Dim::C) {
            shape.ct = end - start;
            log_assert(shape.ct > 0, "expected c > 0");
        }
        else {
            log_fatal("Unimplemented!");
        }

        tt_tensor result(shape, this->get_data_format());

        if (shape_only)
        {
            return result;
        }

        result.reserve_tile_tensor();

        if (dim == Dim::C) {

            for (uint32_t w = 0; w < this->getw(); w++)
            {
                for (uint32_t z = 0; z < this->getz(); z++)
                {
                    for (uint32_t r = 0; r < this->getrt(); r++)
                    {
                        uint32_t output_c = 0;
                        for (uint32_t c = start; c < end; c++)
                        {
                            result.tile_tensor[w][z][r][output_c] = this->tile_tensor[w][z][r][c];
                            output_c++;
                        }
                    }
                }
            }
        }

        return result;
    }

} // end namespace tt
