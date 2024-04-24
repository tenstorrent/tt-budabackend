// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>
#include <array>
#include <ostream>
#include <cstdint>

#include "utils/logger.hpp"
#include "constants.hpp"

namespace tt 
{
/** 
 * @brief A type that denotes the shape of the 4D numpy-like tensor in the units of tiles.
*/
struct tt_shape
{
    uint32_t rt=0;
    uint32_t ct=0;
    uint32_t z=0;
    uint32_t w=0;
    uint32_t tile_height = tt::constants::TILE_HEIGHT;
    uint32_t tile_width = tt::constants::TILE_WIDTH;
    std::vector<uint32_t> to_vec() const
    {
        return {rt, ct, z, w};
    }

    tt_shape from_vec(const std::vector<uint32_t> & v)
    {
        return tt_shape({v[0], v[1], v[2], v[3]});
    }

    bool operator==(const tt_shape &o) const
    {
        return (rt == o.rt) && (ct == o.ct) && (z == o.z) && (w == o.w);
    }

    bool operator!=(const tt_shape &o) const
    {
        return !(*this == o);
    }

    uint32_t &operator[](int idx)
    {
        if(idx >= 4) {
            log_fatal("tt_shape access oob");
        }
        switch (idx) {
            case 0: return w;
            case 1: return z;
            case 2: return rt;
            case 3: return ct;
            default: return ct;
        }   
    }

    int volume() const
    {
        return rt * ct * z * w;
    }

    int volume_full() const
    {
        return getrfull() * getcfull() * z * w;
    }

    int getrfull() const
    {
        return(rt * tile_height);
    }

    int getcfull() const
    {
        return(ct * tile_width);
    }

    static uint32_t get_unit_element() { return 1; }
    static int get_rank() { return 4; }
};

inline std::ostream& operator<<(std::ostream& os, const tt_shape& shape)
{
    os << "tt_shape{";
    os << ".rt = " << shape.rt << ", ";
    os << ".ct = " << shape.ct << ", ";
    os << ".z = " << shape.z << ", ";
    os << ".w = " << shape.w;
    os << ".tile_height = " << shape.tile_height << ", ";
    os << ".tile_width = " << shape.tile_width << ", ";
    os << "}";
    return os;
}

/**
 * @brief A type that denotes the shape (# rows, # columns) of a block of tiles in the units of tiles.
 */
struct tt_block_shape {
    std::uint32_t r = 0; // in tiles
    std::uint32_t c = 0; // in tiles

    // Operator overloads
    bool operator==(const tt_block_shape& other) const
    {
        return this->r == other.r and this->c == other.c;
    }

    bool operator!=(const tt_block_shape& other) const
    {
        return not ((*this) == other);
    }

    // Computed properties
    std::uint32_t volume() const
    {
        return r * c;
    }

    // Converters
    std::vector<uint32_t> to_vec() const
    {
        return {r, c};
    }

    // Validators
    bool is_initialized() const
    {
        return r > 0 and c > 0;
    }
};

inline std::ostream &operator<<(std::ostream &os, const tt_block_shape& block_shape) {
    os << "tt_block_shape{";
    os << ".r = " << block_shape.r << ", ";
    os << ".c = " << block_shape.c;
    os << "}";
    return os;
}

/**
 * @brief A type that denotes the shape of a buffer and is flexible enough to reconstruct the tensor chunk stored in the buffer.
 */
struct tt_buffer_shape
{
    uint32_t w;
    uint32_t z;
    uint32_t num_blocks_r;
    uint32_t num_blocks_c;
    tt_block_shape block_shape;

    bool operator==(const tt_buffer_shape &o) const
    {
        return (w == o.w) && 
                (z == o.z) && 
                (num_blocks_r == o.num_blocks_r) && 
                (num_blocks_c == o.num_blocks_c) && 
                (block_shape == o.block_shape);
    }

    bool operator!=(const tt_buffer_shape &o) const
    {
        return !(*this == o);
    }

    std::uint32_t volume() const
    {
        return w * z * num_blocks_r * num_blocks_c * block_shape.volume();
    }

    std::uint32_t get_num_tiles_r() const
    {
        return num_blocks_r * block_shape.r;
    }

    std::uint32_t get_num_tiles_c() const
    {
        return num_blocks_c * block_shape.c;        
    }

    tt_shape to_tt_shape() const
    {
        return {.rt=this->block_shape.r, 
                .ct=this->block_shape.c, 
                .z=this->z * this->num_blocks_r * this->num_blocks_c, 
                .w=this->w};
    }
};

inline std::ostream& operator<<(std::ostream& os, const tt_buffer_shape& buffer_shape)
{
    os << "buffer_shape{";
    os << ".w = " << buffer_shape.w << ", ";
    os << ".z = " << buffer_shape.z << ", ";
    os << ".num_blocks_r = "  << buffer_shape.num_blocks_r  << ", ";
    os << ".num_blocks_c = "  << buffer_shape.num_blocks_c  << ", ";
    os << ".block_shape = "  << buffer_shape.block_shape;
    os << "}";
    return os;
}

/** 
 * @brief A helper tuple of tensor dims. Used only in tt_tensor_slice. See tt_shape for tensor shape.
*/
struct tt_dims
{
    uint32_t rt, ct, z, w;
};
inline std::ostream& operator<<(std::ostream& os, const tt_dims& dims)
{
    os << "buffer_shape{";
    os << ".w = " << dims.w << ", ";
    os << ".z = " << dims.z << ", ";
    os << ".rt = "  << dims.rt  << ", ";
    os << ".ct = "  << dims.ct  << ", ";
    os << "}";
    return os;
}

/** 
 * @brief Denotes a contiguous slice of a tensor. 
*/
struct tt_tensor_slice
{
    tt_dims start;
    tt_dims end;
};

/** 
 * @brief A type that denotes the shape (# rows, # columns) of a grid of cores.
*/
struct tt_grid_shape {
    std::uint32_t r = 0;
    std::uint32_t c = 0;

        // Operator overloads
    bool operator==(const tt_grid_shape& o) const
    {
        return (r == o.r) && (c == o.c);
    }

    bool operator!=(const tt_grid_shape& o) const
    {
        return !(*this == o);
    }

    std::uint32_t& operator[](int index)
    {
        if(index >= 2)  {
            log_fatal("tt_grid_shape access out-of-bounds");
        }
        switch (index) {
            case 0: return r;
            case 1: return c;
            default: return c;
        }
    }

    std::uint32_t& at(int index)
    {
        if(index >= 2) {
            log_fatal("tt_grid_shape access out-of-bounds");
        }
        switch (index) {
            case 0: return r;
            case 1: return c;
            default: return c;
        }
    }
    std::uint32_t operator[](int index) const
    {
        if(index >= 2) {
            log_fatal("tt_grid_shape access out-of-bounds");
        }
        switch (index) {
            case 0: return r;
            case 1: return c;
            default: return c;
        }
    }

    std::uint32_t at(int index) const
    {
        if(index >= 2)  {
            log_fatal("tt_grid_shape access out-of-bounds");
        }
        switch (index) {
            case 0: return r;
            case 1: return c;
            default: return c;
        }
    }

    // Computed properties
    std::uint32_t volume() const
    {
        return r * c;
    }

    // Converters
    std::vector<uint32_t> to_vec() const
    {
        return {r, c};
    }

    static tt_grid_shape from_array(const std::array<std::uint32_t, 2> & array)
    {
        return tt_grid_shape{.r=array[0], .c=array[1]};   
    }
};

inline std::ostream& operator<<(std::ostream& os, const tt_grid_shape& grid_shape)
{
    os << "tt_grid_shape{";
    os << ".r = " << grid_shape.r << ", ";
    os << ".c = " << grid_shape.c;
    os << "}";
    return os;
}

/** 
 * @brief Tensix core coordinates.
*/
struct tt_core_coord
{
    uint32_t row;
    uint32_t col;
    uint32_t to_core_index(const tt_grid_shape & grid_shape) const
    {
        return (this->row * grid_shape[1]) + this->col;
    }

    static std::vector<int> to_row_column_vector(const tt_core_coord& core_coord)
    {
        return {
                static_cast<int>(core_coord.row), 
                static_cast<int>(core_coord.col)
                };
    }

    static tt_core_coord from_core_index(uint32_t core_index, const tt_grid_shape & grid_shape)
    {
        return tt_core_coord {
            .row = core_index / grid_shape[1],
            .col = core_index % grid_shape[1],
        };
    }

    bool operator< (const tt_core_coord &rhs) const
    {
        if (this->row == rhs.row)
        {
            return this->col < rhs.col;
        }
        return this->row < rhs.row;
    }

    bool operator== (const tt_core_coord &rhs) const
    {
        return (row == rhs.row) && (col == rhs.col);
    }

    bool operator!= (const tt_core_coord &rhs) const
    {
        return !(*this == rhs);
    }
};

inline std::ostream& operator<<(std::ostream& os, const tt_core_coord& coord)
{
    os << "tt_core_coord{";
    os << ".row= " << coord.row << ", ";
    os << ".col= " << coord.col<< ", ";
    os << "}";
    return os;
}

/**
 * @brief Tensix core coordinates pair containing logical core coordinates
*/
struct tt_logical_core_coords
{
    bool operator== (const tt_logical_core_coords &rhs) const;

    tt_core_coord relative;
    tt_core_coord absolute;
};

inline std::ostream & operator<<(std::ostream & os, const tt_logical_core_coords & coords)
{
    os << "tt_logical_core_coords{";
    os << ".relative=" << coords.relative << ", ";
    os << ".absolute=" << coords.absolute << ", ";
    os << "}";
    return os;
}

/**
 * @brief Tensix core coordinates triple containing logical core coordinates
 * and physical absolute coordinates.
*/

struct tt_logical_physical_core_coords
{
    tt_logical_core_coords logical_coords;
    tt_core_coord physical_absolute_coords;
};

inline std::ostream & operator<<(std::ostream & os, const tt_logical_physical_core_coords & coords)
{
    os << "tt_logical_physical_core_coords{";
    os << ".logical=" << coords.logical_coords << ", ";
    os << ".physical=" << coords.physical_absolute_coords << ", ";
    os << "}";
    return os;
}

/** 
 * @brief A location descriptor for any object that resides in DRAM (e.g., hex objects, dram queues).
*/
struct tt_dram_resident
{
    bool d_record_vld = false;
    bool host_resident = false;
    uint32_t upper_host_bits = 0;
    uint32_t d_chan = 0;
    uint64_t d_addr = 0;
    uint64_t d_chip_id = 0;
    uint32_t d_subchannel = 0;//-1 // The dram core within the channel this is mapped to. Always 0 for grayskull
    std::tuple<int, int, int> target_dram = {-1, -1, -1};

    tt_dram_resident() = default;

    uint32_t get_dram_chan() const
    {
        return this->d_chan;
    }

    uint32_t get_dram_subchannel() const
    {
        return this->d_subchannel;
    }

    void set_dram_chan(uint32_t ch)
    {
        d_chan = ch;
        d_record_vld = true;
        std::get<1>(target_dram) = ch;
    }

    void set_dram_addr(uint64_t ad)
    {
        d_addr = ad;
        d_record_vld = true;
    }

    void set_chip_id(uint32_t chip)
    {
        d_chip_id = chip;
        d_record_vld = true;
        std::get<0>(target_dram) = chip;
    }

    void set_dram_subchannel(uint32_t subchan)
    {
        d_subchannel = subchan;
        std::get<2>(target_dram) = subchan;
    }
}; // tt_dram_resident


}  // namespace tt