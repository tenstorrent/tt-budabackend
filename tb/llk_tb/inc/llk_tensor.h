// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "llk_memory.h"
#include "llk_tensor_data_format.h"
#include "llk_tensor_dims.h"
#include "llk_tensor_tile_layout.h"
#include "llk_xy_pair.h"
#include "tile.h"
#include "utils/math.h"

namespace llk {

class Tensor {
   public:
    string name = "";
    DataFormat data_format = DataFormat::Bfp8;
    llk::TensorTileLayout layout = llk::TensorTileLayout::ROW_MAJOR;  // refers to the ordering of the tile
    llk::TensorDims dims;
    bool tilized = false;

    std::vector<std::vector<std::vector<std::vector<c_tile>>>> tile_tensor;

    //! Constructors -- id is optional
    Tensor(int w, int z, int y, int x);
    Tensor(int w, int z, int y, int x, string name);
    Tensor(int w, int z, int y, int x, string name, DataFormat data_format);
    Tensor(TensorDims dims);
    Tensor(TensorDims dims, string name);
    Tensor(TensorDims dims, DataFormat data_format);
    Tensor(TensorDims dims, string name, DataFormat data_format);
    Tensor();

    int num_elements() { return dims.num_elements(); }
    int num_tiles() { return dims.num_tiles(); }
    int num_bytes_when_assembled(bool untilized) { return dims.num_bytes_when_assembled(data_format, untilized); }

    // ! Sets the dimensions of the tensors and allocates the vectors
    void set_dims(TensorDims dims);

    //! Fill tensor with hardcoded data
    void populate_with_debug_data();
    //! Fill tensor with constant data
    void populate_with_constant_data(float constant);
    //! Fill tensor with a vector of supplied data
    void populate_with_vector(const vector<float> &data_vector);
    //! Fill tensor with uniformly distributed data
    void populate_with_uniform_distribution_data(float min, float max, int seed);
    //! Fill tensor with normal distribution data
    void populate_with_normal_distribution_data(float mean, float stdev, int seed);

    //! Moves the data such that it matches a different layout.
    void reorder_layout(llk::TensorTileLayout target_layout);
    void tilize();
    void untilize();
    void untilize_layout();

    //! Generate memory objects for each tile and assembles it together assuming row-major
    void assemble_tiles_to_memory(
        llk::memory &memory, size_t base_address_in_bytes, bool aligned_32B, llk::xy_pair num_blocks);

    //! Generate memory objects for each tile separately
    void assemble_tiles_to_memories(
        std::vector<llk::memory> &header_memories,
        std::vector<llk::memory> &data_memories,
        size_t byte_address,
        bool aligned_32B,
        llk::xy_pair num_blocks);
    //! Reads data a mem vector
    /*!
        Reads in a memory vector, assuming the tensor is set to matching format/layout already.
        \param dims dimensions of the tensor to interpret from vector.
        \param data_vector the vector to read data from. assumes it is tightly packed and has headers
    */
    void import_from_mem_vector(TensorDims dims, std::vector<uint32_t> &data_vector, bool untilized);
    //! Dump tensor to file or to stdout.
    void dump(bool to_stdout, bool to_file = false, std::string filename_suffix = "");

    //! Reset and zero out tensor
    void reset_tensor();

   private:
};

}  // namespace llk
