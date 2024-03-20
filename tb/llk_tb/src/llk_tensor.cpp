// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_tensor.h"

#include <boost/filesystem.hpp>
#include <cassert>
#include <random>
#include <string>
#include <numeric>
#include <unordered_map>

#include "glog/logging.h"
#include "utils/distributions.h"

namespace fs = boost::filesystem;
using namespace llk;

llk::Tensor::Tensor() : dims(0, 0, 0, 0), data_format(DataFormat::Bfp8) {}

llk::Tensor::Tensor(TensorDims dims) : Tensor(dims.w, dims.z, dims.y, dims.x, "", DataFormat::Bfp8) {}

llk::Tensor::Tensor(TensorDims dims, DataFormat data_format) :
    Tensor(dims.w, dims.z, dims.y, dims.x, "", data_format) {}

llk::Tensor::Tensor(TensorDims dims, string name) : Tensor(dims.w, dims.z, dims.y, dims.x, "", DataFormat::Bfp8) {}

llk::Tensor::Tensor(TensorDims dims, string name, DataFormat data_format) :
    Tensor(dims.w, dims.z, dims.y, dims.x, name, data_format) {}

llk::Tensor::Tensor(int w, int z, int y, int x) : Tensor(w, z, y, x, "", DataFormat::Bfp8) {}

llk::Tensor::Tensor(int w, int z, int y, int x, string name) : Tensor(w, z, y, x, name, DataFormat::Bfp8) {}

llk::Tensor::Tensor(int w, int z, int y, int x, string name, DataFormat data_format) :
    dims(w, z, y, x), name(name), data_format(data_format) {
    // Initialize the 4d array of w,z,ct,rt size
    this->set_dims(dims);
    this->reset_tensor();  // Artifact of ctile --> Must reset tile or tile will be empty vector.
}

void llk::Tensor::set_dims(TensorDims input_dims) {
    dims = input_dims;
    auto r_vector = std::vector<c_tile>(dims.rt, c_tile(TensorDims::TILE_ROW_DIM, TensorDims::TILE_COL_DIM, 1, 1));
    auto c_vector = std::vector<std::vector<c_tile>>(dims.ct, r_vector);
    auto z_vector = std::vector<std::vector<std::vector<c_tile>>>(dims.z, c_vector);
    tile_tensor = std::vector<std::vector<std::vector<std::vector<c_tile>>>>(dims.w, z_vector);
}

void llk::Tensor::import_from_mem_vector(TensorDims dims, std::vector<uint32_t> &mem_vector, bool untilized) {
    bool compressed = false;
    auto dis_zerocomp = not compressed;
    set_dims(dims);
    reset_tensor();
    tilized = !untilized;
    int offset_words = 0;
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    // Generate Tile Header + size calculations
                    TileHeader_u tile_header;
                    auto tile_header_size_in_words = untilized ? 0 : tile_header.header.size() / 4;
                    // Read header
                    for (int header_index = 0; header_index < tile_header_size_in_words; header_index++) {
                        tile_header.val[header_index] = mem_vector[offset_words + header_index];
                    }
                    offset_words += tile_header_size_in_words;
                    int data_size_in_bytes =
                        (untilized ? 0 : llk::num_bytes_extra(data_format)) + llk::num_bytes(data_format) * 32 * 32;
                    int data_size_in_words = data_size_in_bytes / 4;
                    if (tilized) {
                        if ((data_size_in_words + tile_header_size_in_words) !=
                            (tile_header.header.tile_size_16B * 4)) {
                            throw std::runtime_error("Tile size mismatched with expected tile size");
                        }
                    }
                    // Read Data
                    tile_tensor[w][z][ct][rt].import_from_mem(
                        0,
                        [mem_vector, offset_words](int addr) -> unsigned int {
                            return mem_vector[addr + offset_words];
                        },
                        static_cast<int>(data_format),
                        dis_zerocomp);
                    offset_words += data_size_in_words;
                }
            }
        }
    }
}

void llk::Tensor::reset_tensor() {
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    tile_tensor[w][z][ct][rt].set_data_format(data_format);
                    tile_tensor[w][z][ct][rt].tile_reset();
                }
            }
        }
    }
}

void llk::Tensor::populate_with_debug_data() {
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    auto data_vector = std::vector<float>(TensorDims::TILE_ROW_DIM * TensorDims::TILE_COL_DIM, 0);

                    tile_tensor[w][z][ct][rt].iterate_xyzw(
                        [data_vector, ct, rt](c_tile *tile, int x, int y, int z, int w) {
                            // tile->value(x, y, z, w).f = data_vector[y * tile->get_x_tile_dim() + x];
                            tile->value(x, y, z, w).f = y * 32 + x;
                        });

                    // auto &tile = tile_tensor[w][z][ct][rt];
                    // std::vector<float> data_vector(llk::TensorDims::TILE_COL_DIM * llk::TensorDims::TILE_ROW_DIM);
                    // for (auto &data : data_vector) {
                    //  data = (float)z;
                    //}
                    // tile.iterate_xyzw([data_vector](c_tile *tile, int x, int y, int z, int w) {
                    //  float &value = tile->value(x, y, z, w).f;
                    //  value = data_vector[y * tile->get_x_tile_dim() + x];
                    //});
                }
            }
        }
    }
    return;
}

void llk::Tensor::populate_with_constant_data(float constant) {
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    tile_tensor[w][z][ct][rt].iterate_xyzw(
                        [constant](c_tile *tile, int x, int y, int z, int w) { tile->value(x, y, z, w).f = constant; });
                }
            }
        }
    }
    return;
}

void llk::Tensor::populate_with_vector(const vector<float>& data_vector) {
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    auto &tile = tile_tensor[w][z][ct][rt];
                    int cnt=0;

                    for (int y=0; y<tile.get_y_tile_dim(); y++) {
                       for (int x=0; x<tile.get_x_tile_dim(); x++) {
                            if ((x>=llk::TensorDims::TILE_ROW_DIM/2) || (y>=llk::TensorDims::TILE_COL_DIM/2)) {
                               tile.value(x, y, 0, 0).f = 0;
                            } else {
                               tile.value(x, y, 0, 0).f = data_vector[cnt++];
                            }
                       }
                    }
                }
            }
        }
    }
}

void llk::Tensor::populate_with_uniform_distribution_data(float min, float max, int seed) {
    std::mt19937 mt(seed);
    VLOG(2) << __FUNCTION__ << "Initializing std::mt19937 with random seed 0x" << std::hex << seed;
    std::uniform_real_distribution<float> dist(min, max);
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    auto &tile = tile_tensor[w][z][ct][rt];
                    std::vector<float> data_vector(llk::TensorDims::TILE_COL_DIM * llk::TensorDims::TILE_ROW_DIM);
                    for (auto &data : data_vector) {
                        data = (float)dist(mt);
                    }
                    tile.iterate_xyzw([data_vector](c_tile *tile, int x, int y, int z, int w) {
                        float &value = tile->value(x, y, z, w).f;
                        value = data_vector[y * tile->get_x_tile_dim() + x];
                    });
                }
            }
        }
    }
    return;
}

void llk::Tensor::populate_with_normal_distribution_data(float mean, float stdev, int seed) {
    std::mt19937 mt(seed);
    VLOG(2) << __FUNCTION__ << "Initializing std::mt19937 with random seed 0x" << std::hex << seed;
    std::normal_distribution<float> dist(mean, stdev);
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    auto &tile = tile_tensor[w][z][ct][rt];
                    std::vector<float> data_vector(llk::TensorDims::TILE_COL_DIM * llk::TensorDims::TILE_ROW_DIM);
                    for (auto &data : data_vector) {
                        data = (float)dist(mt);
                    }
                    tile.iterate_xyzw([data_vector](c_tile *tile, int x, int y, int z, int w) {
                        float &value = tile->value(x, y, z, w).f;
                        value = data_vector[y * tile->get_x_tile_dim() + x];
                    });
                }
            }
        }
    }
    return;
}

void llk::Tensor::reorder_layout(llk::TensorTileLayout target_layout) {
    if (not tilized) {
        LOG(FATAL) << __FUNCTION__ << "::reorder_layout can only be done on tilized tiles)";
    }
    if (((layout == llk::TensorTileLayout::ROW_MAJOR) and (target_layout == llk::TensorTileLayout::COL_MAJOR)) or
        ((layout == llk::TensorTileLayout::COL_MAJOR) and (target_layout == llk::TensorTileLayout::ROW_MAJOR))) {
        // RM --> CM  or
        // CM --> RM
        for (auto w = 0; w < tile_tensor.size(); w++) {
            for (auto z = 0; z < tile_tensor[w].size(); z++) {
                for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                    for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                        // Swap F1 and F2 and append it all back.
                        //     ---- ----
                        //    | F0 | F2 |
                        //     ---- ----
                        //    | F1 | F3 |
                        //     ---- ----
                        auto &tile = tile_tensor[w][z][ct][rt];
                        tile.convert_tile_to_XxY_faces(16, 16);
                        std::vector<c_tile> tiles_16x16;  // [0][y]
                        tile_tensor[w][z][ct][rt].slice_dim(1, tiles_16x16, 'z');
                        tiles_16x16[0].append_z(tiles_16x16[2]);
                        tiles_16x16[0].append_z(tiles_16x16[1]);
                        tiles_16x16[0].append_z(tiles_16x16[3]);
                        tiles_16x16[0].convert_tile_to_XxY_faces(32, 32);
                        tile_tensor[w][z][ct][rt] = tiles_16x16[0];
                    }
                }
            }
        }
        layout = target_layout;
    }
}

// Always assembles in a row-major format, but we can break input into blocks
// W/Z are always assembled sequentially, but XY can be split into blocks.
// Blocks are assembled in a row major way within the xy_plane
// Tiles are also assembled in a row major way within a block
//  ______  ______  ______
// |      ||      ||      |
// |  B0  ||  B1  ||  B2  |
// |      ||      ||      |
//  ______  ______  ______
// |      ||      ||      |
// |  B3  ||  B4  ||  B5  |
// |      ||      ||      |
//  ______  ______  ______
// B0
//  ______   ______
// |      | |      |
// |  T0  | |  T1  |
// |      | |      |
//  ______   ______
// |      | |      |
// |  T2  | |  T3  |
// |      | |      |
//  ______   ______
//
// Memory View
// B0 [ T0, T1, T2, T3 ], B1 [T0, T1, T2, T3], .... B5[...]
//
void llk::Tensor::assemble_tiles_to_memory(
    llk::memory &memory, size_t base_address_in_bytes, bool aligned_32B, llk::xy_pair num_blocks) {
    std::vector<uint32_t> assembled_tiles;
    uint32_t current_word_offset = 0;
    int tile_index = 0;
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            int start_col = 0;
            int block_size_y = tile_tensor[w][z].size() / num_blocks.y;
            int block_size_x = tile_tensor[w][z][start_col].size() / num_blocks.x;
            assert(!(tile_tensor[w][z].size() % num_blocks.y) && "Number of blocks in y must evenly divide y dim");
            assert(
                !(tile_tensor[w][z][start_col].size() % num_blocks.x) &&
                "Number of blocks in y must evenly divide y dim");
            for (auto y_block = 0; y_block < num_blocks.y; y_block++) {
                for (auto x_block = 0; x_block < num_blocks.x; x_block++) {
                    auto block_offset_y = y_block * block_size_y;
                    auto block_offset_x = x_block * block_size_x;
                    for (auto ct = block_offset_y; ct < block_offset_y + block_size_y; ct++) {
                        for (auto rt = block_offset_x; rt < block_offset_x + block_size_x; rt++) {
                            auto &tile = tile_tensor[w][z][ct][rt];
                            if (not tile.tile_packed()) {
                                LOG(FATAL) << "Tiles in tensor needs to be packed before getting assembled.";
                            }

                            size_t unrounded_size_16B = ttl::div_ceil(tile.packed_size_bytes(), 16);
                            size_t data_size_in_words = (unrounded_size_16B * 4);  // 4 Bytes per word
                            auto tile_header_size_in_words = 0;
                            auto new_size = current_word_offset + data_size_in_words + tile_header_size_in_words;

                            if (tilized) {
                                // Generate Tile Header + size calculations
                                TileHeader_u tile_header;
                                tile_header.header.tile_id = tile_index;
                                tile_header.header.tile_size_16B = unrounded_size_16B + 1 + (aligned_32B ? 1 : 0);
                                tile_header_size_in_words = tile_header.header.size() / 4;
                                new_size = current_word_offset + data_size_in_words + tile_header_size_in_words +
                                           (aligned_32B ? 1 * 4 : 0);
                                assembled_tiles.resize(new_size);
                                // Write header to memory vector
                                for (int header_index = 0; header_index < tile_header_size_in_words; header_index++) {
                                    assembled_tiles[current_word_offset + header_index] = tile_header.val[header_index];
                                }
                            } else {
                                assembled_tiles.resize(new_size);
                            }

                            // Write data to memory vector
                            uint32_t *append_ptr =
                                assembled_tiles.data() + current_word_offset + tile_header_size_in_words;
                            tile.write_to_mem(
                                0, true, [append_ptr](int addr, unsigned int value) { append_ptr[addr] = value; });
                            current_word_offset = new_size;
                            tile_index += 1;
                        }
                    }
                }
            }
        }
    }
    assert((tile_index == num_tiles()) && "Number of tiles processed must equal to number of tiles in tensor");
    memory = llk::memory(base_address_in_bytes, assembled_tiles);
}

void llk::Tensor::untilize_layout() {
    std::vector<c_tile> src_tile;
    int x_dim, y_dim, z_dim, w_dim;
    tile_tensor[0][0][0][0].get_dimension(&x_dim, &y_dim, &z_dim, &w_dim);
    if ((z_dim > 1) or (w_dim > 1)) {
        LOG(FATAL) << "Tiles in tensor must have z_dim and w_dim 1 to untilize";
    }

    auto rt_dim = tile_tensor[0][0][0].size();

    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    auto &tile = tile_tensor[w][z][ct][rt];
                    src_tile.push_back(tile);
                }

                int rt_offset = 0;
                int y_offset = 0;
                for (auto rt = 0; rt < rt_dim; rt++) {
                    for (int y = 0; y < y_dim; y++) {
                        for (int x = 0; x < x_dim; x++) {
                            tile_tensor[w][z][ct][rt].value(x, y, 0, 0) =
                                src_tile[(rt * y_dim + y) % rt_dim].value(x, (rt * y_dim + y) / rt_dim, 0, 0);
                        }
                    }
                }

                src_tile.clear();
            }
        }
    }
}

void llk::Tensor::assemble_tiles_to_memories(
    std::vector<llk::memory> &header_memories,
    std::vector<llk::memory> &data_memories,
    size_t byte_address,
    bool aligned_32B,
    llk::xy_pair num_blocks) {
    uint32_t current_byte_address = byte_address;
    header_memories.clear();
    data_memories.clear();
    int tile_index = 0;
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            int start_col = 0;
            int block_size_y = tile_tensor[w][z].size() / num_blocks.y;
            int block_size_x = tile_tensor[w][z][start_col].size() / num_blocks.x;
            assert(!(tile_tensor[w][z].size() % num_blocks.y) && "Number of blocks in y must evenly divide y dim");
            assert(
                !(tile_tensor[w][z][start_col].size() % num_blocks.x) &&
                "Number of blocks in y must evenly divide y dim");
            for (auto y_block = 0; y_block < num_blocks.y; y_block++) {
                for (auto x_block = 0; x_block < num_blocks.x; x_block++) {
                    auto block_offset_y = y_block * block_size_y;
                    auto block_offset_x = x_block * block_size_x;
                    for (auto ct = block_offset_y; ct < block_offset_y + block_size_y; ct++) {
                        for (auto rt = block_offset_x; rt < block_offset_x + block_size_x; rt++) {
                            auto &tile = tile_tensor[w][z][ct][rt];
                            if (not tile.tile_packed()) {
                                LOG(FATAL) << "Tiles in tensor needs to be packed before getting assembled.";
                            }
                            size_t unrounded_size_16B = ttl::div_ceil(tile.packed_size_bytes(), 16);

                            // Generate Tile Header + size calculations
                            TileHeader_u tile_header;
                            tile_header.header.tile_id = tile_index;
                            tile_header.header.tile_size_16B = unrounded_size_16B + 1 + (aligned_32B ? 1 : 0);
                            auto tile_header_size_in_words = tile_header.header.size() / 4;
                            size_t data_size_in_words = (unrounded_size_16B * 4);  // 4 Bytes per word
                            auto aligned_data_size_in_words = data_size_in_words + (aligned_32B ? 1 * 4 : 0);
                            auto aligned_data_size_in_bytes = aligned_data_size_in_words * 4;

                            std::vector<uint32_t> header_vector(tile_header_size_in_words);
                            // Write header to memory vector
                            for (int header_index = 0; header_index < tile_header_size_in_words; header_index++) {
                                header_vector[header_index] = tile_header.val[header_index];
                            }
                            header_memories.push_back(llk::memory(current_byte_address, header_vector));
                            current_byte_address += tile_header.header.size();

                            // Write data to memory vector
                            std::vector<uint32_t> data_vector(aligned_data_size_in_words);
                            uint32_t *append_ptr = data_vector.data();
                            tile.write_to_mem(
                                0, true, [append_ptr](int addr, unsigned int value) { append_ptr[addr] = value; });

                            data_memories.push_back(llk::memory(current_byte_address, data_vector));
                            current_byte_address += aligned_data_size_in_bytes;
                            tile_index += 1;
                        }
                    }
                }
            }
        }
    }
    assert((tile_index == num_tiles()) && "Number of tiles processed must equal to number of tiles in tensor");
    assert(
        (header_memories.size() == data_memories.size()) &&
        "headers and tile data vectors produced from tensor must be equal");
}

void llk::Tensor::tilize() {
    if (not tilized) {
        for (auto w = 0; w < tile_tensor.size(); w++) {
            for (auto z = 0; z < tile_tensor[w].size(); z++) {
                for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                    for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                        // slice 32x32 tile into following 16x16
                        //        32
                        //     ---- ----
                        //    | F0 | F1 |
                        // 32  ---- ----
                        //    | F2 | F3 |
                        //     ---- ----
                        auto &tile = tile_tensor[w][z][ct][rt];
                        std::vector<c_tile> tiles_16x32;          // [x][y]
                        std::vector<c_tile> tiles_grid_16x16_c0;  // [0][y]
                        std::vector<c_tile> tiles_grid_16x16_c1;  // [1][y]
                        tile.slice_dim(16, tiles_16x32, 'x');
                        tiles_16x32[0].slice_dim(16, tiles_grid_16x16_c0, 'y');
                        tiles_16x32[1].slice_dim(16, tiles_grid_16x16_c1, 'y');
                        tiles_grid_16x16_c0[0].append_z(tiles_grid_16x16_c1[0]);
                        tiles_grid_16x16_c0[0].append_z(tiles_grid_16x16_c0[1]);
                        tiles_grid_16x16_c0[0].append_z(tiles_grid_16x16_c1[1]);
                        tile = tiles_grid_16x16_c0[0];
                    }
                }
            }
        }
        tilized = true;
    }
}

void llk::Tensor::untilize() {
    if (tilized) {
        for (auto w = 0; w < tile_tensor.size(); w++) {
            for (auto z = 0; z < tile_tensor[w].size(); z++) {
                for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                    for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                        // take 16x16 and reorder back into a 32x32
                        //        32
                        //     ---- ----
                        //    | F0 | F1 |
                        // 32  ---- ----
                        //    | F2 | F3 |
                        //     ---- ----
                        auto &tile = tile_tensor[w][z][ct][rt];
                        tile.convert_tile_to_XxY_faces(16, 16);
                        std::vector<c_tile> tiles_16x16;  // [0][y]
                        tile.slice_dim(1, tiles_16x16, 'z');
                        auto result_tile_r0 = tiles_16x16[0].concat_x(tiles_16x16[1], {16, 16});
                        auto result_tile_r1 = tiles_16x16[2].concat_x(tiles_16x16[3], {16, 16});
                        tile = result_tile_r0.concat_y(result_tile_r1, {16, 16});
                    }
                }
            }
        }
        tilized = false;
    }
}

void llk::Tensor::dump(bool to_stdout, bool to_file, std::string filename_suffix) {
    fs::ofstream ofs;
    if (to_file) {
        auto output_file = fs::current_path();
        std::string suffix = "";
        if (not filename_suffix.empty()) {
            suffix = "_" + filename_suffix;
        }
        auto output_append = fs::path("/tensor_dumps/llk_tensor_" + name + suffix + ".dump");
        output_file /= output_append;
        if (not fs::exists(output_file.branch_path())) {
            fs::create_directory(output_file.branch_path());
        }
        ofs.open(output_file);
        ofs << "Tile Tensor Dimensions: " << dims.str() << ": \n";
    }
    if (to_stdout) {
        std::cout << "Tile Tensor Dimensions: " << dims.str() << std::endl;
    }
    for (auto w = 0; w < tile_tensor.size(); w++) {
        for (auto z = 0; z < tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < tile_tensor[w][z][ct].size(); rt++) {
                    auto tile = tile_tensor[w][z][ct][rt];
                    if (to_stdout) {
                        std::cout << "Tile Tensor Index: " << TensorDims(w, z, ct, rt).str() << std::endl;
                        tile.debugdump();
                    }
                    if (to_file) {
                        ofs << "Tile Tensor Index: " << TensorDims(w, z, ct, rt).str() << ": \n";
                        ofs << tile.floatdump_str();
                    }
                }
            }
        }
    }
    return;
}
