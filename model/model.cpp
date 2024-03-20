// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Code inside method definitions is indented to preserve git blame

#include "model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem> 
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "l1_address_map.h"
#include "dram_address_map.h"
#include "epoch_q.h"
#include "model/tt_rnd_util.hpp"
#include "data_format.hpp"
#include "common/model/test_common.hpp"
#include "device/tt_device.h"
#include "common/model/tt_core.hpp"

// IO
using std::ifstream;
using std::ofstream;
using std::stringstream;
using std::cout;
using std::endl;
using std::dec;
using std::hex;

// Containers
using std::deque;
using std::string;
using std::vector;

// Functions
using std::to_string;

// Device
using tt::ExpPrecision;

namespace fs = std::experimental::filesystem; // see comment above

void print_tensor_dims(const tt_tensor &tensor) {
    cout << tensor.getw() << ", " << tensor.getz() <<  ", " << tensor.getrfull() <<  ", " << tensor.getcfull() << endl;
}

bool are_tensor_dims_equal(const tt_tensor &lhs, const tt_tensor &rhs) {
    return lhs.same_shape(rhs);
}

bool operator==(const tt_tensor &lhs, const tt_tensor &rhs) {
    bool match = true;

    if(are_tensor_dims_equal(lhs, rhs))
    {
        for(int wi=0;wi<lhs.getw();++wi)
        {
            for(int zi=0;zi<lhs.getz();++zi)
            {
                for(int ri=0;ri<lhs.getrt();++ri)
                {
                    for(int ci=0;ci<lhs.getct();++ci)
                    {
                        if(lhs.tile_tensor[wi][zi][ri][ci] != rhs.tile_tensor[wi][zi][ri][ci])
                        {
                            match = false;
                        }
                    }
                }
            }
        }
    }
    else
    {
        cout << "dim mismatch: " << endl;
        print_tensor_dims(lhs);
        print_tensor_dims(rhs);
        match = false;
    }
    return(match);
}

bool operator!=(const tt_tensor &lhs, const tt_tensor &rhs) {
    return(not (lhs == rhs));
}

bool are_tensors_equal(const tt_tensor &lhs, const tt_tensor &rhs) {
    return lhs == rhs;
}

bool allclose(const tt_tensor &lhs, const tt_tensor &rhs) {
    bool match = true;

    if(are_tensor_dims_equal(lhs, rhs))
    {
        for(int wi=0;wi<lhs.getw();++wi)
        {
            for(int zi=0;zi<lhs.getz();++zi)
            {
                for(int ri=0;ri<lhs.getrt();++ri)
                {
                    for(int ci=0;ci<lhs.getct();++ci)
                    {
                        if(not lhs.tile_tensor[wi][zi][ri][ci].allclose(rhs.tile_tensor[wi][zi][ri][ci], 1.0))
                        {
                            match = false;
                            std::cout << "allclose first mismatch:" << std::endl;
                            lhs.tile_tensor[wi][zi][ri][ci].dump();
                            std::cout << std::endl;
                            rhs.tile_tensor[wi][zi][ri][ci].dump();
                            std::cout << std::endl;
                            goto done;
                        }
                    }
                }
            }
        }
    }
    else
    {
        match = false;
        std::cout << "allclose: shapes mismatch";
        print_tensor_dims(lhs);
        print_tensor_dims(rhs);
    }
    done:
    return(match);
}

bool allclose_hw_check(const tt_tensor &lhs, const tt_tensor &rhs, const tt_comparison_config &comp, bool print_diffs) {
    bool match = true;
    bool check_dims_only = false;
    tt_op *op = lhs.get_producer_op_ptr() ? lhs.get_producer_op_ptr() :
                rhs.get_producer_op_ptr() ? rhs.get_producer_op_ptr() : nullptr;
    if (op) {
        std::cout << "Producer OP = " << op->type << ", name = " << op->name << std::endl;
        check_dims_only = (comp.check_dims_only_types.find(op->type) != comp.check_dims_only_types.end()) or
                          (comp.check_dims_only_names.find(op->name) != comp.check_dims_only_names.end());
    }

    if(are_tensor_dims_equal(lhs, rhs))
    {
        if (!check_dims_only)
        {
            for(int wi=0;wi<lhs.getw();++wi)
            {
                for(int zi=0;zi<lhs.getz();++zi)
                {
                    for(int ri=0;ri<lhs.getrt();++ri)
                    {
                        for(int ci=0;ci<lhs.getct();++ci)
                        {
                            bool allclose = lhs.tile_tensor[wi][zi][ri][ci].allclose(rhs.tile_tensor[wi][zi][ri][ci], comp.rtol, comp.atol, comp.check_pct, false);
                            bool pcc_compare = lhs.tile_tensor[wi][zi][ri][ci].pcc_compare(rhs.tile_tensor[wi][zi][ri][ci], comp.rtol, comp.atol, comp.check_pcc);
                            match = allclose and pcc_compare;
                            if (!match or print_diffs)
                            {
                                if (!match) std::cout << "First mismatched tile:" << std::endl;

                                std::cout << "Tensor dim [ci,ri,zi,wi] = [" << lhs.getct() << "," << lhs.getrt() << "," << lhs.getz() << "," << lhs.getw() << "]" << std::endl;
                                std::cout << "Tile index [ci,ri,zi,wi] = [" << ci << "," << ri << "," << zi << "," << wi << "]" << std::endl;
                                lhs.tile_tensor[wi][zi][ri][ci].dump_diff(rhs.tile_tensor[wi][zi][ri][ci]);

                                if (!match) goto done;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        match = false;
        std::cout << "allclose_hw_check: shapes mismatch";
        print_tensor_dims(lhs);
        print_tensor_dims(rhs);
    }
    done:
    return(match);
}

namespace tt
{
    void extract_range_to_buffer(tt_tensor* tensor, tt_buffer *buf, tt_dims range_start, tt_dims range_end, const tt_block_shape& block_shape)
    {
        int w0 = range_start.w;
        int z0 = range_start.z;
        int r0 = range_start.rt;
        int c0 = range_start.ct;
        int w1 = range_end.w;
        int z1 = range_end.z;
        int r1 = range_end.rt;
        int c1 = range_end.ct;

        std::uint32_t block_r = block_shape.r;
        std::uint32_t block_c = block_shape.c;

        TT_ASSERT(w0>=0);
        TT_ASSERT(z0>=0);
        TT_ASSERT(r0>=0);
        TT_ASSERT(c0>=0);
        TT_ASSERT(w1<=tensor->getw());
        TT_ASSERT(z1<=tensor->getz());
        TT_ASSERT(r1<=tensor->getrt());
        TT_ASSERT(c1<=tensor->getct());

        // TODO(arakhmati): re-enable asserts below?
        // TT_ASSERT(buf->get_rt() == r1-r0);
        // TT_ASSERT(buf->get_ct() == c1-c0);
        // TT_ASSERT(buf->get_zt() == z1-z0);
        TT_ASSERT(buf->get_wt() == w1-w0);
        TT_ASSERT(buf->get_wt() * buf->get_zt() * buf->get_rt() * buf->get_ct() == (w1 - w0) * (z1 - z0) * (r1 - r0) * (c1 - c0));

        TT_ASSERT(tensor->get_num_stored_tiles() >= w1 * z1 * r1 * c1 &&
            "tensor->tile_tensor does not have enough tiles to extract from");

        for(int wi=w0; wi<w1; ++wi)
        {
            for(int zi=z0; zi<z1; ++zi)
            {
                for(int ri=r0; ri<r1; ri += block_r)
                {
                    for(int ci=c0; ci<c1; ci += block_c)
                    {
                        for(int block_r_index = 0; block_r_index < block_r;  ++block_r_index)
                        {
                            for (int block_c_index = 0; block_c_index < block_c; ++block_c_index)
                            {
                                const tt_tile& tile = tensor->tile_tensor.at(wi).at(zi).at(ri + block_r_index).at(ci + block_c_index);
                                buf->add_tile(tile);
                            }
                        }
                    }
                }
            }
        }
    }

} // end namespace tt

bool is_wormhole(const std::string &arch_str){
    return (arch_str.compare("wormhole") == 0) || (arch_str.compare("wormhole_b0") == 0);
}

void tt_dram::params_to_cores(int epoch)
{
    // Go through all param buffers and copy them
    // to pointed to core
    vector<tt_buffer *>::iterator it;
    for(it=buffers[epoch].begin();it!=buffers[epoch].end();++it)
    {
        tt_core *coreptr = (*it)->get_core_ptr();
        coreptr->copy_dram_buf((*it)->get_id(),(*it));
    }
}


tt_buffer_grid* create_buffer_grid(tt_dram_io *dram_queue, const tt_tensor& tensor) {
    tt_buffer_grid *buffer_grid;

    if (dram_queue != NULL) {
        cout << "----Creating buffer grid from IO queue\n";
        // create a buffer grid from dram_io
        buffer_grid = new tt_buffer_grid_from_dram_io(dram_queue);
    } else {
        tt_op* op = tensor.get_producer_op_ptr();
        TT_ASSERT(op != nullptr ,  "Producer op pointer cannot be NULL for intermediate op.");
        cout << "----Creating buffer grid from Previous OP:" << tensor.get_producer_op_ptr()->name << "\n";
        buffer_grid = op->get_output_buffer_grid(tensor.get_producer_op_output_index());
    }
    return buffer_grid;
}


tt_custom_buffer_grid::tt_custom_buffer_grid(const std::vector<std::vector<tt_buffer*>>& buffer_grid) {
    this->buffer_grid = buffer_grid;
}

tt_grid_shape tt_custom_buffer_grid::get_grid_shape() {
    tt_grid_shape grid_shape{(uint32_t) this->buffer_grid.size(), (uint32_t) this->buffer_grid.at(0).size()};
    return grid_shape;
}

std::vector<tt_buffer*> tt_custom_buffer_grid::get_buffers(uint32_t row_index, uint32_t column_index) {
    return {this->buffer_grid.at(row_index).at(column_index)};
}

string tt_custom_buffer_grid::get_source_name() {
    return "custom_buffer_grid";
}

tt_hex::tt_hex(vector<uint32_t> arr, tt_hex_type itype, tt_core *core_ptr, string hex_name)
{
    copy(arr.begin(), arr.end(), back_inserter(hex_vec));
    creating_core = core_ptr;
    associated_routing_core = tt_xy_pair(-1,-1);
    type = itype;
    name = hex_name;
}

tt_hex::tt_hex(vector<uint32_t> arr, tt_hex_type itype, const tt_xy_pair &routing_core, string hex_name)
{
    copy(arr.begin(), arr.end(), back_inserter(hex_vec));
    creating_core = nullptr;
    associated_routing_core = routing_core;
    type = itype;
    name = hex_name;
}

tt_hex::tt_hex(const tt_hex& ihex) {
    hex_vec = ihex.hex_vec;
    type = ihex.type;
    creating_core = ihex.creating_core;
    associated_routing_core = ihex.associated_routing_core;
    base_addr = ihex.base_addr;
    d_addr = ihex.d_addr;
    d_chan = ihex.d_chan;
    d_subchannel = ihex.d_subchannel;
    d_chip_id = ihex.d_chip_id;
    name = ihex.name;
}

void tt_hex::add_hex_array(vector<uint32_t> arr)
{
    copy(arr.begin(), arr.end(), back_inserter(hex_vec));
}

void tt_hex::print_content(string name)
{
    printf("Printing convent of hex file %s\n", name.c_str());
    vector<uint32_t>::iterator it;
    uint32_t addr = d_addr;
    for (it=hex_vec.begin(); it<hex_vec.end(); ++it)
    {
        printf("addr = %d, data = 0x%x\n", addr, *it);
        addr += 4;
    }
}

tt_hex::tt_hex() {}
tt_hex::~tt_hex() {}

const std::vector<std::vector<int>> default_arch_ethernet_cores(const std::string &arch_name) {
    if (is_wormhole(arch_name)) {
        return {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {6, 6}, {7, 6}, {8, 6}, {9, 6}};
    } else {
        return {};
    }
}

const std::vector<std::vector<std::vector<int>>> default_arch_dram_cores(const std::string &arch_name) {
    if (is_wormhole(arch_name)) {
        return {
            { {0,0}, {0,1}, {0,11} },
            { {0,5}, {0,6}, {0,7} },
            { {5,0}, {5,1}, {5,11} },
            { {5,2}, {5,9}, {5,10} },
            { {5,3}, {5,4}, {5,8} },
            { {5,5}, {5,6}, {5,7} }
        };
        // {5, 0}, {0, 1}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {0, 5}, {5, 5}, {0, 6}, {5, 6}, {0, 7}, {5, 7}, {5, 8}, {5, 9}, {5, 10}, {0, 11}, {5, 11}};
    } else {
        return {
            {{1,0},},
            {{1,6},},
            {{4,0},},
            {{4,6},},
            {{7,0},},
            {{7,6},},
            {{10,0},},
            {{10,6},}
        };
    }
}

static bool check_perf_decouple_config_exist(map<string, vector<perf::PerfTriscDecoupleMode>> perf_decouple_res, tt_op* op, perf::PerfDesc perf_desc, perf::PerfTriscDecoupleMode target_mode) {
    if (perf_desc.device_perf_mode == perf::PerfDumpMode::Disable) {
        return false;
    }
    vector<perf::PerfTriscDecoupleMode> all_decoupled_res;
    if (perf_decouple_res.find(op->name) != perf_decouple_res.end()) {
        all_decoupled_res = perf_decouple_res[op->name];
    }
    bool exist = false;
    for (perf::PerfTriscDecoupleMode decouple: all_decoupled_res) {
        if (decouple == target_mode) {
            exist = true;
            break;
        }
    }
    return exist;
}

void check_epoch_is_legal(int epoch, int num_epochs) {
    if (epoch<0 || epoch>(num_epochs-1)) {
        throw std::runtime_error("Error: illegal epoch: " + std::to_string(epoch) + ". Valid epoch range is [0,"+std::to_string(num_epochs-1)+"].");
    }
}

static std::string centered(const std::string& original, std::int32_t target_size)
{
    TT_ASSERT( target_size >= 0 );
    std::uint32_t original_size = static_cast<std::int32_t>(original.size());
    std::int32_t padding = target_size - original_size;
    std::int32_t left_padding = padding / 2;
    std::int32_t right_padding = target_size - original_size - left_padding;

    return padding > 0
           ? std::string( left_padding, '.' ) +
             original +
             std::string( right_padding, '.' )
           : original;
}

std::string g_output_dir = ".";

