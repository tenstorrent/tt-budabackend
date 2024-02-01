// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "netlist/netlist.hpp"
#include "netlist/netlist_utils.hpp"

#define ERROR(error_msg)                                               \
    do {                                                               \
        std::stringstream full_err_msg;                                \
        full_err_msg << "<NET2HLKS-ERROR> " << error_msg << std::endl; \
        throw std::runtime_error(full_err_msg.str());                  \
    } while (0)

class Net2Hlks {
   public:
    Net2Hlks(const std::string &netlist_file, const std::string &output_dir);
    Net2Hlks(const netlist_parser &parsed_netlist, const std::string &output_dir);
    void output_hlks();
    ~Net2Hlks();

   protected:
    inline int pad_32B(int n) {
        int result = (n >> 5);
        if ((n % 32) != 0) {
            result++;
        }
        return (result << 5);
    }

    static inline bool divisible_either_direction(int a, int b) { return ((a % b) == 0) || ((b % a) == 0); }

    std::string netlist_file;
    std::string output_dir;

    netlist_parser parsed_netlist;

    // Last configured stated of the unpacker A
    std::string last_input_unpackerA;
    // Last configured stated of the unpacker B
    std::string last_input_unpackerB;
    // Last configured state of the packer
    std::string last_input_packer;

    std::unordered_map<std::string, bool> bcast_tile_inputs;  // list of inputs for which we enable bcast inside tile
    std::unordered_map<std::string, int> forked_input_names;
    std::vector<std::string> forked_input_skip_wait;
    // std::vector<std::string> kernel_broadcast_input_names;
    std::unordered_map<std::string, int> kernel_broadcast_inputs;

    std::unordered_map<std::string, tt_fused_subop_info> fused_subops_info;

    void insert_postcode(ofstream &hlks, int &postcode);

    void insert_hlks_header(ofstream &hlks, bool insert_quantization_header);
    void insert_hlks_body(ofstream &hlks, const tt_fused_op_info &fused_op);
    void insert_hlks_footer(ofstream &hlks);

    void insert_unpacker_init(
        ofstream &hlks, const std::map<string, string> &in, const tt_scheduled_op_info &curr_op, bool reset_state);
    void insert_init(
        ofstream &hlks,
        const std::map<std::string, std::string> &in,
        const tt_scheduled_op_info &curr_op,
        const tt_scheduled_op_info &prev_op_in_schedule,
        bool inputs_and_intermed_formats_match,
        bool reset_state,
        bool is_quantization_op);
    void insert_hw_config(
        ofstream &hlks,
        const tt_scheduled_op_info &curr_op,
        map<string, string> &in,
        bool insert_hlk_dbg_feature_disable,
        bool int8_fused_op);

    void insert_wait_inputs(
        ofstream &hlks, const std::map<std::string, std::string> &in, const tt_scheduled_op_info &curr_op, int m_k);
    void insert_wait_tiles(
        ofstream &hlks,
        const std::map<std::string, std::string> &in,
        int input,
        const tt_scheduled_op_info &curr_op,
        string &bcast_dim,
        bool &mblock_bcast,
        int &block_tile_dim,
        int m_k);

    void insert_matmul(
        ofstream &hlks,
        const std::map<std::string, std::string> &in,
        const tt_scheduled_op_info &curr_op,
        int id,
        bool matmul_loop_start,
        bool inputs_and_intermed_formats_match);
    void insert_binary_op(
        ofstream &hlks,
        const std::map<std::string, std::string> &in,
        const tt_scheduled_op_info &curr_op,
        const tt_scheduled_op_info &prev_op_in_schedule,
        bool matmul_loop_start,
        bool is_quantization_op);
    void insert_unary_op(
        ofstream &hlks,
        const std::map<std::string, std::string> &in,
        const tt_scheduled_op_info &curr_op,
        const tt_scheduled_op_info &prev_op_in_schedule,
        bool matmul_loop_start);
    void insert_reduce_op(
        ofstream &hlks,
        const std::map<std::string, std::string> &in,
        const tt_scheduled_op_info &curr_op,
        int id,
        bool reduce_loop_start,
        bool inputs_and_intermed_formats_match);

    void insert_sfpu_relu(
        ofstream &hlks, const tt_scheduled_op_info &curr_op, const std::map<std::string, std::string> &in);

    void insert_pop_inputs(
        ofstream &hlks, const std::map<std::string, std::string> &in, const tt_scheduled_op_info &curr_op, int m_k);
    void insert_pop_tiles(
        ofstream &hlks,
        const std::map<std::string, std::string> &in,
        int input,
        const tt_scheduled_op_info &curr_op,
        string &bcast_dim,
        bool &mblock_bcast,
        int &block_tile_dim,
        int m_k);

    void insert_pack_output(ofstream &hlks, const tt_scheduled_op_info &curr_op, bool output_and_intermed_format_match);
    void insert_pack_relu_config(ofstream &hlks, const tt_scheduled_op_info &curr_op);
    void insert_pack_reduce_mask_config(ofstream &hlks, const tt_scheduled_op_info &curr_op);
    void insert_pack_reduce_mask_clear(ofstream &hlks);

    bool is_input_forked(const string &name);

    int is_input_kernel_broadcasted(const string &name);

    void get_tms_params(
        int input,
        const tt_scheduled_op_info &curr_op,
        string &bcast_dim,
        bool &mblock_bcast,
        int &block_tile_dim,
        int &bcast_factor);
    Dim get_tile_bcast_dim(int input, const tt_scheduled_op_info &curr_op);

    bool is_matmul_bcast_ublock(const tt_scheduled_op_info &curr_op);
    bool is_matmul_bcast_mblock(const tt_scheduled_op_info &curr_op);
    const tt_dim_info &get_input_dim(const tt_scheduled_op_info &curr_op, int input_index);

    // Checker
    std::unordered_map<std::string, int> intermed_tile_count;
    void init_checker();
    void checker();
    void push_tiles(string &out, int &num_tiles);
    void pop_tiles(string &in, int &num_tiles);
};
