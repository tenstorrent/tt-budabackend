// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

#include "llk_tensor_data_format.h"
#include "tensix_types.h"
#include "utils/math.h"

namespace llk {

struct TensorDims {
    static constexpr size_t TILE_ROW_DIM = 32;
    static constexpr size_t TILE_COL_DIM = 32;

    // Do we want to just calculate the rt/ct from x and y each time?
    int rt;  // Number of tiles in the row
    int ct;  // Number of tiles in the column dim
    int x;
    int y;
    int z;
    int w;

    TensorDims(int w, int z, int y, int x) : w(w), z(z), x(x), y(y) {
        rt = ttl::div_ceil(x, TILE_COL_DIM);
        ct = ttl::div_ceil(y, TILE_ROW_DIM);
    }

    TensorDims(std::vector<int> dims) : w(dims[0]), z(dims[1]), y(dims[2]), x(dims[3]) {
        assert(dims.size() == 4 && "Size must be 4 for dimensions [w,z,y,x]");
        rt = ttl::div_ceil(x, TILE_COL_DIM);
        ct = ttl::div_ceil(y, TILE_ROW_DIM);
    }
    TensorDims() : TensorDims(0, 0, 0, 0) {}
    bool equal(const llk::TensorDims &rh) {
        return (x == rh.x) && (y == rh.y) && (z == rh.z) && (w == rh.w) && (ct == rh.ct) && (rt == rh.rt);
    }

    int num_tiles() { return rt * ct * z * w; }
    int tile_bytes_when_assembled(DataFormat format) {
        return sizeof(TileHeader) + llk::num_bytes_extra(format) + llk::num_bytes(format) * num_elements_per_tile();
    }
    int block_bytes_when_assembled(DataFormat format) { return llk::num_bytes(format) * num_elements_per_tile(); }
    int num_bytes_when_assembled(DataFormat format, bool untilized) {
        return num_tiles() * (untilized ? block_bytes_when_assembled(format) : tile_bytes_when_assembled(format));
    }

    bool operator==(llk::TensorDims &rh) { return equal(rh); }
    bool operator!=(llk::TensorDims &rh) { return !equal(rh); }

    int num_elements() { return x * y * z * w; }
    int num_elements_per_tile() { return TILE_ROW_DIM * TILE_COL_DIM; }
    std::string str() const {
        std::ostringstream dim_ss;
        dim_ss << "(w=" << w << ",z=" << z << ",y=" << y << ",x=" << x << ")";
        return dim_ss.str();
    }
    std::string unformatted_str() const {
        std::ostringstream dim_ss;
        dim_ss << w << "_" << z << "_" << y << "_" << x;
        return dim_ss.str();
    }
    std::vector<int> get_vector() const {
        std::vector<int> result = {w, z, y, x};
        return result;
    }
};

}  // namespace llk
